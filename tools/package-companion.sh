#!/usr/bin/env bash
# Assemble an architecture-specific, movable app; never copy developer state.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then echo 'Packaging requires macOS.' >&2; exit 1; fi
if ! command -v node >/dev/null || ! command -v npm >/dev/null; then echo 'Node.js 22+ and npm are required to package.' >&2; exit 1; fi
node_bin="$(node -p 'require("node:fs").realpathSync(process.execPath)')"
node_major="$(node -p 'Number(process.versions.node.split(".")[0])')"
if [[ "${node_major}" -lt 22 ]]; then echo 'Node.js 22+ required.' >&2; exit 1; fi
# A runtime linked against Homebrew/mise libraries would not be portable.
if otool -L "${node_bin}" | tail -n +2 | awk '{print $1}' | rg -v '^(/usr/lib/|/System/Library/)' >/dev/null; then
    echo 'Node runtime has non-system dynamic dependencies; use a self-contained Node distribution.' >&2; exit 1
fi
app="${PWD}/build/desktop/Passport Companion.app"
if [[ -e "${app}" ]]; then
    echo 'build/desktop/Passport Companion.app already exists; move it aside before packaging a replacement.' >&2; exit 1
fi
mkdir -p build/desktop
staging="$(mktemp -d "${PWD}/build/desktop/.package.XXXXXX")"
trap 'rm -rf "${staging}"' EXIT
staged_app="${staging}/Passport Companion.app"
npm --prefix service run build
PASSPORT_APP_OUTPUT="${staged_app}" ./tools/build-companion.sh
resources="${staged_app}/Contents/Resources"
mkdir -p "${resources}/runtime" "${resources}/service/tools" "${resources}/main/fonts"
install -m 0644 LICENSE "${resources}/LICENSE"
install -m 0755 "${node_bin}" "${resources}/runtime/node"
node_prefix="$(dirname "$(dirname "${node_bin}")")"
install -m 0644 "${node_prefix}/LICENSE" "${resources}/runtime/LICENSE"
install -m 0644 service/package.json service/package-lock.json "${resources}/service/"
install -m 0644 service/tools/desktop-runtime.mjs "${resources}/service/tools/"
install -m 0644 main/fonts/badge_name_glyphs.txt "${resources}/main/fonts/"
ditto service/dist "${resources}/service/dist"
ditto service/node_modules "${resources}/service/node_modules"
# Production pruning uses the already installed, locked dependency tree offline.
(cd "${resources}/service"; npm prune --omit=dev --ignore-scripts --offline --no-audit --no-fund >/dev/null)
python3 - "${resources}" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1]).resolve()
for item in root.rglob('*'):
    if item.is_symlink() and not item.resolve().is_relative_to(root):
        raise SystemExit(f'Nonportable symlink in package: {item.relative_to(root)}')
for forbidden in ('.env', 'companion-host.json', 'settings.json', 'devices.json'):
    if any(root.rglob(forbidden)):
        raise SystemExit(f'Private state in package: {forbidden}')
PY
# Sign nested Mach-O files before sealing the app. This is a local ad-hoc build.
while IFS= read -r -d '' file_path; do
    if file -b "${file_path}" | rg -q 'Mach-O'; then codesign --force --sign - "${file_path}"; fi
done < <(find "${resources}" -type f \( -perm -111 -o -name '*.node' -o -name '*.dylib' \) -print0)
codesign --force --sign - --identifier com.folotoy.passport.companion "${staged_app}"
codesign --verify --deep --strict "${staged_app}"
# Exercise bundled native dependencies and inventory, independent of PATH.
"${resources}/runtime/node" --input-type=module - "${resources}/service" <<'JS'
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { readFileSync } from 'node:fs';
const service = process.argv[2];
const require = createRequire(service + '/package.json');
await require('sharp')({ create: { width: 1, height: 1, channels: 3, background: 'white' } }).png().toBuffer();
require.resolve('ccusage/src/cli.js');
const { validateBadgeName } = await import(pathToFileURL(service + '/dist/portal/badgeName.js'));
const supported = readFileSync(service + '/../main/fonts/badge_name_glyphs.txt', 'utf8').trim().slice(0, 2);
if (supported.length !== 2 || validateBadgeName(supported) === null) throw new Error('Bundled font inventory missing');
console.log('Bundled runtime/dependencies/font inventory: PASS');
JS
mv "${staged_app}" "${app}"
echo "${app}"
