# AIML RPC: Implementierungsplan nach Review

Stand: 2026-10-02. Status: geplant, noch nicht implementiert.

Die sechs Review-Befunde werden in sieben aufeinander aufbauenden Arbeitspaketen
behoben. Zeilennummern beziehen sich auf den während der Planung gelesenen Stand;
Funktionsnamen sind die dauerhaften Anker. Parallel vorhandene Änderungen an
`take_rpc` und dem strukturellen RPC-Fingerprint sind berücksichtigt und dürfen
nicht überschrieben werden.

## 1. Verbindliche Zielsemantik

- Netzwerkdaten werden ausschließlich mit ihrer tatsächlich empfangenen Länge
  decodiert. Ungültige Frames führen zu einem kontrollierten Fehler, nicht zu
  Teilmutation, Out-of-bounds-Zugriffen oder unbegrenzten Allokationen.
- Low-Level-Client-RPCs gehen ausschließlich zum Server. Ein Server-Handler
  entscheidet ausdrücklich über Antworten und Broadcasts.
- Low-Level-Host-Sends behalten `target == 0` als expliziten Broadcast und
  `target > 0` als Peer-ID. Ein unbekannter positiver Peer wird niemals zum
  Broadcast; negative Targets werden abgewiesen.
- Request/Response verwendet eine explizite Request-ID und Nachrichtenart.
  Methodenname und Schema sind zusätzliche Prüfungen, keine Korrelation.
- Unbeteiligte Nachrichten behalten ihre Reihenfolge und ihren Inhalt.
- Automatische Objekt-RPCs bilden einen eigenen, registrierten Dispatch-Pfad.
  Objekt-ID, Peer-ID und Request-ID sind getrennte Begriffe und Felder.
- Unterstützte automatische RPC-Methoden sind zunächst `void` und besitzen
  ausschließlich serialisierbare Wertparameter. Unzulässige Signaturen erhalten
  Compilerfehler. Rückgabewerte bleiben beim expliziten Request/Response-API.

## 2. Arbeitspaket 0: Build und ABI konsolidieren

**C++-Stellen**

- `src/sys/ser_runtime.h:67`: `aiml_ser_rpc_blob_matches` steht aktuell vor dem
  anonymen `typedef ... AimlTypeNode` ab Zeile 73. Deklaration hinter die
  Typdefinition verschieben. Keine unpassende Forward-Deklaration eines anonymen
  Structs einführen.
- `src/sys/mirror_runtime.h`: öffentliche Deklaration für das inzwischen
  implementierte `aiml_mirror_session_take_rpc` ergänzen; Kommentare zu
  Event-Lebensdauer, Routing und `send_and_await` aktualisieren.
- `src/core/context.h:1243`: der Fallback für `rpc_event` erzeugt nur drei Felder;
  `src/core/builtin_registry.cpp:9708` erzeugt sieben. Eine gemeinsame
  LLVM-Typ-Erzeugungsfunktion verwenden, damit beide Pfade identisch sind.
- `src/sys/mirror_runtime.cpp:185`, `AimlRpcEvent`: C++-Layout und LLVM-Felder
  gemeinsam verwalten/testen; kommende Request-ID-Felder überall ergänzen.

**Abnahme**

Sauberer nativer Build und Compile-Smoke für beide Reihenfolgen der
`rpc_event`-Typinitialisierung. LLVM-DataLayout gegen C++-`sizeof`/`offsetof`
prüfen; wasm32 separat prüfen, keine x86-64-Padding-Konstanten übernehmen.

## 3. Arbeitspaket 1: Paket- und Decodergrenzen absichern [P1]

**C++-Stellen**

- `src/sys/mirror_runtime.cpp:413`, `process_packet`, RPC-Zweig ab Zeile 524.
- `src/sys/mirror_runtime.cpp:1384`, `aiml_rpc_event_decode_into`.
- `src/sys/mirror_runtime.cpp:1400`, `aiml_mirror_session_take_rpc`.
- `src/sys/ser_runtime.cpp:1443`, `aiml_ser_rpc_blob_matches`, und
  `aiml_ser_decode` unmittelbar danach.
- `src/sys/ser_runtime.cpp:493`, rekursiver `dec`-Walker und seine Lesehelfer.
- Deklarationen in `src/sys/ser_runtime.h`.

**Änderungen**

1. Gemeinsamen RPC-Parser mit Ergebnis `NeedMore`, `Complete`, `Invalid`
   einführen. Er erhält `(const uint8_t*, size_t available)` und liefert eine
   validierte Paketansicht sowie die tatsächlich verbrauchten Bytes.
