# sprint_task_6

## Titel

Mirror-Semantik gegen aktuelle Runtime und Repros absichern

## Status

Weiterhin relevant, aber die Fragestellung ist heute konkreter als in der Erstfassung.

## Ziel

Die Mirror-Implementierung soll fuer die realen Problemzonen stabilisiert werden: Typed Registration, Delta-Sync, Event-/RPC-Queues, WS/TCP-Verhalten und reale App-Repros wie `9rooms`.

## Aktueller Stand

- Mirror ist produktiv nutzbar und im Compiler bereits breit exponiert.
- `poll_event` und `poll_rpc` sind heute vorhanden; die alte Formulierung als reine API-Luecke ist deshalb zu schwach.
- Reale Fehlerbilder leben bereits in [mirror_ws_crash_report.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/mirror_ws_crash_report.md) und WS-Tests.

## Was an der alten Fassung veraltet war

- Poll-/Wait-Ergonomie als hypothetische Folgearbeit ist zu allgemein.
- Der wichtigste Wert der Task liegt heute in Regressionen fuer reale Mirror-Semantik, nicht in einem abstrakten Spec-Abgleich allein.

## Fokus

1. Typed Register/Unregister und Delta-Sync.
2. Event-/RPC-Queue-Semantik.
3. WS- und TCP-Paritaet in den kritischen Faellen.
4. App-nahe Repros, besonders `9rooms`.

## Betroffene Stellen

- [spec/44_5_mirror.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/44_5_mirror.md)
- [mirror_ws_crash_report.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/mirror_ws_crash_report.md)
- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
- [src/sys/mirror_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/mirror_runtime.cpp)
- [tests](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests)

## Akzeptanzkriterien

- Die reale Mirror-Semantik ist fuer die wichtigsten Faelle testbar abgesichert.
- WS- und TCP-Pfade divergieren nicht still in Kernfaellen.
- Reale Repros bleiben als Regressionstests erhalten.
