#!/usr/bin/env bash
# Index dump (spec/31 §6.2.1): the cases an aiml unit test cannot check
# honestly, because they swap files underneath the store.
#
# Core case: `cp source target` KEEPS the target's inode.  Whoever restores a single
# .kvwal from a backup has a foreign WAL under a valid-looking
# dump -- the inode matches, and with a larger file the
# watermark matches too.  Only the fingerprint catches it.
#
# Run from the aiml root directory:  bash tests/run_index_dump_gates.sh
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_index_dump
M=/tmp/aiml_idxswap
PASS=0; FAIL=0
ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
W=$(mktemp -d /tmp/aiml_idxswap_work.XXXXXX)

[ -x "$AIML" ] || { echo "no binary at $AIML -- run 'exodus build --all' first"; exit 2; }

rm -rf $M; mkdir -p $M/a $M/b
timeout 120 "$AIML" $FIX/build_a.aiml -r > $W/a.out 2>&1 || { echo "build_a failed"; cat $W/a.out; exit 1; }
timeout 120 "$AIML" $FIX/build_b.aiml -r > $W/b.out 2>&1 || { echo "build_b failed"; cat $W/b.out; exit 1; }

# 1. The dump is created at all, and only on checkpoint.
if [ -f $M/a/idxswap.kvidx ] && [ -f $M/b/idxswap.kvidx ]; then
  ok "checkpoint creates .kvidx"
else
  bad "checkpoint creates .kvidx (a: $(ls $M/a 2>/dev/null | tr '\n' ' '))"
fi

# 2. The dump is considerably smaller than the WAL (otherwise the whole exercise is pointless).
wal=$(stat -c %s $M/a/idxswap.kvwal 2>/dev/null || echo 0)
idx=$(stat -c %s $M/a/idxswap.kvidx 2>/dev/null || echo 0)
if [ "$idx" -gt 0 ] && [ "$idx" -lt "$wal" ]; then
  ok "Dump ($idx B) smaller than WAL ($wal B)"
else
  bad "Dump ($idx B) vs WAL ($wal B)"
fi

# 3. Slip a foreign WAL under the dump -- cp keeps A's inode.
ino_vor=$(stat -c %i $M/a/idxswap.kvwal)
cp $M/b/idxswap.kvwal $M/a/idxswap.kvwal
ino_nach=$(stat -c %i $M/a/idxswap.kvwal)
if [ "$ino_vor" = "$ino_nach" ]; then
  ok "cp kept the inode (an inode check alone does not catch this)"
else
  bad "cp changed the inode ($ino_vor -> $ino_nach) -- the gate does not test what it means to"
fi

timeout 120 "$AIML" $FIX/read_a.aiml -r > $W/r.out 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  bad "reading after the swap crashed (rc=$rc)"; sed -n '1,20p' $W/r.out
else
  len=$(grep -oP '^len=\K[0-9]+' $W/r.out | tail -1)
  a0=$(grep -oP '^has_a0=\K[0-9]+' $W/r.out | tail -1)
  b0=$(grep -oP '^has_b0=\K[0-9]+' $W/r.out | tail -1)
  b7=$(grep -oP '^b7=\K.*' $W/r.out | tail -1)
  if [ "${len:-0}" = "10" ] && [ "${a0:-1}" = "0" ] && [ "${b0:-0}" = "1" ] && [ "$b7" = "aus_b_7" ]; then
    ok "foreign WAL: dump discarded, content of B read (len=$len, b7=$b7)"
  else
    bad "foreign WAL: len=${len:-?} has_a0=${a0:-?} has_b0=${b0:-?} b7=${b7:-?} (expected 10/0/1/aus_b_7)"
    sed -n '1,20p' $W/r.out
  fi
fi
# ── 4. Fallback: the dump cannot be written (spec/31 §6.2.1) ─────────────
# A directory in place of the .kvidx makes rename() fail AND mmap().
# The store must refill the overflow from the list before it releases the
# lock -- otherwise its whole content is gone without a single line missing.
R=/tmp/aiml_ckpt_rueck
rm -rf $R; mkdir -p $R
timeout 120 "$AIML" $FIX/ckpt_rueckfall.aiml -r > $W/rf1.out 2>&1
len1=$(grep -oP 'len=\K[0-9]+' $W/rf1.out | tail -1)
if [ "${len1:-0}" = "1000" ] && [ -f $R/rueck.kvidx ]; then
  ok "fallback pre-run: 1000 keys, dump written"