2. Längen mit Subtraktion prüfen: erst `offset <= available`, dann
   `blob_len <= available - offset`. Den Cast `(int)blob_len` aus der bisherigen
   Prüfung entfernen. In WebSocket-Nachrichten genau ein vollständiges Paket
   verlangen; unerwartete Restbytes sind ein Fehler.
3. Konfigurierbare Obergrenze für RPC-Frames einführen; initialer Vorschlag:
   8 MiB pro Frame. Grenze vor Bufferwachstum/Allokation prüfen und in der Spec
   dokumentieren. Methodennamen müssen 1–255 Bytes lang und frei von NUL sein;
   `strlen(method) & 0xFF` im Sender durch explizite Prüfung ersetzen.
4. `arg_kind` ausschließlich 0 oder 1; bei 0 muss die Blob-Länge 0 sein. Bei 1
   mindestens den vier Byte langen Präfix verlangen, dessen Wert exakt der
   verbleibenden Blob-Länge entsprechen muss. Für neue typisierte RPCs außerdem
   Magic und Fingerprint verlangen. Legacy-Kompatibilität ausdrücklich begrenzen.
5. Neue größenbewusste APIs hinzufügen, zum Beispiel
   `aiml_ser_decode_sized(blob, blob_size, out, nodes, n_nodes)` und
   `aiml_ser_rpc_blob_matches_sized(...)`. Alle RPC-Aufrufer einschließlich
   `take_rpc` verwenden diese APIs. Bestehende persistente Datenformate nicht
   nebenbei ändern; alte größenlose APIs dürfen nur intern vertrauenswürdige
   Buffer verarbeiten.
6. Decoder-Lesehelfer auf überlaufsichere Restlängenprüfungen umstellen.
   Containeranzahlen, `count * element_size`, Verschachtelungstiefe und gesamte
   Decode-Allokation begrenzen. Ein Frame-Limit allein verhindert keine riesige
   Allokation aus einem kleinen, manipulierten Elementzähler.
7. In ein temporäres, nullinitialisiertes Objekt decodieren. Bei Fehler dessen
   bereits erzeugte Heapfelder freigeben; bestehendes Ziel unverändert lassen.
   Erst nach vollständig erfolgreicher Prüfung alte Ziel-Felder freigeben und
   das Ergebnis übertragen. Zielgröße als geprüfte Compiler-Metadaten übergeben,
   nicht unbesehen aus einem beliebigen Root-Node ableiten.
8. `builtin_registry.cpp`, Builtins `decode_rpc`, `take_rpc`, `send_and_await`:
   vorgezogene `CreateMemSet`-Aufrufe entfernen und die transaktionale Runtime
   nutzen. Gemeinsame Pointerextraktion für Value-Modelle, `ptr<>`, `ref<>` und
   aktivierte Refcount-Modelle verwenden; keinen Pointer-Slot als Objekt löschen.

**Regressionen**

Blob-Längen 0/1/2/3, widersprüchlicher innerer Präfix, abgeschnittene Header,
`UINT32_MAX`, unbekanntes `arg_kind`, ungültige Methodenlänge, falscher Fingerprint,
überlaufende Containeranzahl und tiefe Verschachtelung. Zielobjekt mit bestehenden
Strings/Listen muss bei Fehler erhalten bleiben. ASan/UBSan und Leak-Prüfung des
teilweise aufgebauten temporären Objekts.

## 4. Arbeitspaket 2: TCP-Framing auf dasselbe Format bringen [P1]

**C++-Stellen**

- `src/sys/mirror_runtime.cpp:633`, `mirror_recv_from_peers`, besonders RPC-Zweig
  ab Zeile 672: liest derzeit nur `method_len + method + i32`.
- `AimlMirrorPeer` ab Zeile 149; `tcp_recv_all`/`tcp_send_all` ab Zeile 260.
- `mirror_send` ab Zeile 299 und `mirror_send_rpc_v2` ab Zeile 1307.

**Änderungen**

1. Pro Peer Empfangsbuffer und Parse-Position ergänzen. Verfügbare Bytes
   nichtblockierend einlesen; vollständige Pakete über den Parser aus Paket 1
   konsumieren, unvollständige Pakete bis zum nächsten `update` behalten.
2. Für v2 exakt `6 + 1 + method_len + 1 + 4 + 4 + blob_len` Bytes konsumieren.
   Mehrere Pakete in einem TCP-Read und über beliebige Reads fragmentierte
   Pakete unterstützen. Alle bisherigen Paketarten in die Bufferverwaltung
   integrieren, damit DELTA/REGISTER/RPC-Mischverkehr nicht desynchronisiert.
3. Auch Teil-Sends behandeln: pro Peer Sendebuffer und Offset, Weiterleitung
   nach `EAGAIN` beim nächsten Update. Kein stiller Verlust nach kurzem `send`.
