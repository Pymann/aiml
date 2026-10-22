# Analyse: Rendering, Netzwerk und Drag-and-drop in AIML / 9rooms_v2

Stand: 18.10.2026. Ausschließlich statische Codeanalyse, keine Implementierung und keine Laufzeitmessung. Untersucht wurden die aktuellen Arbeitsdateien von AIML (HEAD `d216077`, mit vorhandenen lokalen Änderungen) und 9rooms_v2 (HEAD `ed4f3a7`). Aussagen beziehen sich auf diesen Quellstand, nicht auf einen nachgewiesenen Stand der ausgelieferten Browser-Binaries. Der gemeldete einmalige Deck-Editor-Fehler wurde nicht untersucht.

**Ergebnis:** Die beobachteten Pausen passen sehr gut zur vorhandenen Architektur. Spielaktionen warten im UI-Frame auf RPC-Antworten; anschließend folgen häufig eine zweite Zustandsabfrage und ein vollständiger Board-Neuaufbau. Drag-and-drop hängt an denselben Frames. Dazu kommen eigenständige Schwächen in Gestenerkennung, Trefferprüfung und Browser-Frame-Taktung. Ein langsames Netzwerk verstärkt diese Probleme; es muss sie nicht erst verursachen.

**Evidenz:** „Belegt“ bedeutet direkt im Code nachvollziehbar. „Risiko“ bezeichnet einen daraus ableitbaren Fehlerpfad ohne Reproduktion. Kostenanteile und die konkrete Ursache des gelegentlich misslungenen Greifens bleiben ohne Messung offen. Quellverweise stehen relativ zu diesem Bericht; die genannten Zeilen beziehen sich auf den untersuchten Arbeitsstand.

## 1. Ablauf eines Frames und einer Aktion

Die App legt Logik, Eingabeauswertung, RPCs, Board-Aufbau und Zeichnen in `MainMenuApp.frame`. Der separate Engine-Render-Callback bleibt leer. Erst am Ende von `frame` werden Canvas und Szene gezeichnet.

