#!/usr/bin/env bash
# Ein Build zur Zeit.  Ueberlappende `exodus build --all` uebernehmen Objekte,
# die der andere Lauf gerade mit altem Inhalt erzeugt hat — die Aenderung ist
# dann NICHT in der Bibliothek, und man misst an Code, der nie gebaut wurde.
# (Zweimal passiert; beide Male kostete es mehrere Runden.)
set -uo pipefail
cd "$(dirname "$0")/.."
LOG="${1:-/tmp/aiml_build.log}"
exec 9>/tmp/.aiml_build.lock
if ! flock -n 9; then
    echo "BUILD LAEUFT BEREITS — abgebrochen" >&2
    exit 2
fi
source ../../exvenv/bin/activate
timeout 1800 exodus build --all > "$LOG" 2>&1
RC=$?
echo "exit=$RC  Fehler=$(grep -cE 'error:' "$LOG")"
grep -E "error:" "$LOG" | sed 's|.*/aiml/||' | sort -u | head -5
exit $RC
