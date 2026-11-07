#!/usr/bin/env bash
# Index-Dump (spec/31 §6.2.1): die Faelle, die ein aiml-Unit-Test nicht ehrlich
# pruefen kann, weil sie Dateien unter dem Store austauschen.
#
# Kernfall: `cp quelle ziel` BEHAELT die Inode des Ziels.  Wer eine einzelne
# .kvwal aus einem Backup zuruecklegt, hat eine fremde WAL unter einem gueltig
# aussehenden Dump -- Inode passt, und bei groesserer Datei passt auch der
# Wasserstand.  Nur der Fingerabdruck faellt darauf.
#
# Aufruf aus dem aiml-Wurzelverzeichnis:  bash tests/run_index_dump_gates.sh
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_index_dump
M=/tmp/aiml_idxswap
PASS=0; FAIL=0
ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
W=$(mktemp -d /tmp/aiml_idxswap_work.XXXXXX)

[ -x "$AIML" ] || { echo "kein Binary unter $AIML -- zuerst 'exodus build --all'"; exit 2; }

rm -rf $M; mkdir -p $M/a $M/b
timeout 120 "$AIML" $FIX/build_a.aiml -r > $W/a.out 2>&1 || { echo "build_a fehlgeschlagen"; cat $W/a.out; exit 1; }
timeout 120 "$AIML" $FIX/build_b.aiml -r > $W/b.out 2>&1 || { echo "build_b fehlgeschlagen"; cat $W/b.out; exit 1; }

# 1. Der Dump entsteht ueberhaupt, und nur bei checkpoint.
if [ -f $M/a/idxswap.kvidx ] && [ -f $M/b/idxswap.kvidx ]; then
  ok "checkpoint legt .kvidx an"
else
  bad "checkpoint legt .kvidx an (a: $(ls $M/a 2>/dev/null | tr '\n' ' '))"
fi

# 2. Der Dump ist erheblich kleiner als die WAL (sonst lohnt die ganze Uebung nicht).
wal=$(stat -c %s $M/a/idxswap.kvwal 2>/dev/null || echo 0)
idx=$(stat -c %s $M/a/idxswap.kvidx 2>/dev/null || echo 0)
if [ "$idx" -gt 0 ] && [ "$idx" -lt "$wal" ]; then
  ok "Dump ($idx B) kleiner als WAL ($wal B)"
else
  bad "Dump ($idx B) vs WAL ($wal B)"
fi

# 3. Fremde WAL unter den Dump schieben -- cp behaelt die Inode von A.
ino_vor=$(stat -c %i $M/a/idxswap.kvwal)
cp $M/b/idxswap.kvwal $M/a/idxswap.kvwal
ino_nach=$(stat -c %i $M/a/idxswap.kvwal)
if [ "$ino_vor" = "$ino_nach" ]; then
  ok "cp behielt die Inode (Inode-Pruefung allein greift hier nicht)"
else
  bad "cp hat die Inode gewechselt ($ino_vor -> $ino_nach) -- Gate prueft nicht das Gemeinte"
fi

timeout 120 "$AIML" $FIX/read_a.aiml -r > $W/r.out 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  bad "Lesen nach dem Austausch stuerzte ab (rc=$rc)"; sed -n '1,20p' $W/r.out
else
  len=$(grep -oP '^len=\K[0-9]+' $W/r.out | tail -1)
  a0=$(grep -oP '^has_a0=\K[0-9]+' $W/r.out | tail -1)
  b0=$(grep -oP '^has_b0=\K[0-9]+' $W/r.out | tail -1)
  b7=$(grep -oP '^b7=\K.*' $W/r.out | tail -1)
  if [ "${len:-0}" = "10" ] && [ "${a0:-1}" = "0" ] && [ "${b0:-0}" = "1" ] && [ "$b7" = "aus_b_7" ]; then
    ok "fremde WAL: Dump verworfen, Inhalt von B gelesen (len=$len, b7=$b7)"
  else
    bad "fremde WAL: len=${len:-?} has_a0=${a0:-?} has_b0=${b0:-?} b7=${b7:-?} (erwartet 10/0/1/aus_b_7)"
    sed -n '1,20p' $W/r.out
  fi
fi

echo "---- $PASS ok, $FAIL fehlgeschlagen"
[ "$FAIL" -eq 0 ]
