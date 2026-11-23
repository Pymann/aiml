#!/usr/bin/env bash
# spec/62 §13.5f — what the rewrite must REJECT.  Negative cases, so
# the diagnostic of `--check` is checked and not a program run.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_declared_neg"
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
want needs_rejected.aiml      "needs"
want host_value_rejected.aiml "host"
want undeclared_axis.aiml     "mode"
# spec/62 §18.1: `image:`/`push:` are COMPILED and only rejected at RUN
# time (F62-124a) — the same file is valid for a cluster, and
# a controller should be able to read it (§20.1).  The rejection is held by
# tests/test_pipeline_local_refuses.aiml.
#
# What stays here: form errors that the compiler owns (§20.2 F62-150).
want old_name_rejected.aiml   "agent:"
want unknown_key_rejected.aiml "unknown key"
want stages_key_rejected.aiml "steps:"
want steplayer_rejected.aiml  "steplayer"
if [ $fails -ne 0 ]; then echo "pipeline_declared_neg FAIL ($fails)"; exit 1; fi
echo "pipeline_declared_neg PASS"