4. Fehlerhafte/überlange Frames schließen ausschließlich den betroffenen Peer
   und erzeugen einen diagnostizierbaren Disconnect. Buffer bei Close freigeben.

**Regressionen**

Legacy-i32 und typisierte RPCs über TCP; jedes Byte einzeln senden; mehrere RPCs
in einem Write; RPC zwischen REGISTER und DELTA; Disconnect mitten im Frame;
langsamer Peer darf den Host-Update für andere Peers nicht blockieren. Bestehende
WebSocket-Tests müssen denselben Parserpfad abdecken.

## 5. Arbeitspaket 3: Ungefragtes Weiterleiten entfernen [P1]

**C++-Stellen**

- `src/sys/mirror_runtime.cpp:558–560`, RPC-Enqueue und anschließender Broadcast.
- `mirror_send_rpc_v2` und `aiml_mirror_session_send_rpc[_model]`.
- `spec/44_5_mirror.md`, Routing-/Targeted-Send-Vertrag.

**Änderungen**

1. Den pauschalen `broadcast_pkt` nach eingehenden Low-Level-RPCs entfernen.
   Ein Client-Request wird nur lokal beim Host eingereiht.
2. Explizite Serverantworten weiterhin anhand der vom Socket bestimmten
   `peer_id` adressieren. `net_id` niemals als Ersatz für die Absenderidentität
   verwenden. Spec und Beispiele korrigieren, die unbekannte Targets noch als
   Broadcast oder Antworten über `rpc.net_id` beschreiben.
3. Automatisches Routing erst über den getrennten, registrierten Objektpfad in
   Paket 6 zulassen. Für Low-Level-RPCs keine vom Client wählbare Relay-Option.

**Regressionen**

Host mit zwei Clients: A sendet private Payload, B erhält nichts. Auch ein
von A gesendetes `set_result` erreicht B nicht. Gezielte Hostantwort erreicht nur
A; Host-Broadcast mit Target 0 erreicht beide. Unbekanntes/negatives Target
versendet nichts. Nach Disconnect darf eine alte Peer-ID keinen neuen Peer
adressieren; IDs innerhalb einer Session nicht wiederverwenden.

## 6. Arbeitspaket 4: Stabile Queue-Entnahme [P2]

**C++-Stellen**

- `AimlMirrorSession::rpc_queue` ab `mirror_runtime.cpp:205`.
- `aiml_mirror_session_send_and_await`, Requeue-Schleife aktuell um Zeile 1523.
- `aiml_mirror_session_take_rpc`, insbesondere `kMaxQueued = 64`.
- `next_rpc`, `poll_rpc`, `has_rpc`, Session-Close.

**Änderungen**

1. `std::queue<AimlRpcEvent*>` durch `std::deque` ersetzen und einen gemeinsamen
   Helfer `take_matching_rpc(predicate)` einführen. Nur den Treffer entfernen;
   sämtliche anderen Elemente bleiben an derselben Position.
2. `send_and_await` und `take_rpc` über diesen Helfer implementieren. Kein
   wiederholtes Pop/Push und kein Anhängen übersprungener Events hinter den Rest.
3. Das neue pauschale Löschen aller Events über Position 64 in `take_rpc`
   entfernen: Es löscht auch gültige Pushes anderer Consumer. Queue-Grenzen beim
   Empfang pro Peer anhand Anzahl UND Bytes durchsetzen; Überlast kontrolliert
   melden/Verbindung schließen, niemals stillschweigend fremde Events löschen.
4. Eventbesitz vereinheitlichen: jeder Eintrag wird genau einmal freigegeben.
   Borrow aus `next_rpc`/`poll_rpc` bleibt bis zur nächsten Entnahme desselben
   Borrow-Pfads oder Close gültig; interne Suchentnahme darf ihn nicht freigeben.

**Regressionen**

`A, Result, B` ergibt nach Entnahme `A, B`; Treffer vorn/hinten; kein Treffer;
Timeout; falsches Schema; mehrere gleiche Methoden; mindestens 65 unbeteiligte
Pushes. Unter ASan wiederholte Entnahme und Session-Close prüfen.

## 7. Arbeitspaket 5: Echte Request/Response-Korrelation [P1]

**C++-Stellen**

- `AimlMirrorSession`, `AimlRpcEvent`, `mirror_send_rpc_v2`, `process_packet`.
- `aiml_mirror_session_send_and_await` und neue Reply-Funktion in
  `src/sys/mirror_runtime.h/.cpp`.
- `builtin_registry.cpp`: `rpc_event`, `send_and_await` ab Zeile 10184 und
  neues Builtin `reply_rpc`.
