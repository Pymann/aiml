#!/usr/bin/env bash
# Operator keys in YAML flow maps -- spec/04 "quoting operator keys"
# and spec/09_1 §3.5.
#
# Background: `{ op: [args] }` is a YAML map, `op` a YAML key.  Some
# of the symbolic operators cannot stand bare there.  Three of them
# (`&`, `&&`, `!`) do not even throw -- YAML reads anchor/tag and returns an
# EMPTY key.  In an interpolation the bare form was completely silent until 2026-11-07:
# `${ { %: [lauf, 40] } }` ended up literally as text in the
# string, exit 0.  Measured on 300,000 lines of a measurement probe.
#
# This driver checks not only the exit code, but the TEXT of the
# message -- an error that does not name the cause was exactly the problem.
#
# Invoke from the aiml root directory:  bash tests/_opkey/run.sh
set -u
cd "$(dirname "$0")/../.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_opkey
PASS=0; FAIL=0
W=$(mktemp -d /tmp/aiml_opkey.XXXXXX)
trap 'rm -rf "$W"' EXIT

[ -x "$AIML" ] || { echo "no binary at $AIML -- run 'exodus build --all' first"; exit 2; }

ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }

lauf() { timeout 180 "$AIML" "$FIX/$1.aiml" -r > "$W/$1.out" 2>&1; echo $?; }

# must fail AND name the cause
neg() {
  local name="$1" was="$2"; shift 2
  local rc; rc=$(lauf "$name")
  if [ "$rc" = "0" ]; then
    bad "$name: exit 0 -- $was was silently let through"
    sed -n '1,3p' "$W/$name.out" | sed 's/^/        /'
    return
  fi
  local fehlt=""
  for muster in "$@"; do
    grep -qF -- "$muster" "$W/$name.out" || fehlt="$fehlt
        missing pattern: $muster"
  done
  if [ -n "$fehlt" ]; then
    bad "$name: fails (rc=$rc), but the message does not name the cause$fehlt"
    grep -E '^\[ERROR\]' "$W/$name.out" | head -3 | sed 's/^/        /'
  else
    ok "$name: $was -> hard error with cause"
  fi
}

# must run AND print what is expected
pos() {
  local name="$1" was="$2"; shift 2
  local rc; rc=$(lauf "$name")
  if [ "$rc" != "0" ]; then
    bad "$name: exit $rc, expected 0 -- $was"
    grep -E '^\[ERROR\]' "$W/$name.out" | head -3 | sed 's/^/        /'
    return
  fi
  local fehlt=""
  for muster in "$@"; do
    grep -qF -- "$muster" "$W/$name.out" || fehlt="$fehlt
        missing output: $muster"
  done
  if [ -n "$fehlt" ]; then
    bad "$name: runs, but the output does not match$fehlt"
    grep -vE '^\[(DEBUG|INFO)\]' "$W/$name.out" | head -4 | sed 's/^/        /'
  else
    ok "$name: $was"
  fi
}

echo "---- 1. Interpolation: a bare key was silent (the finding)"
neg interp_percent "bare '%' in an expansion" \
    "operator key '%' in an expansion must be quoted" "spec/09_1"
neg interp_star "bare '*' in an expansion" \
    "operator key '*' in an expansion must be quoted" "spec/09_1"

echo "---- 2. Statement, throw class: there was an error, but without a cause"
neg stmt_percent "bare '%' in a statement" \
    "operator key '%' bare" "{ \"%\": [...] }"
neg stmt_star "bare '*' in a statement" \
    "operator key '*' bare" "{ \"*\": [...] }"

echo "---- 3. Statement, swallow class: YAML does NOT throw"
neg stmt_anchor "bare '&' (anchor, empty key)" \
    "operator key swallowed by YAML"
neg stmt_tilde  "bare '~' (null key)" \
    "operator key swallowed by YAML"
# A bare '!=' is swallowed as a TAG -- the compiler never sees a '!=' and therefore
# cannot point to 'neq' either.  Quoted, one gets the deprecation message.
neg stmt_neq    "bare '!=' (tag, empty key)" \
    "operator key swallowed by YAML"

neg quoted_neq_deprecated "quoted '!=' reaches the deprecation check" \
    "Operator '!=' is not allowed. Use 'neq' instead."

echo "---- 4. The quoted form keeps computing"
pos quoted_ok "quoted operators in statement and expansion" \
    "zitiert rest=1 prod=42 grg=1 ungl=1" \
    "expansion rest=23 prod=286" \
    "blank plus=144 minus=140 durch=13"
pos quoted_all_ops "every symbolic operator quoted" \
    "prod=42 rest=1 grg=1 gge=1 bde=1 ode=1 nic=1" \
    "adresse-gelesen=143"

echo "---- 5. The limit of the rule stays in place"
pos prose "prose \${...} stays literal text" \
    'one writes an expansion as ${...} in the text'
pos esc_quote_fn "escaped quotation mark in an expansion (spec/09_1 §3.4)" \
    "name: [xyz]"
pos esc_quote_member "the same as a member call" \
    "name: [x]"
neg bare_quote "a bare quotation mark ends the scalar" \
    "YAML parse error"

echo "---- $PASS ok, $FAIL failed"
[ "$FAIL" -eq 0 ]
