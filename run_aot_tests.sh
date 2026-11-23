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

    # Every case gets its OWN directory: the objects are collected below as a
    # group, and in a shared directory case 2 would otherwise pull in
    # the main object of case 1 -- two entry points in one link.
    local case_dir="$WORK_DIR/$name"
    mkdir -p "$case_dir"

    # Compile to object file
    if ! $AIML_BIN "$src" -o "$case_dir/${name}.o" 2>"$case_dir/${name}.compile.log"; then
        echo "[ FAIL ] $name — compilation failed"
        cat "$case_dir/${name}.compile.log"
        ((FAILED++))
        return
    fi

    # Collect the objects: the main object AND the import objects that the
    # compiler puts next to it.  Up to here there was a reader for `${name}.o.deps`
    # -- a sidecar that main.cpp has no longer written since 0dd80d77 (2026-03-22)
    # but deletes; since then this test linked without the imports and
    # failed on undefined symbols.  The dependencies are now in
    # `${name}.d` (make format), the objects lie as <path_with_underscores>.o
    # in the same directory.
    local objs
    objs=$(ls "$case_dir"/*.o 2>/dev/null | tr '\n' ' ')

    # Link
    if ! g++ -o "$case_dir/${name}" $objs \
        -L"$LIBAIML_DIR" -laiml -lpthread -ldl -lm -lstdc++ \
        -Wl,-rpath,"$LIBAIML_DIR" 2>"$case_dir/${name}.link.log"; then
        echo "[ FAIL ] $name — linking failed"
        cat "$case_dir/${name}.link.log"
        ((FAILED++))
        return
    fi

    # Run and check output
    local output
    output=$("$case_dir/${name}" 2>&1) || true
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

# Test 3: text IR with -o <file>.ll.  This used to say "default mode, no
# flags" with the expectation that a bare call leaves `tests/test_aot_hello.ll`
# behind.  The bare call today COMPILES AND RUNS (it printed
# AOT_HELLO_PASS), so it writes no file at all -- the case checked a
# default that no longer exists.  Now the .ll path that does exist is checked.
echo "--- AOT: ll_output ---"
((TOTAL++))
LL_OUT="$WORK_DIR/hello_ir.ll"
if $AIML_BIN "tests/test_aot_hello.aiml" -o "$LL_OUT" 2>/dev/null; then
    if [ -f "$LL_OUT" ]; then
        if grep -q "AOT_HELLO_PASS" "$LL_OUT"; then
            echo "[  OK  ] ll_output — .ll file generated with expected content"
            ((PASSED++))
        else
            echo "[ FAIL ] ll_output — .ll file missing expected string"
            ((FAILED++))
        fi
    else
        echo "[ FAIL ] ll_output — no .ll file generated at $LL_OUT"
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
