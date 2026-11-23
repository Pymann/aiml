#!/usr/bin/env bash
# spec/62 §5.5/§13.5g — the graph checks.  Negative cases, so the
# diagnostic of `--check` is checked and not a program run.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_graph_neg"
fails=0
want() {
    local file="$1" needle="$2" out
    out=$("$BIN" --check "$DIR/$file" 2>&1 | grep -v "shorter than 3 characters")
    if printf '%s\n' "$out" | grep -q "$needle"; then
        echo "  ok   $file: rejected with '$needle'"
    else
        echo "  FAIL $file: expected '$needle'"
        printf '%s\n' "$out" | sed 's/^/        /' | head -4
        fails=$((fails+1))
    fi
}
want once_with_axis.aiml       "per: once"
want needs_unknown.aiml        "gibtsnicht"
want duplicate_step_name.aiml  "twice"
if [ $fails -ne 0 ]; then echo "pipeline_graph_neg FAIL ($fails)"; exit 1; fi
echo "pipeline_graph_neg PASS"
