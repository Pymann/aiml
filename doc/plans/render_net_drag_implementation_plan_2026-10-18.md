# Umsetzungsplan: Renderschleife, Netzwerk, Drag & Drop (2026-10-18)

Grundlage: [`9rooms_v2/doc/reports/client_performance_analysis_2026-10-18.md`](../../../9rooms_v2/doc/reports/client_performance_analysis_2026-10-18.md)
(zusammengeführt mit [`doc/analysis/render_network_drag_review_2026-10-18.md`](../analysis/render_network_drag_review_2026-10-18.md)).
Reihenfolge nach User-Vorgabe: **erst das Einfache und die Spec-Widersprüche,
dann die Engine.** Bestehende Schnittstellen werden **nicht geändert, nur
erweitert.**

---

## Umsetzungsstand (2026-10-18, Nacht)

| Punkt | Stand | Beleg |
|---|---|---|
| S0.1 rAF-Widerspruch in `spec/44_1` | **fertig** | 4 Stellen + Preis-Tabelle + „Offen: rAF-Pfad", Querverweis `doc/wasm_frame_loop.md` |
| S0.2 Asyncify-Whitelist §9.4 | **fertig** | Ist-Zustand (globales `-sASYNCIFY=1`) dokumentiert, Whitelist/JSPI als offener Punkt |
| S0.3 z-Ordnung | **fertig** | Code-Kommentar `hit_test` richtiggestellt + neuer `spec/44_6` „Treffer-Reihenfolge" |
| S0.4 `session.update` „once per frame" | **fertig** | `spec/44_5` §5: blockierende Aufrufe unterlaufen die Regel |
| S2.1 Cancel-Flanke | **fertig** | `spec/44_0` §2.2 normativ + §2.4 + §8.2, `spec/44_6` Abbruch-Muster; Runtime 3 Pfade; `tests/test_input_touch_cancel.aiml` grün, **Mutationsprobe rot** |
| S2.2 `cancel_request` räumt die Queue | **fertig** | `spec/44_5` §7.2 + Warnblock; `tests/test_mirror_cancel_request_drains.aiml` grün, **Mutationsprobe rot** |
| S1.2 `poll_cooldown` | **fertig** | `poll_forced` schlägt den Cooldown |
| S1.3 `confirm_phase`-Antwort | **fertig** | `consume_phase_ack` + `BoardScreen.show_action_error` (Float-Pool) |
| S1.4 `apply_drag` 1×/Frame | **fertig** | `last_drag_frame` gegen `engine.frame()` |
| S1.5 z-Ebenen der Drop-Flächen | **fertig** | −8…−1 in Erzeugungsreihenfolge → unverändertes Aussehen, deterministische Treffer |
| S1.6 Request-Lebenszyklus | **teilweise** | Board-Load: `cancel_request` bei Timeout + `-1`-Zweig; Lobby: `-1`-Zweig + `send_request==0`-Guard. Deck-Editor **absichtlich unverändert** (behandelt `-1` schon über das leere Modell, s. u.) |
| **Client-Guard für den Cancel** | **fertig** | `board_drag_controller.apply_drag` + Deck-Editor-Drop in `main_menu_app`: ohne diesen Guard wäre die Engine-Korrektur in 9rooms wirkungslos |
| S1.1 Prod-Build-Vergleich | **Config fertig, Messung blockiert** | `exodus-client-o2.json` (neu, nichts überschrieben) — s. Blocker unten |
| S2.3 Press-Gestenmaschine | **fertig (Engine), 9rooms-Umstellung offen** | `spec/44_6` „Aufnehmen am Druckpunkt"; additiv `engine.update_drag_gesture` + `engine.get_drag_source`, `update_drag` unverändert; `tests/test_prefab_drag_gesture.aiml` grün, **Mutationsprobe rot** (Pick am Cursor → genau die Fehlermeldung). 9rooms benutzt es noch NICHT — das ist eine Verhaltensänderung der Kern-Interaktion und braucht einen Spieltest |
| S2.4 z-Dirty-Flag | **offen** | |
| S2.5 Snapshot/Bereinigung trennen | **offen** | |
| Stufe 3 | **offen** | |

