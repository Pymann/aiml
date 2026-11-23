#!/bin/bash

SKIP_BUILD=0
RUN_MANUAL=0
# Time limit per test.  WITHOUT it ONE hanging test stops the whole suite —
# exactly that happened on 2026-10-15.  Exceeding it is a
# FAILURE with a reason, not a silent carrying on.
TEST_TIMEOUT="${TEST_TIMEOUT:-600}"
# --only <pattern>: only run matching tests.  Otherwise the suite costs
# the whole corpus every time, even when a change affects one test
# (user directive: only affected tests).
ONLY=""
TAKE_ONLY=0
for arg in "$@"; do
    if [ "$TAKE_ONLY" -eq 1 ]; then ONLY="$arg"; TAKE_ONLY=0; continue; fi
    if [ "$arg" = "--no-build" ] || [ "$arg" = "--test-only" ]; then
        SKIP_BUILD=1
    fi
    if [ "$arg" = "--manual" ]; then
        RUN_MANUAL=1
    fi
    if [ "$arg" = "--only" ]; then
        TAKE_ONLY=1
    fi
done

# exodus comes from the Python venv where there is one (in a container it lives
# elsewhere and is already on PATH).
[ -f ../../exvenv/bin/activate ] && source ../../exvenv/bin/activate
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
# Exclude directories, not NAMES: `grep -v "out"` hit every test with
# "out" in its name ("layout", "routes", "outparam") and swallowed 5 tests since
# forever (measured 17.10.2026).
TEST_FILES=$(find tests/ -name "*.aiml" \
    -not -path "*/__aiml_cache/*" -not -path "*/build/*" -not -path "*/out/*" \
    -not -path "*/_*" -not -path "tests/lifetime/neg/*" | sort)
if [ -n "$ONLY" ]; then
    TEST_FILES=$(echo "$TEST_FILES" | grep -E -- "$ONLY")
    echo "Filter --only '$ONLY': $(echo "$TEST_FILES" | grep -c . ) Test(s)" | tee -a "$OUTPUT_FILE"
