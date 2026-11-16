# Mirror Typed List Deep-Mutation Analysis

Datum: 2026-04-08

## Fragestellung

Decken die vorhandenen AIML-Repro-Tests den vermuteten Mirror-Bug fuer `list<Effect>`-artige Felder bereits ab?

Kurzantwort: nein, nicht den wahrscheinlichsten Fehlerfall.

## Kernbefund

Der aktuell wahrscheinlichste Fehler im C++-Mirror-Pfad ist kein genereller `list<struct>`-Encode/Decode-Defekt, sondern die Dirty-Erkennung fuer typed Mirror-Objekte mit Heap-Feldern.

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L819) wird Dirty-State nur per flachem `memcmp(reg->obj_ptr, reg->shadow, reg->obj_size)` erkannt.

Das ist fuer folgende Felder strukturell unzureichend:
- `string`
- `list<T>`
- `map<K:V>`
- allgemein alle Heap-Felder in typed Modellen

Wenn sich nur der Inhalt hinter einem stabilen Pointer aendert, aber die aeussere Inline-Struktur gleich bleibt, sieht der Mirror kein Dirty-Delta.

Typischer Problemfall fuer `list<Effect>`:
- Liste existiert bereits
- `len` bleibt gleich
- `capacity` bleibt gleich
- `data*` bleibt gleich
- aber ein vorhandenes Element wird in-place geaendert, z. B. `effects-0.duration = 2`

Dann bleibt das top-level `memcmp` sehr wahrscheinlich unveraendert, obwohl die semantischen Daten anders sind.

## Was die vorhandenen Tests wirklich abdecken

### 1. `tests/test_mirror_ws_list_mutation.aiml`

Deckt ab:
- Mutation skalare Felder ueber Pointer auf Listenelemente, z. B. `p1.hp`, `p1.is_active`
- `remove` und `push` auf einer Liste
- `push` auf einer Top-Level-Liste
- Ersetzen eines nested Struct-Felds (`root.match`)

Deckt nicht ab:
- in-place Mutation eines bereits existierenden Listenelements bei gleichbleibender Listenstruktur
- Beispiel: `z0.cards-1.order_idx = 99` ohne `push/remove`
- Beispiel: `participant.flags-0.enabled = 1` ohne Aenderung von `flags.len`

Befund:
- der Test beweist, dass Strukturmutationen erkannt werden
- er beweist nicht, dass Deep-Mutationen hinter stabilem `data*` erkannt werden

Referenz:
- [tests/test_mirror_ws_list_mutation.aiml](../../tests/test_mirror_ws_list_mutation.aiml)

### 2. `tests/test_mirror_ws_list_mutation_poll.aiml`

Deckt denselben Fall wie oben ab, nur robuster gegen Timing/Frame-Drain.

Deckt nicht ab:
- denselben kritischen Deep-Mutation-Fall ebenfalls nicht

Referenz:
- [tests/test_mirror_ws_list_mutation_poll.aiml](../../tests/test_mirror_ws_list_mutation_poll.aiml)

### 3. `tests/test_mirror_ws_events_tail_fields.aiml`

Deckt ab:
- `list<model>` vor weiteren Sync-Feldern
- anschliessende Tail-Felder nach Listen
- `push` auf `events` und `active_triggers`
- nested struct `reaction_state`

Das ist ein guter Test gegen einen alten "tail fields after list"-Layout-/Decode-Bug.

Deckt nicht ab:
- in-place Aenderung eines bestehenden `events`- oder `active_triggers`-Elements
- Beispiel: `root.events-0.text = "changed"`
- Beispiel: `root.active_triggers-0.code = 99`

Referenz:
- [tests/test_mirror_ws_events_tail_fields.aiml](../../tests/test_mirror_ws_events_tail_fields.aiml)

### 4. `tests/test_mirror_ws_9rooms_gameview_events.aiml`

