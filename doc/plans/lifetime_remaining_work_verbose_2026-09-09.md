# Lifetime: verbleibende Arbeit, Risiken und konkrete Umsetzung

Stand: 2026-09-09. Grundlage: Nutzerentscheidung zu Kapitel 61 und erneute direkte Quelltextlektüre. WP-C ist verworfen. Dieser Plan führt weder dessen Ganzprogramm-Escape-Analyse noch einen Garbage Collector wieder ein.

Status: ausführlicher Arbeitsplan, keine Implementierung und kein Laufzeit-Abnahmebericht. Es wurde kein Git verwendet. Zeilenangaben sind Momentaufnahmen eines parallel bearbeiteten Arbeitsbaums; Funktionsnamen sind die stabilen Anker. Vor jeder Änderung den aktuellen Stand erneut lesen.

## 1. Ziel und verbindliche Grenzen

Heap-tragende Werte sollen deterministisch freigegeben werden, ohne neue Use-after-free-, Double-Free- oder Datenverlustfehler zu erzeugen. Maßgeblich ist `spec/61_lifetime_ownership.md`: lokal bestimmbare Besitzregeln, explizite Funktionsverträge, compilezeitlich eindeutiger Konsum und keine versteckten Runtime-Drop-Flags.

Eine geringere RSS-Steigung allein ist keine Abnahme. Eine Probe muss tatsächlich ihre Arbeit erfolgreich erledigen und unter einem geeigneten Speicherprüfer sauber bleiben. Ein vorzeitig abgebrochener oder fehlkompilierter Lauf darf nicht als behobenes Leck zählen.

Dieser Plan legt konkrete Vorschläge zur Umsetzung vor. Vorschläge, die noch eine Spezifikationsentscheidung brauchen, sind ausdrücklich als solche bezeichnet. Er aktiviert keine weiteren Drops und ändert nicht die Bedeutung von `persistent` eigenmächtig.

## 2. Aktueller Stand und Korrektur früherer Aussagen

| Bereich | Beobachtung beim Anlegen dieses Dokuments | Konsequenz |
|---|---|---|
| Drop-Schalter | `src/core/context.h:317`: `lifetimeDropsEnabled = true` | Die vorhandenen Cleanup-Pfade sind aktiv. Unvollständige Besitzprüfung ist daher laufzeitrelevant. |
| Direkte String-Temporaries an consumed-Parameter | `ast_compiler_call.cpp:420–429` und `:2473–2483` entfernen inzwischen auch den ausgewerteten Temporary via `consumeStringTemp`. | Früheres Finding nicht unverändert als offen führen. Direkte Interpolation und beide Aufrufpfade gezielt verifizieren. |
| Qualifier-Syntax | Kapitel 61 beschreibt den Modifier inzwischen an Position 4 nach dem Initialwert. | Der frühere Spec-/Parser-Widerspruch ist korrigiert. |
| Harness | Native Probe wird separat ausgeführt und auf Fehler geprüft; Valgrind verwendet einen eigenen Fehlercode. | Wesentliche Verbesserung. Vollständigkeit des instrumentierten Laufs bleibt eigenes Abnahmekriterium. |
| Map-Drop | `ast_compiler_stmt.cpp:380`: Registrierung noch ausdrücklich zurückgestellt; Runtime und Emission existieren bereits. | Vorhandene Primitive sind kein Beleg für sichere Aktivierung. |
| WAL-Startfehler | Nicht zugreifbare WALs wurden als konkrete Serverstartblockade beobachtet; das Fehlerverhalten wurde anschließend geändert. | Beweist weder, dass alle Loginprobleme gelöst sind, noch dass Lifetime-Drops insgesamt sicher sind. |

Die Aussage „fehlende Ownership-Präzision verursacht immer nur Leaks“ ist zu stark. Das bloße Weglassen eines Drops kann ein Leck verursachen. Zusammen mit flachen Kopien, aktiven Ziel-Drops und weiter zugelassenen Zugriffen kann daraus jedoch ein UAF werden.

## 3. Prioritäten und Reihenfolge

