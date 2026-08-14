#!/usr/bin/env bash
# Run a single AIML test through the WASM pipeline (aiml → wasm32 ll → emcc →
# node).  Prints test output, returns the test's exit code (0 = pass).
#
# Usage: tools/run_wasm_test.sh tests/test_iface_list_return.aiml
# Env:
#   AIML_BIN     — path to aiml compiler (default: ./out/aiml/aiml)
#   AIML_WASM    — path to libaiml_wasm.a's dir (default: out/aiml_wasm/aiml_wasm)
#   WASM_BUILD   — scratch dir for .ll/.wasm/.js (default: out/wasm_tests/<name>)
#   KEEP_BUILD   — keep intermediate files (default: empty = remove)
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test.aiml>" >&2
    exit 2
fi

TEST="$1"
NAME=$(basename "$TEST" .aiml)

AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
AIML_WASM_DIR="${AIML_WASM:-out/aiml_wasm/aiml_wasm}"
BUILD_DIR="${WASM_BUILD:-out/wasm_tests/$NAME}"

EMSDK_DIR=$(ls -d __exodus_cache/git/emsdk/*/upstream/emscripten 2>/dev/null | head -1)
EMCC="$EMSDK_DIR/emcc"
NODE=$(ls __exodus_cache/git/emsdk/*/node/*/bin/node 2>/dev/null | head -1)

if [[ -z "$EMCC" || ! -x "$EMCC" ]]; then
    echo "ERROR: emcc not found in __exodus_cache/git/emsdk/*/upstream/emscripten/" >&2
    exit 3
fi
if [[ -z "$NODE" || ! -x "$NODE" ]]; then
    echo "ERROR: node not found in __exodus_cache/git/emsdk/*/node/*/bin/" >&2
    exit 3
fi

mkdir -p "$BUILD_DIR"
LL="$BUILD_DIR/$NAME.ll"
JS="$BUILD_DIR/$NAME.js"
WASM="$BUILD_DIR/$NAME.wasm"

# 1. AIML → wasm32 LLVM IR with main() entry point
"$AIML_BIN" "$TEST" --target wasm32 --emit-main -o "$LL" 2>&1 >/dev/null | grep -v "^$" || true

# 2. emcc links IR + libaiml_wasm.a → .wasm + .js loader
"$EMCC" "$LL" -o "$JS" \
    -L"$AIML_WASM_DIR" -laiml_wasm \
    -sENVIRONMENT=node -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 \
    2>&1 | grep -viE "cache:INFO|system_libs:INFO|signature mismatch: calloc" || true

# 3. Run via node
"$NODE" "$JS"
EXIT_CODE=$?

# Cleanup
if [[ -z "${KEEP_BUILD:-}" ]]; then
    rm -rf "$BUILD_DIR"
fi

exit "$EXIT_CODE"
