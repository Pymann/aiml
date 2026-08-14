#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PERF_DIR="$ROOT_DIR/performance"
CASES_DIR="$PERF_DIR/cases"
RESULTS_DIR="$PERF_DIR/results"
AIML_BIN="$ROOT_DIR/out/aiml/aiml"

mkdir -p "$RESULTS_DIR"

if [ ! -x "$AIML_BIN" ]; then
    echo "AIML binary not found at $AIML_BIN"
    echo "Build the compiler first."
    exit 1
fi

if ! command -v /usr/bin/time >/dev/null 2>&1; then
    echo "/usr/bin/time is required"
    exit 1
fi

run_case() {
    local case_name="$1"
    local input_file="$CASES_DIR/${case_name}.aiml"
    local work_dir="$RESULTS_DIR/$case_name"
    local obj_file="$work_dir/${case_name}.o"
    local exe_file="$work_dir/${case_name}.bin"

    if [ ! -f "$input_file" ]; then
        echo "Missing benchmark case: $input_file"
        return 1
    fi

    mkdir -p "$work_dir"

    echo "============================================================"
    echo "CASE: $case_name"
    echo "============================================================"

    echo "[1/3] JIT cold run"
    /usr/bin/time -f "real=%e user=%U sys=%S maxrss=%M" \
        "$AIML_BIN" "$input_file" \
        > "$work_dir/jit.stdout.txt" \
        2> "$work_dir/jit.time.txt"
    cat "$work_dir/jit.time.txt"

    echo "[2/3] AOT compile"
    /usr/bin/time -f "real=%e user=%U sys=%S maxrss=%M" \
        "$AIML_BIN" "$input_file" -o "$exe_file" \
        > "$work_dir/aot_compile.stdout.txt" \
        2> "$work_dir/aot_compile.time.txt"
    cat "$work_dir/aot_compile.time.txt"

    echo "[3/3] AOT binary run"
    /usr/bin/time -f "real=%e user=%U sys=%S maxrss=%M" \
        "$exe_file" \
        > "$work_dir/aot_run.stdout.txt" \
        2> "$work_dir/aot_run.time.txt"
    cat "$work_dir/aot_run.time.txt"
}

if [ "${1:-}" != "" ]; then
    run_case "$1"
    exit 0
fi

for case_file in "$CASES_DIR"/*.aiml; do
    case_name="$(basename "$case_file" .aiml)"
    run_case "$case_name"
done