1. Die bereits vorgenommenen String-Temporary-Korrekturen und die Verlässlichkeit der Harness absichern.
2. Besitzübertragung bei Zuweisungen eindeutig implementieren; lokale Copy-Fälle und Selbstzuweisung sichern.
3. Besitzstatus pro Kontrollflusspfad verfolgen und uneindeutige Merges ablehnen. Use-after-move gehört in dieses Paket.
4. Return- und weitere Scope-verlassende Ausgänge mit pfadkorrektem Cleanup versehen.
5. Box-Drops zusammen mit ausreichenden Borrow-/Funktionsverträgen erweitern.
6. Map-Drops erst nach abgeschlossenem Layout- und Mutationsaudit aktivieren.
7. Jedes Paket isoliert prüfen, danach die relevanten 9rooms-Abläufe gegen einen Server validieren.

Die Arbeit erfordert Kontrollflussanalyse innerhalb einer Funktion. Sie erfordert nicht automatisch eine Ganzprogramm-Escape-Analyse oder einen vollständigen Umbau des Compilers auf SSA. LLVM-SSA für Werte und der Besitzstatus einer Quellvariable sind unterschiedliche Dinge.

## 4. P1b: Zuweisung und Branch-Ownership

### 4.1 Relevante Code-Stellen

- `src/core/ast_compiler_stmt.cpp:826`, `visitSetStmt`: frühzeitiger Aufruf von `consumeLocalDrop` für die RHS.
- `src/core/ast_compiler_stmt.cpp:1419`, `consumeLocalDrop`: entfernt passende Einträge aus den Cleanup-Frames.
- `src/core/ast_compiler_stmt.cpp:1512`, `visitIfStmt`: Verzweigungsaufbau.
- `src/core/ast_compiler_stmt.cpp:1557`, `visitWhileStmt`: Schleifen und Rückkante.
- `src/core/context.h:275–299`: `ScopeDrop`, `cleanupStack`, `pendingParamDrops`.
- Weitere Übertragungsstellen: Listen-Push, Map-Put, Member-/Index-Set, Return sowie freie und direkte Methodenaufrufe in `ast_compiler_call.cpp`.

### 4.2 Konkreter gefährlicher Fall

Schematischer Ablauf, kein bereits ausgeführter Repro:

```text
äußerer Scope:
    x = eigenständiges Modell mit Heap-Feldern
    innerer Scope:
        y = leeres lokales Modell
        y = x
    verwende x
```

Wenn `y = x` nur die Heap-Pointer kopiert, `x` aus seinem Cleanup entfernt wird und `y` beim inneren Scope-Ende freigegeben wird, zeigt `x` danach auf freigegebenen Speicher. Kapitel 61 verlangt für die lokale Zuweisung in einen gleich oder kürzer lebenden Zielbereich eine unabhängige Kopie.

Ein zusätzlicher einfacher Grenzfall ist `set: [x, x]`: Das darf weder den einzigen Cleanup-Eintrag verlieren noch den eigenen Heap vorzeitig freigeben.

### 4.3 Vorschlag für die Besitzrepräsentation

Besitz und ausstehende Freigabe getrennt darstellen. Die Mitgliedschaft in `cleanupStack` allein ist kein ausreichender Besitznachweis.

Pro relevanter Variable einen stabilen Identifikator und mindestens folgende Informationen führen:

- Herkunft: owned, borrowed, static oder noch nicht verlässlich klassifiziert.
- Deklarations-Scope und bekannte Besitzerbindung.
- Qualifier: scope, persistent oder consumed.
- Pfadzustand: verfügbar, bewegt oder noch nicht initialisiert, soweit diese Unterscheidung benötigt wird.
- Drop-Art und Metadaten für Modell, String, Liste, Map oder Box.

Die konkreten C++-Namen sind Implementierungsdetails. Wichtig ist, dass Shadowing nicht zwei Variablen gleichen Namens zusammenfallen lässt und dass ein gelöschter Drop-Eintrag nicht als Ersatz für einen expliziten Moved-Zustand dient.

### 4.4 Zuweisung erst klassifizieren, dann Cleanup verändern

`visitSetStmt` darf den Quell-Drop nicht entfernen, bevor Ziel, RHS-Typ und Übertragungsart feststehen.

Vorgeschlagener Ablauf:

1. Ziel auflösen und Schreibbarkeit prüfen.
2. RHS auswerten. Den bisherigen Zielwert währenddessen gültig halten.
3. Copy oder Move anhand der verbindlichen Spec-Tabelle bestimmen.
4. Selbstzuweisung und Alias-Beziehungen berücksichtigen.
5. Bei Copy eine unabhängige temporäre Kopie herstellen, bevor der alte Ziel-Heap freigegeben wird. Allein „RHS zuerst auswerten“ reicht bei einem Pointer auf den alten Ziel-Heap nicht.
6. Nur bei erfolgreicher Vorbereitung den alten Zielwert freigeben und den neuen übernehmen.
7. Nur bei tatsächlichem Move die Quelle als bewegt markieren und ihre spätere Freigabe auf diesem Pfad unterdrücken.