- `context.h`: gemeinsame `rpc_event`-Definition aus Paket 0.

**Protokollentscheidung**

Neues, eindeutig unterscheidbares `PKT_RPC_V3` einführen; v2 nicht heuristisch
umdeuten. V3 erhält einen festen Header mit Version, Gesamtlänge, Nachrichtenart
(`push`, `request`, `response`, `object_call`), `request_id:u64`, separater
Objekt-ID und Routingmetadaten für Objektaufrufe. Alle Integer explizit LE
codieren; niemals C++-Structs als Wireformat kopieren. Methoden-/Payloadformat
und Feldbreiten vor Implementierung in der Spec vollständig festschreiben.

Capability-Austausch beim Verbindungsaufbau ergänzen. Während Capability-Status
unbekannt ist, keine neuen Request/Response-Pakete senden. Mit alten Peers nur
explizit unterstützte v2-One-Way-Kommunikation erlauben; `send_and_await` meldet
fehlende Unterstützung, statt unsicher auf Methodenmatching zurückzufallen.
Diese Protokolländerung benötigt koordinierte Aktualisierung von Client/Server.

**Änderungen**

1. Pro Verbindung monotone Request-IDs vergeben, 0 für Push reservieren.
   Verbindungsgeneration und erwarteten Antwort-Peer im lokalen Pending-Eintrag
   speichern; bei Disconnect Pending-Requests beenden. Überlauf kontrolliert
   behandeln, niemals eine noch relevante ID erneut vergeben.
2. `session.reply_rpc(ev, method, arg[, model])` ergänzen. Antwortziel aus dem
   vertrauenswürdigen `ev.peer_id`, Request-ID aus `ev.request_id` übernehmen;
   Response-Nachrichtenart setzen. Server-Handler und Tests auf dieses API
   migrieren. Ohne Echo der ID auf Serverseite ist die Korrektur unvollständig.
3. `send_and_await` wartet auf `response + request_id + erwartete Verbindung`;
   erst dann Methode/Schema/Payload prüfen. Eine korrelierte, aber kaputte Antwort
   beendet den Request als Decode-/Protokollfehler und wird nicht endlos gescannt.
4. Deadline mit monotoner Uhr bestimmen. Anzahl von 2-ms-Sleeps ist keine
   Zeitmessung; Update-/Decode-Dauer muss zum Timeout zählen.
5. Verspätete, doppelte und unbekannte Response-IDs werden gezielt als solche
   verworfen/diagnostiziert, ohne Pushes zu entfernen. Bereits vor dem Send
   wartende gleichartige Antworten dürfen nie den neuen Request erfüllen.
6. Request-ID-API auch für asynchrone Consumer bereitstellen, z.B.
   `send_request(...) -> u64` und `take_response(id, target)`. `take_rpc(method,
   target)` bleibt ausdrücklich ein Push-/Legacy-Queue-Filter und beansprucht
   keine sichere Request-Korrelation.

**Regressionen**

Request A läuft ab, Request B hat dasselbe Antwortschema, Antwort A trifft vor B
ein: Nur B erfüllt B. Zwei ausstehende Requests mit vertauschter Antwortreihenfolge;
Antwort vom falschen Peer; doppelte Antwort; neue Verbindung mit alten Events;
Timeout 0; typgleiche Push-Nachricht `set_result`; v2/v3-Inkompatibilität mit
klarem Fehler. Native↔WASM muss dieselben Wire-IDs und Headergrößen verwenden.

## 8. Arbeitspaket 6: Automatische Objekt-RPCs tatsächlich implementieren [P2]

**C++-Stellen**

- `src/core/ast/model_builder.cpp`, Einlesen von `rpc`.
- `src/core/ast_compiler.cpp:4355`, Übernahme von `rpcMethods` und Modellcodegen.
- `src/core/context.h:206`, Klassenmetadaten für `rpcMethods`.
- `src/core/ast_compiler_call.cpp:891`, `visitObjectMethodCall`, einschließlich
  Interface-Dispatch ab Zeile 944 und direkter Methodenauflösung um Zeile 3458.
- `builtin_registry.cpp`: `register`/`register_id` um Zeilen 9890/9925 sowie
  `set_rpc_target` um Zeile 10371.
- `mirror_runtime.cpp`: `AimlMirrorRegistered`, Register/Unregister ab Zeile
  1002, `set_rpc_target` ab Zeile 1258 und Session-Update.

**Compileränderungen**

1. `rpc:` gegen die tatsächlich aufgelösten Methoden validieren: Namen,
   Vererbung, Signaturen, `void`, serialisierbare Parameter. Für Parameter einen
   synthetischen Argument-Struct plus Serializerbeschreibung generieren; keine
   native Pointeradresse auf das Netzwerk schreiben.
