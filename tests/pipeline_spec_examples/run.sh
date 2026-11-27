#!/usr/bin/env bash
# SPEC: 62_pipelines
# run.sh — the examples of spec/62 §11 and §13.6 are aiml, and the compiler says so.
# They are taken from the chapter itself (the first yaml block after each heading), so a
# change to the language that breaks them breaks this test, not a reader.  The helpers an
# example only names (read_head, count_warnings, StageRow) are supplied here.
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
AIML="${AIML:-$ROOT/out/aiml/aiml}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0

block_after() {   # the first ```yaml block after the line that starts with $1
    awk -v head="$1" '
        index($0, head) == 1 { seen = 1; next }
        seen && /^```yaml/   { inb = 1; next }
        inb && /^```/        { exit }
        inb                  { print }' "$ROOT/spec/62_pipelines.md"
}

check() {   # name, file
    local out rc
    out=$("$AIML" --check "$2" 2>&1); rc=$?
    if [ $rc -ne 0 ] || printf '%s\n' "$out" | grep -q "^error"; then
        echo "  FAIL $1 does not compile:"; printf '%s\n' "$out" | grep "^error" | head -12
        fail=1
    else
        echo "  ok   $1 compiles"
    fi
}

{ block_after "## 11. Example"
  cat <<'AIML'
- def:
    name: read_head
    args: [[dir, string]]
    return: string
    body:
      - return: { call: [io.read_file, "${dir}/src/.git/HEAD"] }
- def:
    name: count_warnings
    args: [[text, string]]
    return: i32
    body:
      - return: text.len
- def:
    name: main
    return: i32
    body:
      - decl: [pipe, pipeline, { call: [build_matrix] }]
      - return: { call: [pipe.lane_count] }
AIML
} > "$WORK/example_11.aiml"
[ "$(grep -c "pipeline:" "$WORK/example_11.aiml")" -ge 1 ] || { echo "  FAIL §11 block not found"; fail=1; }
check "§11 build_matrix" "$WORK/example_11.aiml"

{ cat <<'AIML'
- model:
    name: StageRow
    fields:
      - [name, string]
      - [label, string]
AIML
  block_after "### 13.6 Generating pipelines"
  cat <<'AIML'
- def:
    name: main
    return: i32
    body:
      - decl: [rows, list<StageRow>]
      - return: { call: [run_generated, rows] }
AIML
} > "$WORK/example_13_6.aiml"
check "§13.6 run_generated" "$WORK/example_13_6.aiml"

[ $fail -eq 0 ] && echo "pipeline_spec_examples: PASS" || { echo "pipeline_spec_examples: FAIL"; exit 1; }
