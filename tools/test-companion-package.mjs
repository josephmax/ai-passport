/** Launch the actual relocated bundle backend with no developer PATH/state. */
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { createRequire } from "node:module";

const app = path.resolve(process.argv[2] ?? "build/desktop/Passport Companion.app");
const root = await mkdtemp(path.join(tmpdir(), "passport package "));
const resources = path.join(app, "Contents/Resources");
const node = path.join(resources, "runtime/node");
const entry = path.join(resources, "service/tools/desktop-runtime.mjs");
let active;
const cli = spawnSync(path.join(app, "Contents/MacOS/PassportCompanion"), ["--help"], {
  cwd: tmpdir(), env: { HOME: root, PATH: "/usr/bin:/bin" }, encoding: "utf8", timeout: 10_000,
});
assert.equal(cli.status, 0); assert.match(cli.stdout, /--device/);
const require = createRequire(path.join(resources, "service/package.json"));
const usage = spawnSync(node, [require.resolve("ccusage/src/cli.js"), "--help"], {
  cwd: tmpdir(), env: { HOME: root, PATH: "/usr/bin:/bin" }, encoding: "utf8", timeout: 20_000,
});
assert.equal(usage.status, 0, "Bundled usage collector could not run");
async function boot() {
  const child = spawn(node, [entry, root], {
    cwd: tmpdir(), env: { HOME: root, PATH: "/usr/bin:/bin", LANG: "en_US.UTF-8" },
    stdio: ["pipe", "pipe", "pipe"],
  });
  active = child;
  const exited = new Promise((resolve) => child.once("exit", (code, signal) => resolve({ code, signal })));
  let buffer = "";
  let diagnostic = "";
  const ready = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Bundled service startup timed out")), 90_000);
    child.once("error", reject);
    child.once("exit", (code, signal) => { clearTimeout(timeout); reject(new Error(`Bundled service exited before readiness (${code}/${signal}): ${diagnostic}`)); });
    child.stdout.on("data", (chunk) => {
      buffer += chunk.toString();
      const lines = buffer.split("\n"); buffer = lines.pop() ?? "";
      for (const line of lines) {
        if (line.startsWith("[companion-ready] ")) {
          clearTimeout(timeout);
          const { port } = JSON.parse(line.slice("[companion-ready] ".length));
          resolve(`http://127.0.0.1:${port}`);
        }
      }
      if (buffer.length > 65536) buffer = "";
    });
    // Drain logs without leaking local paths/provider details into test output.
    child.stderr.on("data", chunk => { diagnostic = (diagnostic + chunk.toString()).slice(-1500); });
  });
  const base = await ready;
  const response = await fetch(`${base}/api/companion`);
  assert.equal(response.status, 200);
  const identity = await response.json(); assert.equal(identity.v, 1);
  assert.match(identity.host, /^[0-9a-f-]{36}$/);
  assert.equal((await fetch(`${base}/portal`)).status, 200);
  const rejected = await fetch(`${base}/api/companion`, { headers: { Origin: "https://example.com" } });
  assert.equal(rejected.status, 403);
  return { child, exited, base, host: identity.host };
}
async function stop(session) {
  // EOF models the owning desktop app exiting/crashing, not just a normal signal.
  session.child.stdin.end();
  let timeout;
  const result = await Promise.race([
    session.exited,
    new Promise((_, reject) => { timeout = setTimeout(() => reject(new Error("Orphan service remained after stdin EOF")), 5_000); }),
  ]).finally(() => clearTimeout(timeout));
  assert.ok(result.code === 0 || result.signal === "SIGTERM");
  await assert.rejects(fetch(`${session.base}/api/companion`));
  active = undefined;
}
try {
  const first = await boot(); await stop(first);
  const second = await boot(); assert.equal(second.host, first.host); await stop(second);
  console.log("Packaged loopback service, portal, origin guard, stable host and owner-exit cleanup: PASS");
} finally {
  if (active && active.exitCode === null) active.kill("SIGKILL");
  await rm(root, { recursive: true, force: true });
}