2. Pro RPC einen öffentlichen Wrapper und einen lokalen Body/Dispatch-Thunk
   erzeugen. Der Wrapper trifft einmal die Routingentscheidung; der Thunk
   decodiert Argumente und ruft direkt den lokalen Body. Empfang darf nicht
   erneut durch den Netzwerk-Wrapper laufen und Relay-Schleifen erzeugen.
3. Direkte und Interface-/VTable-Aufrufe auf denselben Wrapper führen. Receiver
   und Argumentausdrücke nur einmal auswerten. Nicht registrierte Objekte behalten
   normale lokale Methodenaufrufe; registrierte Objekte benutzen RPC-Routing.
4. Registrierung übergibt RPC-Deskriptortabelle mit Methodennamen,
   Argument-Schema und Thunk. Unbekannte Methoden oder falsche Schemas dürfen
   niemals in beliebige lokale Methoden dispatchen.

**Runtimeänderungen und Ownership**

1. Registrierte Objekte erhalten Session-Zuordnung, stabile Netz-ID und
   `owner_peer_id`. Objektadressen sind nur lokale Lookup-Schlüssel. Mehrfache
   Registrierung desselben Objekts in verschiedenen Sessions als mehrdeutig
   ablehnen; Unregister/Close entfernt Zuordnung und Dispatch-Metadaten.
2. Host vergibt/kontrolliert globale Objekt-IDs und bestätigt sie dem
   registrierenden Client. Clientseitige Zähler dürfen nicht kollidierende globale
   IDs erzeugen. Register-Nachrichten übertragen autorisierte Ownership;
   Owner-Disconnect invalidiert/entfernt die betroffenen Registrierungen.
3. `set_rpc_target` speichert pro Objekt/Methode einen validierten Enum.
   Unbekannte Methoden/Ziele oder unregistrierte Objekte melden einen Fehler.
4. Ziele exakt implementieren:
   - `owner`: nur Owner; lokal, falls Aufrufer Owner ist.
   - `server`: nur Host; auf dem Host lokal.
   - `all`: jeder beteiligte Peer einschließlich Sender genau einmal.
   - `others`: alle beteiligten Peers außer Sender, einschließlich Host, sofern
     dieser nicht Sender ist.
5. Clients vermitteln Remote-Ziele über den Host. Nur `object_call` mit gültiger
   Objekt-/Methodenregistrierung darf geroutet werden. Host prüft freigegebene
   Zielpolitik; bloße Clientangaben sind keine Relay-Erlaubnis. Sender-ID wird
   ausschließlich aus der Verbindung gewonnen und nur vom Host weitergegeben.
   Vom Server festgelegte Methoden-/Target-Freigaben können Clients nicht durch
   ihr lokales `set_rpc_target` erweitern.
6. Beim Empfang erlaubt nur der separate Objektpfad automatische Ausführung;
   Low-Level-RPCs bleiben für manuelle Handler in der Queue. Ein Paket wird nie
   automatisch ausgeführt UND anschließend manuell erneut zugestellt.
7. Lokale Ausführung und Remote-Weiterleitung für `all`/`others` zusammen planen,
   sodass der Host den Sender nicht nochmals beliefert. Definierten Schutz vor
   erneuter Ausführung desselben weitergereichten Objektaufrufs vorsehen.

**Regressionen**

Host plus zwei Clients; alle vier Ziele jeweils von Host und Client aufrufen;
pro Ziel exakte Ausführungsanzahl und Absender prüfen. Unregistriertes Objekt,
unbekannte Methode, ungültiges Ziel, fehlende Freigabe, kollidierende Registrierung,
Owner-Disconnect, Unregister, geerbte RPC-Methode, Interface-Aufruf und
Seiteneffekt in einem Argument. Negative Compilerfälle für nicht serialisierbare
Parameter und nicht unterstützte Rückgabewerte. Native↔WASM mit String-/Listen-
Argumenten. Test beweist insbesondere: kein erneutes Senden beim Dispatch-Thunks.

## 9. Geplante Testdateien und Integrationsreihenfolge

Neue gezielte Runtime-Suite, z.B. `tests/runtime/test_mirror_rpc_protocol.cpp`,
mit privatem testbarem Parser-/Queue-Modul; produktive `.cpp`-Dateien nicht als
Dauerlösung in Tests inkludieren. Zusätzlich:

