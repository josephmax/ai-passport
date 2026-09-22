/**
 * CLI: generate the default placeholder asset bundle (v1) to a file, or
 * verify it when no output path is given.
 *
 *   npm run gen:placeholder                      # verify generation + sizes
 *   npm run gen:placeholder -- out.apb1.bin      # also write the binary
 *
 * The running service auto-publishes this bundle as v1 on first start when no
 * version exists; this script exists for inspection and offline seeding.
 */

import { writeFileSync } from "node:fs";
import { generatePlaceholderBundle } from "../src/assets/placeholder.js";
import { parseBundle, MANIFEST_NAME } from "../src/assets/bundleFormat.js";

const outArg = process.argv[2];
const buf = generatePlaceholderBundle(1);
const parsed = parseBundle(buf);

console.log(`bundle v${parsed.version}: ${buf.length} bytes, ${parsed.files.length} files`);
for (const f of parsed.files) {
  console.log(`  ${f.name.padEnd(16)} ${f.data.length} bytes`);
}
const manifest = parsed.files.find((f) => f.name === MANIFEST_NAME);
if (manifest) {
  console.log("manifest ok:", JSON.parse(manifest.data.toString("utf8")).actions.run);
}
if (outArg) {
  writeFileSync(outArg, buf);
  console.log(`written to ${outArg}`);
}