Falls Deep-Copy fehlschlagen kann, benötigt dieser Ablauf einen auswertbaren Erfolgskontrakt. `aiml_ser_deep_copy` mit `void` genügt nicht für ein verlässlich fehlerfestes Replace. Vorhandene Decoder-/KV-Arbeit getrennt halten, aber diese Schnittstelle berücksichtigen.

`persistent` ist kein allgemeiner Freibrief, Quellen zu teilen. Wenn die Spec eine Copy verlangt, muss tatsächlich eine unabhängige Kopie entstehen oder die Operation für nicht kopierbare Ressourcen abgelehnt werden.

### 4.5 Branch-Zustände und Merge

Vor einer Verzweigung den Besitzstatus als Eingangszustand sichern. THEN und ELSE jeweils vom gleichen Eingangszustand aus analysieren. Danach nur die tatsächlich weiterlaufenden Vorgänger am Merge zusammenführen.

| Zustand an weiterlaufenden Vorgängern | Ergebnis |
|---|---|
| überall verfügbar | verfügbar; späterer Drop bleibt |
| überall bewegt | bewegt; kein späterer Quell-Drop |
| teils verfügbar, teils bewegt | Compile-Fehler nach §6 |
| Vorgänger endet mit Return/Throw | kein normaler Merge-Vorgänger; dort eigenes Exit-Cleanup |

Ein Return-Branch darf den Cleanup des nicht zurückkehrenden Geschwisterpfads nicht aus der gemeinsamen Compiler-Datenstruktur löschen.

Schleifen zusätzlich prüfen: Ein vor der Schleife angelegter Wert darf nicht in der ersten Iteration bewegt und in der zweiten erneut konsumiert werden. Null Iterationen, Rückkante, Break und Neuinitialisierung brauchen definierte Zustandsübergänge. Für den ersten sicheren Implementierungsschritt unklare Muster konservativ ablehnen, statt den Zustand nur nach der textuellen Reihenfolge der Statements zu bestimmen.

### 4.6 Abnahme

- Lokale Kopie bleibt nach Ende des Quell- und des Ziel-Scopes jeweils unabhängig verwendbar.
- Selbstzuweisung erhält Inhalt und genau einen Besitzer.
- Echter Member-/Container-Move überlebt das Ende des Quell-Scopes.
- Bedingter Konsum mit uneindeutigem Merge wird abgelehnt.
- Ein terminierender Branch verändert nicht den Besitzstatus seines weiterlaufenden Geschwisters.
- Schleifen testen: null, eine und mehrere Iterationen; wiederholter Konsum einer äußeren Quelle wird erkannt.
- Gültige Fälle zeigen keine Invalid-Reads, Double-Frees oder proportional wachsende verlorene Allokationen.

## 5. Use-after-move und Warning-first

Use-after-move ist eine Sicherheitsvoraussetzung aktiver Besitzübertragung, nicht nur eine Komfortdiagnose. Nach Freigabe durch den neuen Besitzer ist ein erneuter Zugriff auf die Quelle potenziell UAF.

### 5.1 Benötigte Prüfstellen

- Variablenauflösung und Lesen in `ast_compiler_expr.cpp` sowie Methodenaufruf auf einer Variablen.
- Übergabe als Funktionsargument, Member-/Indexzugriff und erneuter Konsum.
- Reinitialisierung: darf einen zuvor bewegten Slot wieder gültig machen, ohne dessen alten Heap erneut freizugeben.
- Borrow-Aliase bleiben ein separates Problem; bloßes Nullen des Quellslots sichert keine vorher entkommenen Heap-Pointer.

### 5.2 Rollout-Vorschlag

Warning-first ist vertretbar in einem Analysemodus, in dem die betroffenen neuen destruktiven Operationen noch nicht aktiv sind oder durch eine nachweislich sichere Ersatzsemantik ersetzt werden. Eine Warnung bei unverändert aktivem Move und Drop macht das Programm nicht sicher.

Wenn ein Programm trotz uneindeutiger Besitzverhältnisse weiter übersetzt werden soll, muss für genau diesen Fall eine sichere, dokumentierte Behandlung existieren. Nicht pauschal „nur Leck“ behaupten. Ein bewusst zurückgestellter Drop kann als begrenzte Übergangsmaßnahme dienen, sichert jedoch nicht automatisch andere aktive Besitzer-Drops.

