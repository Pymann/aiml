#!/usr/bin/env bash
# Nebenindex-Dump (spec/45 "Persistence"): die Faelle, die ein aiml-Unit-Test
# nicht ehrlich pruefen kann, weil sie Dateien unter dem Store austauschen.
#
# Der wichtigste ist Gate 3.  Ein gruener Unit-Test beweist NICHT, dass der
# Dump benutzt wird -- faellt sein Stempel, fuellt der Store still auf und
# alles stimmt trotzdem.  Darum wird hier eine GUELTIGE Datei VERGIFTET: ein
# Schluessel im Stringblock umgeschrieben, der Stempel bleibt gueltig.  Kommt
# danach der vergiftete Wert heraus, hat der Dump geantwortet.  (Am 06.11.2026
# hat ein Fehler im Auffuellen seine eigene Bestzahl erzeugt -- 0,06 s, weil
# nichts getan wurde.  Diese Pruefung ist die Lehre daraus.)
#
# Aufruf aus dem aiml-Wurzelverzeichnis:  bash tests/run_sidx_gates.sh
set -u
cd "$(dirname "$0")/.."
AIML="${AIML:-./out/aiml/aiml}"
FIX=tests/_sidx
M=/tmp/aiml_sidxgate
PASS=0; FAIL=0
ok()  { echo "[ OK ] $1"; PASS=$((PASS+1)); }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
W=$(mktemp -d /tmp/aiml_sidxgate_work.XXXXXX)

[ -x "$AIML" ] || { echo "kein Binary unter $AIML -- zuerst 'exodus build --all'"; exit 2; }

rm -rf $M; mkdir -p $M/a $M/b
timeout 120 "$AIML" $FIX/build.aiml -r > $W/build.out 2>&1 \
  || { echo "build fehlgeschlagen"; cat $W/build.out; exit 1; }
timeout 120 "$AIML" $FIX/build_other.aiml -r > $W/other.out 2>&1 \
  || { echo "build_other fehlgeschlagen"; cat $W/other.out; exit 1; }

NAM=$M/a/sidxgate__name.kvsidx
SKU=$M/a/sidxgate__sku.kvsidx
TAG=$M/a/sidxgate__tag.kvsidx

# 1. Je indiziertes Feld eine Datei, und nur bei checkpoint.
if [ -f "$NAM" ] && [ -f "$SKU" ] && [ -f "$TAG" ]; then
  ok "checkpoint legt je Feld eine .kvsidx an"
else
  bad "checkpoint legt je Feld eine .kvsidx an (da: $(ls $M/a | tr '\n' ' '))"
fi

# 2. Kopf: Magic und Zahlen stimmen.
magic=$(head -c 8 "$NAM" 2>/dev/null)
vals=$(od -An -tu4 -j48 -N4 "$NAM" 2>/dev/null | tr -d ' ')
keys=$(od -An -tu4 -j52 -N4 "$NAM" 2>/dev/null | tr -d ' ')
if [ "$magic" = "AIMLKVSX" ] && [ "$vals" = "2" ] && [ "$keys" = "3" ]; then
  ok "Kopf: AIMLKVSX, 2 Feldwerte, 3 Paare"
else
  bad "Kopf: magic='$magic' val_count=$vals key_count=$keys (erwartet AIMLKVSX/2/3)"
fi

# 3. DER Beweis: gueltige Datei vergiften, Stempel unberuehrt lassen.
cp "$NAM" $W/name.gut
python3 - "$NAM" <<'PY'
import sys
p=sys.argv[1]
d=bytearray(open(p,'rb').read())
at=d.find(b'aaa\x00', 64)
assert at > 0, "Schluessel 'aaa' nicht im Stringblock gefunden"
d[at:at+3]=b'zzz'
open(p,'wb').write(d)
PY
timeout 120 "$AIML" $FIX/read.aiml -r > $W/poison.out 2>&1
if grep -q "HIT=zzz" $W/poison.out; then
  ok "der Dump beantwortet die Abfrage (vergifteter Schluessel kommt durch)"
else
  bad "vergifteter Schluessel kam NICHT durch -- es wurde aufgefuellt, der Dump ist tote Last"
  sed -n '1,12p' $W/poison.out
fi
# Die anderen Felder duerfen davon unberuehrt bleiben.
if grep -q "SKU_S1=aaa" $W/poison.out && grep -q "WERKZEUG_N=2" $W/poison.out; then
  ok "ein vergiftetes Feld laesst die anderen in Ruhe (eine Datei je Feld)"
else
  bad "ein vergiftetes Feld hat die anderen mitgerissen"
  sed -n '1,12p' $W/poison.out
fi
cp $W/name.gut "$NAM"

# 4. Beschaedigter Kopf: verworfen, Ergebnis trotzdem richtig.
printf 'KAPUTT!!' | dd of="$NAM" bs=1 seek=0 conv=notrunc status=none
timeout 120 "$AIML" $FIX/read.aiml -r > $W/broken.out 2>&1
if grep -q "HAMMER_N=2" $W/broken.out && grep -q "HIT=aaa" $W/broken.out; then
  ok "kaputter Kopf: verworfen, voll aufgefuellt, Ergebnis richtig"
else
  bad "kaputter Kopf: Ergebnis falsch"
  sed -n '1,12p' $W/broken.out
fi
cp $W/name.gut "$NAM"

# 5. Fremde .kvsidx unterschieben -- cp behaelt die Inode des Ziels.
ino_vor=$(stat -c %i "$NAM")
cp $M/b/sidxgate__name.kvsidx "$NAM"
ino_nach=$(stat -c %i "$NAM")
if [ "$ino_vor" = "$ino_nach" ]; then
  ok "cp behielt die Inode (sonst prueft Gate 5 nicht das Gemeinte)"
else
  bad "cp hat die Inode gewechselt ($ino_vor -> $ino_nach)"
fi
timeout 120 "$AIML" $FIX/read.aiml -r > $W/foreign.out 2>&1
if grep -q "HAMMER_N=2" $W/foreign.out && ! grep -q "HIT=xxx" $W/foreign.out; then
  ok "fremde .kvsidx: Stempel faellt (fremde WAL-Inode), richtig aufgefuellt"
else
  bad "fremde .kvsidx wurde angenommen -- Schluessel aus dem anderen Store sichtbar"
  sed -n '1,12p' $W/foreign.out
fi
cp $W/name.gut "$NAM"

# 6. Datei weg: faellt still auf das Auffuellen zurueck.
rm -f "$NAM"
timeout 120 "$AIML" $FIX/read.aiml -r > $W/gone.out 2>&1
if grep -q "HAMMER_N=2" $W/gone.out; then
  ok "fehlende .kvsidx: aufgefuellt, Ergebnis richtig"
else
  bad "fehlende .kvsidx: Ergebnis falsch"
  sed -n '1,12p' $W/gone.out
fi

echo "---"
echo "sidx-Gates: $PASS ok, $FAIL fehlgeschlagen"
rm -rf $W
[ $FAIL -eq 0 ] || exit 1
