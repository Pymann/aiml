# spec/61 Ownership — Stand 2026-09-11 (Feature default AUS, `AIML_LIFETIME_DROPS=1` opt-in)

## Behoben & validiert (Feature AN)
Alle mit Proben nativ + valgrind + WASM abgesichert; aiml-Suite Baseline 363/1; Memsan-Gate grün.

1. **Crash-Klasse `list<Model>`** (WASM-Client-Absturz): dasselbe Modell in zwei Container / `at`→push
   teilte inneren Heap → Double-Free. Fix: `list<Model>.push` deep-kopiert das Element, außer die
   Quelle ist ein beweisbar toter owned Local (dann Move). `ListImpl::pushDeepCopyModel` +
   Push-Handler-Routing. Probe `model_into_two_containers`.
2. **Container-Feld aus Borrow** (`dat.produces = req.produces`, `persist_from_req`-Crash): shallow
   memcpy → geteilter Buffer. Fix: `emitContainerFieldDeepCopy` (zero-leak). Probe
   `member_container_deepcopy`.
3. **`map.put` deklarierter String-Key** (`mana_choice`-Regression): deklarierter Interp-Key wurde
   nicht konsumiert → decref gab den Map-Key frei. Fix: `consumeLocalDrop(key/val)`. Probe
   `map_put_declared_key`.

## Design-Klärung (deine Frage)
9rooms behandelt Wert-Container als Werte (`get_copy→push`, `at`→push, get-modify-`set`); geteilte
Identität via `list<ptr<Model>>`; **0** „mutate-after-push". ⇒ deep-copy-on-push ist semantisch
korrekt, **kein `push_at`/`push_from` nötig**.

## Neues Werkzeug: `AIML_TRACE_SYMBOL`
Compile-Zeit-Env (`=name1,name2` oder `1`/`all`), zero-overhead wenn ungesetzt. Emittiert Laufzeit-
`printf` bei **DECL** (Anfangswert) und **DROP** (Wert vor dem Free): String→Inhalt, Liste→len+addr,
Modell→addr. Zeigt exakt, wann ein Symbol gemoved (kein DROP) oder freigegeben wird. In WASM landen
die `[aiml-trace]`-Zeilen in der Browser-Konsole.

## OFFEN: `artwork_id`-UAF im Client (Feature AN)
Symptom: `@art:XX��` / `Invalid UTF-8 0x95` — `card.artwork_id` liest Garbage im Render-Loop. **OFF
läuft.** Headless NICHT reproduzierbar: ~12 Repros (Daten-Pfad, push/deep-copy, at-views,
member-store, komplexe nested `list<Model>`, echter `rebuild_library`-Nachbau — nativ UND WASM) alle
grün. ⇒ client-runtime-spezifisch. Stärkster Verdacht: **Asyncify × Drop im Wire-Empfang**
(`net_list_cards`: no-init `res` mit Drop, gefüllt via `send_and_await` das SUSPENDET; `req` per P2a
gedroppt bei `return res`).

### So pinnen wir es live (Tracer)
Ich habe in `client/card_full_populator.aiml` am Lese-Punkt eingebaut:
`- decl: [artdbg, string, card.artwork_id]` (DEBUG-markiert, leicht entfernbar).

1. Client bauen mit beiden Envs (bei sudo hinter `sudo`):
   `sudo AIML_LIFETIME_DROPS=1 AIML_TRACE_SYMBOL=artdbg ./run_web.sh --build`
2. Laufen lassen, Browser-Konsole beobachten: `[aiml-trace] DECL artdbg (string) = "…"` pro
   gerenderter Karte. Der **erste Frame mit Garbage** + was davor passierte (Reload? RPC?) zeigt die
   Ursache. Konsolen-Log hierher schicken.

## Umgebung
`sudo …run_web.sh --build` hatte 1611 Dateien in `__exodus_cache`/9rooms auf **root** gesetzt →
Build-/emcc-Permission-Fehler. Per `chown` behoben. Künftig WASM möglichst **ohne** sudo bauen, sonst
chown wiederholen.

## Deferred
`map<K:Model>`-**Value**-Insert deep-kopiert noch nicht (MapImpl put: 2 Store-Sites). 9rooms nutzt
kein `map<*:Model>` → nicht client-relevant; als kleines Folgepaket analog `pushDeepCopyModel`.
