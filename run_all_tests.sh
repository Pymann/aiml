#!/bin/bash

source ../../exvenv/bin/activate
exodus build
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed. Aborting tests."
    exit 1
fi

# Define the AIML executable path
AIML_BIN="./out/aiml"
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

echo "Running AIML Tests..." | tee -a "$OUTPUT_FILE"
echo "======================" | tee -a "$OUTPUT_FILE"

# Find all .aiml files in the tests/ directory
# Exclude the ones in __aiml_cache or build directories
TEST_FILES=$(find tests/ -name "*.aiml" | grep -v "__aiml_cache" | grep -v "build" | grep -v "out")

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

    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"
    echo "Running: $TEST" | tee -a "$OUTPUT_FILE"
    echo "--------------------------------------------------------" | tee -a "$OUTPUT_FILE"

    # Run the test, capture stdout and stderr, and append to the output file
    # Allow the script to continue even if the command fails
    $AIML_BIN -r "$TEST" >> "$OUTPUT_FILE" 2>&1
    
    # Capture the exit code
    EXIT_CODE=$?
    
    # Determine expected exit code
    EXPECTED_CODE=0
    if [[ "$TEST" == *"test_collections.aiml" ]]; then
        EXPECTED_CODE=30
    elif [[ "$TEST" == *"test.aiml" ]]; then
        EXPECTED_CODE=42
    elif [[ "$TEST" == *"test_const_var.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_modifiers_const.aiml" ]]; then
        EXPECTED_CODE=1
    elif [[ "$TEST" == *"test_modifiers_private.aiml" ]]; then
        EXPECTED_CODE=1
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
