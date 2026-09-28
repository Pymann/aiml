#!/bin/bash

SKIP_BUILD=0
for arg in "$@"; do
    if [ "$arg" = "--no-build" ] || [ "$arg" = "--test-only" ]; then
        SKIP_BUILD=1
    fi
done

source ../../exvenv/bin/activate
if [ $SKIP_BUILD -eq 0 ]; then
    exodus build
    if [ $? -ne 0 ]; then
        echo "Error: Compilation failed. Aborting tests."
        exit 1
    fi
else
    echo "Skipping build step (--no-build/--test-only)."
fi

# Define the AIML executable path
AIML_BIN="./out/aiml/aiml"
if [ ! -f "$AIML_BIN" ]; then
    AIML_BIN="./out/aiml"
fi
if [ ! -f "$AIML_BIN" ]; then
    AIML_BIN="./build/aiml"
fi

# Define the output file
OUTPUT_FILE="test_results.txt"

# Clear the output file
> "$OUTPUT_FILE"

# Check for custom AIML_CACHE environment variable
if [ -z "$AIML_CACHE" ]; then
    AIML_CACHE="__aiml_cache"
fi

# Clear the AIML cache
echo "Clearing AIML Cache ($AIML_CACHE)..." | tee -a "$OUTPUT_FILE"
rm -rf "$AIML_CACHE"

# Clear AionDB persistent store (created by test_persist.aiml and similar tests)
rm -rf aiondb/

# Clear reopen test directories in /tmp (write+read test pairs need a clean slate)
rm -rf /tmp/aiml_test_aiondb_kv_reopen_20260319/
rm -rf /tmp/aiml_test_aiondb_ts_reopen_20260319/
rm -rf /tmp/aiml_test_aiondb_store_reopen_20260319/
rm -rf /tmp/aiml_test_aiondb_store_heap_reopen_20260410/
rm -rf /tmp/aiml_test_db_indexed_reopen_20260607/
rm -rf /tmp/aiml_test_db_indexed_backfill_20260607/
rm -rf /tmp/aiml_chkpt_test/

# Create binary test file for test_binary_io.aiml
python3 -c "
import struct, os
with open('/tmp/aiml_binary_io_test.bin','wb') as f:
    f.write(struct.pack('<B', 0xAB))
    f.write(struct.pack('<B', 1))
    f.write(struct.pack('<i', -1234567))
    f.write(struct.pack('<I', 3000000000))
    f.write(struct.pack('<q', 9000000000))
    f.write(struct.pack('<f', 3.14))
    f.write(struct.pack('<d', 2.718281828))
    f.write(b'hello\x00')
    f.write(bytes([0xCA, 0xFE]))
"

echo "Running AIML Tests..." | tee -a "$OUTPUT_FILE"
echo "======================" | tee -a "$OUTPUT_FILE"

# Find all .aiml files in the tests/ directory.
# Exclude __aiml_cache / build / out, plus subdirectories prefixed with "_"
# (convention: tests/_*/ contains helper files imported by other tests, not
# runnable standalone — e.g. tests/_cross/ for cross-file import repros).
# Sorted so paired tests (e.g. `_1_write` before `_2_read`) run in order —
# `find` itself returns inode order, which would race the read against
# its write counterpart and skip silently.
#
# tests/lifetime/neg/ is excluded: those are NEGATIVE lifetime tests that MUST
# fail (compile-time borrow-check rejection or a runtime free-check), validated
# by tests/lifetime/run_lifetime.sh (which is MODE-aware). This generic runner
# has no MODE/neg semantics and scored the compile-error ones as failures
# (Expected 0), so they belong to the lifetime suite, not the corpus.
TEST_FILES=$(find tests/ -name "*.aiml" | grep -v "__aiml_cache" | grep -v "build" | grep -v "out" | grep -v "/_" | grep -v "lifetime/neg" | sort)

TOTAL=0
PASSED=0
FAILED=0

SKIPPED=0

