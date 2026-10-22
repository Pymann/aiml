#!/usr/bin/env bash
# Run a single AIML test in a REAL browser (aiml → wasm32 → emcc → headless
# Chrome).  Counterpart to run_wasm_test.sh, which runs under node: node has no
# DOM and no WebGL, so it can prove the input state machine but NOT the parts
# that depend on a canvas — window↔backing-store coordinates and the whole
# prefab/GL path.  That is what this script covers (spec/44_0 §8, spec/44_9).
#
# Usage: tools/run_browser_test.sh tests/test_input_injection_click.aiml
# Env:
#   AIML_BIN    — aiml compiler (default: ./out/aiml/aiml)
#   AIML_WASM   — dir holding libaiml_wasm.a
#   BROWSER     — chrome binary (default: google-chrome)
#   KEEP_BUILD  — keep the generated page (default: empty = remove)
#
# SINGLE_FILE=1 inlines the wasm into the .js, so the page loads over file://
# and no web server is needed for a test run.
set -uo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test.aiml>" >&2
    exit 2
fi

TEST="$1"
NAME=$(basename "$TEST" .aiml)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
AIML_WASM_DIR="${AIML_WASM:-out/aiml_wasm/aiml_wasm}"
BROWSER="${BROWSER:-google-chrome}"
BUILD_DIR="${BROWSER_BUILD:-out/browser_tests/$NAME}"

EMSDK_DIR=$(ls -d __exodus_cache/git/emsdk/*/upstream/emscripten 2>/dev/null | head -1)
EMCC="$EMSDK_DIR/emcc"
[[ -x "$EMCC" ]]              || { echo "ERROR: emcc not found" >&2; exit 3; }
command -v "$BROWSER" >/dev/null || { echo "ERROR: $BROWSER not found" >&2; exit 3; }

mkdir -p "$BUILD_DIR"
LL="$BUILD_DIR/$NAME.ll"
JS="$BUILD_DIR/$NAME.js"
HTML="$BUILD_DIR/$NAME.html"

"$AIML_BIN" "$TEST" --target wasm32 --emit-main -o "$LL" 2>&1 >/dev/null | grep -v "^$" || true
[[ -s "$LL" ]] || { echo "ERROR: aiml produced no IR for $TEST" >&2; exit 4; }

"$EMCC" "$LL" -o "$JS" \
    -L"$AIML_WASM_DIR" -laiml_wasm \
    -sENVIRONMENT=web -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 -sSINGLE_FILE=1 -sEXIT_RUNTIME=1 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 \
    -sEXPORTED_RUNTIME_METHODS=stringToUTF8,cwrap,ccall \
    2>&1 | grep -viE "cache:INFO|system_libs:INFO|signature mismatch" || true
[[ -s "$JS" ]] || { echo "ERROR: emcc produced no output" >&2; exit 5; }

# The page collects the test's stdout and its exit code into the DOM, because
# --dump-dom is what we can read back without driving the devtools protocol.
cat > "$HTML" <<'EOF'
<!doctype html>
<meta charset="utf-8">
<title>aiml browser test</title>
<body style="margin:0;background:#111">
<canvas id="canvas" width="400" height="300"></canvas>
<pre id="out" data-done="0" data-code="">booting…</pre>
<script>
  var outEl = document.getElementById('out');
  var lines = [];
  function emit(t) { lines.push(t); outEl.textContent = lines.join('\n'); }
  var Module = {
    canvas: document.getElementById('canvas'),
    print:    function (t) { emit(t); },
    printErr: function (t) { emit('[err] ' + t); },
    onExit:   function (c) { outEl.dataset.code = String(c); outEl.dataset.done = '1'; },
    onAbort:  function (r) { emit('[abort] ' + r); outEl.dataset.code = '99'; outEl.dataset.done = '1'; }
  };
  window.onerror = function (m) { emit('[onerror] ' + m); outEl.dataset.code = '98'; outEl.dataset.done = '1'; };
</script>
<script src="SCRIPT_NAME"></script>
EOF
sed -i "s|SCRIPT_NAME|$NAME.js|" "$HTML"

# swiftshader: a headless Chrome has no GPU, but the prefab path needs WebGL2.
DUMP=$("$BROWSER" --headless=new --disable-gpu --no-sandbox \
        --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader \
        --allow-file-access-from-files --virtual-time-budget=20000 \
        --dump-dom "file://$SCRIPT_DIR/$HTML" 2>/dev/null)

# Report what the test printed.  The <pre> content spans lines, so this is a
# range match, not a per-line one — a line-wise sed silently printed NOTHING,
# which would make a future failure undiagnosable.
echo "$DUMP" \
  | awk '/<pre id="out"/{inside=1; sub(/.*<pre id="out"[^>]*>/, "")} \
         inside{if (match($0, /<\/pre>/)) {sub(/<\/pre>.*/, ""); print; exit} print}' \
  | sed 's/&lt;/</g; s/&gt;/>/g; s/&amp;/\&/g'

CODE=$(echo "$DUMP" | grep -oE 'data-code="[0-9]*"' | head -1 | grep -oE '[0-9]+')
DONE=$(echo "$DUMP" | grep -oE 'data-done="[01]"' | head -1 | grep -oE '[01]')

if [[ -z "${KEEP_BUILD:-}" ]]; then rm -rf "$BUILD_DIR"; fi

if [[ "${DONE:-0}" != "1" ]]; then
    echo "ERROR: test did not finish in the browser (no exit recorded)" >&2
    exit 6
fi
exit "${CODE:-7}"
