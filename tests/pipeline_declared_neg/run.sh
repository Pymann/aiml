#!/usr/bin/env bash
# spec/62 §13.5f — was die Umschreibung ABLEHNEN muss.  Negativfaelle, also
# wird die Diagnose von `--check` geprueft und nicht ein Programmlauf.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_declared_neg"
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
want needs_rejected.aiml      "needs"
want host_value_rejected.aiml "host"
want undeclared_axis.aiml     "mode"
# spec/62 §18.1: `image:`/`push:` werden UEBERSETZT und erst beim LAUF
# abgelehnt (F62-124a) — dieselbe Datei ist fuer einen Cluster gueltig, und
# ein Kontroller soll sie lesen koennen (§20.1).  Die Ablehnung haelt
# tests/test_pipeline_local_refuses.aiml.
#
# Was hier bleibt: Formfehler, die der Uebersetzer besitzt (§20.2 F62-150).
want old_name_rejected.aiml   "agent:"
want unknown_key_rejected.aiml "unknown key"
want stages_key_rejected.aiml "steps:"
want steplayer_rejected.aiml  "steplayer"
if [ $fails -ne 0 ]; then echo "pipeline_declared_neg FAIL ($fails)"; exit 1; fi
echo "pipeline_declared_neg PASS"