for TEST in $TEST_FILES; do
    # Check for resource requirements declared in test file comments.
    # A line "# Requires: display" means the test needs a graphical display.
    if grep -q "# Requires: display" "$TEST"; then
        DISPLAY_OK=0
        # Require both an accessible display AND an interactive terminal.
        # Non-interactive contexts (CI, IDE sub-processes) may crash GLFW even
        # when DISPLAY is technically set.
        if tty -s; then
            if [ -n "$DISPLAY" ] && xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
                DISPLAY_OK=1
            elif [ -n "$WAYLAND_DISPLAY" ] && [ -S "/run/user/$(id -u)/$WAYLAND_DISPLAY" ]; then
                DISPLAY_OK=1
            fi
        fi
        if [ $DISPLAY_OK -eq 0 ]; then
            echo "[ SKIP ] $TEST (Requires: display — no interactive display available)" | tee -a "$OUTPUT_FILE"
            ((SKIPPED++))
            ((TOTAL++))
            continue
        fi
    fi

    # Libraries are not standalone tests — skip them.
    if grep -q "# Requires: library" "$TEST"; then
        echo "[ SKIP ] $TEST (library — imported by other tests)" | tee -a "$OUTPUT_FILE"
        ((SKIPPED++))
        ((TOTAL++))
        continue
    fi

    # Peer-process tests need a paired server/client running elsewhere.
    # Without the peer they always fail (connect refused); skip in solo runs.
    if grep -q "# Requires: peer" "$TEST"; then
        echo "[ SKIP ] $TEST (Requires: peer — needs paired peer process)" | tee -a "$OUTPUT_FILE"
        ((SKIPPED++))
        ((TOTAL++))
        continue
    fi

    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"
    echo "Running: $TEST" | tee -a "$OUTPUT_FILE"
    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"

    TEST_LOG_START=$(wc -l < "$OUTPUT_FILE")
    # Run the test, capture stdout and stderr, and append to the output file.
    # Asset-Registry tests use the helper-script: it parses `# AssetRoots:`
    # from the test header and invokes aiml with --asset-roots.
    if grep -q '^# AssetRoots:' "$TEST"; then
        AIML_BIN="$AIML_BIN" tests/_asset_helper/run_asset_test.sh "$TEST" >> "$OUTPUT_FILE" 2>&1
    else
        $AIML_BIN "$TEST" >> "$OUTPUT_FILE" 2>&1
    fi

    # Capture the exit code
    EXIT_CODE=$?
    
    # Determine expected exit code
    EXPECTED_CODE=0
    if [[ "$TEST" == *"test_collections.aiml" ]]; then
        EXPECTED_CODE=30
    elif [[ "$TEST" == *"test.aiml" ]]; then
        EXPECTED_CODE=42
    elif [[ "$TEST" == *"test_asset_neg_"* ]]; then
        # Asset-Registry negative tests: scanner/compiler must hard-fail.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_classgroup_neg_"* ]]; then
        # classgroup negative tests (spec/58): static checks must hard-fail.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_const_var.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_modifiers_const.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_modifiers_private.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_modifiers_too_many_args.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_method_too_many_args.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_static_too_many_decls_per_file.aiml" ]]; then
        # Negative test: more than 3 top-level model/class/interface
        # declarations in one file — see spec/49 §6.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_static_main_no_external_compose.aiml" ]]; then
        # Negative test: 3+ project files but main() does not instantiate
        # a class from another file — see spec/49 §7.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_static_string_switch_error.aiml" ]]; then
        # Negative test: same variable compared against 2+ string literals —
        # hand-rolled string switch, must be enum/polymorphic. See spec/49 §16.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_prefab_static_error.aiml" ]]; then
        # Negative test: engine.instantiate("<Name>") with an undeclared prefab
        # name — silent runtime no-op, must be a hard compile error. See spec/44_8.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_prefab_child_error.aiml" ]]; then
        # Negative test (Tier 2): v.set_text("<child>", …) with a child name not
        # in the prefab's `children:` — silent find_child no-op, hard compile
        # error. See spec/44_8.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_prefab_short_name_error.aiml" ]]; then
        # Negative test: prefab child name shorter than 5 chars — prefab/child
        # names are the cross-file wiring vocabulary, >=5 enforced. See spec/44_8.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_enum_arg_type_error.aiml" ]]; then
        # Negative test: string literal passed to an enum-typed argument —
        # hard type mismatch caught at the call site. See spec/14 §"Argument
        # Type Checking".
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_decl_modifier_third_position_invalid.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_orphan_def.aiml" ]]; then
        # Negative test: a `- def:` without `name:` must fail with a clear
        # diagnostic — see header comment in the test for the rationale.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_void_call_init_error.aiml" ]]; then
        # Negative test: void-call initializer must fail at compile time
        # instead of triggering a stack overflow in LLVM type layout.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_ptr_push_into_value_list_error.aiml" ||
            "$TEST" == *"test_ptr_member_push_into_value_list_error.aiml" ||
            "$TEST" == *"test_ptr_call_push_into_value_list_error.aiml" ||
            "$TEST" == *"test_list_model_nested_get_push_repro.aiml" ]]; then
        # Negative test: push(ptr<T>) into value list<T> — an 8-byte pointer
        # into a value-struct slot corrupts the heap / SIGSEGVs on serialize.
        # Must be a compile error (deref { '?': [x] } or use list<ptr<T>>).
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_string_to_enum_typecheck.aiml" ]]; then
        # Negative test: assigning a string literal to an enum-typed slot must
        # be a compile error (enum is i32 — a string would coerce to garbage).
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_implicit_sibling_visibility_error.aiml" ]]; then
        # Negative test (spec/21 §3): a symbol from a file outside the using
        # file's own import closure is not visible, even if the entry imports it.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_ptr_bind_get_borrow_error.aiml" ]]; then
        # Negative test (spec/61 §5.4): binding a container/store .get/.at read
        # (a ref<T> borrow) to ptr<T> (a unique owner) must be a compile error —
        # it would free a box it does not own. Use ref<T> or get_copy.
        EXPECTED_CODE=1
    fi

    # These negatives must fail at compile time, not via the old runtime corruption.
    if [[ "$TEST" == *"test_ptr_push_into_value_list_error.aiml" ||
          "$TEST" == *"test_ptr_member_push_into_value_list_error.aiml" ||
          "$TEST" == *"test_ptr_call_push_into_value_list_error.aiml" ||
          "$TEST" == *"test_list_model_nested_get_push_repro.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "pointer instead of a value"; then
            EXIT_CODE=99
        fi
    fi

    if [[ "$TEST" == *"test_implicit_sibling_visibility_error.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "VisThing"; then
            EXIT_CODE=99
        fi
    fi

    if [ $EXIT_CODE -eq $EXPECTED_CODE ]; then
        echo "[  OK  ] $TEST (Exit Code: $EXIT_CODE)" | tee -a "$OUTPUT_FILE"
        ((PASSED++))
    else
        echo "[ FAIL ] $TEST (Expected: $EXPECTED_CODE, Got: $EXIT_CODE)" | tee -a "$OUTPUT_FILE"
        ((FAILED++))
    fi
    ((TOTAL++))
    
    echo "" | tee -a "$OUTPUT_FILE"
done

echo "======================" | tee -a "$OUTPUT_FILE"
echo "Test Run Summary:" | tee -a "$OUTPUT_FILE"
echo "Total:  $TOTAL" | tee -a "$OUTPUT_FILE"
echo "Passed: $PASSED" | tee -a "$OUTPUT_FILE"
echo "Failed: $FAILED" | tee -a "$OUTPUT_FILE"
if [ $SKIPPED -gt 0 ]; then
    echo "Skipped: $SKIPPED" | tee -a "$OUTPUT_FILE"
fi

echo "Detailed results saved to $OUTPUT_FILE"
