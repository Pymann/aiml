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
# spec/62 §9.1: the probe on the stage (F62-160/163).
want stage_resolve_missing.aiml   "no \`resolve:\`"
want stage_resolve_form.aiml      "shell line"
want pipeline_resolve_rejected.aiml "unknown key"
# spec/62 §18.6: secrets on the stage (F62-165/167).
want secret_value_rejected.aiml   "an entry is { name, env }"
want secret_env_reserved.aiml     "belongs to the executor"
want secret_cache_pure.aiml       "a stage with secrets is opaque"
# spec/62 F62-111/F62-168/F62-65: stage needs, outputs, the keys of a step.
want stage_needs_unknown.aiml     "no step of an earlier"
want stage_outputs_rejected.aiml  "outputs"
want output_outside_ws.aiml       "must lie in the workspace"
want step_stage_attr.aiml         "attribute of the stage"
want step_unknown_key.aiml        "unknown key \`cahce:\`"
# spec/62 F62-31 / F62-61: env of a pure step, the fields of exec_defaults.
want pure_exe_env.aiml            "inherits the environment"
want exec_defaults_field.aiml     "no field of ExeOptions"
if [ $fails -ne 0 ]; then echo "pipeline_declared_neg FAIL ($fails)"; exit 1; fi
echo "pipeline_declared_neg PASS"
