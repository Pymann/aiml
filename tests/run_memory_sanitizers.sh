#!/usr/bin/env bash
#
# run_memory_sanitizers.sh — memory-safety acceptance gate for the heap-lifetime
# work (WP-C).  Runs AIML programs under valgrind memcheck, which instruments the
# JIT-generated AIML code AND the C++ runtime at run time (dynamic binary
# instrumentation) — so it catches double-free / use-after-free / invalid-read-write
# AND leaks originating from GENERATED code, without needing an ASan-instrumented
# codegen build (the ASan static build fails to link against libassimp; DBI sidesteps
# that entirely — see doc/analysis).
#
# Two things it verifies:
#   1. CONTROL: an intentional double-free from AIML code (_mem_sanitize/ctl_double_free)
#      MUST be reported by valgrind.  If not, valgrind is not instrumenting generated
#      code and the gate is worthless → hard fail.
#   2. PROBES: the WP-C-relevant probes MUST show NO invalid-free / UAF / invalid
#      read-write.  "definitely lost" bytes are printed for before/after WP-C tracking
#      (a leak alone is not a memory-safety failure; a NEW invalid-free/UAF is).
#
# Usage: bash tests/run_memory_sanitizers.sh
# Env:   AIML_BIN (default ./out/aiml/aiml)
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
VG="valgrind --leak-check=full --show-leak-kinds=definite --error-exitcode=0 --num-callers=20"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if ! command -v valgrind >/dev/null 2>&1; then
    echo "SKIP: valgrind not installed"; exit 0
fi
if [ ! -x "$AIML_BIN" ]; then
    echo "FAIL: aiml binary not found at $AIML_BIN"; exit 1
fi

# Regex for a genuine memory-SAFETY violation (not a mere leak).
UNSAFE='Invalid free|Invalid read|Invalid write|delete\[\]|has not been freed|contains unaddressable|jump .* uninitialised'

fails=0

run_vg() {  # name, logfile, cmd...
    local name="$1" log="$2"; shift 2
    echo "── $name ──"
    $VG --log-file="$log" "$@" >/dev/null 2>&1 || true
}

# ── 1. CONTROL: intentional double-free MUST be caught ────────────────────────
run_vg "control: intentional double-free (must be DETECTED)" "$TMP/ctl.log" \
    "$AIML_BIN" -r tests/_mem_sanitize/ctl_double_free.aiml
if grep -qE 'Invalid free' "$TMP/ctl.log"; then
    echo "  OK: valgrind detected the intentional double-free (harness is live)"
else
    echo "  FAIL: control double-free NOT detected — harness cannot see generated-code errors"
    fails=$((fails+1))
fi

# ── 2. PROBES: must be memory-SAFE (leaks tracked, not failed) ────────────────
probe() {  # name, logfile, env..., -- cmd...
    local name="$1" log="$2"; shift 2
    run_vg "$name" "$log" "$@"
    if grep -qE "$UNSAFE" "$log"; then
        echo "  FAIL: memory-safety violation:"
        grep -E "$UNSAFE" "$log" | head -3 | sed 's/^/    /'
        fails=$((fails+1))
    else
        local lost
        lost=$(grep -E 'definitely lost:' "$log" | tail -1 | sed 's/.*definitely lost://')
        echo "  OK: memory-safe (definitely lost:${lost:- n/a})"
    fi
}

probe "get_copy_at: correctness + corrupt→-1 + refill + final-drop" "$TMP/gca.log" \
    "$AIML_BIN" -r tests/test_kv_get_copy_at.aiml
probe "store-then-drop: get_copy/fresh → member store → move (A3, no double-free/UAF)" "$TMP/std.log" \
    "$AIML_BIN" -r tests/_mem_sanitize/store_then_drop.aiml
AIML_MEM_PROBE=A AIML_MEM_ITERS=200 \
    probe "leak-probe A (model deep-copy, N=200)" "$TMP/pa.log" \
    env AIML_MEM_PROBE=A AIML_MEM_ITERS=200 "$AIML_BIN" -r tests/test_mem_leak_probe.aiml

echo "────────────────────────────────────────"
if [ "$fails" -eq 0 ]; then
    echo "PASS: control detected + all probes memory-safe"
    exit 0
else
    echo "FAIL: $fails memory-sanitizer check(s) failed"
    exit 1
fi