Drei neue Regressionstests, alle mit bestandener Mutationsprobe (erst den Fehler
wieder eingebaut, FAIL gesehen, zurückgenommen, PASS gesehen) — ohne diese Probe
wäre nicht belegt, dass die Tests den jeweiligen Fehler überhaupt treffen:
`tests/test_input_touch_cancel.aiml`, `tests/test_mirror_cancel_request_drains.aiml`,
`tests/test_prefab_drag_gesture.aiml`.

Regression nach allen Änderungen (`--only input|prefab|mirror`, Xvfb :99):
**10 / 32 / 42 OK, 0 eigene Fehlschläge.** Der einzige rote Eintrag in allen drei
Läufen ist `tests/closures/run.sh` — ein **untracked** Verzeichnis (`??`) aus einer
fremden, uncommitteten Session (die auch `compiler.cpp`/`compiler.h` und ~40 Specs
geändert hat). Nicht meine Baustelle, deshalb auch **nichts committet**.

**Korrektur am Plan (beim Umsetzen gefunden):** `spec/44_0` §2.4 spezifiziert
**bereits** eine vollständige Gestenmaschine — `input.drag_active/started/ended`,
`drag_origin_x/y`, `drag_delta_*` und eine konfigurierbare Schwelle
(`drag_set_threshold`, Default 5 px), gespeist vom **Press-Ursprung**.
`engine.update_drag` benutzt davon **nichts** und pickt stattdessen pegelgesteuert
mit der aktuellen Position. S2.3 ist damit viel kleiner als geplant: nicht „eine
Gestenmaschine bauen", sondern „die vorhandene an den Prefab-Drag anschließen"
(additiv als zweiter Aufruf neben `update_drag`). Die Cancel-Flanke ist dort
jetzt schon angeschlossen (`input.drag_cancelled`).

**Blocker bei S1.1 (ein Befehl, dann läuft die Messung):** `9rooms_v2/__exodus_cache`
ist leer — die projektlokale emsdk-Installation fehlt. Für ein `output_type: wasm`
besteht exodus auf **genau diesem** projektlokalen Pfad; ein absoluter Verweis auf
die vorhandene emsdk unter `aiml/__exodus_cache/git/emsdk` wird beim Linken
ignoriert (zweimal reproduziert, der Compile-Schritt lief jeweils durch). Es
braucht also einmal das emsdk-Setup aus den `git_packages` — ein
Toolchain-Download im GB-Bereich. Den habe ich nachts **nicht** unaufgefordert
angestoßen; `exodus-client-o2.json` ist 1:1 von `exodus-client.json` abgeleitet
(gleiches emsdk-Setup) und unterscheidet sich nur in
`optimization: 2`, `-sASSERTIONS=0`, `-g1` und dem Fehlen aller
`AIML_MEM_DIFF*`-Variablen. Sobald emsdk einmal installiert ist:

```bash
exodus build -c exodus-client-o2.json     # schreibt nach out/client_o2/, fasst out/client/ nicht an
```

Dann fps-Log derselben Szene (Lobby + Board-Eintritt) gegen den heutigen Build
vergleichen — das ist die Messung zu Rangfolge #1 und #2.

**Zweite Korrektur:** S1.6 war für den Deck-Editor zu breit. Dessen drei
`take_response`-Stellen prüfen bewusst nur `== 0`, weil ein `-1` das Zielmodell
leer lässt → `success != 1` → Warnpfad; der Kommentar dort sagt das ausdrücklich.
Richtig so, nicht angefasst.

---

## Leitplanken

Verbindlich für jeden Punkt unten:

