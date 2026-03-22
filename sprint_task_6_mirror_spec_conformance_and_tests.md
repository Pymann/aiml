# sprint_task_6

## Titel

Mirror-Spec-Konformitaet und Testabdeckung ausbauen

## Ziel

Die Mirror-Implementierung soll gegen [44_5_mirror.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/44_5_mirror.md) systematisch abgeglichen und durch gezielte Tests abgesichert werden.

## Hintergrund

Mirror funktioniert im Kern, aber die letzten Repros haben gezeigt:

- WS-spezifische Semantik ist empfindlich
- `session.update()` fuehlt sich hoeherwertig an als es derzeit runtime-seitig wirklich ist
- reale Nutzung fuehrt zu viel Poll-/Wait-Boilerplate
- mehrere Bugs lagen nicht in der Spec-Idee, sondern in C++-Details und unklarer Queue-Semantik

## Scope

- Spec-Funktionen gegen echte Runtime- und Compiler-Pfade abgleichen
- Unterschiede zwischen TCP- und WS-Pfad explizit pruefen
- Objektregistrierung, Typed-Deltas, Queue-Verhalten und RPC-Abwicklung stabilisieren
- Poll-/Wait-Ergonomie als API-Luecke sichtbar machen

## Betroffene Stellen

- [44_5_mirror.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/44_5_mirror.md)
- [mirror_ws_crash_report.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/mirror_ws_crash_report.md)
- [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)
- [mirror_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/mirror_runtime.cpp)
- [tests](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests)

## Umsetzungsrichtung

1. Mirror-Spec in Feature-Bloecke aufteilen:

- Session-Lifecycle
- Register/Unregister
- Sync/Deltas
- Events
- RPCs

2. Fuer jeden Block markieren:

- implementiert
- teilweise implementiert
- nur dokumentiert
- fehleranfaellig oder transportabhaengig

3. Daraus gezielte Repros und Regressionstests ableiten.

4. Optional API-Luecken fuer ergonomische Builtins wie `poll_event`/`poll_rpc`/`wait_*` als Folgearbeit festhalten.

## Akzeptanzkriterien

- Die reale Mirror-Semantik ist gegen die Spec transparent kartiert.
- WS- und TCP-Pfade sind in den wichtigsten Faellen testbar abgesichert.
- Fruehere reale Fehlerfaelle sind als Regressionstests erhalten.
- Offene API-Luecken sind klar von echten Bugs getrennt.

## Ergebnis

Nach Abschluss ist sichtbar, was Mirror laut Spec wirklich kann, was die Runtime aktuell kann und welche Luecken zuerst geschlossen werden muessen.
