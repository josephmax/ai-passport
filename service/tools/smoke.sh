#!/usr/bin/env bash
# End-to-end smoke test for the AI Passport local service.
# Boots the built server on a random port with a temp data dir, then exercises:
# health -> portal login -> pairing code -> device pair -> snapshot -> asset bundle.
# Never leaves a process running. Usage: npm run smoke   (from service/)
set -euo pipefail

cd "$(dirname "$0")/.."

PORT="${SMOKE_PORT:-3991}"
BASE="http://127.0.0.1:${PORT}"
DATA_DIR="$(mktemp -d /tmp/pp-smoke-XXXXXX)"
JAR="$(mktemp /tmp/pp-smoke-jar-XXXXXX)"
SERVER_PID=""

cleanup() {
  if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$DATA_DIR" "$JAR"
}
trap cleanup EXIT

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }

[ -d dist ] || { echo "dist/ missing; building..."; npm run build; }

echo "[smoke] starting server on :$PORT (data: $DATA_DIR)"
DATA_DIR="$DATA_DIR" PORT="$PORT" HOST=127.0.0.1 \
PORTAL_PASSWORD=smoketest SESSION_SECRET=smoke-secret \
node dist/index.js >"$DATA_DIR/server.log" 2>&1 &
SERVER_PID=$!

for i in $(seq 1 150); do
  if curl -sf "$BASE/api/health" >/dev/null 2>&1; then break; fi
  sleep 0.2
  [ "$i" = 150 ] && { cat "$DATA_DIR/server.log" >&2; fail "server did not come up"; }
done
echo "[smoke] health OK"

# --- portal login -----------------------------------------------------------
curl -sf -c "$JAR" -o /dev/null "$BASE/portal/login"
curl -sf -b "$JAR" -c "$JAR" -o /dev/null -d "password=smoketest" "$BASE/portal/login" \
  || fail "portal login failed"
curl -sf -b "$JAR" -o /dev/null "$BASE/portal" || fail "portal accounts page rejected session"
echo "[smoke] portal login OK"

# --- pairing code via portal --------------------------------------------------
curl -sf -b "$JAR" -o /dev/null -X POST "$BASE/portal/devices/pair-code" || fail "pair-code generation failed"
CODE=$(curl -sf -b "$JAR" "$BASE/portal/devices" | sed -nE 's/.*class="pairecode">([0-9]{6})<.*/\1/p' | head -1)
[ -n "$CODE" ] || fail "no pairing code found on devices page"
echo "[smoke] pairing code generated"

# --- device pairing -----------------------------------------------------------
PAIR_RESP=$(curl -sf -X POST -H "content-type: application/json" \
  -d "{\"code\":\"$CODE\",\"deviceName\":\"smoke-device\"}" "$BASE/api/pair") \
  || fail "pair request failed"
TOKEN=$(printf '%s' "$PAIR_RESP" | sed -nE 's/.*"token":"([0-9a-f]{32})".*/\1/p')
[ -n "$TOKEN" ] || fail "no token in pair response: $PAIR_RESP"
echo "[smoke] paired"

# code is single-use
HTTP=$(curl -s -o /dev/null -w '%{http_code}' -X POST -H "content-type: application/json" \
  -d "{\"code\":\"$CODE\",\"deviceName\":\"again\"}" "$BASE/api/pair")
[ "$HTTP" = "403" ] || fail "reused pairing code returned $HTTP, expected 403"

# --- snapshot -------------------------------------------------------------------
SNAPSHOT=$(curl -sf -H "X-Device-Token: $TOKEN" "$BASE/api/snapshot") || fail "snapshot request failed"
echo "$SNAPSHOT" | node -e '
  let s=""; process.stdin.on("data",d=>s+=d).on("end",()=>{
    const j=JSON.parse(s);
    if (Buffer.byteLength(s) > 4096) throw new Error("snapshot exceeds device buffer");
    if (j.schema !== 1) throw new Error("schema != 1");
    if (!Number.isFinite(Date.parse(j.servedAt)) ||
        Math.abs(Date.now() - Date.parse(j.servedAt)) > 60_000 ||
        Date.parse(j.generatedAt) > Date.parse(j.servedAt))
      throw new Error("response time is missing, stale, or older than the reading");
    if (!Array.isArray(j.accounts) || j.accounts.length < 1) throw new Error("no accounts");
    if (!j.accounts[0].quotas) throw new Error("primary account has no quotas");
    if (!j.assetBundle || j.assetBundle.version < 1) throw new Error("no assetBundle.version");
    if (j.weather !== null && typeof j.weather.code !== "number") throw new Error("bad weather");
    if (process.env.SMOKE_REQUIRE_LOCAL === "1" &&
        (!Number.isSafeInteger(j.dailyTokens.used) || j.dailyTokens.used < 0 ||
         j.dailyTokens.coverage !== "local-agent-logs")) throw new Error("no live local Token reading");
    if (process.env.SMOKE_REQUIRE_LOCAL === "1" &&
        (!Array.isArray(j.agents) || !["pi","zcode","codex","claude"].every(
          name => j.agents.some(a => a.agent === name && Number.isSafeInteger(a.dailyTokens) &&
            "rolling5h" in a && "weekly" in a)))) throw new Error("local Agent detail or quota slots missing");
    console.log(`[smoke] snapshot OK: ${j.accounts.length} account(s), bundle v${j.assetBundle.version}, weather=${j.weather ? j.weather.city + "/" + j.weather.kind : "n/a"}`);
  });' || fail "snapshot validation failed"

curl -sf -b "$JAR" -o /dev/null -X POST "$BASE/portal/accounts/refresh" || fail "manual refresh failed"
curl -sf -b "$JAR" "$BASE/portal/accounts" | node -e '
  let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{
    if (!s.includes("今日 Token：") || !s.includes("立即采集")) throw new Error("missing usage UI");
  });' || fail "portal usage page failed"
echo "[smoke] usage UI and manual refresh OK"

HTTP=$(curl -s -o /dev/null -w '%{http_code}' -H "X-Device-Token: deadbeef" "$BASE/api/snapshot")
[ "$HTTP" = "401" ] || fail "bad token returned $HTTP, expected 401"

# --- asset bundle -----------------------------------------------------------------
BUNDLE="$DATA_DIR/downloaded.bin"
curl -sf "$BASE/assets/bundle_v1.bin" -o "$BUNDLE" || fail "bundle download failed"
node -e '
  const fs = require("fs");
  const b = fs.readFileSync(process.argv[1]);
  if (b.subarray(0,4).toString("ascii") !== "APB1") throw new Error("bad magic");
  const version = b.readUInt16LE(4);
  const count = b.readUInt16LE(6);
  if (version !== 1) throw new Error("bundle version " + version);
  if (count < 5) throw new Error("too few files: " + count);
  console.log(`[smoke] bundle OK: APB1 v${version}, ${count} files, ${b.length} bytes`);
' "$BUNDLE" || fail "bundle validation failed"

echo "[smoke] ALL PASS"