1. **Additiv.** Neue Aufrufe, neue Felder, neue Dateien. Keine geänderte
   Signatur, kein entfernter Aufruf, kein geändertes Rückgabeformat. Wo eine
   Verhaltensänderung unvermeidlich ist, steht sie unten **ausdrücklich** als
   solche (Abschnitt „Verhaltensänderungen" — genau vier, alle Bugfixes).
2. **Spec → Tests → Code** (Arbeitsprinzip 3). Kein Code vor Spec-Text und
   Testfall, außer der User genehmigt es im Einzelfall.
3. **Spec muss wahr sein** (Prinzip 2). Stufe 0 ist deshalb Pflicht und steht
   vor allem anderen.
4. **Bauen** nur mit `exodus` aus der `exvenv`; für aiml selbst **immer**
   `exodus build --all`. Nie den Compiler-Binary direkt auf Einzeldateien.
5. **Ein** `run_all_tests.sh` gleichzeitig. Nie Sanitizer und Suite parallel.
   Logs persistent ins Session-Dir, nicht `/tmp`. `cleanup.sh` vor jedem Lauf.
6. Keine Routine-Gesamtabnahmen von selbst, nur die betroffenen Tests.
7. Keine interaktiven Demos in Testschleifen (blockieren die Suite).
8. Knappe Kommentare; bei `.aiml`-Annotationen meist keine.

---

## Stufe 0 — Spec-Widersprüche (nur Spec, kein Code)

Vier Stellen, an denen die Spec etwas anderes sagt als die Implementierung.
Kein Testfall nötig: Abnahme ist „der Spec-Text ist am Code nachprüfbar wahr".

### S0.1 — `spec/44_1`: Browser-Schleife ist nicht rAF

**Befund:** vier Stellen behaupten `emscripten_set_main_loop_arg` +
`requestAnimationFrame` (Zeilen 19, 95, 212/218, 314); die Implementierung
fährt `while (!quit) { web_loop_tick(); emscripten_sleep(16); }`.

**Die Richtung ist bereits entschieden** — nicht von mir: `doc/wasm_frame_loop.md`
erklärt die Sleep-Schleife als **„load-bearing — do not simplify this back to
`emscripten_set_main_loop`"** und belegt per Isolationstabelle, dass ein
WS-Send, der zum rAF-Scheduler zurückkehrt, die Schleife still tötet. Also:
**Spec an die Implementierung anpassen**, nicht umgekehrt.

**Änderung:**
- Platform-Mapping-Zeile und §4.1 auf die manuelle Asyncify-Schleife umstellen.
- §7.1: „VSync via `requestAnimationFrame` (always active)" ersetzen durch die
  wahre Aussage samt Preis: **Frameperiode = Arbeit + 16 ms**, also nie 60 fps,
  keine Compositor-Synchronisation, `setTimeout`-Drosselung im Hintergrundtab.
- §9.3 (Frame-Limiter Browser) entsprechend.
- Querverweis auf `doc/wasm_frame_loop.md` als Begründung.
- Neuer Unterpunkt „Offen: rAF-Pfad" mit den Vorbedingungen (alle
  suspendierenden Aufrufe aus dem Frame heraus) — damit der Zielzustand
  dokumentiert bleibt, statt als falsche Gegenwart dazustehen.

### S0.2 — `spec/44_1` §9.4: Asyncify-Whitelist