else
  bad "fallback pre-run: len=${len1:-?}, kvidx=$(ls $R 2>/dev/null | tr '\n' ' ')"
  sed -n '1,10p' $W/rf1.out
fi
rm -f $R/rueck.kvidx && mkdir -p $R/rueck.kvidx
timeout 120 "$AIML" $FIX/ckpt_rueckfall.aiml -r > $W/rf2.out 2>&1
rc=$?
len2=$(grep -oP 'len=\K[0-9]+' $W/rf2.out | tail -1)
e2=$(grep -oP 'erst=\K[^ ]+' $W/rf2.out | tail -1)
l2=$(grep -oP 'letzt=\K[^ ]+' $W/rf2.out | tail -1)
if [ "$rc" -eq 0 ] && [ "${len2:-0}" = "1000" ] && [ "$e2" = "w_0" ] && [ "$l2" = "w_999" ]; then
  ok "dump unwritable: content still readable in the same process (len=$len2)"
else
  bad "dump unwritable: rc=$rc len=${len2:-?} erst=${e2:-?} letzt=${l2:-?} (expected 0/1000/w_0/w_999)"
  sed -n '1,10p' $W/rf2.out
fi

# ── 5. Peak usage: checkpoint releases the RAM index ────────────────────────
# The same 1.6 million keys, once in four phases with a checkpoint per phase,
# once in one go.  If the store releases the overflow after every checkpoint,
# the peak stays at one phase; otherwise both runs are equally expensive.
# Runs for a few minutes -- hence at the very end.
if [ "${IDX_GATES_SCHNELL:-0}" = "1" ]; then
  echo "[ -- ] gate 5 (peak usage) skipped: IDX_GATES_SCHNELL=1"
else
  rm -rf /tmp/aiml_ckpt_phasen /tmp/aiml_ckpt_ein
  mkdir -p /tmp/aiml_ckpt_phasen /tmp/aiml_ckpt_ein
  spitze() { /usr/bin/time -f '%M' -o "$2" timeout 1800 "$AIML" "$1" -r > "$3" 2>"$3.err"; }
  spitze $FIX/ckpt_einphasig.aiml $W/ein.rss $W/ein.out
  spitze $FIX/ckpt_phasen.aiml    $W/pha.rss $W/pha.out
  rss_ein=$(tail -1 $W/ein.rss 2>/dev/null || echo 0)
  rss_pha=$(tail -1 $W/pha.rss 2>/dev/null || echo 0)
  len_ein=$(grep -oP 'len=\K[0-9]+' $W/ein.out | tail -1)
  len_pha=$(grep -oP 'len=\K[0-9]+' $W/pha.out | tail -1)
  p2=$(grep -oP 'p2=\K[^ ]+' $W/pha.out | tail -1)

  if [ "${len_ein:-0}" = "1600000" ] && [ "${len_pha:-0}" = "1600000" ] && [ "$p2" = "wert_700000" ]; then
    ok "both runs hold 1.6 million keys, phase 2 readable after three checkpoints"
  else
    bad "content: len_ein=${len_ein:-?} len_pha=${len_pha:-?} p2=${p2:-?} (expected 1600000/1600000/wert_700000)"
    sed -n '1,10p' $W/pha.out; sed -n '1,10p' $W/pha.out.err
  fi

  # Measured 08.11.2026 (1.6 million keys, 4 phases): with releasing 411 vs.
  # 510 MB = 81%, without releasing 510 vs. 510 = 100%.  No more distance can be
  # gained: the WAL mappings, the list of the last checkpoint and the
  # fresh dump count in BOTH runs, only the overflow differs between
  # them.  88% leaves the measurement 7 points of slack and catches the mutation.
  grenze=$(( rss_ein * 88 / 100 ))
  if [ "${rss_pha:-0}" -gt 0 ] && [ "$rss_pha" -lt "$grenze" ]; then
    ok "peak in phases $((rss_pha/1024)) MB < 88% of single-phase $((rss_ein/1024)) MB"
  else
    bad "peak in phases $((rss_pha/1024)) MB vs. single-phase $((rss_ein/1024)) MB -- the checkpoint does not release the overflow"
  fi
fi


echo "---- $PASS ok, $FAIL failed"
[ "$FAIL" -eq 0 ]
