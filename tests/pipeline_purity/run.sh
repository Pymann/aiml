#!/usr/bin/env bash
# spec/62 §10.2 — the purity check.  Every case is a small file;
# what is checked is the DIAGNOSTIC of `--check` (spec/49), not a program run:
# an error at compile time cannot be executed.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_purity"
fails=0

# want <file> <severity|none> <text fragment>
want() {
    local file="$1" sev="$2" needle="${3:-}"
    local out
    out=$("$BIN" --check "$DIR/$file" 2>&1)
    # name length etc. are not the topic here
    out=$(printf '%s\n' "$out" | grep -v "shorter than 3 characters")
    if [ "$sev" = "none" ]; then
        if printf '%s\n' "$out" | grep -qE "^(error|warning)[[:space:]].*(reinheit|purity|pure)"; then
            echo "  FAIL $file: unerwartete Reinheits-Diagnose"
            printf '%s\n' "$out" | sed 's/^/        /' | head -3
            fails=$((fails+1))
        else
            echo "  ok   $file: no purity diagnostic"
        fi
        return
    fi
    if printf '%s\n' "$out" | grep -q "^$sev	.*$needle"; then
        echo "  ok   $file: $sev with '$needle'"
    else
        echo "  FAIL $file: expected $sev with '$needle'"
        printf '%s\n' "$out" | sed 's/^/        /' | head -4
        fails=$((fails+1))
    fi
}

want pure_ok.aiml           none
want impure_time.aiml       error   "time.now"
want sysexe_no_assert.aiml  error   "assert_pure"
want sysexe_assert.aiml     none
want sysenv_banned.aiml     error   "sys.env"
want io_abs.aiml            error   "/etc/hostname"
want io_computed.aiml       warning "io.read_file"

if [ $fails -ne 0 ]; then echo "pipeline_purity FAIL ($fails)"; exit 1; fi
echo "pipeline_purity PASS"
