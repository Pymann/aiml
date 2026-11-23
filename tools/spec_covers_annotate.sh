#!/usr/bin/env bash
# spec_covers_annotate.sh — schreibt die `# SPEC:`-Kopfzeile in die Tests.
#
# Teilung der Arbeit: die ZUORDNUNG macht tools/spec_covers.aiml (lesend, in aiml),
# das EINFUEGEN macht dieses Skript. `sed -i '1i'` ist byte-treu, und jede Aenderung
# ist genau eine zusaetzliche Zeile — das laesst sich mit `git diff --stat`
# vollstaendig pruefen, was bei Stringbau in aiml nicht so einfach waere.
#
# Herkunft steht IM Schluessel, damit Zusage und Vermutung greppbar getrennt sind:
#   `# SPEC:`   der Test nennt die Kapitel selbst       -> uebernommen
#   `# SPEC?:`  aus dem Dateinamen abgeleitet           -> zu bestaetigen
# Der Schluessel heisst SPEC und nicht COVERS, weil `# COVERS:` in den Laboren
# (tests/closures, tests/clearance) schon als PROSA-Feld belegt ist — zwei
# Bedeutungen fuer einen Schluessel waere genau die Doppeldeutigkeit, die ein
# spaeteres Werkzeug stillschweigend falsch liest.
# Dateien ohne beides werden NICHT angefasst und aufgelistet: sie brauchen einen
# Menschen, und eine geratene Annotation waere schlimmer als keine.
#
#   bash tools/spec_covers_annotate.sh            # Trockenlauf (zaehlt nur)
#   WRITE=1 bash tools/spec_covers_annotate.sh    # schreibt
set -uo pipefail
cd "$(dirname "$0")/.."
REPORT="${REPORT:-logs/covers_report.tsv}"
WRITE="${WRITE:-0}"
[ -f "$REPORT" ] || { echo "Report fehlt: $REPORT — erst tools/spec_covers.aiml laufen lassen"; exit 1; }

n_auth=0; n_name=0; n_none=0; n_skip=0
: > logs/covers_needs_human.txt
while IFS=$'\t' read -r fpath acol ncol dcol; do
  [ -n "${fpath:-}" ] || continue
  [ -f "$fpath" ] || continue
  auth="${acol#A=}"; byname="${ncol#N=}"
  if grep -q "^# SPEC" "$fpath"; then n_skip=$((n_skip+1)); continue; fi
  if [ -n "$auth" ]; then
    line="# SPEC: $auth"
    n_auth=$((n_auth+1))
  elif [ -n "$byname" ]; then
    line="# SPEC?: $byname   # derived from the file name, to be confirmed"
    n_name=$((n_name+1))
  else
    echo "$fpath" >> logs/covers_needs_human.txt
    n_none=$((n_none+1)); continue
  fi
  if [ "$WRITE" = "1" ]; then
    sed -i "1i $line" "$fpath" || { echo "FEHLER bei $fpath"; exit 1; }
  fi
done < "$REPORT"

echo "COVERS:   $n_auth  (aus Spec-Zitaten im Test)"
echo "COVERS?:  $n_name  (aus dem Dateinamen, zu bestaetigen)"
echo "ohne:     $n_none  -> logs/covers_needs_human.txt"
echo "schon da: $n_skip"
[ "$WRITE" = "1" ] || echo "(Trockenlauf — WRITE=1 zum Schreiben)"