## 6. P2a: Cleanup vor Return und anderen Ausgängen

### 6.1 Relevante Stellen

- `src/core/ast_compiler_stmt.cpp:1287`, `visitRetStmt`.
- `src/core/ast_compiler_stmt.cpp:1439`, `visitBlockStmt` und normale Drop-Emission.
- `src/core/ast_compiler.cpp:4586–4596`: gestagte String-Drops für consumed-Parameter.
- Try-/Catch-Verarbeitung im selben Statement-Compiler.

### 6.2 Konkreter Umbau

Die Emission eines Drops in einen gemeinsamen Helfer auslagern, beispielsweise `emitDrop(entry)`. Daneben einen Helfer für die auf einem bestimmten Ausgang tatsächlich zu verlassenden Scopes vorsehen. Die Emission liest den pfadspezifischen Zustand; sie löscht nicht global die Zustände anderer Pfade.

Für Return:

1. Rückgabeausdruck einschließlich Konvertierungen und vorhandener Rückgabe-Dekoratoren auswerten.
2. Den endgültigen Rückgabewert beziehungsweise dessen unabhängigen Besitz sichern.
3. Übertragene Quellen/Temporaries auf diesem Pfad von der Freigabe ausnehmen.
4. Alle übrigen aktiven Besitzer in umgekehrter Initialisierungsreihenfolge freigeben: innere Scopes zuerst, dann äußere.
5. Erst danach den Return-Terminator emittieren.
6. Falls `ret_dead` für weitere Codegenerierung erhalten bleibt, dort keinen logisch noch ausstehenden Live-Cleanup erneut erzeugen. Erreichbarkeit ausdrücklich führen.

Ein Rückgabewert, der ein Feld oder Borrow auf einen zu droppenden lokalen Besitzer ist, benötigt vorher Copy oder einen zulässigen Transfer. Sonst verwandelt das Schließen des Return-Lecks einen bisher überlebenden Pointer in einen UAF.

Normaler Blockausgang, Return und Break dürfen dieselbe Freigabe nicht auf demselben ausgeführten Pfad doppelt auslösen. Break verlässt nur die Scopes bis zum Schleifenziel. Unterstützte Fehler-/Unwind-Ausgänge gesondert erfassen; eine Return-Korrektur allein ist keine vollständige Exit-Abnahme.

### 6.3 Abnahme

- Explizites abschließendes Return und früher Return aus verschachtelten Blöcken.
- Nicht weitergegebener consumed-String wird genau einmal freigegeben.
- Zurückgegebener Besitzer bleibt beim Aufrufer gültig.
- Rückgabe eines Modellfelds wird vor dem Owner-Drop korrekt abgesichert.
- Kein doppelter Drop über normalen Ausgang und Return.
- Die Rückgabekonvertierung/-Dekoration darf nicht nach der Freigabe auf alte Daten zugreifen.

## 7. Box-Drop und Borrow-Verträge

Box-Drop ist mehr als das Freigeben der äußeren Allocation. Verschachtelte owned Felder müssen vorher abgeräumt werden; geliehene oder statische Felder dürfen dabei nicht als owned behandelt werden.

### 7.1 Umfang ohne WP-C

Erforderlich sind lokal überprüfbare Herkunft und explizite Funktionsverträge. Ein vollständiger Ganzprogramm-Borrow-Checker ist nicht Voraussetzung. Nicht lokal nachweisbare Fälle müssen konservativ abgelehnt oder von der Erweiterung ausgespart werden.

Vor Aktivierung klären:

- `{new:T}`-Herkunft bleibt bei Übergaben bekannt; borrowed `ptr<T>` wird nicht automatisch zur Box.
- Consumed-Parameter übertragen Besitz nur entsprechend dem Signaturvertrag.
- Speicherung/Rückgabe eines Borrows: Copy, Diagnose oder ausdrücklich erlaubte Lebensdauerbindung gemäß verbindlicher Spec.
- Temporäre Boxen, Member-Boxen und wiederholtes Ersetzen haben jeweils einen definierten Freigabepunkt.
- Ressourcen-/Handle-Typen verwenden ihren passenden Destructor; nicht jedes Handle darf mit gewöhnlichem `free` behandelt oder durch Pointee-Kopie geklont werden.

### 7.2 9rooms-Migration und Abnahme