**Befund:** Spec nennt `-sASYNCIFY -sASYNCIFY_IMPORTS=[engine_swap_internal]`;
reale Builds nutzen globales `-sASYNCIFY=1` ohne `ASYNCIFY_ONLY`.
**Änderung:** Ist-Zustand dokumentieren, die Whitelist als offenen
Optimierungspunkt kennzeichnen (sie ist Teil von S1.1/Rangfolge #2).

### S0.3 — z-Ordnung wirkt erst nach dem Zeichnen

**Befund:** `aiml_prefab_hit_test` behauptet im Kommentar „Iterate in reverse
z_order (topmost first)", iteriert aber die Instanzliste; das stimmt nur, weil
`prefab_draw_scene` die Liste als Seiteneffekt sortiert. `set_z` sortiert nicht.
**Änderung:** (a) Kommentar im Code richtigstellen; (b) `spec/44_6` hält
ausdrücklich fest, dass `set_z` für **Treffer- und Drag-Prüfung** erst nach dem
nächsten `draw_scene` wirkt, und dass neu erzeugte Instanzen bis dahin am
Listenende stehen. Das ist unangenehm, aber wahr — und S2.4 ändert es danach
bewusst und sichtbar.

### S0.4 — `session.update` „once per frame" ist nicht einhaltbar

**Befund:** `spec/44_5` §5 sagt „called once per frame"; `send_and_await` pumpt
intern pro 2-ms-Tick, also dutzende Male pro Frame, mitten im Render-Durchlauf.
**Änderung:** §5 um einen Hinweis ergänzen, dass die blockierenden
Komfort-Aufrufe (`send_and_await`, `connect`) die Session selbst pumpen und
diese Regel damit unterlaufen; Empfehlung für Frame-Anwendungen auf
`send_request`/`take_response` zeigen. Vorbereitung für K1 (§4.5 des Berichts).

---

## Stufe 1 — Einfach und lokal (9rooms + Build-Konfiguration)

Kein Engine-Code. Jeder Punkt für sich abnehmbar.

### S1.1 — Produktions-Build messbar machen *(additiv: neue Datei)*

**Nicht** `exodus-client.json` umschreiben — das überschreibt die
ausgelieferten Artefakte in `out/client/`. Stattdessen **neu**:
`9rooms_v2/exodus-client-prod.json` mit `"name": "client_prod"` (eigenes
Ausgabeverzeichnis `out/client_prod/`), darin:

- `"optimization": "2"`, `ASSERTIONS=0`, `-g1`
- **ohne** `AIML_MEM_DIFF*`
- `ASYNCIFY_ONLY`-Liste, falls sie sich ermitteln lässt; sonst erst mal ohne

**Abnahme:** beide Builds nebeneinander, gleiche Szene (Lobby + Board-Eintritt),
fps-Log und `client.wasm`-Größe vergleichen. Erst wenn die Zahl steht, ist die
Entscheidung über die Prod-Config eine Entscheidung und keine Vermutung.

**Risiko, das benannt werden muss:** `-O0 → -O2` kann latentes UB im generierten
Code sichtbar machen. Deshalb separater Build, separates Verzeichnis, nichts
überschrieben. Vorher nichts deployen.

### S1.2 — `poll_cooldown` nicht durch den auslösenden Klick setzen

`poll_board_state` setzt bei Press/Release `poll_cooldown = 30` — auch bei dem
Klick, der selbst `board_poll = 60` angefordert hat (§2.4 des Berichts). Fix:
ein Flag „Refresh wurde angefordert" schlägt den Cooldown. Zusätzlich
Framezähler → monotone Zeitstempel.
**Abnahme:** Stoppuhr am Check-Button; die ~1 s Totzeit verschwindet.

### S1.3 — `confirm_phase`-Antwort auswerten + lokales Pending

Heute wird die Antwort verworfen und nur `board_poll = 60` gesetzt. Fix:
Ergebnis auswerten, Ablehnung am Board anzeigen, sofortiges lokales
„wird gesendet" — klar getrennt vom bestätigten Phasenwechsel.
**Abnahme:** Blindtest ohne Konsole: gesendet / wartet / abgelehnt / nicht
geklickt sind unterscheidbar.

### S1.4 — `apply_drag` einmal pro Frame

Nach einer Aktion ruft `main_menu_app.frame` `board.render` ein zweites Mal,
also `apply_drag` zweimal mit denselben Flanken (§3.3). Fix: Frame-Latch wie
schon bei `deck_click_latch`, nur für den Drag-Eintritt.
**Abnahme:** Aktion auslösen, während die Maus gedrückt bleibt → kein
Zweit-Greifen.

### S1.5 — Explizite z-Ebenen für die Drop-Flächen *(rein AIML)*

`spell_stack`, `trap_band_ali`, `perm_spell_band`, `mana_band_ali`, die fünf
`combat_zones`, `decks_ali`, `soul_self/others` haben alle z = 0 und werden
nur durch die Erzeugungsreihenfolge getrennt (§3.5). Fix: eine dokumentierte
Ebenenvergabe (z. B. Bänder 1, Zonen 2, Decks 3, Soulmages 4). Keine
Engine-Änderung, wirkt sofort.
**Abnahme:** Ablegen auf einer Überlappung trifft reproduzierbar dieselbe
Fläche, unabhängig von `init`.

### S1.6 — Request-Lebenszyklus an den Aufrufstellen schließen

`send_request == 0` und `take_response == -1` werden teils nicht behandelt
(`pump_board_load` Schritt 2 prüft nur `== 0`; `poll_consume` lässt `-1` am
Gate vorbei und rotiert mit stillem Datenverlust weiter). Dazu fehlt
`cancel_request` beim Board-Load-Timeout und bei Ansichtswechseln.
**Abnahme:** Protokollfehler beendet den Pendingzustand sichtbar statt in den
300-Frame-Timeout zu laufen.
**Achtung:** vollständig wirkt das erst mit S2.2 — `cancel_request` räumt heute
die bereits eingereihte Antwort nicht weg.

---

## Stufe 2 — Engine, additiv

### S2.1 — Cancel-Flanke, die kein Release ist *(wichtigster Punkt)*

**Fehler:** `touchcancel` hängt am selben Handler wie `touchend` und erzeugt
`pend_release[0] = 1`; `bin_touchstart` tut dasselbe bei zwei Fingern. Ein
abgebrochener Touch läuft damit in `handle_drop` und **löst einen Spielzug aus**
(§3.11). Betrifft jede AIML-App mit Touch-Drag.

**Spec zuerst** (`spec/44_0`):
- §7 um die Cancel-Flanke erweitern: neue Abfrage `input.mouse_cancelled(btn)`
  neben `mouse_pressed`/`mouse_released` — gleiche Frame-Semantik.
- Vertrag festschreiben: **ein Cancel beendet die Geste und erzeugt niemals
  Tap oder Drop.** `touchcancel` und der Zwei-Finger-Übergang liefern Cancel,
  **nicht** Release.
- §8.2 additiv: `input.post_touch_cancel()` als Injection-Aufruf, Frame-Vertrag
  aus §8.3 unverändert.
- `spec/44_6`: `engine.update_drag` bricht bei Cancel ab (Snap-back wie
  `cancel_drag`), ohne Drop-Übergang.

**Test** (neu, `tests/test_input_touch_cancel.aiml`): Karte greifen, Cancel
injizieren, prüfen: `get_dragged() == null`, Position = Ursprung, **kein**
Drop-Übergang beobachtet. Desktop + WASM-Pfad (emsdk-node, siehe
`wasm-headless-testing`).

**Code:** eigener `bin_touchcancel`, eigenes `pend_cancel`-Latch,
`aiml_prefab_update_drag` wertet es aus. Der Zwei-Finger-Pfad in
`bin_touchstart` sendet Cancel statt Release.

**Zusatz:** Blur-/Visibility-Reset (`visibilitychange`, `blur`) geht über
denselben Cancel-Pfad — sonst tauscht man eine hängende Karte gegen einen
unbeabsichtigten Zug.

### S2.2 — `cancel_request` räumt die Queue mit

`cancel_request` löscht nur die ID aus `outstanding`; eine schon eingereihte
Antwort bleibt liegen und ist über `take_response` nicht erreichbar (ID-Prüfung
zuerst, dann `-1` ohne Entfernen). Der Client drainiert `poll_rpc` nirgends,
nutzt `cancel_request` aber bereits. Verwaiste Events laufen gegen das
Per-Peer-Limit aus `spec/44_5` §7.2 (4096 / 64 MiB → **Peer wird getrennt**).

**Spec:** `spec/44_5` §7.2 bei `cancel_request` ergänzen: „entfernt die
Anmeldung **und** eine bereits eingereihte Antwort dieser ID".
**Test:** Request → Antwort eintreffen lassen (`update`) → `cancel_request` →
`has_rpc()` ist wieder beim Ausgangswert.
**Code:** `rpc_take_matching` + `aiml_free_rpc_event` im Cancel — beides
vorhanden, es wird nur nicht gerufen. Signatur unverändert.

### S2.3 — Press-Gestenmaschine *(additiv neben `update_drag`)*

`update_drag` ist pegelgesteuert, pickt mit der *aktuellen* Position und hat
keine Schwellenphase (§3.7, §3.10, §3.12). Statt es zu ändern:

**Neu** `engine.update_gesture(mx, my, btn, rel, cancel, threshold)` →
Zustand (`none`/`candidate`/`dragging`/`dropped`/`cancelled`), das intern
- das Press-Ziel an der **Druckposition** und der zuletzt gezeichneten Szene
  auflöst und festhält,
- erst nach `threshold` zum Drag wird,
- genau ein Ende liefert: Tap **oder** Drop **oder** Cancel.

`engine.update_drag` bleibt unverändert gültig; 9rooms wechselt, andere Apps
nicht. Die 12-px-Logik des Boards (heute nach dem Drag-Start, in
Bildschirmkoordinaten) wird dadurch ersetzt, nicht repariert — die Schwelle
gehört in Designkoordinaten und vor den Start.

**Spec:** `spec/44_6` neuer Abschnitt; `spec/44_0` §8 um
`post_touch_cancel`-Zusammenspiel ergänzen.
**Test:** schneller Drag über eine simulierte Frame-Pause; gehaltene Taste
fährt erst später über eine Karte; Klick bei bewegter Maus trifft die gedrückte
Karte.

### S2.4 — z-Sortierung per Dirty-Flag

`set_z` markiert die Szene als unsortiert; Treffer- und Drag-Prüfung sortieren
bei Bedarf, statt auf das nächste `draw_scene` zu warten. Behebt den
Ein-Frame-Verzug und das „neue Instanzen sind oben" nach einem Reload (§3.4).
Signaturen unverändert.
**Test:** `set_z` + sofortiger `hit_test` im selben Frame trifft die neue
Reihenfolge.

### S2.5 — Snapshot und Bereinigung trennen

`web_loop_tick` zieht den Input-Snapshot **nach** `update` → Flanken hinken der
live geschriebenen Mausposition einen Frame nach (§3.12). `input_update`
vorzuziehen ist falsch: der native Pfad löscht dort als Erstes die Latches.

Also zwei interne Schritte (Snapshot übernehmen / Ereignisse bereinigen), vom
Frame-Treiber an den richtigen Stellen gerufen. **Öffentliche Schnittstelle
unverändert** — `input.update()` bleibt, was es ist.

**Gefahr:** `spec/44_0` §8.3 schreibt den Injection-Frame-Vertrag fest („eine
injizierte Flanke feuert genau einmal, gleicher Vertrag auf jeder Plattform").
Dieser Punkt kommt deshalb **zuletzt** in Stufe 2 und nur mit den drei
bestehenden `test_input_injection_*` grün.

---

## Stufe 3 — groß, nicht in einer Nacht

Nur Reihenfolge, Details im Bericht (§4.5, §5):

1. Board-RPCs asynchron, auf Basis eines typisierten Anfrage-Handles (K2) mit
   Besitzerbereich und Generation.
2. Nativer WS/WSS-Empfang inkrementell + Backpressure (Voraussetzung für jedes
   Frame-Budget, K3).
3. Artwork asynchron mit Platzhalter (K4).
4. Board-Ansichten über `iid` erhalten statt Vollreload.
5. Rein lesende Spielerprojektion aus `MatchService.get`, dann zweiten
   Round-Trip streichen; `source_iid` + erwartete Revision in die Requests.
6. Session-Pump-Vertrag + Framevertrag in `engine.run` (K1, zusammen mit dem
   `ctx`-Parameter aus `spec/44_1` „Offen" — additive Vier-Argument-Fassung).
7. rAF-Pfad, erst wenn 1–3 den Frame frei von suspendierenden Aufrufen haben.

---

## Verhaltensänderungen (die vier Ausnahmen von „nur additiv")

Die Schnittstelle bleibt überall gleich; bei diesen vier ändert sich das
*Verhalten* bestehender Aufrufe. Alle vier sind Bugfixes, keiner war
dokumentiert zugesichert:

| Punkt | Was sich ändert | Wer könnte sich darauf verlassen |
|---|---|---|
| S2.1 | `touchcancel` / Zwei-Finger liefern Cancel statt Release | nur Code, der den Bug nutzt (keiner — der Zug ist unerwünscht) |
| S2.2 | `cancel_request` entfernt zusätzlich die eingereihte Antwort | Code, der sie danach per `poll_rpc` abholt — im Client nicht vorhanden |
| S2.4 | `set_z` wirkt sofort für Treffer/Drag statt ab dem nächsten Draw | Code, der den Verzug einplant — nirgends belegt |
| S2.5 | Flanken und Mausposition stammen aus demselben Frame | der Injection-Vertrag `spec/44_0` §8.3 — **muss** erhalten bleiben, deshalb zuletzt |

Entscheidung dazu steht beim User; mein Vorschlag ist, alle vier ungated zu
machen, weil ein Gate hier nur zwei Verhalten gleichzeitig am Leben hält (wie
bei den zwei memV3-Gates, die den `free:`-Operator still zum No-op gemacht
haben).

---

## Was ich heute Nacht mache — in dieser Reihenfolge

Ohne Rückfrage, weil es unter Prinzip 3 (Spec+Tests zuerst) fällt und nichts
überschreibt:

1. **Stufe 0 komplett** (S0.1–S0.4). Reine Spec- und Kommentararbeit, macht die
   Spec wieder wahr. Danach `exodus build --all`, um sicher zu sein, dass die
   Doku-Änderungen nichts angefasst haben.
2. **S2.1 Spec + Test** — Cancel-Flanke in `spec/44_0`/`44_6` schreiben,
   `tests/test_input_touch_cancel.aiml` anlegen. Der Test ist zunächst **rot**;
   das ist der Beweis, dass er den Fehler trifft.
3. **S2.2 Spec + Test** (klein, scharf abgrenzbar).
4. **S2.1 + S2.2 Code**, dann die betroffenen Tests plus die drei
   `test_input_injection_*` und die `test_mirror_*`. Keine Gesamtsuite.
5. **S1.4, S1.5, S1.2** in 9rooms — lokal, je ein kleiner Eingriff.
6. **S1.1**: `exodus-client-prod.json` anlegen und bauen (dauert), fps-Log
   gegen den heutigen Build.

Das reicht für die Nacht; S2.3 und S2.5 kommen danach, weil sie beide die
Gestenmaschine berühren und ich sie nicht halb offen lassen will.

## Was ich *nicht* mache

- Kein Deploy, kein Anfassen von `exodus-client.json` oder `out/client/`.
- Keine Gesamt-Suite und keine Abnahmeläufe von selbst (Prinzip aus
  `no-long-acceptance-runs`); nur die betroffenen Tests.
- Keine Stufe-3-Punkte anfangen — die brauchen erst K2 bzw. eine
  Server-Entscheidung.
- Kein `rm -rf __aiml_cache` (exodus invalidiert selbst).

## Was eine Entscheidung braucht (Default, falls keine kommt)

1. **Prod-Config umstellen?** Ich baue nur die Vergleichs-Config und messe.
   Das Umstellen von `exodus-client.json` und das Deploy bleiben bei dir.
2. **Die vier Verhaltensänderungen ungated?** Default: ja, ungated (Begründung
   oben). Sag Stopp, dann baue ich S2.1 hinter ein Spec-Flag — würde ich aber
   nicht empfehlen.
3. **S2.3 `update_gesture` in der Engine oder erst im Board?** Default: Engine,
   additiv — weil `cosmos` und jede künftige Touch-App denselben Fehler sonst
   erbt. Board-lokal wäre schneller, aber die dritte Kopie derselben Logik.
