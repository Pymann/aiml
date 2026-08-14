#!/usr/bin/env bash
# WASM-Testsuite-Runner.  Compiles each AIML test to wasm32, runs via node,
# tracks pass/fail/skip.  Counterpart to run_all_tests.sh for the native JIT.
#
# Test selection: matches the {tests, expected_exit} pairs in tests/wasm_tests.list,
# OR — if --all is passed — iterates over every tests/*.aiml that doesn't open
# a window / hit network / spawn processes (a sane subset of run_all_tests.sh).
#
# Usage:
#   bash run_wasm_tests.sh              # run the curated wasm_tests.list
#   bash run_wasm_tests.sh --all        # run every applicable test (slow)
#   bash run_wasm_tests.sh tests/foo.aiml [tests/bar.aiml ...]   # explicit list
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ ! -x ./out/aiml/aiml ]]; then
    echo "ERROR: aiml binary not found at ./out/aiml/aiml — run 'exodus build' first." >&2
    exit 2
fi
if [[ ! -f ./out/aiml_wasm/aiml_wasm/libaiml_wasm.a ]]; then
    echo "ERROR: libaiml_wasm.a not found — run 'exodus build --config exodus-wasm-runtime.json' first." >&2
    exit 2
fi

# Resolve which tests to run.
TESTS=()
LIST_FILE="tests/wasm_tests.list"
if [[ $# -gt 0 && "$1" == "--all" ]]; then
    echo "Running ALL tests/*.aiml through WASM (this is slow)..."
    while IFS= read -r f; do TESTS+=("$f"); done < <(find tests -maxdepth 1 -name "test_*.aiml" | sort)
elif [[ $# -gt 0 ]]; then
    TESTS=("$@")
elif [[ -f "$LIST_FILE" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
        TESTS+=("$line")
    done < "$LIST_FILE"
else
    echo "No tests/wasm_tests.list file and no arguments given."
    echo "Either create tests/wasm_tests.list with test paths, or pass --all, or pass paths explicitly."
    exit 2
fi

if [[ ${#TESTS[@]} -eq 0 ]]; then
    echo "No tests to run."
    exit 0
fi

PASS=0
FAIL=0
FAILED=()

for TEST in "${TESTS[@]}"; do
    if [[ ! -f "$TEST" ]]; then
        echo "[SKIP ] $TEST (file not found)"
        continue
    fi
    printf "[ ... ] %s\r" "$TEST"
    # KEEP_BUILD off — cleanup after each test.  Capture full output for fail diagnostic.
    OUT=$(WASM_BUILD="out/wasm_tests/$(basename "$TEST" .aiml)" tools/run_wasm_test.sh "$TEST" 2>&1)
    RC=$?
    if [[ $RC -eq 0 ]]; then
        echo "[ OK  ] $TEST"
        PASS=$((PASS + 1))
    else
        echo "[FAIL ] $TEST (exit $RC)"
        echo "--- output ---"
        echo "$OUT" | tail -20
        echo "---"
        FAIL=$((FAIL + 1))
        FAILED+=("$TEST")
    fi
done

echo ""
echo "======================"
echo "WASM Test Run Summary:"
echo "Total:  $((PASS + FAIL))"
echo "Passed: $PASS"
echo "Failed: $FAIL"
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo ""
    echo "Failed tests:"
    for f in "${FAILED[@]}"; do echo "  - $f"; done
fi

[[ $FAIL -eq 0 ]]
