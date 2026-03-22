# WebSocket Mirror Analysis Report

## Status

Der ursprünglich reproduzierte C++-Crash im WebSocket-Mirror-Pfad ist nach dem Neubau nicht mehr vorhanden.

Der zunächst verbleibende Fehler in `tests/test_mirror_ws_list_mutation.aiml` ist nach weiterer Analyse kein Serializer-/Decode-Fehler im engeren Sinn.

Die eigentliche Ursache ist:

- Im WebSocket-Pfad verarbeitet ein einzelner `sess.update`-/`client.update`-Tick nur genau eine WS-Frame.
- Der ursprüngliche Test prüft aber bereits nach einem einzelnen Update-Aufruf, obwohl bis dahin noch nicht alle relevanten Frames abgearbeitet sind.

Damit hat sich der Befund erneut verschoben:

- Der alte Crash war ein Debug-/Logging-Problem.
- Der spätere Fehlzustand war in diesem Repro primär ein Timing-/Drain-Problem im Test bzw. in der Update-Semantik.

## Reproduktionsstand

### Positiver Kontrolltest

`tests/test_mirror_ws_complex.aiml` läuft erfolgreich durch.

Abgedeckt sind dort:

- verschachtelte 9rooms-artige Sync-Modelle
- mehrere `list<...>`-Felder
- Initial-Register und Replica-Sync über `ws://`
- nachträgliches Delta auf einem verschachtelten Teilmodell

### Fehler-Repro

`tests/test_mirror_ws_list_mutation.aiml` schlägt weiterhin fehl.

Der Lauf kommt jetzt deutlich weiter:

```text
host-mut: root registered net_id=1
...
[mirror-dbg] ws_accept=ok
[mirror-dbg] mirror_send WS fd=5 len=14 ret=0
[mirror-dbg] send_delta net_id=1 obj=... data_len=478
...
[mirror-dbg] mirror_send WS fd=5 len=488 ret=0
...
client-mut: ws connect ok
...
client-mut: replica registered net_id=1
...
host-mut: list mutation delta applied
[mirror-dbg] mirror_send WS fd=5 len=550 ret=0
...
[mirror-dbg] ws_recv_raw: msg=ok len=488
[mirror-dbg] typed-decode net_id=1 field0='mut-1'
client-mut: round=1 last_event=boot
client-mut: p1 hp=28 active=0
client-mut: zone0 count=2 card_instances.len=3
FAIL mut: p1.hp=28
```

## Was jetzt sicher feststeht

### 1. Der alte Crash ist behoben

Vor dem Neubau crashte der Prozess in `send_delta_to_peer(...)` beim Debug-Print mit `%s`.

Dieser Absturz tritt nicht mehr auf.

Die neue Debug-Ausgabe ist typbewusst genug, um den initialen Typed-Delta-Blob vollständig zu senden:

```text
[mirror-dbg] send_delta net_id=1 obj=... data_len=478
  [0] kind=5 off=0
  [1] kind=6 off=32
  [2] kind=6 off=48
  [3] kind=6 off=64
  [4] kind=STRING off=0 'mut-1'
  [5] kind=2 off=8
  [6] kind=STRING off=16 'boot'
  [7] kind=2 off=24
  [8] kind=5 off=0
  [9] kind=2 off=0
```

### 2. Initialer Replica-Sync funktioniert

Der Client verbindet sich erfolgreich:

- `client-mut: ws connect ok`
- `client-mut: replica registered net_id=1`

Danach verarbeitet er ein Typed-Delta:

- `[mirror-dbg] typed-decode net_id=1 field0='mut-1'`

Die grundlegende WebSocket-Mirror-Pipeline funktioniert also:

- Accept
- Register
- Initial Sync
- Empfang und Decode auf Client-Seite

### 3. Der verbleibende Fehler betrifft die spätere Mutation

Nach der Host-Meldung

- `host-mut: list mutation delta applied`

wird erneut ein WebSocket-Paket gesendet:

- `[mirror-dbg] mirror_send WS fd=5 len=550 ret=0`

Trotzdem sieht der Client anschließend noch den alten Zustand:

- `client-mut: p1 hp=28 active=0`

Die erwartete Änderung an dem Eintrag in der synchronisierten Liste ist also auf der Replica nicht sichtbar.

### 4. Der Client hat vor der Assertion nur die erste WS-Frame verarbeitet

Der entscheidende Hinweis liegt in der Größenfolge der Pakete:

- Initiales Typed-Delta: `len=488`
- Späteres Delta nach Listenmutation: `len=550`

Im fehlschlagenden Originallauf sieht der Client vor der Assertion nur:

- `[mirror-dbg] ws_recv_raw: msg=ok len=488`

Die spätere `len=550`-Frame wird in diesem Testlauf vor der Assertion noch nicht verarbeitet.

## C++-Ursache

