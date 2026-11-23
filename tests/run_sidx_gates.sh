#!/usr/bin/env bash
# Secondary index dump (spec/45 "Persistence"): the cases an aiml unit test
# cannot check honestly, because they swap files underneath the store.
#
# The most important one is gate 3.  A green unit test does NOT prove that the
# dump is used -- if its stamp fails, the store silently refills and
# everything is still right.  So here a VALID file gets POISONED: one
# key in the string block is rewritten, the stamp stays valid.  If the
# poisoned value comes out afterwards, the dump answered.  (On 06.11.2026
# a bug in the refill produced its own best figure -- 0.06 s, because
# nothing was done.  This check is the lesson from that.)
#
# Run from the aiml root directory:  bash tests/run_sidx_gates.sh
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_sidx
M=/tmp/aiml_sidxgate
PASS=0; FAIL=0
ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
W=$(mktemp -d /tmp/aiml_sidxgate_work.XXXXXX)

[ -x "$AIML" ] || { echo "no binary at $AIML -- run 'exodus build --all' first"; exit 2; }

rm -rf $M; mkdir -p $M/a $M/b
timeout 120 "$AIML" $FIX/build.aiml -r > $W/build.out 2>&1 \
  || { echo "build failed"; cat $W/build.out; exit 1; }
timeout 120 "$AIML" $FIX/build_other.aiml -r > $W/other.out 2>&1 \
  || { echo "build_other failed"; cat $W/other.out; exit 1; }

NAM=$M/a/sidxgate__name.kvsidx
SKU=$M/a/sidxgate__sku.kvsidx
TAG=$M/a/sidxgate__tag.kvsidx

# 1. One file per indexed field, and only on checkpoint.
if [ -f "$NAM" ] && [ -f "$SKU" ] && [ -f "$TAG" ]; then
  ok "checkpoint creates one .kvsidx per field"
else
  bad "checkpoint creates one .kvsidx per field (present: $(ls $M/a | tr '\n' ' '))"
fi

# 2. Header: magic and counts are right.
magic=$(head -c 8 "$NAM" 2>/dev/null)
vals=$(od -An -tu4 -j48 -N4 "$NAM" 2>/dev/null | tr -d ' ')
keys=$(od -An -tu4 -j52 -N4 "$NAM" 2>/dev/null | tr -d ' ')
if [ "$magic" = "AIMLKVSX" ] && [ "$vals" = "2" ] && [ "$keys" = "3" ]; then
  ok "Kopf: AIMLKVSX, 2 Feldwerte, 3 Paare"
else
  bad "header: magic='$magic' val_count=$vals key_count=$keys (expected AIMLKVSX/2/3)"
fi

# 3. THE proof: poison a valid file, leave the stamp untouched.
cp "$NAM" $W/name.gut
python3 - "$NAM" <<'PY'
import sys
p=sys.argv[1]
d=bytearray(open(p,'rb').read())
at=d.find(b'aaa\x00', 64)
assert at > 0, "key 'aaa' not found in the string block"
d[at:at+3]=b'zzz'
open(p,'wb').write(d)
PY
timeout 120 "$AIML" $FIX/read.aiml -r > $W/poison.out 2>&1
if grep -q "HIT=zzz" $W/poison.out; then
  ok "the dump answers the query (poisoned key comes through)"
else
  bad "poisoned key did NOT come through -- it was refilled, the dump is dead weight"
  sed -n '1,12p' $W/poison.out
fi
# The other fields must stay untouched by it.
if grep -q "SKU_S1=aaa" $W/poison.out && grep -q "WERKZEUG_N=2" $W/poison.out; then
  ok "one poisoned field leaves the others alone (one file per field)"
else
  bad "one poisoned field dragged the others down with it"
  sed -n '1,12p' $W/poison.out
fi
cp $W/name.gut "$NAM"

# 4. Damaged header: discarded, result still right.
printf 'KAPUTT!!' | dd of="$NAM" bs=1 seek=0 conv=notrunc status=none
timeout 120 "$AIML" $FIX/read.aiml -r > $W/broken.out 2>&1
if grep -q "HAMMER_N=2" $W/broken.out && grep -q "HIT=aaa" $W/broken.out; then
  ok "broken header: discarded, fully refilled, result right"
else
  bad "broken header: result wrong"
  sed -n '1,12p' $W/broken.out
fi
cp $W/name.gut "$NAM"

# 5. Slip in a foreign .kvsidx -- cp keeps the target's inode.
ino_vor=$(stat -c %i "$NAM")
cp $M/b/sidxgate__name.kvsidx "$NAM"
ino_nach=$(stat -c %i "$NAM")
if [ "$ino_vor" = "$ino_nach" ]; then
  ok "cp kept the inode (otherwise gate 5 does not test what it means to)"
else
  bad "cp changed the inode ($ino_vor -> $ino_nach)"
fi
timeout 120 "$AIML" $FIX/read.aiml -r > $W/foreign.out 2>&1
if grep -q "HAMMER_N=2" $W/foreign.out && ! grep -q "HIT=xxx" $W/foreign.out; then
  ok "foreign .kvsidx: stamp fails (foreign WAL inode), correctly refilled"
else
  bad "foreign .kvsidx was accepted -- keys from the other store visible"
  sed -n '1,12p' $W/foreign.out
fi
cp $W/name.gut "$NAM"

# 6. File gone: silently falls back to refilling.
rm -f "$NAM"
timeout 120 "$AIML" $FIX/read.aiml -r > $W/gone.out 2>&1
if grep -q "HAMMER_N=2" $W/gone.out; then
  ok "missing .kvsidx: refilled, result right"
else
  bad "missing .kvsidx: result wrong"
  sed -n '1,12p' $W/gone.out
fi

echo "---"
echo "sidx gates: $PASS ok, $FAIL failed"
rm -rf $W
[ $FAIL -eq 0 ] || exit 1
