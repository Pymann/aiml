#!/usr/bin/env bash
# spec/62 §10.2 — die Reinheitspruefung.  Jeder Fall ist eine kleine Datei;
# geprueft wird die DIAGNOSE von `--check` (spec/49), nicht ein Programmlauf:
# ein Fehler bei der Uebersetzung laesst sich nicht ausfuehren.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/pipeline_purity"
fails=0

# want <datei> <severity|none> <textstueck>
want() {
    local file="$1" sev="$2" needle="${3:-}"
    local out
    out=$("$BIN" --check "$DIR/$file" 2>&1)
    # Namenslaenge u.ae. sind hier nicht das Thema
    out=$(printf '%s\n' "$out" | grep -v "shorter than 3 characters")
    if [ "$sev" = "none" ]; then
        if printf '%s\n' "$out" | grep -qE "^(error|warning)[[:space:]].*(reinheit|purity|pure)"; then
            echo "  FAIL $file: unerwartete Reinheits-Diagnose"
            printf '%s\n' "$out" | sed 's/^/        /' | head -3
            fails=$((fails+1))
        else
            echo "  ok   $file: keine Reinheits-Diagnose"
        fi
        return
    fi
    if printf '%s\n' "$out" | grep -q "^$sev	.*$needle"; then
        echo "  ok   $file: $sev mit '$needle'"
    else
        echo "  FAIL $file: erwartet $sev mit '$needle'"
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
