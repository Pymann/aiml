#!/usr/bin/env bash
# run.sh — the measurement switches from spec/70 §11.3: --phase-times, --parse-only, --no-cache.
#
# The point of the switches is a number someone relies on.  So the cases
# here do not check "runs through" but the form: exactly the counters from
# §11.2, integer milliseconds, on STDERR (stdout belongs to the program), and
# -- most importantly -- that the switches do NOT change the result (F70-93).
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
DIR=tests/phase_times
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

echo "── Messschalter (spec/70 §11) ──"
fail=0
ok()   { printf "  ok   %s\n" "$1"; }
bad()  { printf "  FAIL %s\n" "$1"; fail=1; }

# ── F70-90: --phase-times names exactly the counters from §11.2, on stderr ──
err="$WORK/pt.err"; out="$WORK/pt.out"
"$AIML" "$DIR/probe.aiml" --phase-times -o "$WORK/probe.ll" > "$out" 2> "$err"
rc=$?
[ $rc -eq 0 ] || bad "--phase-times: rc=$rc"
for want in parse reparse ir emit total; do
    if grep -qE "^phase	$want	[0-9]+$" "$err"; then ok "counter '$want' on stderr, integer"
    else bad "counter '$want' missing or has the wrong form"; fi
done
if grep -qE "^phase	" "$out"; then bad "counters land on stdout — stdout belongs to the program (F70-90)"
else ok "stdout stays free of measurement data"; fi
# total must not be smaller than the largest single phase
tot=$(grep -E "^phase	total	" "$err" | cut -f3)
mx=$(grep -E "^phase	(parse|reparse|ir|emit)	" "$err" | cut -f3 | sort -n | tail -1)
if [ -n "$tot" ] && [ -n "$mx" ] && [ "$tot" -ge "$mx" ]; then ok "total >= largest single phase ($tot >= $mx)"
else bad "total=$tot is smaller than the largest phase=$mx"; fi

# ── total ALSO covers the object path (compileToObject used to run outside it there) ──
oerr="$WORK/obj.err"
"$AIML" "$DIR/probe.aiml" --phase-times -o "$WORK/probe.o" > /dev/null 2> "$oerr"
otot=$(grep -E "^phase	total	" "$oerr" | cut -f3)
omx=$(grep -E "^phase	(parse|reparse|ir|emit)	" "$oerr" | cut -f3 | sort -n | tail -1)
if [ -n "$otot" ] && [ -n "$omx" ] && [ "$otot" -ge "$omx" ]; then ok "object path: total >= largest phase ($otot >= $omx)"
else bad "object path: total=$otot < largest phase=$omx — the object output falls outside total"; fi

# ── F70-91: --parse-only produces nothing and only runs up to the AST ──
rm -f "$WORK/po.ll"
"$AIML" "$DIR/probe.aiml" --parse-only -o "$WORK/po.ll" > "$WORK/po.out" 2> "$WORK/po.err"
rc=$?
[ $rc -eq 0 ] || bad "--parse-only: rc=$rc"
if [ -s "$WORK/po.ll" ]; then bad "--parse-only wrote IR anyway (F70-91)"
else ok "--parse-only writes nothing"; fi
"$AIML" "$DIR/probe.aiml" --parse-only --phase-times -o "$WORK/po2.ll" > /dev/null 2> "$WORK/po2.err"
if grep -qE "^phase	(ir|emit)	" "$WORK/po2.err"; then
    bad "--parse-only reports ir/emit — those phases do not run at all (F70-91)"
else ok "--parse-only reports no ir/emit phase"; fi
if grep -qE "^phase	parse	[0-9]+$" "$WORK/po2.err"; then ok "--parse-only reports the parse phase"
else bad "--parse-only does not report the parse phase"; fi

# ── F70-91: a frontend error stays an error ──
want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$DIR/neg_frontend_error.aiml" | sed 's/# EXPECT-ERROR: //')
msg=$("$AIML" "$DIR/neg_frontend_error.aiml" --parse-only -o "$WORK/neg.ll" 2>&1); rc=$?
if [ $rc -eq 0 ]; then bad "--parse-only swallowed the frontend error"
elif ! echo "$msg" | grep -qF "$want"; then bad "message without '$want'"; echo "$msg" | head -3 | sed 's/^/        /'
else ok "frontend error is reported with --parse-only too"; fi

# ── F70-92/93: --no-cache does not change the result ──
"$AIML" "$DIR/probe.aiml" -o "$WORK/a.ll" > /dev/null 2>&1
"$AIML" "$DIR/probe.aiml" --no-cache -o "$WORK/b.ll" > /dev/null 2>&1
if [ ! -s "$WORK/a.ll" ] || [ ! -s "$WORK/b.ll" ]; then bad "--no-cache: one of the two outputs is missing"
elif cmp -s "$WORK/a.ll" "$WORK/b.ll"; then ok "--no-cache yields byte-identical IR (F70-93)"
else bad "--no-cache changes the output (F70-93)"; fi
if "$AIML" "$DIR/probe.aiml" --no-cache --phase-times -o "$WORK/c.ll" 2>&1 | grep -q "unknown"; then
    bad "--no-cache is not recognised"
else ok "--no-cache is accepted"; fi

[ $fail -eq 0 ] && echo "── phase_times PASS ──" || echo "── phase_times FAIL ──"
exit $fail
