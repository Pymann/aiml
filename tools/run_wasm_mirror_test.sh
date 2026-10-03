#!/usr/bin/env bash
# WASM↔native mirror RPC roundtrip test.
#
# Spawns the AIML server natively (listens ws://localhost:18888), compiles
# the AIML client to WASM, runs it via node, then kills the server.
# Tests the wire-format compatibility between x86-64 (encoder) and wasm32
# (decoder) for nested list<string> in deserialised objects.
#
# Returns 0 if the client's assertions all pass, 1 otherwise.
set -u

cd "$(dirname "${BASH_SOURCE[0]}")/.."

SERVER_AIML="tests/wasm/test_mirror_rpc_server.aiml"
CLIENT_AIML="tests/wasm/test_mirror_rpc_client.aiml"

if [[ ! -f "$SERVER_AIML" || ! -f "$CLIENT_AIML" ]]; then
    echo "ERROR: server/client AIML files missing." >&2
    exit 2
fi

# Resolve binaries.
AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
NODE=$(ls __exodus_cache/git/emsdk/*/node/*/bin/node 2>/dev/null | head -1)
EMCC=$(ls __exodus_cache/git/emsdk/*/upstream/emscripten/emcc 2>/dev/null | head -1)
if [[ ! -x "$AIML_BIN" ]]; then
    echo "ERROR: aiml binary missing at $AIML_BIN" >&2; exit 2
fi
if [[ -z "$NODE" || ! -x "$NODE" || -z "$EMCC" || ! -x "$EMCC" ]]; then
    echo "ERROR: node/emcc not found in __exodus_cache/git/emsdk/" >&2; exit 2
fi

LOG_DIR=$(mktemp -d -t wasm_mirror_test.XXXXXX)
SERVER_LOG="$LOG_DIR/server.log"
CLIENT_LOG="$LOG_DIR/client.log"

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [[ -z "${KEEP_LOGS:-}" ]]; then
        rm -rf "$LOG_DIR"
    else
        echo "Logs kept in $LOG_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Build the client to WASM first (slow) — the server only lives for a fixed time.
echo "[1/3] building WASM client..."
BUILD_DIR="$LOG_DIR/build"
mkdir -p "$BUILD_DIR"
LL="$BUILD_DIR/client.ll"
JS="$BUILD_DIR/client.js"

"$AIML_BIN" "$CLIENT_AIML" --target wasm32 --emit-main -o "$LL" 2>&1 | tee -a "$CLIENT_LOG" | grep -v "^$" || true

"$EMCC" "$LL" -o "$JS" \
    -Lout/aiml_wasm/aiml_wasm -laiml_wasm \
    -lwebsocket.js \
    -sENVIRONMENT=node -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 \
    >>"$CLIENT_LOG" 2>&1 || {
        echo "ERROR: emcc link failed. Tail of $CLIENT_LOG:"; tail -30 "$CLIENT_LOG"; exit 1; }

# 2. Spawn the native server in the background, then run the client via node.
echo "[2/3] starting native server + running client..."
"$AIML_BIN" "$SERVER_AIML" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

# Give it ~500ms to bind the port.
sleep 0.6
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERROR: server died on startup. Log:"
    cat "$SERVER_LOG"
    exit 1
fi
if ! grep -q "listening on ws" "$SERVER_LOG"; then
    echo "WARN: server hasn't logged 'listening' yet, sleeping a bit more..."
    sleep 0.5
fi

"$NODE" "$JS" 2>&1 | tee -a "$CLIENT_LOG"
CLIENT_EXIT=${PIPESTATUS[0]}

echo "[3/3] cleanup (server pid=$SERVER_PID, client exit=$CLIENT_EXIT)"
echo ""
echo "=== Server log ==="
cat "$SERVER_LOG"
echo ""

if [[ $CLIENT_EXIT -ne 0 ]]; then
    echo "RESULT: FAIL (client exit $CLIENT_EXIT)"
    exit 1
fi

# Also check the client log for the expected PASS marker.
if ! grep -q "^PASS$" "$CLIENT_LOG"; then
    echo "RESULT: FAIL (no PASS marker in client output)"
    exit 1
fi

echo "RESULT: PASS"
exit 0