fi

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

    # "# Requires: wasm" means the test exercises the Emscripten code path and
    # is meaningless in the native JIT (e.g. the browser input backend, which
    # only exists in the wasm build).  run_wasm_tests.sh runs these.
    if grep -q "# Requires: wasm" "$TEST"; then
        echo "[ SKIP ] $TEST (Requires: wasm — run via run_wasm_tests.sh)" | tee -a "$OUTPUT_FILE"
        ((SKIPPED++))
        ((TOTAL++))
        continue
    fi

    # "# Requires: driver" means the file belongs to a run.sh in its own
    # directory and is not a standalone test — a negative case is meant
    # NOT to compile, and started on its own that looks like a failure.
    # The drivers run at the end of this script.
    if grep -q "# Requires: driver" "$TEST"; then
        echo "[ SKIP ] $TEST (Requires: driver — runs via the run.sh of its directory)" | tee -a "$OUTPUT_FILE"
        ((SKIPPED++))
        ((TOTAL++))
        continue
    fi

    # "# Requires: manual" — the test opens a window and runs until a
    # HUMAN closes it.  In a test loop that is not a test but a
    # halt: on 2026-10-15 the run stood for over 20 minutes in
    # test_mem_render_probe, because nobody sat in front of it.  With --manual they run.
    if grep -q "# Requires: manual" "$TEST"; then
        if [ "${RUN_MANUAL:-0}" != "1" ]; then
            echo "[ SKIP ] $TEST (Requires: manual — waits for a human; start with --manual)" | tee -a "$OUTPUT_FILE"
            ((SKIPPED++))
            ((TOTAL++))
            continue
        fi
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
        AIML_BIN="$AIML_BIN" timeout -k 5 "$TEST_TIMEOUT" tests/_asset_helper/run_asset_test.sh "$TEST" >> "$OUTPUT_FILE" 2>&1
    else
        timeout -k 5 "$TEST_TIMEOUT" $AIML_BIN "$TEST" >> "$OUTPUT_FILE" 2>&1
    fi

    # Capture the exit code
    EXIT_CODE=$?
    
    # Determine expected exit code.
    #
    # First the FILE HEADER: almost every test carries "# Expected exit code: N" there.
    # Until 2026-10-11 nobody read it, and the list below was the only
    # truth — a negative test without an entry counted as a failure, although it
    # did exactly what its header announces (test_mixed_operand_error).
    # The list below stays and OVERRIDES the header, so that no
    # existing entry loses its meaning.
    EXPECTED_CODE=$(grep -m1 -oE '^# Expected exit code: *[0-9]+' "$TEST" | grep -oE '[0-9]+$')
    [ -n "$EXPECTED_CODE" ] || EXPECTED_CODE=0
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
    elif [[ "$TEST" == *"test_static_same_arg_resp_helpers.aiml" ]]; then
        # Negative test: response helpers still grouped by same_arg (spec/49 §6.7).
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_static_main_no_external_compose.aiml" ]]; then
        # Negative test: 3+ project files but main() does not instantiate
        # a class from another file — see spec/49 §7.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_model_set_base_into_field_error.aiml" ]]; then
        # Negative test: a base value into a derived FIELD -- ambiguous, and
        # the old route crashed (spec/15).
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_return_list_no_list_error.aiml" ]]; then
        # Negative test: `return: [a, b, c]` in an i32 function -- before, the last
        # element silently won. Hard error (spec/10).
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
    elif [[ "$TEST" == *"test_process_capture_detach_error.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_print_call_nonstring_error.aiml" ]]; then
        # Negative test (spec/09 §print): a call argument must return string.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_string_to_enum_typecheck.aiml" ]]; then
        # Negative test: assigning a string literal to an enum-typed slot must
        # be a compile error (enum is i32 — a string would coerce to garbage).
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_ref_container_error.aiml" ]]; then
        # Negative test (spec/61 §3): ref<list|map|buffer> is not a valid type.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_cascade_handler_byvalue_error.aiml" ]]; then
        # Negative test (spec/57 §3a): cascade handler must take ref<carrier>, no return.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_static_value_roundtrip_error.aiml" ]]; then
        # Negative test (spec/49 §21): value round-trip through a call is a compile error.
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
    elif [[ "$TEST" == *"test_ptr_set_get_borrow_error.aiml" ]]; then
        # Negative test (spec/61 §5.4): set: [ptr_local, store.get(..)] binds a borrow
        # to a unique owner — compile error like the decl form.
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_db_query_ptr_list_error.aiml" ]]; then
        # Negative test (spec/31 §4.1, spec/61 §5.4): db.query yields store borrows;
        # binding it to list<ptr<T>> must be a compile error. Use list<ref<T>>.
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

    if [[ "$TEST" == *"test_ref_container_error.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "is not a valid type"; then
            EXIT_CODE=99
        fi
    fi
    if [[ "$TEST" == *"test_cascade_handler_byvalue_error.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "spec/57 §3a"; then
            EXIT_CODE=99
        fi
    fi
    if [[ "$TEST" == *"test_static_value_roundtrip_error.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "spec/49 §21"; then
            EXIT_CODE=99
        fi
    fi
    if [[ "$TEST" == *"test_implicit_sibling_visibility_error.aiml" ]]; then
        if ! tail -n "+$((TEST_LOG_START + 1))" "$OUTPUT_FILE" | grep -q "VisThing"; then
            EXIT_CODE=99
        fi
    fi

    # `timeout` reports 124 (or 137 after SIGKILL).  Counting that as an ordinary
    # failure would be misleading — a test that HANGS is something
    # different from one that returns a wrong result.
    if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 137 ]; then
        echo "[ FAIL ] $TEST (TIME LIMIT ${TEST_TIMEOUT}s exceeded — is the test hanging?)" | tee -a "$OUTPUT_FILE"
        ((FAILED++))
        ((TOTAL++))
        echo "" | tee -a "$OUTPUT_FILE"
        continue
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

# ── driver tests: one run.sh per directory ──────────────────────────────────
# These tests need more than "start the file, read the exit code": paired
# processes (bundle_socket) or expected DIAGNOSTICS instead of runs
# (pipeline_purity, pipeline_declared_neg).  They still belong in the
# suite — otherwise they never run along and rot.
for DRIVER in tests/*/run.sh; do
    [ -x "$DRIVER" ] || continue
    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"
    echo "Running driver: $DRIVER" | tee -a "$OUTPUT_FILE"
    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"
    timeout 300 bash "$DRIVER" >> "$OUTPUT_FILE" 2>&1
    DRC=$?
    if [ $DRC -eq 0 ]; then
        echo "[  OK  ] $DRIVER" | tee -a "$OUTPUT_FILE"
        ((PASSED++))
    else
        echo "[ FAIL ] $DRIVER (Exit Code: $DRC)" | tee -a "$OUTPUT_FILE"
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