Deckt ab:
- echte 9rooms-Modelle
- `GameView.events`, `active_triggers`, `reactable_triggers`
- `reaction_state`
- mehrere `push`-Operationen auf Listen echter View-Modelle

Das ist der naechste existierende Test an eurem echten GameView-Pfad.

Deckt nicht ab:
- in-place Mutation eines bestehenden Eintrags in `events`
- in-place Mutation eines bestehenden Eintrags in `active_triggers`
- in-place Mutation eines bestehenden Eintrags in `reactable_triggers`

Beispiele, die aktuell nicht getestet werden:
- `v.events-0.text = "changed"`
- `v.active_triggers-0.resolved = 1`
- `v.reactable_triggers-0.expired = 1`

Referenz:
- [tests/test_mirror_ws_9rooms_gameview_events.aiml](../../tests/test_mirror_ws_9rooms_gameview_events.aiml)

### 5. `tests/test_mirror_rpc_model_ws.aiml`

Deckt ab:
- typed RPC-Payload Decode eines Modells mit `list<string>`

Deckt nicht ab:
- Mirror-Dirty-Tracking eines bereits registrierten typed Sync-Objekts
- also nicht das eigentliche Problem der periodischen Delta-Erkennung

Referenz:
- [tests/test_mirror_rpc_model_ws.aiml](../../tests/test_mirror_rpc_model_ws.aiml)

## Analyse der wahrscheinlichen Ursache im C++-Code

### Dirty-Erkennung ist shallow

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L819) gilt:

- dirty wenn `memcmp(obj_ptr, shadow, obj_size) != 0`

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L321) wird der Shadow danach ebenfalls nur per `memcpy` aktualisiert.

Das bedeutet:
- vergleicht wird nur die Inline-Struktur des Root-Modells
- nicht die rekursiv serialisierte Bedeutung
- nicht die Inhalte hinter Heap-Pointern

### Typed Encode/Decode wirkt fuer `list<struct>` selbst konsistent

Der eigentliche Runtime-Codec in [src/sys/ser_runtime.cpp](../../src/sys/ser_runtime.cpp) sieht fuer `AIML_TD_LIST` normal aus:
- Encode iteriert ueber `count` und `elem_size`
- Decode alloziert Liste neu und decodiert rekursiv pro Element
- `list<struct>` und nested lists sind im Code ausdruecklich vorgesehen

Das spricht gegen die pauschale These "Mirror kann list<Effect> nicht serialisieren".

Praeziser ist:
- Mirror serialisiert `list<Effect>` wahrscheinlich dann korrekt, wenn ueberhaupt ein Delta ausgeloest wird
- der Fehler ist wahrscheinlich, dass bestimmte Deep-Mutationen gar kein Delta ausloesen

## Bewertung der bestehenden Testabdeckung

Aktueller Stand:
- `list<struct>` Roundtrip: teilweise gut abgedeckt
- Listen vor/nach Tail-Feldern: abgedeckt
- echte 9rooms-Eventlisten im Mirror: abgedeckt
- Deep-Mutation eines existierenden Listenelements: nicht abgedeckt

Damit fehlt genau der Test, der die aktuelle Hauptvermutung entweder bestaetigt oder sauber widerlegt.

## Konkrete Luecke fuer `list<Effect>`

Wenn euer reales Problem `list<Effect>` oder `list<PendingEffect>` betrifft, dann fehlt mindestens ein Test nach diesem Muster:

1. Host registriert typed Modell mit `list<Effect>` und mindestens 1 Eintrag
2. Client registriert Replica und bestaetigt Initialzustand
3. Host mutiert kein `push/remove`, sondern ein Feld des existierenden Elements
4. Host ruft nur normale `sess.update`-Ticks auf
5. Client prueft, ob genau diese Deep-Mutation angekommen ist