| Testdatei (neu) | Prüft |
| --- | --- |
| `tests/test_mirror_rpc_tcp.aiml` | i32 und typisierte TCP-RPCs |
| `tests/test_mirror_rpc_no_relay.aiml` | Isolation zweier Clients, explizite Targets |
| `tests/test_mirror_rpc_queue_order.aiml` | stabile Entnahme, mehr als 64 Pushes |
| `tests/test_mirror_rpc_request_id.aiml` | Timeout-Straggler, ID-/Peer-Korrelation |
| `tests/test_mirror_rpc_auto_dispatch.aiml` | normale Methodenaufrufe und Zielmatrix |
| `tests/test_mirror_rpc_decode_failure.aiml` | Ziel bleibt erhalten, Speicherfreigabe |
| `tests/wasm/test_mirror_rpc_v3_client.aiml` | Wire-/ABI-Parität zum nativen Server |

Bestehende Tests weiterführen: `test_mirror_rpc_int_compat`,
`test_mirror_rpc_model_ws`, `test_mirror_rpc_model_map`, `test_mirror_rpc_peer_id`,
`test_mirror_rpc_dispatcher`, `test_mirror_rpc_handler_builtin`,
`test_rpc_typed_lists_inherited`, `test_iface_list_ptr_rpc_binary` und die
bestehenden Single-/Multifile-WASM-RPC-Tests. TCP-/WS-Tests mit isolierten Ports
oder dynamischer Portvergabe ausführen.

Empfohlene Commit-Reihenfolge:

1. Build/ABI und längenbewusster Parser/Decoder mit Sanitizer-Regressionen.
2. TCP-Buffering und korrektes v2-Framing.
3. Relay entfernen, gezielte Zustellung testen und Spec korrigieren.
4. Queue-Entnahme vereinheitlichen und stilles Löschen in `take_rpc` entfernen.
5. V3-Header/Capabilities, Request-IDs, Reply-API und Handler-Migration gemeinsam.
6. RPC-Wrapper/Thunks, Registrierung/Ownership und Target-Routing gemeinsam.
7. Native/WASM-Integration, Dokumentation und vollständige relevante Testsuite.

Jeder Commit muss kompilieren und seine gezielten Regressionen bestehen.
Automatisches Routing darf nicht als bloße Implementierung des bisherigen
`set_rpc_target`-Stubs abgenommen werden: Compiler-Wrapper, Objektidentität,
Empfangs-Dispatch und Autorisierung gehören zusammen.

## 10. Fertig-Kriterien

- Alle sechs Review-Befunde besitzen einen zuvor fehlschlagenden Regressionstest.
- Malformed-Packet-Suite unter ASan/UBSan ohne Speicherfehler; Decode-Fehlerpfade
  ohne Leaks und ohne Mutation des bestehenden Zielobjekts.
- TCP und WebSocket liefern dieselbe RPC-Semantik; native/WASM-Interop getestet.
- Keine private Client-Payload wird implizit weitergereicht.
- Alte/fremde Antworten können keinen neuen Request erfüllen.
- Push-Reihenfolge bleibt erhalten, Queue-Überlast ist sichtbar und begrenzt.
- `rpc:` und alle dokumentierten Targets wirken bei normalen Methodenaufrufen.
- `spec/44_5_mirror.md`, Runtime-Header und Compiler-Builtins stimmen überein;
  v2/v3-Migration und Grenzen sind explizit dokumentiert.

## 11. Entscheidung vom 2026-10-03: DELTA, Replika-Anfangszustand und WS9-Test

Status: verbindliche Präzisierung des Plans; die nachstehenden Runtime-/Test-
Korrekturen sind damit noch nicht implementiert. Die Implementierung ist seit
Erstellung dieses Plans weiterentwickelt worden; dieser Abschnitt bezieht sich
auf den am 2026-10-03 geprüften Stand.

### 11.1 Auch DELTA wird strikt und transaktional

**Entscheidung: keine implizite Best-effort-Ausnahme für DELTA.** Empfangene
Snapshots werden vollständig validiert und entweder vollständig übernommen oder
vollständig verworfen. Bei einem Fehler bleiben der letzte gültige Zustand bzw.
der gültige lokale Default-Zustand unverändert. Schemaabweichungen müssen sichtbar
werden; ein Verbindungsabbruch ist bei einem Schemafehler nicht zwingend, ein
Erfolgs-/Ready-Signal ist jedoch verboten.

Begründung anhand der tatsächlichen Fixtures:

- `tests/ws_context_card_a.aiml`: `SharedCard = {name:string, cost:i32}`.
- `tests/ws_context_card_b.aiml`: zusätzlich `rarity:i32`.
- `tests/ws_context_shared_view.aiml`: enthält `list<SharedCard>`.
- `test_mirror_ws_shared_module_context_mismatch` prüft also wirklich verschiedene
  verschachtelte Schemata, nicht lediglich verschiedene lokale Compiler-Typ-IDs.
  Er verwendet nur ein Kartenelement und prüft die bereits gelesenen Präfixfelder.

