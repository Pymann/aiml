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

    # Jeder Fall bekommt sein EIGENES Verzeichnis: die Objekte werden unten als
    # Gruppe eingesammelt, und in einem gemeinsamen Verzeichnis zieht Fall 2
    # sonst das Hauptobjekt von Fall 1 mit -- zwei Einsprungpunkte in einem Link.
    local case_dir="$WORK_DIR/$name"
    mkdir -p "$case_dir"

    # Compile to object file
    if ! $AIML_BIN "$src" -o "$case_dir/${name}.o" 2>"$case_dir/${name}.compile.log"; then
        echo "[ FAIL ] $name — compilation failed"
        cat "$case_dir/${name}.compile.log"
        ((FAILED++))
        return
    fi

    # Objekte einsammeln: das Hauptobjekt UND die Import-Objekte, die der
    # Compiler danebenlegt.  Bis hierher stand ein Leser fuer `${name}.o.deps`
    # -- ein Sidecar, das main.cpp seit 0dd80d77 (2026-03-22) nicht mehr
    # schreibt, sondern loescht; seitdem linkte dieser Test ohne die Importe und
    # scheiterte an undefinierten Symbolen.  Die Abhaengigkeiten stehen heute in
    # `${name}.d` (Make-Format), die Objekte liegen als <pfad_mit_unterstrichen>.o
    # im selben Verzeichnis.
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

# Test 3: Text-IR mit -o <datei>.ll.  Frueher stand hier "default mode, no
# flags" und die Erwartung, dass ein blosser Aufruf `tests/test_aot_hello.ll`
# hinterlaesst.  Der blosse Aufruf UEBERSETZT UND FUEHRT HEUTE AUS (er druckte
# AOT_HELLO_PASS), schreibt also gar keine Datei -- der Fall pruefte eine
# Vorgabe, die es nicht mehr gibt.  Jetzt wird der .ll-Pfad gepruefet, den es gibt.
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