Beispielhafte kritische Aenderungen:
- `effects-0.amount = 7`
- `effects-0.kind = "poison"`
- `effects-0.remaining_turns = 2`
- `pending_effects-0.resolved = 1`

## Schlussfolgerung

Die vorhandenen Repro-Tests sind nicht nutzlos, aber sie decken den wahrscheinlichsten Fehlerfall noch nicht ab.

Sie zeigen:
- typed `list<model>` Decode funktioniert in mehreren Faellen
- mehrere reale 9rooms-Listenpfade funktionieren fuer `push/remove` und Feldersatz

Sie zeigen nicht:
- dass Deep-Mutationen in bestehenden Listenelementen bei unveraenderter Listenstruktur im Mirror dirty werden

Genau dort ist die aktuelle Testluecke.

## Empfohlener naechster Schritt

Der naechste sinnvolle Test ist ein minimaler WS-Mirror-Repro mit:
- `model EffectLite`
- `model Root { effects: list<EffectLite> }`
- Initialzustand mit 1-2 Effekten
- danach nur `effects-0.some_field = new_value`
- keine `push/remove`

Wenn dieser Test fehlschlaegt, ist die Ursache praktisch bestaetigt:
- shallow dirty tracking in `mirror_runtime.cpp`
- nicht primaer `aiml_ser_encode/decode`

## Nachtrag aus 9rooms-Laufzeitlogs

Die frischen 9rooms-Logs schaerfen den Befund deutlich nach: Dort scheitert nicht nur eine Deep-Mutation, sondern ein reales Mirror-Delta wird auf dem Client beim Decode verworfen.

Konkrete Indizien aus dem Repro-Match:
- Der Angriff von `Canopy Viper` laeuft serverseitig durch und feuert `ON_HIT`.
- `execute_decoded_effect(...)` wird fuer den Trefferpfad ausgefuehrt.
- `view_sync_creatures(...)` prueft direkt danach fuer das Ziel `iid=36` den Status `200`.
- Der Host sendet danach ein typed Delta mit `blob=2498`.
- Der Client verwirft genau dieses Paket mit `ser_decode failed (schema mismatch?)`.

Siehe dazu:
- `test/9rooms/frontend_logs.txt` um den Angriff bei `20:13:19`
- `test/9rooms/logs.txt` mit
  - `absurd LIST count=2079857120 idx=14 off=336 esz=56`
  - `ser_decode failed (schema mismatch?) — discarding packet`

Wichtige Einordnung:
- Der bisherige Report-Fokus auf shallow dirty tracking bleibt plausibel, ist aber fuer den 9rooms-Fall nicht mehr vollstaendig.
- Im 9rooms-Fall gibt es harte Laufzeitevidenz fuer einen Decode-/Schema-Fehler auf dem Replica-Client.
- Dieser Fehler tritt bereits vor dem `Canopy Viper`-Treffer mehrfach auf; der Status-Effekt macht ihn nur sichtbar.

Damit gibt es aktuell zwei realistische Fehlerklassen, die getrennt getestet werden muessen:
1. Deep-Mutationen werden nicht dirty und daher nie gesendet.
2. Gesendete typed Deltas mit mehreren Listen-/Tail-Feldern werden auf dem Client unter bestimmten realen Schemata falsch decodiert.

## Praezisierung des Zielbilds fuer neue Tests

Die bisherige Testluecke ist deshalb zweigeteilt:

### A. Dirty-Tracking-Repro

Ein Minimaltest muss weiterhin pruefen:
- bestehende `list<model>`
- kein `push/remove`
- nur Feldmutation eines existierenden Elements
- Erwartung: Delta kommt trotzdem an

Dieser Test beantwortet die Frage:
- erkennt der Mirror Deep-Mutationen ueberhaupt als dirty?

### B. Decode-/Schema-Repro mit 9rooms-naher Struktur

