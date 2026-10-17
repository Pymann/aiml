#!/usr/bin/env bash
# run.sh — Proben zu spec/65 (Symbol-Clearance).
#
#   bash tests/clearance/run.sh              # bewertet STAGE=step2 (Vorgabe)
#   STAGE=step3 bash tests/clearance/run.sh  # + opt-in Flusspruefung
#   STAGE=session ...                                  # + eingeschraenkte Sitzung
#   STAGE=host ...                                     # + Proben, die einen Wirt brauchen
#   STAGE=write ...                                    # + Schreibseite (create/replace/collision)
#   ONLY=07 ...                                        # nur passende Proben
#
# Jede Probe nennt im Kopf, welchen Umsetzungsschritt sie braucht:
#   # NEEDS: step2   -> Syntax annehmen + `level`/`grants` validieren (immer an)
#   # NEEDS: step3   -> opt-in Flusspruefung (AIML_STATIC_CLEARANCE)
#   # NEEDS: session -> eingeschraenkte Sitzung, allein pruefbar (AIML_CLEARANCE)
#   # NEEDS: exports -> braucht die Export-Tabelle des Wirts (AIML_EXPORTS), aber keine
#                      aufrufbaren Adressen — die Erwartung ist eine Diagnose
#   # NEEDS: host    -> braucht einen WIRT mit AUFRUFBAREN Symbolen (Signatur + Adresse),
#                      und `level:` in eigener Quelle ist Selbstbeglaubigung (F65-27)
#   # NEEDS: write   -> die SCHREIBSEITE (§5b): `create_level`/`replace_level`, der
#                      Kollisionsfehler und die Hot-Reload-Berechtigung (F65-26 … F65-32)
#   # NEEDS: dynload -> braucht spec/29_2 §4.3 (dynamisches Nachladen).  Nicht dieses
#                      Kapitel: `sys.bundle` kann nur eine Funktion buendeln, die der Wirt
#                      SELBST uebersetzt hat — ein NEUER Name ist damit nicht ins Buendel
#                      zu bekommen, und der `clearance-create`-Zweig bleibt unerreichbar.
# Proben oberhalb von STAGE werden als AUSSTEHEND gemeldet und zaehlen NICHT
# als Fehlschlag — sonst waere das Labor dauerhaft rot und damit wertlos.
#
# Sitzung je Probe (vom Wirt gesetzt, nie aus der Quelle — spec/65 F65-6):
#   # CLEARANCE: none|<1..255>   # CREATE-LEVEL: N   # REPLACE-LEVEL: N
#   # STATIC-CLEARANCE: on
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
ONLY="${ONLY:-}"
STAGE="${STAGE:-step2}"

rank() { case "$1" in step2) echo 1;; step3) echo 2;; session) echo 3;; guard) echo 4;; exports) echo 5;; host) echo 6;; write) echo 7;; dynload) echo 8;; *) echo 1;; esac; }
WANT=$(rank "$STAGE")

session_env() {
  local f="$1" cl st cr rp ex
  cl=$(grep -m1 -oE '^# CLEARANCE: *[a-z0-9]+'  "$f" | awk '{print $3}')
  cr=$(grep -m1 -oE '^# CREATE-LEVEL: *[0-9]+'  "$f" | awk '{print $3}')
  rp=$(grep -m1 -oE '^# REPLACE-LEVEL: *[0-9]+' "$f" | awk '{print $3}')
  ex=$(grep -m1 -oE '^# EXPORTS: *[^ ]+' "$f" | awk '{print $3}')
  st=$(grep -m1 -oE '^# STATIC-CLEARANCE: *on'  "$f")
  [ -n "${cl:-}" ] && [ "$cl" != "none" ] && echo "AIML_CLEARANCE=$cl"
  [ -n "${cr:-}" ] && echo "AIML_CREATE_LEVEL=$cr"
  [ -n "${rp:-}" ] && echo "AIML_REPLACE_LEVEL=$rp"
  [ -n "${ex:-}" ] && echo "AIML_EXPORTS=$ex"
  [ -n "${st:-}" ] && echo "AIML_STATIC_CLEARANCE=1"
}
needs_of() { grep -m1 -oE '^# NEEDS: *[a-z0-9]+' "$1" | awk '{print $3}'; }

pass=0; fail=0; pend=0
echo "── spec/65 Clearance-Labor (STAGE=$STAGE) ──"
for f in tests/clearance/[0-9]*.aiml; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  need=$(needs_of "$f"); [ -z "$need" ] && need=step2
  mapfile -t sess < <(session_env "$f")
  if [ "$(rank "$need")" -gt "$WANT" ]; then
    pend=$((pend+1)); printf "  ..   %-42s AUSSTEHEND (braucht %s)\n" "$base" "$need"; continue
  fi
  out=$(env "${sess[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  line=$(echo "$out" | grep -E "PASS|FAIL|error:" | head -1)
  if [ $rc -eq 0 ]; then pass=$((pass+1)); printf "  ok   %-42s %s\n" "$base" "$line"
  else fail=$((fail+1));  printf "  FAIL %-42s rc=%-3d %s\n" "$base" "$rc" "$line"; fi
done

npass=0; nfail=0
for f in tests/clearance/neg/*.aiml; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  need=$(needs_of "$f"); [ -z "$need" ] && need=step2
  want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
  if [ "$(rank "$need")" -gt "$WANT" ]; then
    pend=$((pend+1)); printf "  ..   %-42s AUSSTEHEND (braucht %s)\n" "$base" "$need"; continue
  fi
  mapfile -t sess < <(session_env "$f")
  out=$(env "${sess[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  if [ $rc -eq 0 ]; then
    nfail=$((nfail+1)); printf "  FAIL %-42s ANGENOMMEN, erwartet: %s\n" "$base" "$want"
  elif echo "$out" | grep -qF "$want"; then
    npass=$((npass+1)); printf "  ok   %-42s abgelehnt: %s\n" "$base" "$want"
  else
    nfail=$((nfail+1))
    printf "  FAIL %-42s falscher Grund (erwartet %s): %s\n" "$base" "$want" \
           "$(echo "$out" | grep -E 'error:' | head -1)"
  fi
done

echo "── positiv $pass ok / $fail fail · negativ $npass ok / $nfail fail · ausstehend $pend ──"
[ $fail -eq 0 ] && [ $nfail -eq 0 ]