Das positionsbasierte Wireformat enthält keine Feld-IDs oder Längengrenze pro
Struct-Element. Schon bei zwei Karten kann der Empfänger das fehlende `rarity`
aus den Bytes des nächsten Elements lesen. Ein begrenzter Bufferzugriff macht
solche Daten weder kompatibel noch semantisch richtig. Der bestehende grüne Test
ist deshalb keine Grundlage für einen Versionskompatibilitätsvertrag.

**Konkrete Umsetzung:**

1. `src/sys/mirror_runtime.cpp`, `process_packet`, DELTA-Zweig um Zeile 590:
   `aiml_ser_decode_bounded` und vorheriges Free/Memset durch transaktionales
   Decodieren ersetzen. `has_decoded`, Shadow und eventuelle Ready-Meldung nur
   nach erfolgreichem Commit aktualisieren.
2. `aiml_mirror_session_register_id_typed`, Pending-Blob-Zweig um Zeile 1287:
   denselben zentralen Apply-Helper verwenden. Rückgabewert prüfen, Fehler nicht
   als erfolgreichen Anfangssnapshot markieren. Pending-Blob in jedem Fall
   geordnet freigeben, Fehler diagnostizieren.
3. Auch die Schemaidentität prüfen: bloßes vollständiges Konsumieren der Bytes
   erkennt z.B. gleich große, unterschiedlich interpretierte Felder nicht.
   Für DELTA einen strukturellen Wire-Schema-Fingerprint übertragen/abgleichen,
   der unabhängig von nativen Offsets, Pointerbreite und lokalen Registry-IDs
   ist. Gleiche Wire-Schemata aus verschiedenen Importkontexten müssen passen.
   DELTA-Formatänderung ausdrücklich versionieren bzw. aushandeln; vorhandene
   ungetaggte Frames niemals fälschlich als schemaverifiziert bezeichnen.
4. Den bisherigen Mismatch-Erfolgstest in einen negativen Test überführen:
   unterschiedliches Schema wird abgelehnt, Default-/Vorherzustand bleibt
   unverändert, und ein beobachtbares Decode-/Schemafehler-Ergebnis entsteht.
   Beide Richtungen und mindestens zwei verschachtelte Listenelemente prüfen.
5. Separaten positiven Importkontext-Test mit identischem Wire-Schema, aber
   unterschiedlichen Importsets/Compiler-Kontexten erhalten.

Soll später Schema-Evolution unterstützt werden, braucht sie ein separates,
versioniertes Format mit eindeutigen Feldgrenzen/IDs oder explizite Migrationen.
Dabei bleibt das Decodieren strikt: erst Quellschema validieren, dann definiert
migrieren. Ein stilles Best-effort-Fallback wird nicht eingeführt.

### 11.2 Ungültiger Replika-Anfangszustand bleibt ein eigener Fehler

**Entscheidung: gültige lokale Initialisierung ist Voraussetzung der Replikation;
`release_old = 0` und Fehler-Toleranz sind keine dauerhafte Reparatur.**

Gesicherte Befunde im aktuellen Runtime-Code:

- `process_packet` setzt `has_decoded = true`, auch wenn der Decoder scheitert.
- `register_id_typed` ignoriert beim Pending-Blob den Decode-Rückgabewert.
- Beim ersten Decode wird direkt in ein bereits vorhandenes Modell geschrieben;
  der bisherige Default-/Heap-Inhalt hat keinen einheitlichen Ersetzungsvertrag.
- `session.close` und `PKT_UNREGISTER` geben teilweise Modellfelder frei,
  während Compiler-Scope-Cleanup ebenfalls Ownership besitzt. Diese Zuständigkeit
  muss vor einer pauschalen Änderung zu `release_old = 1` geklärt werden.

Die konkrete Ursache des gemeldeten ungültigen Zustands vor dem ersten Decode
ist durch diese Analyse noch nicht isoliert. Nicht als behoben deklarieren.

**Umsetzung und Abnahme:**

1. Minimalreproduktion ohne Netzwerk: dasselbe Replika-Modell deklarieren,
   String-/Listen-/Map-Defaults lesen, serialisieren und verlassen. Lokal,
   importiert-global und bei aktivierter Refcount-Repräsentation testen.
