#!/usr/bin/env bash
# JIT import cache (spec/37 §3): hit rule, publishing, concurrency, layout, revision sweep.
# Each case runs against its own fresh $AIML_CACHE.  Run from the aiml repo root:
#   bash tests/run_cache_tests.sh
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_cache
PASS=0; FAIL=0

ok()   { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad()  { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
fresh(){ export AIML_CACHE; AIML_CACHE=$(mktemp -d /tmp/aiml_cache_test.XXXXXX); }
run()  { timeout 120 "$AIML" "$FIX/$1.aiml" -r >"$2.out" 2>"$2.err"; }
snap() { find "$AIML_CACHE" -type f -name '*.o' -printf '%P %T@\n' | sort; }
W=$(mktemp -d /tmp/aiml_cache_work.XXXXXX)

# 1. Different sibling sets do not evict each other: A, AB, A, AB → second round all hits.
fresh
run root_a  "$W/a1"; run root_ab "$W/ab1"
s1=$(snap)
run root_a  "$W/a2"; run root_ab "$W/ab2"
s2=$(snap)
if grep -qx 'a=1' "$W/a2.out" && grep -qx 'ab=2' "$W/ab2.out" && [ -n "$s1" ] && [ "$s1" = "$s2" ]; then
  ok "no eviction across sibling sets"
else
  bad "no eviction across sibling sets"; diff <(echo "$s1") <(echo "$s2") | head -10
fi

# 2. A failed import compile publishes nothing; the next run reports the error again.
fresh
run root_broken "$W/b1"; rc1=$?
run root_broken "$W/b2"; rc2=$?
leftover=$(find "$AIML_CACHE" -type f -name '*lib_broken*')
if [ "$rc1" -ne 0 ] && [ "$rc1" -lt 128 ] && [ "$rc2" -ne 0 ] && [ "$rc2" -lt 128 ] \
   && grep -q '__cache_test_no_such_fn' "$W/b2.err" && [ -z "$leftover" ]; then
  ok "failed compile is not cached"
else
  bad "failed compile is not cached (rc1=$rc1 rc2=$rc2 leftover=[$leftover])"; grep -a ERROR "$W/b2.err" | head -3
fi

# 3. Concurrent runs on a cold cache: all succeed, no temp files remain, follow-up is all hits.
fresh
pids=()
for i in 1 2 3 4; do
  run root_a  "$W/pa$i" & pids+=($!)
  run root_ab "$W/pab$i" & pids+=($!)
done
prc=0; for p in "${pids[@]}"; do wait "$p" || prc=1; done
outs_ok=1
for i in 1 2 3 4; do
  grep -qx 'a=1' "$W/pa$i.out" && grep -qx 'ab=2' "$W/pab$i.out" || outs_ok=0
done
tmps=$(find "$AIML_CACHE" -name '*.tmp.*')
s1=$(snap); run root_a "$W/pa9"; run root_ab "$W/pab9"; s2=$(snap)
if [ "$prc" -eq 0 ] && [ "$outs_ok" -eq 1 ] && [ -z "$tmps" ] && [ "$s1" = "$s2" ]; then
  ok "concurrent cold runs"
else
  bad "concurrent cold runs (rc=$prc outputs=$outs_ok tmps=[$tmps])"
fi

# 4. Computing keys writes nothing to stdout.
fresh
run root_a "$W/s1"
if [ "$(cat "$W/s1.out")" = "a=1" ]; then ok "stdout clean"; else bad "stdout clean"; head -3 "$W/s1.out"; fi

# 5. Layout rev_<rev>/<path>__<key32>.o and revision sweep.
fresh
run root_a "$W/l1"
objs=$(cd "$AIML_CACHE" && find . -type f -name '*.o' | sed 's|^\./||')
layout_ok=1; [ -n "$objs" ] || layout_ok=0
while read -r o; do
  [[ "$o" =~ ^rev_[^/]+/[A-Za-z0-9_]+__[0-9a-f]{32}\.o$ ]] || layout_ok=0
done <<< "$objs"
own=$(dirname "$(echo "$objs" | head -1)")
mkdir -p "$AIML_CACHE/rev_zz_old" "$AIML_CACHE/rev_zz_fresh"
touch "$AIML_CACHE/rev_zz_old/x.o" "$AIML_CACHE/$own/stale.o.tmp.99"
touch -d '2 hours ago' "$AIML_CACHE/rev_zz_old" "$AIML_CACHE/$own/stale.o.tmp.99"
run root_a "$W/l2"
if [ "$layout_ok" -eq 1 ] && [ ! -e "$AIML_CACHE/rev_zz_old" ] && [ -d "$AIML_CACHE/rev_zz_fresh" ] \
   && [ ! -e "$AIML_CACHE/$own/stale.o.tmp.99" ] && grep -qx 'a=1' "$W/l2.out"; then
  ok "layout + revision sweep"
else
  bad "layout + revision sweep (layout=$layout_ok)"; echo "$objs" | head -5; ls "$AIML_CACHE"
fi

rm -rf "$W" /tmp/aiml_cache_test.*
echo "cache tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