Betroffene Stellen gezielt erfassen, nicht blind alle Pointer umschreiben: Services mit gespeicherten Referenzen, Callbacks, Mirror-/Netzwerkobjekte sowie Match-/Spieler-Lebensdauern. Migration anhand Compilerdiagnosen und konkreter Besitzerketten durchführen.

Serverprüfung: Start, Login, Tokenvalidierung, Deck-Lesen/-Speichern, Match anlegen/beenden, Verbindungsabbruch und Wiederverbindung. Jeder Ablauf braucht nachgewiesenen fachlichen Erfolg sowie passende Speicherprüfung. Dateirechte und verfügbare KVs vorher als Betriebsgrundlage prüfen; eine nicht verfügbare Datenbank ist kein Lifetime-Test.

## 8. Vollständiger Map-Drop

### 8.1 Relevante Stellen

- `src/core/map_impl.cpp` und `map_impl.h`: Layout, Hashing, Insert/Replace/Delete, Wachstum und Rehash.
- `src/sys/string_runtime.cpp:132` zum Lesezeitpunkt: `aiml_map_free`; Zeile kann durch parallele Runtime-Erweiterung wandern.
- `src/core/ast_compiler_stmt.cpp:380`: zurückgestellte Registrierung.
- `src/core/ast_compiler_stmt.cpp:1488–1498`: Drop-Emission für Map-Einträge.
- `src/core/ast_compiler_call.cpp`, Map-Put-Pfad: Besitz von Argumenten und temporären Strings.

### 8.2 Layout-Audit

Die Runtime-Freigabe muss dasselbe Layout interpretieren wie die Map-Operationen. Bei Open Addressing ist die Zahl gültiger Einträge nicht gleich dem Index des letzten belegten Slots. Typischerweise müssen Capacity-Slots durchlaufen und deren Occupancy-Zustände ausgewertet werden; die konkreten Zustandswerte aus der Implementierung übernehmen.

Folgende Invarianten schriftlich abgleichen:

- Offsets, Integerbreiten, Schlüssel-/Wert-Stride und Alignment.
- Freier Slot, belegter Slot und Tombstone eindeutig unterscheiden.
- Keine Freigabe uninitialisierter oder bereits gelöschter Slots.
- Rehash überträgt vorhandenen Besitz; alte Slot-Hüllen dürfen dieselben Elemente nicht nochmals freigeben.
- Containerallocation und Elementheap sind getrennte Ressourcen.

### 8.3 Mutations-Audit

Für `put` explizit festlegen, ob Schlüssel und Wert kopiert oder konsumiert werden. Replace eines vorhandenen Schlüssels braucht eine Regel für den alten Wert, den bestehenden Schlüssel und den neu gelieferten Schlüssel. Temporäre Argumente dürfen weder nach Übernahme im Statement-Cleanup landen noch bei abgelehntem Insert verloren gehen.

Delete/Clear/Replace und finaler Drop müssen zusammenpassen. Ein korrekter finaler Drop heilt kein Leck beim wiederholten Überschreiben. Ein korrekter Put heilt keinen Double-Free beim Rehash.

Strings, verschachtelte Modelle, Listen und Maps getrennt prüfen. Raw-/Borrow-Pointer dürfen nicht allein aufgrund ihrer Pointerdarstellung als owned Element freigegeben werden.

### 8.4 Abnahme vor Aktivierung

- Leere Map, ein Element, viele Elemente und bewusst erzeugte Kollisionen.
- Growth/Rehash, Tombstones, Delete gefolgt von Reinsert, mehrfaches Clear.
- Mehrfaches Replace desselben Schlüssels mit neu alloziertem Wert.
- Schlüssel und Werte als Strings sowie unterstützte verschachtelte Heap-Typen.
- Put mit direkter Interpolation und mit lokalen Quellen.
- Abnahme unter Speicherprüfer plus N/2N-Allokationsvergleich; kein UAF/Double-Free, kein proportionaler Verlust bei konstantem Arbeitsbestand.

## 9. String-Temporary-Korrektur: nicht erneut implementieren, sondern absichern

Die zuletzt gelesenen freien und direkten Methodenpfade rufen für consumed-Parameter sowohl `consumeLocalDrop` als auch `consumeStringTemp` auf. Das adressiert den gemeldeten Fall, bei dem der Callee und das Statement-Cleanup denselben String freigaben.

Verifikation muss direkte Interpolation verwenden; ein vorher per `decl` gespeicherter String prüft nur den lokalen Pfad:

