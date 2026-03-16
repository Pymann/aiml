#!/bin/bash
# AOT Smoke Tests — verify that -o (object file) compilation works
# These tests compile .aiml → .o, link with libaiml.so, and run the resulting binary.

AIML_BIN="./out/aiml/aiml"
LIBAIML_DIR="./out/libaiml"
WORK_DIR=$(mktemp -d)
trap "rm -rf $WORK_DIR" EXIT

TOTAL=0
PASSED=0
FAILED=0

run_aot_test() {
    local name="$1"
    local src="$2"
    local expected="$3"
    ((TOTAL++))

    echo "--- AOT: $name ---"

    # Compile to object file
    if ! $AIML_BIN "$src" -o "$WORK_DIR/${name}.o" 2>"$WORK_DIR/${name}.compile.log"; then
        echo "[ FAIL ] $name — compilation failed"
        cat "$WORK_DIR/${name}.compile.log"
        ((FAILED++))
        return
    fi

    # Collect object files (main + deps)
    local objs="$WORK_DIR/${name}.o"
    local depsfile="$WORK_DIR/${name}.o.deps"
    if [ -f "$depsfile" ]; then
        while IFS= read -r dep; do
            [ -n "$dep" ] && objs="$objs $dep"
        done < "$depsfile"
    fi

    # Link
    if ! g++ -o "$WORK_DIR/${name}" $objs \
        -L"$LIBAIML_DIR" -laiml -lpthread -ldl -lm -lstdc++ \
        -Wl,-rpath,"$LIBAIML_DIR" 2>"$WORK_DIR/${name}.link.log"; then
        echo "[ FAIL ] $name — linking failed"
        cat "$WORK_DIR/${name}.link.log"
        ((FAILED++))
        return
    fi

    # Run and check output
    local output
    output=$("$WORK_DIR/${name}" 2>&1) || true
    if echo "$output" | grep -q "$expected"; then
        echo "[  OK  ] $name"
        ((PASSED++))
    else
        echo "[ FAIL ] $name — expected '$expected' in output, got:"
        echo "$output"
        ((FAILED++))
    fi
}

echo "========================================"
echo "AIML AOT Smoke Tests"
echo "========================================"

# Test 1: Simple hello (no imports)
run_aot_test "hello" "tests/test_aot_hello.aiml" "AOT_HELLO_PASS"

# Test 2: With import (lib_globals.aiml)
run_aot_test "import" "tests/test_aot_import.aiml" "AOT_IMPORT_PASS"

# Test 3: Raw .ll output (default mode, no flags)
echo "--- AOT: ll_output ---"
((TOTAL++))
if $AIML_BIN "tests/test_aot_hello.aiml" 2>/dev/null; then
    if [ -f "tests/test_aot_hello.ll" ]; then
        if grep -q "AOT_HELLO_PASS" "tests/test_aot_hello.ll"; then
            echo "[  OK  ] ll_output — .ll file generated with expected content"
            ((PASSED++))
        else
            echo "[ FAIL ] ll_output — .ll file missing expected string"
            ((FAILED++))
        fi
        rm -f "tests/test_aot_hello.ll"
    else
        echo "[ FAIL ] ll_output — no .ll file generated"
        ((FAILED++))
    fi
else
    echo "[ FAIL ] ll_output — aiml command failed"
    ((FAILED++))
fi

echo "========================================"
echo "AOT Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================"
exit $FAILED
