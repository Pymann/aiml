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

echo "Running AIML Tests..." | tee -a "$OUTPUT_FILE"
echo "======================" | tee -a "$OUTPUT_FILE"

# Find all .aiml files in the tests/ directory
# Exclude the ones in __aiml_cache or build directories
TEST_FILES=$(find tests/ -name "*.aiml" | grep -v "__aiml_cache" | grep -v "build" | grep -v "out")

TOTAL=0
PASSED=0
FAILED=0

for TEST in $TEST_FILES; do
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

echo "Detailed results saved to $OUTPUT_FILE"
