#!/usr/bin/env bash
# spec/62 §5.5/§13.5g — die Graph-Pruefungen.  Negativfaelle, also wird die
# Diagnose von `--check` geprueft und kein Programmlauf.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_graph_neg"
fails=0
want() {
    local file="$1" needle="$2" out
    out=$("$BIN" --check "$DIR/$file" 2>&1 | grep -v "shorter than 3 characters")
    if printf '%s\n' "$out" | grep -q "$needle"; then
        echo "  ok   $file: abgelehnt mit '$needle'"
    else
        echo "  FAIL $file: erwartet '$needle'"
        printf '%s\n' "$out" | sed 's/^/        /' | head -4
        fails=$((fails+1))
    fi
}
want once_with_axis.aiml       "per: once"
want needs_unknown.aiml        "gibtsnicht"
want duplicate_step_name.aiml  "twice"
if [ $fails -ne 0 ]; then echo "pipeline_graph_neg FAIL ($fails)"; exit 1; fi
echo "pipeline_graph_neg PASS"
