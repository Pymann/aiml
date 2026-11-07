#!/usr/bin/env bash
# Operator-Schluessel in YAML-Flow-Maps -- spec/04 "Operator-Schluessel zitieren"
# und spec/09_1 §3.5.
#
# Hintergrund: `{ op: [args] }` ist eine YAML-Map, `op` ein YAML-Schluessel.  Ein
# Teil der symbolischen Operatoren kann dort nicht blank stehen.  Drei davon
# (`&`, `&&`, `!`) werfen nicht einmal -- YAML liest Anker/Tag und liefert einen
# LEEREN Schluessel.  In einer Interpolation war die blanke Form bis 07.11.2026
# ganz still: `${ { %: [lauf, 40] } }` landete wortwoertlich als Text in der
# Zeichenkette, Austritt 0.  Gemessen an 300.000 Zeilen einer Messprobe.
#
# Dieser Treiber prueft nicht nur den Austrittscode, sondern den TEXT der
# Meldung -- ein Fehler, der die Ursache nicht nennt, war genau das Problem.
#
# Aufruf aus dem aiml-Wurzelverzeichnis:  bash tests/_opkey/run.sh
set -u
cd "$(dirname "$0")/../.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_opkey
PASS=0; FAIL=0
W=$(mktemp -d /tmp/aiml_opkey.XXXXXX)
trap 'rm -rf "$W"' EXIT

[ -x "$AIML" ] || { echo "kein Binary unter $AIML -- zuerst 'exodus build --all'"; exit 2; }

ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }

lauf() { timeout 180 "$AIML" "$FIX/$1.aiml" -r > "$W/$1.out" 2>&1; echo $?; }

# muss scheitern UND die Ursache nennen
neg() {
  local name="$1" was="$2"; shift 2
  local rc; rc=$(lauf "$name")
  if [ "$rc" = "0" ]; then
    bad "$name: Austritt 0 -- $was wurde still durchgelassen"
    sed -n '1,3p' "$W/$name.out" | sed 's/^/        /'
    return
  fi
  local fehlt=""
  for muster in "$@"; do
    grep -qF -- "$muster" "$W/$name.out" || fehlt="$fehlt
        fehlendes Muster: $muster"
  done
  if [ -n "$fehlt" ]; then
    bad "$name: scheitert (rc=$rc), aber die Meldung nennt die Ursache nicht$fehlt"
    grep -E '^\[ERROR\]' "$W/$name.out" | head -3 | sed 's/^/        /'
  else
    ok "$name: $was -> harter Fehler mit Ursache"
  fi
}

# muss laufen UND das Erwartete ausgeben
pos() {
  local name="$1" was="$2"; shift 2
  local rc; rc=$(lauf "$name")
  if [ "$rc" != "0" ]; then
    bad "$name: Austritt $rc, erwartet 0 -- $was"
    grep -E '^\[ERROR\]' "$W/$name.out" | head -3 | sed 's/^/        /'
    return
  fi
  local fehlt=""
  for muster in "$@"; do
    grep -qF -- "$muster" "$W/$name.out" || fehlt="$fehlt
        fehlende Ausgabe: $muster"
  done
  if [ -n "$fehlt" ]; then
    bad "$name: laeuft, aber die Ausgabe stimmt nicht$fehlt"
    grep -vE '^\[(DEBUG|INFO)\]' "$W/$name.out" | head -4 | sed 's/^/        /'
  else
    ok "$name: $was"
  fi
}

echo "---- 1. Interpolation: blanker Schluessel war still (der Befund)"
neg interp_percent "blankes '%' in einer Expansion" \
    "operator key '%' in an expansion must be quoted" "spec/09_1"
neg interp_star "blankes '*' in einer Expansion" \
    "operator key '*' in an expansion must be quoted" "spec/09_1"

echo "---- 2. Anweisung, Wurf-Klasse: Fehler gab es, aber ohne Ursache"
neg stmt_percent "blankes '%' in einer Anweisung" \
    "operator key '%' bare" "{ \"%\": [...] }"
neg stmt_star "blankes '*' in einer Anweisung" \
    "operator key '*' bare" "{ \"*\": [...] }"

echo "---- 3. Anweisung, Schluck-Klasse: YAML wirft NICHT"
neg stmt_anchor "blankes '&' (Anker, leerer Schluessel)" \
    "operator key swallowed by YAML"
neg stmt_tilde  "blankes '~' (Null-Schluessel)" \
    "operator key swallowed by YAML"
# Blankes '!=' wird als TAG geschluckt -- der Compiler sieht nie ein '!=' und kann
# darum auch nicht auf 'neq' verweisen.  Zitiert bekommt man die Veraltet-Meldung.
neg stmt_neq    "blankes '!=' (Tag, leerer Schluessel)" \
    "operator key swallowed by YAML"

neg quoted_neq_deprecated "zitiertes '!=' erreicht die Veraltet-Pruefung" \
    "Operator '!=' is not allowed. Use 'neq' instead."

echo "---- 4. Die zitierte Form rechnet weiter"
pos quoted_ok "zitierte Operatoren in Anweisung und Expansion" \
    "zitiert rest=1 prod=42 grg=1 ungl=1" \
    "expansion rest=23 prod=286" \
    "blank plus=144 minus=140 durch=13"
pos quoted_all_ops "jeder symbolische Operator zitiert" \
    "prod=42 rest=1 grg=1 gge=1 bde=1 ode=1 nic=1" \
    "adresse-gelesen=143"

echo "---- 5. Die Grenze der Regel bleibt stehen"
pos prose "Prosa-\${...} bleibt wortwoertlicher Text" \
    'eine Expansion schreibt man als ${...} im Text'
pos esc_quote_fn "escaptes Anfuehrungszeichen in einer Expansion (spec/09_1 §3.4)" \
    "name: [xyz]"
pos esc_quote_member "dasselbe als Member-Aufruf" \
    "name: [x]"
neg bare_quote "blankes Anfuehrungszeichen beendet den Skalar" \
    "YAML parse error"

echo "---- $PASS ok, $FAIL fehlgeschlagen"
[ "$FAIL" -eq 0 ]