2. C++-Anker: `Compiler::initClassContainerMembers` in
   `src/core/ast_compiler.cpp:131`, globale Initialisierung in derselben Datei;
   `src/core/ast_compiler_stmt.cpp` um Zeile 1711 und ScopeDrop-Registrierung;
   `builtin_registry.cpp`, `MIRROR_FIND_OBJ_PTR` und `register_id`.
   Initialisierung, richtige Objektadresse und erzeugte Serializer-Deskriptoren
   gegeneinander prüfen. Keine pauschale Nullinitialisierung lebender Container.
3. Ziel-Ownership festlegen: Mirror registriert ein vom AIML-Aufrufer besessenes
   Objekt als Borrow. Erfolgreiches Apply ersetzt dessen Felder korrekt; Close
   und Unregister lösen die Registrierung und Runtime-Buffer, zerstören aber
   nicht heimlich das weiterhin lebende AIML-Modell. Den Compiler-Scope-Drop
   und den bestehenden Runtime-Free-Pfad darauf abstimmen.
4. Bereitschaft getrennt von gültiger Initialisierung behandeln:
   `WaitingForSnapshot` ist ein gültiges lokales Objekt; `Ready` bedeutet einen
   erfolgreich übernommenen Snapshot. Ein fehlerhafter Snapshot darf weder
   Bereitschaft noch erfolgreiche Decode-Historie erzeugen.
5. Regressionen für DELTA vor/nach `register_id`, erstes DELTA ungültig, gültig →
   ungültig → gültig, Close vor dem ersten DELTA und Modellzugriff nach Close.
   ASan/UBSan plus Leak-/Double-Free-Prüfung; bestehende Defaults müssen bei
   abgewiesenem Snapshot erhalten bleiben.

### 11.3 Timing-Abhängigkeit im WS9-Test entfernen

Prüfung am 2026-10-03: `test_mirror_ws_9rooms_imports.aiml` mit dem vorhandenen
Binary 16-mal seriell ausgeführt, 16/16 erfolgreich. Logs liegen lokal unter
`/tmp/aiml-ws9-review/run-*.log`. Das reproduziert weder die berichtete Fehlerrate
noch widerlegt es die Race-Stelle.

**Konkrete Race-Stelle:** Im Host-Zweig des Tests werden nach
`object_registered` lediglich 16 ms gewartet und zweimal `sess.update` aufgerufen.
Danach setzt der Test `got_ci = 1` und kopiert `player_name` in den View.
REGISTER bestätigt jedoch weder den Empfang noch das erfolgreiche Anwenden des
folgenden DELTA. Kommt dieses später, wird der View mit dem alten Namen gefüllt
und anschließend nicht mehr berichtigt.

**Entscheidung: Zustands-/Acknowledgement-Synchronisierung statt längerer Sleeps.**

1. Nach REGISTER nur die Replika binden. In der Update-Schleife bis zur Deadline
   auf erfolgreich angewandtes ClientInfo warten; im konkreten Test zusätzlich
   `player_name == "Nikolai"`, `deckset_id == "deckset-alpha"` und
   `player_id == "token-123"` prüfen. Erst dann View füllen und `got_ci` setzen.
2. Generisch ist ein Ready-/Snapshot-Zähler aus einem erfolgreichen Apply besser
   als ein Feldwert als Signal. Ein leerer Name kann im echten Programm legitim
   sein; die oben genannten Werte sind ausschließlich Test-Prädikate.
3. Host bis zur Client-Bestätigung des vollständig geprüften View weiterpumpen.
   Client bestätigt über explizites RPC; Host validiert seine eigenen
   ClientInfo-Felder vor der Bestätigung. Die derzeitigen 40 festen
   Abschluss-Updates ersetzen keine Empfangsbestätigung.
4. Keine unsynchronisierten Reads von Host-Globals durch den Client-Thread.
   Host prüft Serverfelder selbst; dessen Ergebnis nach Thread-Join auswerten
   bzw. über einen synchronisierten Kanal übertragen. Objekt-Lifetime beim
   Session-Close darf nicht mit den Assertions konkurrieren.
5. Timeouts in jeder Phase müssen den tatsächlich wartenden Zustand benennen
   und als Fehler enden. Sleep dient höchstens der Poll-Taktung, nicht dem
   Nachweis der Bereitschaft.
6. Zusätzlich zum Wiederholungslauf DELTA gezielt nach REGISTER verzögern,
   z.B. über einen Test-Transport/Proxy. Der Test muss auch mit deutlich mehr
   als 16 ms Verzögerung bestehen. Danach 50–100 serielle Läufe; ein zufällig
   grüner Lauf allein ist keine Abnahme. Fester Port verlangt isolierte Ausführung.

Konkrete C++-Änderungen, API-Signaturen und Code-Snippets zu Abschnitt 11:
[DELTA-/Replika-/WS9-Implementierungsplan](delta_replica_ws9_implementation_plan_2026-10-03.md).