```text
call drop_str("tmp-${idx}")
call holder.keep("kept-${idx}")
```

Beide Varianten prüfen: Empfänger gibt den String selbst frei oder speichert ihn und liest ihn nach Rückkehr des Aufrufers. Außerdem Borrow-Parameter als Gegenfall prüfen: keine Besitzübertragung, Statement-Temporary wird erst nach dem synchronen Aufruf freigegeben.

Die neue Behandlung nicht mit dem weiterhin offenen allgemeinen Moved-Zustand verwechseln. Das Entfernen eines Temporary-Cleanups ersetzt keine Use-after-move-Diagnostik für benannte Variablen.

## 10. Verlässliche Abnahme-Harness

Bestehende Basis: `tests/run_memory_sanitizers.sh`, Proben unter `tests/_mem_sanitize/`.

Die Trennung von nativer Korrektheit und instrumentierter Speicherprüfung ist sinnvoll, weil RSS-Werte unter Valgrind nicht wie native RSS-Werte interpretiert werden dürfen. Daraus folgt aber keine allgemeine Erlaubnis, den instrumentierten Probe-Exit zu ignorieren.

Vorschlag:

1. Native Ausführung muss erfolgreich sein und ihre fachlichen Assertions ausführen.
2. Für den Valgrind-Modus ausschließlich die native RSS-Schranke explizit deaktivieren; fachliche Assertions weiterhin verbindlich auswerten.
3. Einen vollständigen Valgrind-Bericht und regulären Abschluss verlangen. Fehlendes Log, fehlender Abschluss, Signal oder Werkzeugstartfehler sind kein Erfolg.
4. Valgrind-Fehlercode und Berichtsinhalt prüfen; keinen beliebigen Nichtnull-Exit als harmlose RSS-Abweichung einstufen.
5. Absichtlich fehlerhafte Kontrollprobe getrennt ausführen und ihren erwarteten Fehler erkennen.
6. Leak-Zahlen mit festgehaltenem Arbeitsumfang vergleichen. Ein ausgegebener Leak-Wert ohne Bewertung ist noch keine Leak-Abnahme.
7. Nichtverfügbarkeit des Prüfwerkzeugs als nicht ausgeführte Abnahme ausweisen; ein Skip darf in einem verbindlichen Gate nicht als bestandene Sicherheitsprüfung gelten.

Jede relevante Probe sollte Binary-/Quellstand und genaue Aufrufparameter im Ergebnis erfassen. Eine alte Binary kann eine neue Quelltextänderung nicht validieren. Dafür sind keine Git-Kommandos erforderlich; Buildzeit und bei Bedarf Dateihashes reichen zur Zuordnung.

## 11. Entscheidungstore und Ergebnisablage

| Tor | Erforderlicher Nachweis | Noch nicht daraus ableitbar |
|---|---|---|
| Temporary-Übergabe | direkte und lokale Stringargumente korrekt, beide Aufrufpfade sauber | allgemeine Ownership-Sicherheit |
| Assignment/Dataflow | Copy-/Move-Tabelle, Branches und Schleifen eingehalten | sichere Box-/Map-Freigabe |
| Return-Cleanup | Rückgabe gültig, übrige Besitzer genau einmal freigegeben | vollständige Unwind-Abdeckung ohne eigene Prüfung |
| Box/Map | Herkunft, Layout und sämtliche Mutationen abgesichert | 9rooms-Migrationsfreiheit |
| 9rooms-e2e | fachlich erfolgreiche Abläufe und valide Speicherläufe | sämtliche denkbaren Anwendungen geprüft |

Ergebnisse jeweils als separaten datierten Bericht mit geprüften Fällen, Fehlern, Grenzen und Messdaten ablegen. Offene Punkte nicht allein wegen grüner Teilproben schließen.

## 12. Praktischer nächster Implementierungsschritt

Nach Verifikation der bereits geänderten Temporary-Übergabe zuerst `visitSetStmt` und den Besitzstatus korrigieren. Die neue Entscheidung über Copy/Move muss vor der Mutation der Cleanup-Liste stehen. Danach Branch-Merges und Use-after-move auf demselben Zustandsmodell aufbauen. Erst dann Return-Cleanup auf dieses Modell aufsetzen und zusätzliche Besitzerarten aktivieren.

Dieser Ablauf hält die Pakete klein, bleibt beim expliziten Ansatz aus Kapitel 61 und vermeidet, dass ein vermeintlicher Leakfix unbemerkt fremden Heap freigibt.