Ein zweiter Test muss gezielt den 9rooms-Befund nachstellen:
- mehrere Listen gleicher Elementfamilie oder aehnlicher Layout-Komplexitaet
- Listen gefolgt von weiteren Listen und Tail-Feldern
- verschachtelte Struct-Felder zwischen bzw. nach Listen
- mehrere Updates nacheinander, nicht nur Initialzustand
- Client muss nach jedem Update weiter decodieren koennen

Dieser Test beantwortet die Frage:
- kippt der Replica-Decode bei einem bestimmten Feldlayout oder Update-Muster?

## Konkrete Testkandidaten

### 1. `test_mirror_ws_list_deep_element_mutation.aiml`

Zweck:
- isolierter Repro fuer die urspruengliche Hauptvermutung `list<Effect>` / Deep-Mutation

Minimalmodell:
- `EffectLite { code, amount, remaining_turns, key }`
- `Root { tick, effects: list<EffectLite> }`

Ablauf:
1. Host registriert `Root` mit 2 Effekten.
2. Client verbindet und bestaetigt Initialzustand.
3. Host aendert nur `effects-0.remaining_turns` und `effects-1.amount`.
4. Keine Listenlaenge aendert sich.
5. Client muss die neuen Feldwerte sehen.

Fehlschlag bedeutet:
- Dirty-Tracking ist shallow und verpasst Deep-Mutationen.

### 2. `test_mirror_ws_gameview_tail_decode_repro.aiml`

Zweck:
- isolierter Repro fuer `absurd LIST count ... off=336 esz=56` / Client-Decode-Discard

Minimalmodell, stark an 9rooms orientiert:
- `EventLite`
- `TriggerLite`
- `ReactionLite`
- `PlayerLite` mit mindestens einer `list<CreatureLite>`
- `GameViewTailRepro` mit dieser Reihenfolge:
  - skalare Meta-Felder
  - `me`, `opp`
  - `event_seq`, `event_count`, `events`
  - `trigger_seq_counter`, `active_trigger_count`, `active_triggers`
  - `reaction_state`, `reactable_triggers`
  - 2-3 Tail-Felder danach

Wichtig:
- `EventLite` und `TriggerLite` sollten Strings plus mehrere `i32` enthalten, damit der Element-Layout-Footprint nicht zu trivial ist.
- `CreatureLite` sollte mindestens ein `map<string:i32>` oder eine weitere Heap-Struktur tragen, damit `PlayerLite` 9rooms-aehnlicher wird.

Ablauf:
1. Host registriert Initialzustand mit je 1 Event/Trigger/Reactable.
2. Client registriert Replica.
3. Host fuehrt mehrere aufeinanderfolgende Updates aus:
   - `events.push(...)`
   - `active_triggers.push(...)`
   - `reactable_triggers.push(...)`
   - `reaction_state.window_prompt = ...`
   - Tail-Feld aendern
4. Nach jedem Schritt muss der Client erfolgreich `update` + Feldpruefung bestehen.

Fehlschlag bedeutet:
- nicht nur Dirty-Tracking, sondern echter typed Decode-/Schemaschaden im Replica-Pfad.

### 3. `test_mirror_ws_9rooms_gameview_events_mutate_existing.aiml`

Zweck:
- existierenden 9rooms-nahen Test gezielt erweitern statt nur neue Minimaltests daneben zu legen

Aenderungen gegen den aktuellen Test:
- nach erfolgreichem Initialsync keine neuen `push`es zuerst
- stattdessen in-place Mutationen:
  - `v.events-0.text = "Round start updated"`
  - `v.active_triggers-0.resolved = 1`
  - `v.reactable_triggers-0.expired = 1`
  - `v.reaction_state.window_prompt = "Updated"`
- erst danach zusaetzlich ein `push` auf alle drei Listen

Warum dieser Test wichtig ist:
- er prueft dieselbe Modellfamilie wie der reale 9rooms-Fall
- er trennt sauber `in-place mutate` von `push`
- er zeigt, ob der bestehende Test nur deshalb gruen ist, weil er nur Strukturwachstum prueft

