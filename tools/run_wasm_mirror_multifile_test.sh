#!/usr/bin/env bash
# Multi-file variant of run_wasm_mirror_test.sh.  Server + client both import
# tests/wasm/mirror_rpc_lib.aiml — matching DataCreatures' tile_behaviors-as-
# library structure.  If this fails but the single-file variant passes,
# multi-file ABI lowering for struct-returning vtable methods is the bug.
set -u

cd "$(dirname "${BASH_SOURCE[0]}")/.."

SERVER_AIML="tests/wasm/test_mirror_rpc_server_multifile.aiml"
CLIENT_AIML="tests/wasm/test_mirror_rpc_client_multifile.aiml"
LIB_AIML="tests/wasm/mirror_rpc_lib.aiml"

if [[ ! -f "$SERVER_AIML" || ! -f "$CLIENT_AIML" || ! -f "$LIB_AIML" ]]; then
    echo "ERROR: AIML files missing." >&2; exit 2
fi

AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
NODE=$(ls __exodus_cache/git/emsdk/*/node/*/bin/node 2>/dev/null | head -1)
EMCC=$(ls __exodus_cache/git/emsdk/*/upstream/emscripten/emcc 2>/dev/null | head -1)

LOG_DIR=$(mktemp -d -t wasm_mirror_mf_test.XXXXXX)
SERVER_LOG="$LOG_DIR/server.log"
CLIENT_LOG="$LOG_DIR/client.log"
BUILD_DIR="$LOG_DIR/build"
mkdir -p "$BUILD_DIR"

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [[ -z "${KEEP_LOGS:-}" ]]; then rm -rf "$LOG_DIR"; else echo "Logs kept in $LOG_DIR"; fi
}
trap cleanup EXIT INT TERM

# 1. Start native server.
echo "[1/4] starting native server (multi-file)..."
"$AIML_BIN" "$SERVER_AIML" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 0.6

if ! grep -q "listening on ws" "$SERVER_LOG"; then sleep 0.5; fi

# 2. Compile the client (its `- import:` is resolved internally by aiml,
#    generating one .ll per imported file as a side effect).
echo "[2/4] compiling client (imports resolved internally)..."
CLIENT_LL="$BUILD_DIR/client.ll"
JS="$BUILD_DIR/client.js"

"$AIML_BIN" "$CLIENT_AIML" --target wasm32 --emit-main -o "$CLIENT_LL" >>"$CLIENT_LOG" 2>&1

# Find any sibling .ll files emitted alongside (one per import).
SIBLINGS=$(find "$BUILD_DIR" -name "*.ll" ! -name "client.ll" 2>/dev/null)
echo "[2.5/4] sibling .ll from imports: $(echo $SIBLINGS | wc -w) files"

echo "[3/4] linking with emcc..."
"$EMCC" "$CLIENT_LL" $SIBLINGS -o "$JS" \
    -Lout/aiml_wasm/aiml_wasm -laiml_wasm \
    -lwebsocket.js \
    -sENVIRONMENT=node -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 \
    >>"$CLIENT_LOG" 2>&1 || {
        echo "ERROR: emcc link failed. Tail of $CLIENT_LOG:"; tail -30 "$CLIENT_LOG"; exit 1; }

# 4. Run.
echo "[4/4] running WASM client..."
"$NODE" "$JS" 2>&1 | tee -a "$CLIENT_LOG"
CLIENT_EXIT=${PIPESTATUS[0]}

echo ""; echo "=== Server log ==="; cat "$SERVER_LOG"; echo ""

if [[ $CLIENT_EXIT -ne 0 ]]; then echo "RESULT: FAIL (exit $CLIENT_EXIT)"; exit 1; fi
if ! grep -q "^PASS$" "$CLIENT_LOG"; then echo "RESULT: FAIL (no PASS marker)"; exit 1; fi
echo "RESULT: PASS"
exit 0
