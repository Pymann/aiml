#!/usr/bin/env bash
# What the secondary index dump saves on opening (spec/45 "Persistence").
# The same program is measured twice: once with the .kvsidx files,
# once without them -- the difference IS the refill.
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_sidx
M=/tmp/aiml_sidxmess
W=$(mktemp -d /tmp/aiml_sidxmess_work.XXXXXX)
[ -x "$AIML" ] || { echo "no binary at $AIML"; exit 2; }

echo "=== write (300000 rows, 2 indexed fields) ==="
rm -rf $M
/usr/bin/time -f "  schreiben+checkpoint: %e s, RSS %M KB" \
  timeout 1800 "$AIML" $FIX/gross_write.aiml -r > $W/write.out 2>&1
tail -3 $W/write.out
# Without data every following figure is a fake best figure -- abort here, do not
# keep measuring (lesson of 06.11.2026).
if ! grep -q "GROSS_CKPT=0 ZEILEN=300000" $W/write.out; then
  echo "ABORT: the write did not run through -- no measurement"
  rm -rf $W
  exit 1
fi

echo "=== file sizes ==="
ls -la $M | awk '{printf "  %10s  %s\n", $5, $9}' | grep -v '^\s*$'

lauf() {   # $1 = label
  /usr/bin/time -f "  $1: %e s, RSS %M KB (anon+file)" \
    timeout 1800 "$AIML" $FIX/gross_read.aiml -r 2>&1 | tail -2
}

echo "=== open WITH dump ==="
lauf "with dump  "
lauf "with dump  "

echo "=== open WITHOUT secondary index dump (only the .kvsidx removed) ==="
mkdir -p $W/weg && mv $M/*.kvsidx $W/weg/
lauf "without dump"
lauf "without dump"
mv $W/weg/*.kvsidx $M/

echo "=== open WITHOUT any dump (the primary index too) ==="
mv $M/*.kvsidx $W/weg/ && mv $M/mess.kvidx $W/weg/
lauf "no dump at all"
mv $W/weg/* $M/
rm -rf $W