Im WebSocket-Empfangspfad verarbeitet `mirror_recv_from_peers(...)` pro `update()`-Aufruf genau eine eingehende WS-Nachricht:

Dateireferenz:

- `src/sys/mirror_runtime.cpp:549`
- `src/sys/mirror_runtime.cpp:559`
- `src/sys/mirror_runtime.cpp:745`

Relevante Stelle:

```cpp
/* WS: recv returns one frame at a time (binary-safe) */
int ws_fd = peer->transport.ws_ptr ? aiml_ws_fd(peer->transport.ws_ptr) : -1;
if (ws_fd < 0 || !tcp_readable(ws_fd)) break;
...
void* msg = aiml_ws_recv_raw(peer->transport.ws_ptr, &msg_len);
...
process_packet(s, peer, (const uint8_t*)msg, msg_len);
free(msg);
break;  /* WS delivers one frame per recv call */
```

Das `break` beendet die Verarbeitung für diesen Peer sofort nach genau einer WS-Frame.

Da `aiml_mirror_session_update(...)` nur einmal `mirror_recv_from_peers(s)` aufruft, gilt effektiv:

- Ein `update()`-Tick verarbeitet auf WS-Seite höchstens eine Frame pro Peer.

## Technische Einordnung

Der Fehler sitzt nicht in `aiml_ser_encode(...)` oder `aiml_ser_decode(...)`, sondern in der Kombination aus:

- WS-Frame-Verarbeitungssemantik in `mirror_runtime.cpp`
- Testannahme, dass ein einzelnes `client.update` bereits alle wartenden Frames abarbeitet

Der Serializer selbst verhält sich in diesem Fall konsistent:

- Der Host sendet nach der Mutation tatsächlich ein zweites Typed-Delta mit größerer Nutzlast.
- Wenn der Client dieses zweite Delta auch verarbeitet, kommen die mutierten Werte korrekt an.

## Gegenprobe

Zur Verifikation wurde ein zusätzlicher Repro angelegt:

- `tests/test_mirror_ws_list_mutation_poll.aiml`

Dieser Gegenrepro führt nach `register_id` nicht nur ein einzelnes `client.update` aus, sondern mehrere Update-Ticks mit kurzen Sleeps dazwischen.

Ergebnis:

```text
[mirror-dbg] ws_recv_raw: msg=ok len=488
[mirror-dbg] typed-decode net_id=1 field0='mut-1'
...
[mirror-dbg] mirror_send WS fd=5 len=550 ret=0
[mirror-dbg] ws_recv_raw: msg=ok len=550
[mirror-dbg] typed-decode net_id=1 field0='mut-1'
...
poll-mut: p1 hp=17 active=1 zone0=2 cards=4
WS list mutation poll PASS
```

Damit ist belegt:

- Das zweite Delta wird korrekt vom Host erzeugt.
- Das zweite Delta wird korrekt vom Client dekodiert.
- Die mutierten `list<struct>`-Daten kommen korrekt an, wenn der Client genügend `update()`-Ticks erhält.

## Konsequenz

Der ursprüngliche Repro `tests/test_mirror_ws_list_mutation.aiml` ist aktuell irreführend, weil er ein Verhalten voraussetzt, das die C++-Runtime nicht garantiert:

- mehrere wartende WS-Frames in einem einzigen `client.update` vollständig abzuarbeiten

Man kann das auf zwei Arten bewerten:

1. Testproblem:
   Der Test muss nach `register_id` mehrere `update()`-Ticks abwarten, bis alle relevanten Frames verarbeitet wurden.

2. API-/Runtime-Problem:
   Falls `update()` semantisch eigentlich als "drain what is pending" gedacht ist, dann ist die WS-Implementierung in `mirror_runtime.cpp` zu restriktiv und sollte mehr als eine Frame pro Tick verarbeiten.

## Empfehlung

Die kleinste sichere Maßnahme ist:

- `tests/test_mirror_ws_list_mutation.aiml` an die tatsächliche Update-Semantik anpassen und mehrere `client.update`-Ticks vor der Assertion ausführen.

Falls die gewünschte API-Semantik dagegen lautet, dass ein `update()` möglichst alle aktuell lesbaren WS-Frames abarbeitet, dann sollte `mirror_recv_from_peers(...)` angepasst werden:

- das `break` nach einer einzelnen WS-Frame entfernen oder in eine Drain-Schleife umformen
- dabei Fairness gegenüber anderen Peers und Tick-Laufzeit bewusst behandeln

## Fazit

Der ursprüngliche Crash ist behoben.

Der vermeintliche verbleibende Listen-/Serializer-Bug ließ sich nicht bestätigen.

Der aktuelle belastbare Befund ist stattdessen:

- WebSocket-Mirror verarbeitet derzeit nur eine Frame pro `update()`-Tick.
- Der fehlschlagende Repro asserted zu früh.
- Mit mehreren Update-Ticks synchronisiert die Replica korrekt und der Poll-Repro besteht.
