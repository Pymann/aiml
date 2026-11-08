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
# ── 4. Rueckfall: der Dump laesst sich nicht schreiben (spec/31 §6.2.1) ──────
# Ein Verzeichnis an der Stelle der .kvidx laesst rename() scheitern UND mmap().
# Der Store muss den Ueberlauf aus der Liste auffuellen, bevor er die Sperre
# abgibt -- sonst ist sein ganzer Inhalt weg, ohne dass eine Zeile fehlt.
R=/tmp/aiml_ckpt_rueck
rm -rf $R; mkdir -p $R
timeout 120 "$AIML" $FIX/ckpt_rueckfall.aiml -r > $W/rf1.out 2>&1
len1=$(grep -oP 'len=\K[0-9]+' $W/rf1.out | tail -1)
if [ "${len1:-0}" = "1000" ] && [ -f $R/rueck.kvidx ]; then
  ok "Rueckfall-Vorlauf: 1000 Schluessel, Dump geschrieben"
else
  bad "Rueckfall-Vorlauf: len=${len1:-?}, kvidx=$(ls $R 2>/dev/null | tr '\n' ' ')"
  sed -n '1,10p' $W/rf1.out
fi
rm -f $R/rueck.kvidx && mkdir -p $R/rueck.kvidx
timeout 120 "$AIML" $FIX/ckpt_rueckfall.aiml -r > $W/rf2.out 2>&1
rc=$?
len2=$(grep -oP 'len=\K[0-9]+' $W/rf2.out | tail -1)
e2=$(grep -oP 'erst=\K[^ ]+' $W/rf2.out | tail -1)
l2=$(grep -oP 'letzt=\K[^ ]+' $W/rf2.out | tail -1)
if [ "$rc" -eq 0 ] && [ "${len2:-0}" = "1000" ] && [ "$e2" = "w_0" ] && [ "$l2" = "w_999" ]; then
  ok "Dump unschreibbar: Inhalt im selben Prozess weiter lesbar (len=$len2)"
else
  bad "Dump unschreibbar: rc=$rc len=${len2:-?} erst=${e2:-?} letzt=${l2:-?} (erwartet 0/1000/w_0/w_999)"
  sed -n '1,10p' $W/rf2.out
fi

# ── 5. Spitzenverbrauch: checkpoint gibt den RAM-Index ab ────────────────────
# Dieselben 1,6 Mio Schluessel, einmal in vier Phasen mit checkpoint je Phase,
# einmal in einem Zug.  Gibt der Store den Ueberlauf nach jedem checkpoint ab,
# bleibt die Spitze bei einer Phase; sonst sind beide Laeufe gleich teuer.
# Laeuft ein paar Minuten -- deshalb ganz am Ende.
if [ "${IDX_GATES_SCHNELL:-0}" = "1" ]; then
  echo "[ -- ] Gate 5 (Spitzenverbrauch) uebersprungen: IDX_GATES_SCHNELL=1"
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
    ok "beide Laeufe tragen 1,6 Mio Schluessel, Phase 2 nach drei Checkpoints lesbar"
  else
    bad "Inhalt: len_ein=${len_ein:-?} len_pha=${len_pha:-?} p2=${p2:-?} (erwartet 1600000/1600000/wert_700000)"
    sed -n '1,10p' $W/pha.out; sed -n '1,10p' $W/pha.out.err
  fi

  # Gemessen 08.11.2026 (1,6 Mio Schluessel, 4 Phasen): mit Abgeben 411 gegen
  # 510 MB = 81%, ohne Abgeben 510 gegen 510 = 100%.  Mehr Abstand ist nicht zu
  # holen: die WAL-Abbildungen, die Liste des letzten Checkpoints und der
  # frische Dump zaehlen in BEIDEN Laeufen mit, nur der Ueberlauf unterscheidet
  # sie.  88% laesst der Messung 7 Punkte Luft und faellt auf die Mutation.
  grenze=$(( rss_ein * 88 / 100 ))
  if [ "${rss_pha:-0}" -gt 0 ] && [ "$rss_pha" -lt "$grenze" ]; then
    ok "Spitze phasenweise $((rss_pha/1024)) MB < 88% von einphasig $((rss_ein/1024)) MB"
  else
    bad "Spitze phasenweise $((rss_pha/1024)) MB gegen einphasig $((rss_ein/1024)) MB -- der checkpoint gibt den Ueberlauf nicht ab"
  fi
fi


echo "---- $PASS ok, $FAIL fehlgeschlagen"
[ "$FAIL" -eq 0 ]
