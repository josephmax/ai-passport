#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./tools/build-companion.sh
if [[ "$#" == 0 ]]; then
    "${PWD}/build/Passport Companion.app/Contents/MacOS/PassportCompanion" --help
    exit 1
fi
for arg in "$@"; do
    case "${arg}" in
        --scan|--list-audio|--test-audio|--help)
            exec "${PWD}/build/Passport Companion.app/Contents/MacOS/PassportCompanion" "$@" ;;
    esac
done
if ! command -v npm >/dev/null || ! command -v node >/dev/null; then
    echo 'Node.js 22+ and npm must be available on PATH.' >&2; exit 1
fi
npm --prefix service run build
port="$(node service/tools/prepare-companion.mjs)"
base="http://127.0.0.1:${port}"
if ! curl --fail --silent --max-time 2 "${base}/api/companion" >/dev/null; then
    (cd service; exec env HOST=127.0.0.1 PORT="${port}" node dist/index.js) &
    service_pid=$!
    trap 'kill "${service_pid}" 2>/dev/null || true' EXIT
    ready=false
    for attempt in {1..60}; do
        if curl --fail --silent --max-time 1 "${base}/api/companion" >/dev/null; then ready=true; break; fi
        if ! kill -0 "${service_pid}" 2>/dev/null; then echo 'Local service failed to start.' >&2; exit 1; fi
        sleep 1
    done
    if [[ "${ready}" != true ]]; then echo 'Local service startup timed out.' >&2; exit 1; fi
fi
"${PWD}/build/Passport Companion.app/Contents/MacOS/PassportCompanion" --service "${base}" "$@"