## Empfohlene Reihenfolge fuer die Implementierung

1. `test_mirror_ws_list_deep_element_mutation.aiml`
- billigster und scharfster Test fuer die Dirty-Tracking-Hypothese

2. Erweiterung von `test_mirror_ws_9rooms_gameview_events.aiml`
- geringster Zusatzaufwand auf bestehender echter Modellbasis

3. `test_mirror_ws_gameview_tail_decode_repro.aiml`
- gezielter Layout-/Decode-Repro, falls 1 und 2 den Packet-Discard nicht alleine sichtbar machen

## Aktualisierte Schlussfolgerung

Der vorhandene Report bleibt im Kern richtig, aber nach den 9rooms-Logs ist er zu eng gefasst.

Aktueller Gesamtbefund:
- Es fehlt weiterhin ein harter Repro fuer Deep-Mutation ohne Listenstruktur-Aenderung.
- Zusaetzlich gibt es nun reale Evidenz fuer einen Replica-Decode-Fehler bei einem 9rooms-nahen typed Modell mit mehreren Listenfeldern.
- Fuer die Fehlersuche brauchen wir daher mindestens zwei Repros: einen fuer Dirty-Tracking und einen fuer den Decode-/Schema-Pfad.


## Testergebnisse nach Implementierung der neuen Repros

Ausgefuehrte Tests:
- `tests/test_mirror_ws_list_deep_element_mutation.aiml`
- `tests/test_mirror_ws_9rooms_gameview_events.aiml`
- `tests/test_mirror_ws_gameview_tail_decode_repro.aiml`

Ergebnis:
- `test_mirror_ws_list_deep_element_mutation.aiml`: PASS
- `test_mirror_ws_9rooms_gameview_events.aiml`: PASS
- `test_mirror_ws_gameview_tail_decode_repro.aiml`: FAIL

Wichtige Interpretation:
- Die reine Hypothese "in-place Mutation eines list<model>-Elements wird generell nicht dirty" ist in der minimalistischen Form zu grob. Der neue Deep-Mutation-Test geht grün.
- Die erweiterte 9rooms-Event-/Trigger-Liste geht ebenfalls grün. Ein reiner `events/active_triggers/reactable_triggers`-Pfad reicht also nicht, um den realen Fehler zu reproduzieren.
- Der neue 9rooms-nahe `GameView`-Tail-Repro faellt jedoch reproduzierbar aus. Der Client sieht Meta-Felder, Eventlisten und Tail-Felder korrekt, aber `CreatureView.passives` fehlt auf der Replica.

Konkreter Fehlbefund aus dem roten Test:
- `round=1`
- `me.creatures.len=2`
- `opp.creatures.len=2`
- `event_seq=2`
- `events.len=2`
- `active_triggers.len=2`
- `reactable_triggers.len=2`
- `reaction_state.window_prompt='Fenster geschlossen'`
- `interaction_mode=''`
- FAIL: `opp1 missing frozen passive`

Das ist fachlich sehr nah am 9rooms-Symptom:
- Der Spielzustand an sich kommt an.
- Auch Event-/Trigger-/Tail-Teile kommen an.
- Aber die `passives`-Map in `CreatureView` kommt nicht verlaesslich auf der Replica an.

Aktualisierte Prioritaet fuer die Fehlersuche:
1. `map<string:i32>` in nested `list<CreatureView>` / `PlayerView.creatures` Mirror-Pfad untersuchen.
2. Fokus auf reale `GameView -> PlayerView -> list<CreatureView> -> passives`-Kette, nicht mehr primaer auf generische `list<Effect>`-Deep-Mutation.
3. Zusaetzlich pruefen, ob der Fehler ein Replica-Decode-Problem oder ein Host-Encode-/Layout-Problem fuer `map<string:i32>` in diesem konkreten nesting ist.
