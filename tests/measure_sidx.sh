#!/usr/bin/env bash
# Was der Nebenindex-Dump beim Oeffnen spart (spec/45 "Persistence").
# Gemessen wird dasselbe Programm zweimal: einmal mit den .kvsidx-Dateien,
# einmal ohne sie -- der Unterschied IST das Auffuellen.
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_sidx
M=/tmp/aiml_sidxmess
W=$(mktemp -d /tmp/aiml_sidxmess_work.XXXXXX)
[ -x "$AIML" ] || { echo "kein Binary unter $AIML"; exit 2; }

echo "=== Schreiben (300000 Zeilen, 2 indizierte Felder) ==="
rm -rf $M
/usr/bin/time -f "  schreiben+checkpoint: %e s, RSS %M KB" \
  timeout 1800 "$AIML" $FIX/gross_write.aiml -r > $W/write.out 2>&1
tail -3 $W/write.out
# Ohne Daten ist jede Folgezahl eine Scheinbestzahl -- hier abbrechen, nicht
# weitermessen (Lehre vom 06.11.2026).
if ! grep -q "GROSS_CKPT=0 ZEILEN=300000" $W/write.out; then
  echo "ABBRUCH: das Schreiben lief nicht durch -- keine Messung"
  rm -rf $W
  exit 1
fi

echo "=== Dateigroessen ==="
ls -la $M | awk '{printf "  %10s  %s\n", $5, $9}' | grep -v '^\s*$'

lauf() {   # $1 = Beschriftung
  /usr/bin/time -f "  $1: %e s, RSS %M KB (anon+file)" \
    timeout 1800 "$AIML" $FIX/gross_read.aiml -r 2>&1 | tail -2
}

echo "=== Oeffnen MIT Dump ==="
lauf "mit Dump   "
lauf "mit Dump   "

echo "=== Oeffnen OHNE Nebenindex-Dump (nur die .kvsidx weg) ==="
mkdir -p $W/weg && mv $M/*.kvsidx $W/weg/
lauf "ohne Dump  "
lauf "ohne Dump  "
mv $W/weg/*.kvsidx $M/

echo "=== Oeffnen OHNE jeden Dump (auch den Primaerindex) ==="
mv $M/*.kvsidx $W/weg/ && mv $M/mess.kvidx $W/weg/
lauf "gar kein Dump"
mv $W/weg/* $M/
rm -rf $W