- [App-Loop und Callback-Anbindung](../../../9rooms_v2/client/main_menu_app.aiml#L546)
- [Zeichnen am Frame-Ende](../../../9rooms_v2/client/main_menu_app.aiml#L1083)
- [Engine-Loop](../../src/sys/engine_runtime.cpp#L491)

Beim Angriff ist der Ablauf konkret:

1. `board.render` verarbeitet den Drag und erzeugt einen Angriffs-Intent.
2. `net_attack_creature` wartet synchron auf `attack_creature` (Timeout 5 s).
3. Bei Erfolg wartet `net_get_match_state` synchron auf den frischen Zustand (Timeout 4 s).
4. Die App konvertiert die Antwort, verarbeitet Todesanzeigen, zerstört das Board und baut es durch einen weiteren `board.render` wieder auf.
5. Erst später folgt `engine.draw_scene`.

Belege: [Angriffspfad](../../../9rooms_v2/client/main_menu_app.aiml#L863), [RPC-Wrapper](../../../9rooms_v2/client/net/match_rpc.aiml#L213).

**Folge:** Die sichtbare Reaktion hängt mindestens von zwei aufeinanderfolgenden Anfrage-/Antwortzyklen und der lokalen Aufbauzeit ab. Beispiel, keine Messung: Bereits zwei Zyklen zu je 80 ms ergeben ungefähr 160 ms vor zusätzlicher lokaler Arbeit. Ein schneller Renderer kann dieses Warten nicht ausgleichen.

Der Phasen-Check ruft ebenfalls synchron `net_confirm_phase` auf. Seine Antwort wird an dieser Stelle nicht ausgewertet; lediglich `board_poll=60` wird gesetzt. Die periodische Abfrage kann trotzdem noch am Eingabe-Cooldown scheitern. Der Kommentar „sofort neu pollen“ entspricht deshalb nicht in jeder Situation dem tatsächlichen Ablauf. Der Check wird bis zur bestätigten Zustandsübernahme wieder auf den alten Serverwert gesetzt.

Belege: [Check-Verarbeitung](../../../9rooms_v2/client/main_menu_app.aiml#L792), [Cooldown](../../../9rooms_v2/client/main_menu_app.aiml#L676), [Check-Anzeige](../../../9rooms_v2/client/board_screen.aiml#L385).

**UX-Problem:** „Gesendet“, „Server wartet“, „abgelehnt“ und „noch nicht geklickt“ sind hier nicht sauber getrennt. Beim Angriff landen Ablehnungen im Log; beim Phasen-Check wird selbst das Ergebnis nicht explizit behandelt.

## 2. Netzwerk: brauchbare Grundlage, blockierende Nutzung

### 2.1 Synchrone RPCs halten den Spiel-Frame an — belegt, höchste Priorität

`send_and_await` sendet eine korrelierte Anfrage und pumpt die Session in einer Warteschleife mit 2-ms-Sleeps. Es ruft währenddessen weder die App-Framefunktion noch den Renderer auf. Unter WASM führt der Sleep über Asyncify zurück zum Browser, aber der AIML-Spiel-Frame bleibt suspendiert. Der Browser kann somit weiter Ereignisse empfangen, während die Spieloberfläche ihr letztes Bild zeigt.

Belege: [send_and_await](../../src/sys/mirror_runtime.cpp#L1955), [Sleep unter WASM](../../src/sys/time_runtime.cpp#L39).

Positiv: Request-ID und antwortender Peer werden geprüft; fremde Antworten bleiben in der Queue. Das frühere Problem einer rein FIFO-basierten Antwortzuordnung sollte diesem aktuellen Code nicht pauschal unterstellt werden. `send_request`/`take_response` existieren bereits; der Board-Eintritt nutzt sie. Es fehlt vor allem ihre konsequente Verwendung für normale Spielaktionen und Board-Polls.

Trade-off: Synchrone Wrapper vereinfachen den Kontrollfluss und die Fehlerbehandlung. Asynchrone Aktionen benötigen explizite Zustände für ausstehend, erfolgreich, abgelehnt, Timeout und Verbindungsabbruch. Dafür bleiben Eingabe und Animation unabhängig von der Netzlatenz.

### 2.2 Polling ist an Frames und Interaktionen gebunden — belegt

Das Board fragt nach 60 Frames ab, setzt bei Press/Release einen Cooldown von 30 Frames und pollt nicht während eines aktiven Drags. Das schützt bestehende Prefab-Handles vor Zerstörung, koppelt aber Datenfrische an FPS und Nutzerverhalten.

Bei 60 FPS sind 60 Frames ungefähr eine Sekunde; bei 30 FPS ungefähr zwei. Häufige Eingaben oder langes Festhalten können den Zustand weiter verzögern. Besonders ungünstig: Ein veraltetes `combat_pending=1` sperrt weiter das Draggen, und neue Klickversuche verschieben die Abfrage, die diesen Zustand korrigieren könnte.

Trade-off: Polling ist einfach, robust und kann verpasste Updates heilen. Server-Push reduziert Leerlaufabfragen und Reaktionszeit, braucht aber Revisionen, Wiederanmeldung und eine vollständige Resynchronisation nach Lücken. Ein sinnvoller Zwischenschritt wäre nichtblockierendes Polling mit Zeitintervallen und höchstens einer ausstehenden Abfrage.

### 2.3 Die Zustandsantwort ist teuerer als ein kleiner Status-Ping — belegt; Größenordnung ungemessen

`MatchService.get` lädt den Matchzustand, prüft Timeouts, kann Änderungen speichern und KI-Kampf fortsetzen, erzeugt eine gefilterte Spielersicht und sammelt sichtbare Kartendaten. Der Client konvertiert diese Antwort erneut in `MockState`. Auch unveränderte Matches durchlaufen damit relevante Arbeit.

Belege: [MatchService.get](../../../9rooms_v2/sources/match/match_service.aiml#L206), [Client-Konvertierung](../../../9rooms_v2/client/match_to_mock.aiml#L234).

Ein kompletter Snapshot ist leicht nachvollziehbar und heilt Abweichungen. Revisionen und Deltas sparen Bandbreite, Serialisierung und Projektion, verlangen aber eine definierte Reihenfolge, Baseline und Snapshot-Fallback. Auch statische Kartendefinitionen könnten getrennt von dynamischen Matchdaten versioniert werden.

Die zweite Zustandsabfrage nach einer Aktion lässt sich **nicht blind** durch Verwendung der ersten Antwort streichen: Die Handler führen teilweise nach Erzeugung des Response-Objekts noch KI-/Auflösungsarbeit aus. Außerdem erstellt `get` die gefilterte Sicht samt `visible_cards`. Ein zusammengefasster Antwortvertrag müsste den endgültigen, für diesen Spieler gefilterten Zustand liefern.

Belege: [ConfirmPhaseHandler](../../../9rooms_v2/sources/game/rpc_handlers/match_play_handlers.aiml#L38), [AttackCreatureHandler](../../../9rooms_v2/sources/game/rpc_handlers/match_combat_handlers.aiml#L41).

### 2.4 Server und nativer WebSocket-Pfad können andere Arbeit aufhalten — belegt

Der Gameserver verarbeitet RPCs nacheinander in einer Schleife. KI-Aktionen und Regelauflösung laufen vor der Antwort; periodische Match-Ticks laufen ebenfalls dort. Ein langsamer Handler verzögert deshalb weitere Clients in diesem Prozess. Die tatsächliche Dauer dieser Arbeit wurde nicht gemessen.

Beleg: [Server-Schleife](../../../9rooms_v2/sources/game/game_server.aiml#L298).

Zusätzlich bedeutet `ws_poll(..., 0)` im nativen WS/WSS-Pfad nur, dass Daten bzw. ein Socket-Ereignis vorhanden sind. Anschließend liest `ws_recv_frame_raw` Header und vollständigen Payload mit blockierenden `ws_read_n`-Aufrufen. Ein Teilframe kann daher die gesamte Session-Pumpe blockieren. Auch der Schreibpfad schreibt synchron vollständig.

Der aktuelle Accept-Pfad setzt bereits einen 10-s-Lesetimeout. Das begrenzt einzelne blockierende Reads, macht den Empfang aber nicht nichtblockierend und ist keine harte 10-s-Gesamtfrist für einen Frame. Die äußere RPC-Frist wird während eines darunter blockierenden Reads nicht laufend geprüft. Die 4-/5-s-RPC-Timeouts sind deshalb auf diesem Pfad keine garantierten maximalen Wandzeiten.

Belege: [Accept/Timeout](../../src/sys/ws_runtime.cpp#L615), [Low-level I/O](../../src/sys/ws_runtime.cpp#L424), [Frame-Empfang](../../src/sys/ws_runtime.cpp#L750), [Poll](../../src/sys/ws_runtime.cpp#L1172), [Mirror-Pumpe](../../src/sys/mirror_runtime.cpp#L882).

Abgrenzung: Der Browser-WebSocket-Empfang arbeitet mit bereits angekommenen Nachrichten; diese native Teilframe-Schwäche darf ihm nicht direkt zugeschrieben werden. Ein Browserclient kann ihre Folgen aber über den nativen Server spüren. Der Mirror-TCP-Pfad puffert bereits Teilpakete und arbeitet nichtblockierend. Auch die Annahme neuer WS-Verbindungen wird bereits schrittweise bearbeitet.

Trade-off: Ein inkrementeller Parser und gepufferte Writes brauchen mehr Zustand, Limits und Backpressure. Sie beseitigen jedoch das grundlegende Warten auf einen einzelnen Peer. Zusätzliche Threads können CPU-Arbeit entkoppeln, lösen diesen Vertrag nicht automatisch; Matchänderungen müssen weiterhin geordnet bleiben.

## 3. Rendering und Frame-Taktung

### 3.1 Browser-Takt: Arbeitszeit plus 16 ms — belegt

Die aktuelle WASM-Engine nutzt eine manuelle Asyncify-Schleife: `web_loop_tick`, danach `emscripten_sleep(16)`. Sie verwendet hier **nicht** `requestAnimationFrame`, obwohl einige App-Kommentare das noch nahelegen. Die Entscheidung ist als Workaround für frühere Asyncify/Main-loop-Probleme dokumentiert.

Die Wartezeit kommt zusätzlich zur Frame-Arbeit. Rechnerisch ergeben 5 ms Arbeit plus 16 ms Schlaf rund 48 FPS, 10 ms plus 16 ms rund 38 FPS; Browser-Scheduling kann davon abweichen. Der Timer ist zudem nicht direkt an die Bildschirmpräsentation gekoppelt. Native Frames verwenden hingegen Swap/Poll und einen Restbudget-Limiter.

Beleg: [Tatsächliche Loop-Implementierung](../../src/sys/engine_runtime.cpp#L503).

Trade-off: Der Workaround stabilisiert den beschriebenen Ablauf, kostet aber Taktqualität. Eine spätere Umstellung auf browsergesteuerte Frames sollte mit Entfernung der suspendierenden Arbeit aus Frame-Callbacks erfolgen und die dokumentierten Freeze-Szenarien erneut prüfen. Ein isolierter Austausch der Schleife wäre riskant.

### 3.2 Vollständige Board-Rebuilds — belegt

`reload` zerstört unter anderem Hintergrund, Phasenpanel, Handkarten, Zonen, Feldkarten, Vorschau und zahlreiche Overlays. Der nächste `render` initialisiert sie wieder. Erfolgreiche Aktionen erzwingen diesen Weg oft direkt.

Beleg: [BoardScreen.reload](../../../9rooms_v2/client/board_screen.aiml#L460).

Positiv: Periodische Polls vergleichen bereits eine Inhaltssignatur; Tap- und Angreiferanzeigen können ohne Rebuild aktualisiert werden. Floats bleiben beim Reload erhalten. Es wird also nicht bei jedem Frame oder jedem Poll alles neu erzeugt.

Die Granularität bleibt trotzdem grob: Änderungen an Stats können das gesamte Board neu aufbauen. Das erzeugt CPU-/Allokationsspitzen und zerstört visuelle Identität, Hover-Bezüge und potenziell Interaktions-Handles. Der Drag-Schutz existiert ausdrücklich im periodischen Poll-Pfad und ist kein allgemeiner Vertrag für jede Zustandsübernahme.

Trade-off: Vollaufbau vereinfacht Konsistenz. Ein Abgleich über stabile Karten-Instanz-IDs hält Objekte und Gesten stabil, verlangt aber explizite Updates für Position, Stats, Ausstattung und Sichtbarkeit. Ein Zwischenweg wären begrenzte Aktualisierungen einzelner Zonen oder Karten.

### 3.3 Rendering verarbeitet auch Eingaben — belegt

`BoardScreen.render` ruft `apply_drag`, Phasen-Check, Tastaturaktionen, Zielauswahl und Ability-UI auf. Nach einer erfolgreichen Aktion wird dieselbe Methode innerhalb desselben App-Frames erneut aufgerufen. Das ist keine reine Darstellung. Einzelne Pfade besitzen bereits Latches gegen doppelte Verarbeitung, aber diese Schutzlogik ist verteilt.

Beleg: [BoardScreen.render](../../../9rooms_v2/client/board_screen.aiml#L675).

Risiko: Ein Eingabeereignis kann auf einen neuen Zustand oder neu erzeugte Widgets erneut angewendet werden. Nach einem Reload liegt die globale Szene außerdem noch nicht zwingend in der vom nächsten Draw hergestellten Z-Reihenfolge vor. Eine saubere Reihenfolge wäre: Eingabesnapshot einmal verarbeiten, Ergebnisse übernehmen, Layout aktualisieren, zeichnen.

### 3.4 CPU-/GPU-Arbeit: plausible Zusatzkosten, kein nachgewiesenes GPU-Limit

Die Prefab-Runtime sortiert die globale Instanzliste in jedem Draw, kopiert sie für eine sichere Traversierung und synchronisiert Container/Cascade-Inhalte während des Renderns. Viele lokale Batches führen eigene Buffer-Uploads, Uniform-Abfragen, Texture-Bindings und Draw Calls aus. Text und komplexe Karten erhöhen diese Arbeit.

Belege: [draw_scene](../../src/sys/prefab_runtime.cpp#L5217), [render_instance](../../src/sys/prefab_runtime.cpp#L2104), [flush_batch](../../src/sys/prefab_runtime.cpp#L971).

Positiv: Texturen, Fonts und Materialprogramme haben Caches; unsichtbare Instanzen werden beim eigentlichen Rendern übersprungen. Ein Board-Rebuild bedeutet deshalb nicht automatisch erneutes Laden aller Texturen.

Trade-offs: Dirty-Flags sparen Sortierung/Layout, benötigen verlässliche Invalidierung. Größere Batches reduzieren API-Aufrufe, dürfen aber transparente Zeichenreihenfolge, Clipping und Materialien nicht verfälschen. Diese Optimierungen sollten nach CPU-/GPU-Messung priorisiert werden. Für die aktionsgebundenen Pausen ist der synchrone Kontrollfluss wesentlich direkter belegt.

### 3.5 Artwork kann beim Kartenaufbau synchron nachgeladen werden — belegt

Token- und CardFull-Aufbau rufen `art_ensure` auf. Bei Cache-Miss folgen Verbindungsaufbau, Header-/Chunk-Empfang, Bilddekodierung und Texturregistrierung synchron. Eine neue Karte kann dadurch eine zusätzliche Pause verursachen. Bereits gecachte Bilder vermeiden den Transfer; das ist keine Erklärung für jeden wiederholten Klick.

Belege: [Token-Aufbau](../../../9rooms_v2/client/board_widgets.aiml#L42), [Artwork-Fetch](../../../9rooms_v2/client/net/card_art_cache.aiml#L84).

Ein Platzhalter mit Hintergrundladen würde den Aufbau entkoppeln. Dafür müssen Fehler, Wiederholungen, Ladepriorität und Speicherbudget explizit verwaltet werden.

## 4. Drag-and-drop: konkrete Fehlerklassen

### 4.1 Ein schneller Drag kann vollständig zwischen zwei Spiel-Updates liegen

Die Browser-Eingabe speichert Press/Release-Flanken, aber `engine.update_drag` startet anhand von `btn_down`. Erfolgen Drücken und Loslassen während eines blockierten Frames, kann der nächste Snapshot gleichzeitig beide Flanken und `button=0` enthalten. Dann startet kein Prefab-Drag. Beginnt die Verarbeitung erst nach einer Mausbewegung, wird außerdem die aktuelle Position statt einer gespeicherten Press-Position zum Auswählen der Karte verwendet.

Belege: [Browser-Snapshot](../../src/sys/input_runtime.cpp#L1491), [Browser-Mousedown](../../src/sys/input_runtime.cpp#L1051), [Prefab-Drag](../../src/sys/prefab_runtime.cpp#L6763).

**Das ist eine konkrete, zum Symptom passende Möglichkeit, keine Reproduktion deines Falls.** Das entgegengesetzte Verhalten ist ebenfalls möglich: Bei bereits gehaltener Taste startet ein Drag erst beim späteren Hineinfahren über eine Karte. Es gibt in diesem Prefab-Pfad keine vorgeschaltete Press-Kandidat-/Bewegungsschwellen-Phase.

### 4.2 Kampfreaktionen sperren alle Drags

Bei `combat_pending=1` wird ein vorhandener Drag abgebrochen und vor `engine.update_drag` zurückgekehrt. Damit soll ein Reaktionsklick nicht versehentlich zum Drag werden. Das ist absichtliche Spiellogik, wirkt ohne eindeutig erkennbaren Sperrgrund aber wie ein Eingabefehler. Ein durch Polling veralteter Zustand kann die Sperre länger sichtbar halten.

Beleg: [BoardDragController.apply_drag](../../../9rooms_v2/client/board_drag_controller.aiml#L443).

### 4.3 Aufnahme und Ablage haben unterschiedliche Regeln

Die Aufnahme durchsucht rückwärts alle sichtbaren, draggbaren Instanzen und berücksichtigt verschachtelte Transformationen. Nichtdraggbarer Vordergrund wird dabei übersprungen: Ein Overlay ist damit nicht automatisch eine Eingabesbarriere.

Die Ablage nutzt dagegen `hit_test`: nur Top-level-Instanzen, rechteckige Bounds, erstes sichtbares Objekt. Ein dekoratives Objekt kann deshalb das eigentliche Ziel verdecken. Rotation wird dort nicht in eine präzise gedrehte Trefferfläche umgesetzt. Beide Pfade verlassen sich auf die zuletzt sortierte Instanzliste; Z-Änderungen werden erst beim Zeichnen sortiert.

Belege: [Aufnahme](../../src/sys/prefab_runtime.cpp#L6763), [hit_test](../../src/sys/prefab_runtime.cpp#L5474), [Drop-Auswertung](../../../9rooms_v2/client/board_drag_controller.aiml#L220).

Das beweist nicht, dass ein bestimmtes Overlay deinen Drag blockierte. Es belegt aber fehlende einheitliche Regeln für Vordergrund, Durchklickbarkeit und Drop-Ziele.

### 4.4 Die gezogene Karte ist nicht zwingend der serverseitige Angreifer

Feldkarten werden grundsätzlich mit `draggable=1` erzeugt, auch gegnerische. Beim Kreatur-auf-Kreatur-Drop wird für den Live-Angriff im Wesentlichen die Ziel-ID vorgemerkt; `AttackCreatureReq` enthält keine Quell-ID. Der Server benutzt seinen aktuellen Angreifer. Dadurch kann die visuelle Geste etwas anderes ausdrücken als das Kommando — oder erst serverseitig abgelehnt werden.

Belege: [Feldkartenaufbau](../../../9rooms_v2/client/board_base.aiml#L244), [Drop-Regeln](../../../9rooms_v2/client/board_drag_controller.aiml#L305), [Request-Modell](../../../9rooms_v2/sources/models/match_play_reqs.aiml#L43).

Serverautorität ist richtig. Für eindeutige Bedienung sollte trotzdem die beim Greifen gemeinte Quell-ID samt Zustandsrevision überprüfbar sein; erlaubte Gesten und Ablehnungsgründe sollten bereits lokal sichtbar werden.

### 4.5 Zusätzliche Robustheitslücken

Der Prefab-Drag endet bei `btn_released`, nicht generell bei verlorenem Kontakt. Browser-Mouseup ist bereits fensterweit registriert, Touchcancel ebenfalls; das sind vorhandene Verbesserungen. In der untersuchten Input-Runtime fehlt jedoch ein allgemeiner Blur-/Visibility-Reset für gehaltene Tasten und Drags. Fokusverlust bleibt damit ein Prüfpunkt.

Die Board-Logik wertet Bewegungen unter 12 Pixeln als möglichen Pin-Klick statt Drop. Diese Entscheidung erfolgt nach dem Start des Drags und ist nicht ausdrücklich auf Touch beschränkt. Kurze Mausgesten können deshalb als Ability-Pinning erscheinen. Der Schwellwert verwendet Bildschirmkoordinaten, während Teile des Layouts Designkoordinaten verwenden.

Trade-off: Eine gemeinsame Gestenmaschine mit Press-Ursprung, Pointer-ID, Capture, Schwelle und explizitem Cancel benötigt mehr Zustände. Sie macht Aufnahme, Bewegung, Drop, Tap und Abbruch dafür nachvollziehbar und unabhängig von temporären Prefab-Adressen.

## 5. Prioritäten und Architekturentscheidung

| Priorität | Maßnahme für eine spätere Umsetzung | Nutzen | Aufwand / Trade-off |
|---|---|---|---|
| P0 | Aktions-RPCs und Board-Poll nichtblockierend; UI-Pendingzustand | Frames laufen während Netzwerkwartezeit weiter | Explizite Request-Lebenszyklen und Fehleranzeige |
| P0 | Native WS/WSS-Reads und Writes inkrementell, mit Budgets | Ein Peer hält die Session nicht an | Parserzustand, Queues, Limits, Backpressure |
| P1 | Eingabe einmal je Frame; Drag an Press-Ursprung und stabile ID binden | Zuverlässiges Greifen, keine erneute Eingabe bei Rebuild | Zentrale Gesten-/Event-Verarbeitung |
| P1 | Aktionsantwort nach sämtlicher Folgearbeit, gefiltert und versioniert | Weniger sequenzielle Roundtrips | Einheitlicher Response-Vertrag |
| P1 | Browser-Frameplanung nach Entkopplung der RPCs überarbeiten | Gleichmäßigerer Takt | Asyncify-Regressionsprüfung |
| P1 | Karten/Board inkrementell aktualisieren; Artwork asynchron | Weniger CPU-Spitzen und Handle-Wechsel | Gezielte Invalidierung und Cache-Lebenszyklus |
| P2 | Push/Revisionen/Deltas plus Snapshot-Fallback | Frischer Zustand, weniger wiederholte Daten | Reconnect und Lückenbehandlung |
| P2 | Renderer-Batching, Dirty-Sort und Layout-Caches nach Profiling | Niedrigere Grundkosten | Komplexere Renderverwaltung |

WebSocket/WSS ist für ein rundenbasiertes Spiel grundsätzlich eine passende Grundlage. Ein Wechsel zu UDP, WebRTC oder HTTP würde die synchron wartende App und die vollständigen Rebuilds nicht beseitigen. Zunächst sind Kontrollfluss, Zustandsvertrag und Eingabeverarbeitung entscheidend.

Vollständige optimistische Spielsimulation ist dafür nicht erforderlich: Lokales Greifen, Hover, Pending-Indikator und eine Vorschau können sofort reagieren, während der Server den Zug entscheidet. Bei komplexen Effekten und verdeckten Informationen wäre vollständige Vorhersage deutlich aufwendiger und müsste sichtbare Rollbacks beherrschen.

Bei asynchroner Umstellung muss ein Timeout als „Ausgang unklar“ behandelt werden. Transport-Request-IDs ersetzen keine fachliche Idempotenz. `ConfirmPhaseReq` enthält beispielsweise keine erwartete Phase/Runde; ein unbedachter Retry kann auf einen inzwischen fortgeschrittenen Zustand treffen. Aktions-ID und erwartete Revision sollten deshalb Teil des Entwurfs sein.

## 6. Was vor einer Umsetzung gemessen werden sollte

Es wurden keine Benchmarks oder Live-Spiele gestartet. Für die Zuordnung der tatsächlichen Kosten wäre ein gemeinsamer Trace über folgende Zeitpunkte ausreichend:

- Physisches Press/Release → Eingabesnapshot → Drag-Kandidat → sichtbarer Drag.
- Klick/Drop → Request gesendet → Server empfangen → Handler/KI fertig → Antwort empfangen.
- Decode → MockState-Konvertierung → Board-Abgleich/Rebuild → Draw → nächster präsentierter Frame.
- Je Aktion: Zahl der RPCs, Antwortbytes, erstellte/zerstörte Prefabs, Draw Calls und Artwork-Cache-Misses.

Wichtiger als durchschnittliche FPS sind Roh-Framezeiten und lange Ausreißer. Die Engine begrenzt `dt` vor der FPS-Mittelung auf 100 ms; dieser bestehende FPS-Wert bildet lange Hänger daher nicht vollständig ab ([Engine-Timing](../../src/sys/engine_runtime.cpp#L373)).

Gezielte Szenarien: Phasencheck und Angriff bei 0/50/150 ms Zusatzlatenz; schneller vollständiger Drag während einer RPC-Wartezeit; Wechsel von `combat_pending`; langes Festhalten über einen Poll-Termin; Fokusverlust; Drop unter einem Overlay; kalter und warmer Artwork-Cache; zwei Clients mit einem absichtlich verzögerten WS-Teilframe. Letzteres gehört in eine isolierte Testumgebung.

Als Erfolgskriterien sollten getrennt gelten: sofortiges lokales Eingabefeedback, weiterlaufende Frames während Requests, eindeutig sichtbare Pending-/Fehlerzustände und keine verlorene oder doppelt ausgeführte Geste. Die serverseitige Zugbestätigung darf weiterhin Netzlatenz benötigen.

## 7. Konkrete Fix-Möglichkeiten

Die folgenden Vorschläge sind Entwürfe für eine spätere Implementierung. Sie wurden nicht umgesetzt oder getestet. „Klein“, „mittel“ und „groß“ bezeichnen den relativen Eingriff in die Architektur, keine belastbare Aufwandsschätzung. Jeder Fix nennt eine begrenzte Einstiegslösung und, wo sinnvoll, einen weitergehenden Ausbau.

### F1 — Aktions-RPCs aus dem Frame-Wartepfad entfernen

**Betrifft:** `client/main_menu_app.aiml`, `client/net/match_rpc.aiml`; Befunde 1 und 2.1. **Eingriff: mittel.**

Für `confirm_phase`, `attack_creature` und `attack_soulmage` zuerst asynchrone Send-Wrapper nach dem vorhandenen Muster `net_get_match_state_send` ergänzen. Die App speichert Request-ID, Aktion, Match-ID, Startzeit und den lokalen Kontext. Pro Frame prüft sie mit `take_response`, ob ein Ergebnis vorliegt, und kehrt sofort zurück, solange nichts angekommen ist.

Der Aktionsablauf erhält explizite Zustände: bereit → ausstehend → Zustandsabgleich → fertig beziehungsweise Fehler/Ausgang unklar. Als erste, überschaubare Variante bleibt höchstens eine zustandsändernde Aktion pro Match gleichzeitig ausstehend. Hover, Vorschau und unabhängige UI-Bedienung bleiben verfügbar. Abhängige Spielaktionen werden sichtbar gesperrt. Eine zweite asynchrone Zustandsabfrage darf zunächst bleiben; F3 beseitigt sie später.

Beim Phasencheck wird unmittelbar „Bestätigung wird gesendet“ dargestellt. Der echte Server-Ack bleibt davon getrennt. Ablehnung und Verbindungsfehler werden am Board angezeigt. Ein lokales Pending-Symbol darf nicht als bereits erfolgter Phasenwechsel erscheinen.

**Wichtig:** Die Request-Zuordnung muss auch beim Verlassen des Matches oder bei Reconnect auslaufen können. Dafür eine lokale Sitzungs-/Ansichtsgeneration mitspeichern und Antworten alter Generationen nicht mehr auf die neue Ansicht anwenden. Ein Timeout darf keinen automatischen erneuten Spielzug auslösen; bis F3 existiert, erst den Zustand abgleichen beziehungsweise die Unsicherheit anzeigen.

**Prüfung:** Bei 150 ms Zusatzlatenz und bei ausbleibender Antwort laufen Hover und Animation weiter. Doppelklick sendet keine zweite identische Aktion. Eine Antwort für ein verlassenes Match verändert das aktuelle Board nicht. Fehler beenden den Pendingzustand nachvollziehbar.

### F2 — Board-Polling zeitbasiert und unabhängig vom Drag machen

**Betrifft:** `poll_board_state`, `sync_board_poll`; Befund 2.2. **Eingriff: mittel.**

Die Framezähler durch monotone Zeitpunkte ersetzen. Einen Poll senden, wenn das Intervall abgelaufen ist und noch keiner aussteht; Antworten in späteren Frames übernehmen. Keine nachträgliche Serie „verpasster“ Polls senden. Das konkrete Intervall anhand der gemessenen Last und gewünschten Reaktionszeit wählen.

Netzempfang und visuelle Aktualisierung trennen: Auch während eines Drags darf eine Antwort eingehen. Solange F6 noch nicht implementiert ist, kann der neueste Snapshot zwischengespeichert und der destruktive Board-Rebuild bis zum Gestenende verschoben werden. Entfernt der neue Zustand die gezogene Karte oder beendet die zulässige Interaktion, die Geste explizit abbrechen und den Snapshot anwenden. Dadurch wird ein langes Festhalten nicht zur dauerhaften Zustandsblockade.

Ohne Serverrevisionen sollte ein alter, bereits laufender Poll nicht nach einer erfolgreichen Mutation deren neueren Zustand überschreiben: eine lokale Anforderungsgeneration nutzen und anschließend eine frische Abfrage starten. Mit F3 entscheidet die Serverrevision über die Reihenfolge.

**Trade-off:** Das Zwischenspeichern ist eine Übergangslösung und kann Darstellung kurz verzögern. Mit stabilen Board-Objekten lassen sich Updates feiner übernehmen. Ein kürzeres Pollintervall allein verschärft dagegen Last und bisherige Frame-Pausen.

**Prüfung:** Vergleichbare Aktualisierungsintervalle bei 30 und 60 FPS; höchstens ein laufender Poll; Gegnerzustand wird trotz wiederholter Klicks empfangen; kein Zugriff auf zerstörte Drag-Objekte.

### F3 — Eindeutiger, versionierter Aktionsvertrag

**Betrifft:** Request-/Response-Modelle, Match-Handler, Match-Service und Client; Befunde 2.3 und 4.4. **Eingriff: mittel bis groß.**

Eine Aktion erhält eine fachliche `action_id` und einen erwarteten Spielkontext. Für den Phasencheck sind Phase und Runde besonders wichtig; ein Angriff trägt zusätzlich die tatsächlich gewählte `source_iid`. Der Server validiert diese Quelle gegen seinen aktuellen Angreifer. Bei einer Abweichung lehnt er nachvollziehbar ab, statt die Geste still auf eine andere Kreatur zu beziehen.

Ein monotoner Match-Revisionswert wird bei relevanten Zustandsänderungen fortgeschrieben, einschließlich Änderungen durch Timer und KI. Die Antwort enthält Aktionsergebnis und Revision. Für einen einfacheren ersten Vertrag darf eine Änderung der erwarteten Revision zur Ablehnung mit frischem Zustand führen. Später können aktionsspezifische Vorbedingungen unnötige Ablehnungen bei unabhängigen Änderungen vermeiden.

Nach der zum Request gehörenden Folgearbeit wird der endgültige Zustand neu gelesen und über eine gemeinsame, **rein lesende**, spielerbezogene Projektion ausgegeben. Diese Projektion sollte aus dem bestehenden `get`-Pfad herausgelöst werden: `get` unverändert aufzurufen könnte erneut Timer-/KI-Arbeit auslösen. Die Antwort muss dieselbe Sichtbarkeitsfilterung und benötigte Kartendaten wie der bisherige Snapshot besitzen. Erst danach kann der Client den zusätzlichen `get_match_state`-Roundtrip streichen.

Für Retries speichert der Server das Ergebnis einer `action_id`, gebunden an Spieler und Match. Mutation und Deduplizierungsentscheidung müssen zusammen konsistent sein. Soll der Schutz auch Serverneustarts überstehen, reicht ein flüchtiger Cache nicht. Aufbewahrungsdauer, Speicherlimit und Verhalten nach Ablauf explizit festlegen.

**Prüfung:** Dieselbe Aktion zweimal übertragen, Antwort verlieren und erneut anfordern, Antworten vertauscht liefern. Der Zug wird einmal ausgeführt; die Ansicht fällt nicht auf eine ältere Revision zurück. Ein Phasencheck aus Runde N bestätigt nicht Runde N+1. Ein Angriff mit falscher Quell-ID wird abgelehnt. Verdeckte Karten bleiben in allen Antwortpfaden verdeckt.

### F4 — Native WebSocket-Pumpe tatsächlich nichtblockierend machen

**Betrifft:** `src/sys/ws_runtime.cpp`, `src/sys/mirror_runtime.cpp`; Befund 2.4. **Eingriff: groß, innerhalb der Engine abgrenzbar.**

Für jede WS-Verbindung Empfangspuffer und Parserfortschritt halten: Header, erweiterte Länge, Maskierung und Payload. Nonblocking-Sockets verwenden; Lesen endet bei `EAGAIN`/`EWOULDBLOCK` und wird beim nächsten Pump-Aufruf fortgesetzt. Eine Nachricht wird erst nach vollständigem Empfang an Mirror übergeben. Ping/Pong, Close und gegebenenfalls fragmentierte Nachrichten müssen denselben inkrementellen Lebenszyklus verwenden.

Ausgehende Frames in eine begrenzte Queue schreiben und Teilübertragungen mit Offset fortsetzen. Bei TLS `SSL_ERROR_WANT_READ` und `SSL_ERROR_WANT_WRITE` entsprechend behandeln, einschließlich des jeweils benötigten Readiness-Interesses. Nur `O_NONBLOCK` einzuschalten wäre kein vollständiger Fix: Der bisherige vollständige Lese-/Schreibvertrag könnte Teilergebnisse als Fehler behandeln.

Pro Verbindung und Pump-Durchlauf ein Byte-/Zeitbudget vorsehen. Zusätzlich maximale Nachrichtengröße, Queuegrößen und eine monotone Gesamtfrist für unvollständige Nachrichten definieren. Eine volle Queue muss als Backpressure oder klarer Verbindungsfehler sichtbar werden; sie darf nicht unbegrenzt wachsen oder den Frame warten lassen.

**API-Option:** Eine neue Try-Receive-/Pump-Schnittstelle für Mirror einführen und bestehende blockierende WS-Funktionen vorerst als ausdrücklich blockierende Komfortfunktionen behalten. Das begrenzt Änderungen an anderen AIML-Anwendungen. Mirror selbst darf die blockierenden Funktionen dann nicht mehr nutzen.

**Prüfung:** Header und Payload byteweise verzögern; ein Peer liest nicht mehr; TLS kann vorübergehend nicht schreiben; nur Ping/Pong trifft ein; Gegenstelle trennt mitten im Frame. Andere Clients bleiben bedienbar, Speicher bleibt begrenzt und keine Nachricht geht durch einen Teilwrite verloren.

### F5 — Eine Geste besitzt Ursprung, Zielkontext und genau ein Ende

**Betrifft:** `input_runtime.cpp`, `prefab_runtime.cpp`, `board_drag_controller.aiml`; Befunde 3.3 und 4.1–4.5. **Eingriff: mittel bis groß.**

Die Eingabe als geordnete Ereignisse mit Pointer-ID, Position und Zeitpunkt aufnehmen. Ein unveränderlicher Eingabesatz wird am Frameanfang konsumiert. Für Positionsänderungen darf zusammengefasst werden; Press, Release und Cancel müssen geordnet erhalten bleiben. Wenn eine komplette kurze Geste zwischen zwei Frames liegt, wird sie dadurch noch korrekt ausgewertet.

Ein Press führt zu einem Kandidaten anhand der **Press-Position** und der zu diesem Zeitpunkt sichtbaren Anordnung. Nach Überschreiten einer definierten Bewegungsschwelle wird daraus ein Drag. Die Geste besitzt die stabile Karten-ID, den anfänglichen Spielkontext und einen optionalen Render-Handle. Release führt genau einmal zu Tap oder Drop; Fokusverlust, Pointer-Cancel, Kartenentfernung und Szenenwechsel führen zu Cancel. Ein Cancel erzeugt keinen Spielzug.

Maus und Touch dürfen unterschiedliche Schwellen erhalten, sollten aber dieselbe Zustandsmaschine verwenden. Für den Browser eine einheitliche Pointer-Event-Anbindung mit Capture prüfen; bestehende Maus-/Touch-Callbacks dabei nicht parallel doppelt auslösen lassen. Schwellen in einer ausdrücklich definierten UI-Einheit messen und Geräte-/Canvas-Skalierung berücksichtigen.

`BoardScreen.render` in Eingabeverarbeitung, visuelle Aktualisierung und Darstellung aufteilen. Ein zusätzlicher Rebuild oder Draw verarbeitet keine Eingabe erneut. Als Übergang kann ein Frame-/Event-Verbrauchsmarker die erneute Verarbeitung verhindern; er ersetzt die Trennung langfristig nicht.

**Board-Regeln:** Vor Dragbeginn klären, welche Geste erlaubt ist: Angriff, Bewegung, Ausrüstung oder reine Vorschau. Gegnerkarten können weiterhin betrachtet werden, müssen deshalb aber nicht als eigene Angreifer ziehbar sein. Bei `combat_pending` den konkreten Sperrgrund anzeigen und erlaubte Reaktionsklicks getrennt weiterführen.

**Prüfung:** Schneller Drag während einer simulierten Frame-Pause; gehaltene Taste fährt erst später über eine Karte; Release außerhalb des Canvas; Fokusverlust; Szenenwechsel; erneutes Rendern im selben Frame. Pro physischer Geste entsteht höchstens ein Intent und nach jedem Abbruch ist die nächste Geste möglich.

### F6 — Board-Objekte anhand ihrer IDs erhalten

**Betrifft:** `board_screen.aiml`, `board_base.aiml`, `board_helpers.aiml`; Befund 3.2. **Eingriff: mittel bis groß.**

Eine Zuordnung von `iid` zu Kartenansicht und zugehörigen Badges führen. Bei einem Snapshot zunächst Änderungen ermitteln: neue/entfernte Karten, Zonenwechsel und geänderte Eigenschaften. Neue Karten erzeugen, entfernte kontrolliert abbauen, bestehende aktualisieren. Phasenpanel, Hintergrund und unveränderte Zonen bleiben bestehen.

Für eine begrenzte erste Umsetzung nur Stats, Tap, Angreifer und Zielrahmen direkt aktualisieren; strukturelle Änderungen dürfen noch zonenweise neu aufbauen. Später Karten bei Zonenwechsel umpositionieren beziehungsweise umhängen. Einen aktiven Drag als separate visuelle Vorschau behandeln, damit die autoritative Kartenposition aktualisiert werden kann, ohne die Geste an ein zerstörbares Prefab zu binden.

Die heutige Stringsignatur kann vorübergehend als Rückfallebene dienen. Langfristig tatsächliche Felddifferenzen oder lokale Dirty-Flags verwenden. Eine Matchrevision sagt, **dass** etwas geändert wurde; sie ersetzt nicht die Entscheidung, **welche** Ansicht betroffen ist.

**Prüfung:** Eine reine Statänderung erzeugt keine neuen Karten-Prefabs. Hover und Vorschau bleiben bei unveränderten Karten erhalten. Tod, Ausstattung, Besitzer- und Zonenwechsel werden korrekt abgebildet. Bestehende Todes-/Schadensanzeigen laufen weiterhin genau einmal.

### F7 — Gemeinsame Trefferprüfung mit expliziten Eingaberegeln

**Betrifft:** `prefab_runtime.cpp` und Board-Drop-Auswertung; Befund 4.3. **Eingriff: mittel.**

Eine gemeinsame Picking-Grundlage für Klick, Dragstart und Drop verwenden: gleiche Welttransformation, Sichtbarkeit über die Elternkette, Clipping und aktuelle Z-Reihenfolge. Für rotierte Elemente den Punkt in lokale Koordinaten transformieren. Die Sortierung muss vor der Eingabe korrekt sein; ein Dirty-Flag bei Z-Änderung genügt, sofern alle Änderungen diesen Pfad benutzen.

Unabhängige Eigenschaften für „blockiert darunterliegende Eingabe“, „empfängt selbst Eingabe“, „draggbar“ und „akzeptiert diesen Drop-Typ“ vorsehen. Dekoration kann explizit durchlässig sein; ein modales Overlay blockiert auch dann, wenn es nicht draggbar ist. Beim Drop die eigene Drag-Vorschau aus der Kandidatenmenge ausschließen, statt sie vorübergehend unsichtbar zu schalten.

**Trade-off:** Bestehende Prefabs benötigen kompatible Standardwerte. Neue Regeln zunächst für das Board aktivieren oder vorhandene Oberflächen auf Durchklickverhalten prüfen; ein global veränderter Default kann andere Apps beeinträchtigen.

**Prüfung:** Überlappende Karten, dekorativer Rahmen, modales Overlay, verschachteltes/gescrolltes Element, Rotation und Z-Wechsel im selben Frame. Das visuell erwartete zulässige Ziel wird gewählt, gesperrte Ebenen werden nicht durchklickt.

### F8 — Browser-Takt ohne zusätzlichen Vollframe-Sleep

**Betrifft:** `engine_runtime.cpp`; Befund 3.1. **Eingriff: klein als Zwischenlösung, mittel beim Loop-Wechsel.**

Als begrenzte Zwischenlösung kann die manuelle Schleife nur das verbleibende Framebudget schlafen, gemessen mit monotoner Zeit. Bei Überlast bleibt ein expliziter kooperativer Yield nötig; ein enges Nachholen mehrerer Frames wäre kontraproduktiv. Das verringert den additiven 16-ms-Effekt, koppelt die Präsentation aber noch nicht an den Bildschirmtakt.

Die nachhaltige Variante verwendet browsergesteuerte Frames, nachdem alle darin erreichbaren suspendierenden Aufrufe einschließlich Artwork-Laden entfernt oder sicher ausgelagert sind. Netzwerk- und Assetpumpen liefern dann nur fertige Ergebnisse. Die in der Engine dokumentierte Asyncify-/Main-loop-Regression muss mit einem gezielten Minimalfall erneut abgesichert werden.

Roh-Wandzeit für Telemetrie und begrenztes Simulations-`dt` getrennt halten. Ein Hintergrundtab erhält eine definierte Pause-/Resume-Behandlung und holt keine unbeschränkte Zahl alter Simulationstakte nach.

**Prüfung:** Framezeitverteilung bei leerer und voller Szene, wiederholte WS-Sends, langsame Antworten, Tabwechsel und Größenänderung. Die Loop darf weder aussetzen noch nach einer Pause in eine Aufholschleife geraten.

### F9 — Artwork als Hintergrundauftrag mit Platzhalter

**Betrifft:** `card_art_cache.aiml`, `board_widgets.aiml`, `card_full_populator.aiml`; Befund 3.5. **Eingriff: mittel.**

`art_ensure` im UI-Pfad soll künftig den Cache prüfen und bei Bedarf einen Auftrag vormerken; Kartenaufbau liefert unmittelbar einen Platzhalter. Download, Decode und GPU-Upload erhalten getrennte Zustände und begrenzte Arbeit pro Frame. Fertige Texturen werden über den Artwork-Hash an weiterhin vorhandene Ansichten gebunden.

Mehrere Anfragen desselben Hashs zusammenfassen, sichtbare Karten priorisieren und Fehler mit begrenztem Retry/Backoff behandeln. Negative Cacheeinträge verhindern Wiederholung pro Frame. Ein GPU-Speicherbudget mit Referenzierung beziehungsweise LRU verhindert unbegrenztes Wachstum; sichtbare Texturen dürfen dabei nicht unkontrolliert verdrängt werden.

**Besonderheit des bestehenden Medienprotokolls:** Solange dessen Chunk-Antworten keine eigene Request-Zuordnung haben, auf derselben Verbindung höchstens einen Transfer gleichzeitig bearbeiten. Asynchronität benötigt keine parallelen Transfers. Bei einem nicht mehr benötigten Bild den laufenden Transfer sauber beenden oder die Verbindung kontrolliert zurücksetzen, damit folgende Antworten zuordenbar bleiben.

**Prüfung:** Kalter Cache mit vielen Karten, langsamer/ausgefallener Medienserver und Szenenwechsel während eines Downloads. Das Board bleibt bedienbar, identische Bilder werden einmal geladen, und fertig geladene Bilder erscheinen nur an noch passenden Ansichten.

### F10 — CPU-/GPU-Kosten und Serverarbeit gezielt begrenzen

**Betrifft:** Prefab-Renderer, Match-Handler und Scheduler; Befunde 2.4 und 3.4. **Eingriff: abhängig vom Messergebnis.**

Im Renderer zunächst Draw Calls, Uploadbytes, Sortierzeit, Layout-/Textzeit und Allokationen erfassen. Danach Uniform-Locations pro Shader cachen, Sortierung nur bei Änderungen ausführen und unveränderte Text-/Layoutdaten wiederverwenden. Benachbarte kompatible Batches dürfen zusammengelegt werden, solange Zeichenreihenfolge, Clipping und Materialzustand erhalten bleiben. Ein globales Sortieren nach Textur würde transparente Überlagerungen verändern und ist deshalb kein sicherer pauschaler Fix.

Wenn serverseitige KI-/Regelarbeit dominiert, diese in begrenzte Arbeitsschritte pro Match zerlegen. Der Server kann nach einer atomaren Entscheidung einen expliziten „Auflösung läuft“-Zustand melden und Folgearbeit später fortsetzen. Dabei müssen Spielregeln bestimmen, an welchen Grenzen ein Zustand sichtbar oder wieder bedienbar sein darf. Ein vorzeitiges Erfolgs-Response bei noch inkonsistenter Mutation wäre falsch.

Worker sind eine Alternative für tatsächlich isolierbare, CPU-intensive Berechnungen. Ergebnisse nur gegen die noch gültige Matchrevision übernehmen. Gemeinsame KV-/Matchobjekte ohne geklärte Besitz- und Synchronisationsregeln in Threads zu verschieben wäre kein belastbarer Fix.

**Prüfung:** Gleicher Spielausgang vor und nach Zerlegung, keine konkurrierenden Mutationen desselben Matches, begrenzte Wartezeit anderer Clients. Renderer-Änderungen zusätzlich mit überlappenden transparenten Karten und Clipping visuell vergleichen.

## 8. Empfohlene Umsetzung in überprüfbaren Etappen

1. **Messbasis und unmittelbare Rückmeldung:** Die Tracepunkte aus Abschnitt 6 sowie explizite Pending-/Fehleranzeige vorsehen. Ausgangswerte für Phasencheck, Angriff und schnellen Drag erfassen.
2. **Warten entkoppeln:** F1 und F2 zuerst für die genannten Board-Aktionen umsetzen. F4 separat auf Engine-Ebene bearbeiten; sonst bleibt auch eine asynchrone App gegenüber blockierenden nativen Pump-Aufrufen empfindlich.
3. **Gesten stabilisieren:** F5 und F7; anschließend F6 zur Erhaltung der Kartenansichten. Dadurch benötigt die Interaktion immer weniger Ausnahmen für Reloads.
4. **Roundtrips und kalte Ladepausen reduzieren:** F3 und F9. Die bisherige zweite Zustandsabfrage erst entfernen, wenn der neue Antwortvertrag endgültige, gefilterte Zustände garantiert.
5. **Frameplanung und gemessene Restkosten:** F8 nach Prüfung aller suspendierenden Frame-Pfade; F10 anhand verbleibender Engpässe. Push/Deltas erst dann ergänzen, wenn Datenmenge oder Aktualisierungslatenz es rechtfertigen.

Für jede Etappe gelten die Szenarien aus Abschnitt 6 und die jeweilige Prüfung unter F1–F10. Ein gutes Zwischenziel ist bereits erreicht, wenn die Spielbestätigung noch dieselbe Netzzeit benötigt, die Oberfläche dabei aber weiter reagiert und jede Geste eindeutig angenommen, abgelehnt oder abgebrochen wird. Eine vollständige Neuschreibung von Engine oder Netzwerkstack ist dafür keine Voraussetzung.

## 9. Sollte AIML Netzwerk und Rendering stärker auf Engine-Ebene verbinden?

**Ja: Eine gemeinsame Ablaufsteuerung in der Engine ist sinnvoll.** Besonders wertvoll wären verbindliche Regeln dafür, wann Netzwerkfortschritt stattfindet, wann fertige Antworten angewendet werden und wann Eingabe, Layout und Darstellung laufen. Die Engine sollte dabei weiter Frames liefern können, solange Antworten ausstehen. Eine engere Verbindung darf deshalb keine zusätzliche Abhängigkeit der Bildausgabe vom Eingang einer Nachricht schaffen.

Die folgenden API- und Architekturideen sind Entwurfsvorschläge, keine bereits vorhandenen oder überprüften AIML-Funktionen. Sie bauen auf den untersuchten Runtime-Pfaden auf. Ihre vollständige sprachliche und compilerseitige Integration müsste gesondert entworfen werden.

### 9.1 Was die heutige Trennung die Anwendung selbst lösen lässt

Die Bausteine existieren bereits: Die Engine besitzt den Frameablauf, Mirror besitzt Sessions und korrelierte Requests, Prefabs besitzen Szene und Eingabeinteraktion. In 9rooms verbindet jedoch `MainMenuApp.frame` diese Bausteine von Hand:

- `net_pump` muss rechtzeitig und regelmäßig aufgerufen werden.
- Jeder Screen organisiert selbst Requests, Wartezeiten und Antwortübernahme.
- Das Board verschiebt Polls, damit ein Reload keinen Drag zerstört.
- `board.render` wertet Eingaben aus und kann nach einer Antwort erneut aufgerufen werden.
- Clear und Draw wurden an das Frame-Ende verschoben, damit suspendierende Netzwerkaufrufe kein leeres Zwischenbild zeigen.

Diese Verbindungen sind teilweise notwendige Anwendungspolitik, teilweise wiederkehrende Engine-Infrastruktur. Die wiederkehrenden Teile zentral zu übernehmen würde die Zahl der Sonderfälle in neuen Spielen und Screens senken. Allein die Existenz von `send_request` reicht dafür nicht: Ein einfach verwendbarer Lebenszyklus für dessen Ergebnis fehlt auf Anwendungsebene weiterhin.

### 9.2 Ein geeigneter gemeinsamer Framevertrag

Ein möglicher Ablauf wäre:

1. **Eingabe übernehmen:** Ereignisse bis zur Framegrenze in einen unveränderlichen Satz übernehmen. Pointer-Gesten behalten ihre Identität über mehrere Frames.
2. **Netzwerk fortschreiben:** Empfang, Versand und Timeouts innerhalb eines begrenzten Budgets bearbeiten. Vollständige Antworten und Verbindungsereignisse nur bereitstellen.
3. **Ergebnisse übernehmen:** Passende Antworten an einer definierten Stelle auf den Spielzustand anwenden. Dabei Sitzung, Match, Revision und Lebensdauer prüfen; ungültig gewordene Gesten abbrechen.
4. **Eingaben und Spiel-Update ausführen:** Jede Eingabe genau einmal auswerten. Die zuvor sichtbare Karten-ID bleibt bei einer Geste erhalten; die Aktion wird gegen den aktuellen Zustand validiert. Neue Requests werden lediglich vorgemerkt.
5. **Darstellung abgleichen:** Geänderte Karten, Overlays und Layouts aktualisieren. Assets dürfen Aufträge auslösen, aber nicht auf deren Fertigstellung warten.
6. **Zeichnen und präsentieren:** Einen konsistenten Zustand darstellen. Ausgehende Requests können vorher noch budgetiert an den Transport übergeben werden, ohne auf die Antwort zu warten.

Die genaue Reihenfolge von Netzwerkübernahme und Spiel-Update ist eine Designentscheidung. Wesentlich ist, dass sie dokumentiert und stabil ist. Netzwerk-Callbacks dürfen nicht beliebig mitten in `render_instance` die Szene verändern. Ebenso darf eine neue Antwort keine bereits eingelesene Press-Position nachträglich auf eine andere Karte umdeuten.

**Nutzen:** Ein eintreffender Zustand besitzt einen klaren Übergabepunkt. Rendern, Input und Netzwerkzugriff müssen nicht mehr über verstreute Cooldowns und Latches gegeneinander abgesichert werden. Das hilft auch beim Nachstellen von Fehlern: Der Trace kann zeigen, welches Ereignis an welcher Framegrenze übernommen wurde.

**Grenze:** Ein Budget wirkt nur, wenn die darunterliegenden Operationen nichtblockierend oder unterbrechbar sind. Ein blockierendes `recv` wird durch die Einordnung in „Netzwerkphase mit 2-ms-Budget“ nicht plötzlich zeitlich begrenzt. F4 bleibt eine Voraussetzung. Auch das Deserialisieren einer einzelnen großen Nachricht kann das Budget überschreiten; Nachrichtenlimits und Messung bleiben nötig.

### 9.3 Welche Funktionen in die Engine gehören

| Vorgeschlagene Fähigkeit | Zuständigkeit der Engine | Zuständigkeit der Anwendung |
|---|---|---|
| Session an einen Laufkontext binden | Regelmäßiges Pumpen, klarer Besitzer, Deregistrierung | Welche Session zu welchem Spiel gehört |
| Typisierte asynchrone Anfrage | Request-ID, Timeout, Decode, genau eine Fertigmeldung | Fachliche Bedeutung von Erfolg und Ablehnung |
| Ergebniswarteschlange | Übernahme an einer definierten Framegrenze | Änderung des Match-/UI-Zustands |
| Lebensdauerbereich für Requests | Geschlossene Bereiche liefern keine UI-Callbacks mehr | Bereich an Screen, Match oder Sitzung binden |
| Pendingzustand | Abfragbarer Status und Benachrichtigung | Anzeige, Text und welche Aktionen gesperrt sind |
| Darstellungsinvalidierung | Geänderte Ansichten gezielt aktualisieren | Zuordnung eines Matchfeldes zu einer Kartenansicht |
| Netzwerk-/Frame-Telemetrie | Einheitliche Zeitpunkte und Korrelations-IDs | Fachliche Aktionsnamen und relevante Messszenarien |

Als denkbare API könnte ein Laufkontext eine Session registrieren und ein Request-Handle mit Zuständen wie ausstehend, erfolgreich, abgelehnt auf Transportebene, Timeout oder geschlossen liefern. Der Rückgabewert einer typisierten Spielantwort bleibt davon getrennt: Eine korrekt empfangene Antwort kann eine fachliche Ablehnung enthalten. Diese Trennung verhindert, dass „Nachricht angekommen“ mit „Angriff erlaubt“ verwechselt wird.

Callbacks oder fortsetzbare Aufgaben müssten an der definierten Übernahmestelle laufen und innerhalb des Framebudgets bleiben. Eine einfache Poll-API für Handles wäre als erster Schritt ausreichend und könnte auf der vorhandenen Request-ID-Technik aufbauen. Sie vermeidet zunächst die zusätzliche Komplexität einer neuen Coroutine-Sprachfunktion.

Beim Schließen eines Screens wird dessen Ergebniszustellung abgemeldet. Das bedeutet **nicht**, dass ein bereits gesendeter Spielzug auf dem Server zurückgenommen wurde. Engine-Cancel und fachliches Rückgängigmachen sind verschiedene Operationen; diese Semantik muss im API-Vertrag ausdrücklich stehen.

### 9.4 Welche Verbindung zu eng wäre

Direkte Netzwerk-RPCs aus Prefab-Renderfunktionen würden die jetzigen Probleme weiter verbreiten. Eine Kartenansicht sollte ihre darzustellenden Daten erhalten und Benutzerabsichten melden. Tokenauflösung, KI-Arbeit oder ein serverseitiger Zug gehören nicht in das Zeichnen eines Widgets.

Auch eine automatische Spiegelung beliebiger Serverobjekte direkt in Prefabs wäre für 9rooms zu weitgehend. Spielerspezifisch verdeckte Informationen, stabile Kartenidentität, Animationen und lokale Vorschau verlangen eine bewusste Projektion. Die Engine kann bei Änderungsbenachrichtigung und Zuordnung helfen; die Anwendung muss entscheiden, welche Daten sichtbar werden und wie eine Änderung dargestellt wird.

Für die erste Integration sollten daher **explizite Zustandsübergaben** erhalten bleiben: Antwort → validierter Spielzustand → Ansichtsänderung. Eine spätere deklarative Datenbindung kann diese Schritte verkürzen, sollte aber Debugging, Kosten und Invalidierung nachvollziehbar lassen.

### 9.5 Gemeinsamer Scheduler oder eigener Netzwerkthread?

**Empfohlener Einstieg: ein gemeinsamer, budgetierter Scheduler auf dem bisherigen Spielthread.** Nichtblockierende I/O-Pumpen und Ergebnisübernahme an Framegrenzen benötigen keine parallele Veränderung des Spielzustands. Das passt zur bestehenden nativen und WASM-Struktur und hält Prefab-Zugriffe auf einem Thread.

Vorteile sind einfache Besitzverhältnisse, stabile Reihenfolge und geringerer Synchronisationsaufwand. Nachteil: CPU-intensive Dekodierung oder Spiel-Updates können weiterhin lange Frames verursachen. Deshalb sollten Nachrichtengrößen, Pump-Budgets und Decode-Zeiten sichtbar sein.

Ein eigener Netzwerkthread wäre auf nativen Plattformen eine spätere Option: Er empfängt und verarbeitet Transportdaten, übergibt fertige Ergebnisse über eine begrenzte Queue und verändert selbst keine Szene. Auf Webplattformen müssten Worker, Speicherübergabe und die konkrete Build-Konfiguration eigens berücksichtigt werden. Ein einheitlicher öffentlicher Schedulervertrag kann bestehen bleiben, auch wenn der Transport intern je Plattform unterschiedlich arbeitet.

Multithreading bringt zusätzlich Reihenfolge-, Shutdown- und Speicherbesitzfragen mit sich. Es lohnt sich erst, wenn Messungen zeigen, dass budgetierte Verarbeitung auf einem Thread nicht genügt. Es behebt weder einen zweiten unnötigen Roundtrip noch die fachliche Verwechslung von gezogener und tatsächlich angreifender Kreatur.

### 9.6 Backpressure und Fairness gehören zum gemeinsamen Vertrag

Sobald die Engine Netzwerk und Frames gemeinsam steuert, muss sie Überlast ausdrücklich behandeln. Sonst wird aus blockierendem Warten lediglich eine wachsende Nachrichtenwarteschlange.

- Ausgehende Queues brauchen Grenzen und einen sichtbaren „noch nicht angenommen“- beziehungsweise Fehlerzustand.
- Zustands-Snapshots können gegebenenfalls auf die neueste Revision reduziert werden. Aktionsantworten und einmalige Spielevents dürfen nicht pauschal verworfen werden.
- Aktive Spieleingaben sollten gegenüber Hintergrundlisten und Artwork priorisiert werden, ohne diese dauerhaft zu verdrängen.
- Pro Session und Frame muss genügend Fairness bestehen, damit ein stark sendender Peer die anderen nicht monopolisiert.
- Dekodierung, Zustandsübernahme und Darstellung benötigen jeweils eigene Messwerte. „Netzwerkdauer“ allein verdeckt lokale Arbeit.

Welche Nachrichten zusammengefasst werden dürfen, entscheidet deren fachlicher Vertrag. Das kann die Engine nicht aus beliebigen serialisierten Modellen ableiten. Eine optionale Kennzeichnung als ersetzbarer Snapshot wäre dafür geeigneter als eine automatische Heuristik.

### 9.7 Einstieg ohne Engine-Neuschreibung

Eine begrenzte Erweiterung könnte zunächst aus vier Bausteinen bestehen:

1. **Ein Session-Pump-Vertrag**, der für grafische und headless Anwendungen nutzbar ist und pro Aufruf begrenzten Fortschritt macht. Der Gameserver benötigt dieselbe Transportzuverlässigkeit, aber keine Rendering-Abhängigkeit.
2. **Ein Request-Verwalter** auf Basis von `send_request`/`take_response`, mit monotonichem Timeout, Besitzerbereich und Ergebniswarteschlange.
3. **Explizite Engine-Phasen** für Ergebnisübernahme, Input/Update und Draw; suspendierende Komfort-RPCs werden in Framephasen diagnostiziert. Eine Warnung kann zunächst optional sein, später ein strenger Entwicklungsmodus.
4. **Ein gemeinsamer Trace**, der physische Eingabe, Request, Zustandsrevision und Frame miteinander verbindet.

9rooms kann diese Erweiterung zuerst für Phasencheck und Angriff nutzen. Bestehende synchrone APIs bleiben für Skripte, Werkzeuge und Tests verfügbar; grafische Screens migrieren schrittweise. Nach jedem Schritt sollte geprüft werden, dass sich das Framework auch ohne Game-Loop headless benutzen lässt.

**Abnahmekriterien für diese Engine-Erweiterung:** Ein einfaches Beispiel kann eine Anfrage starten, Pendingstatus zeichnen und weiter bedient werden, ohne selbst eine Warteschleife zu schreiben. Das Schließen seiner Ansicht verhindert spätere Zugriffe auf zerstörte UI-Objekte. Eine langsame Session hält weder andere Sessions noch die Präsentation an. Ein schneller Drag wird auch dann genau einmal verarbeitet, wenn gleichzeitig eine Antwort eintrifft.

### 9.8 Bewertung

Die engere Integration hat für AIML einen hohen Nutzen, weil die bisher sichtbaren Fehler mehrere Schichten gleichzeitig betreffen. Der größte Gewinn liegt in einem **gemeinsamen Lebenszyklus für Frame, Request und Eingabe**. Damit wird der flüssige Ablauf zum vorgesehenen Standard der Engine, statt von der Disziplin jedes Screens abzuhängen.

Die Kosten sind ein größerer öffentlicher API-Vertrag und die Verantwortung für Lebensdauer, Überlast und plattformübergreifende Reihenfolge. Dieser Ausbau sollte deshalb zunächst klein und explizit erfolgen. Er ersetzt die Fixes F1–F9 nicht; er liefert die gemeinsame Infrastruktur, auf der diese Fixes anschließend einfacher und konsistenter umgesetzt werden können.
