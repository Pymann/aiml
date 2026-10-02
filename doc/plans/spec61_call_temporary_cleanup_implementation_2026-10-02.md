# Implementierungsplan: Spec-61-Ownership bei Call-Temporaries

Stand: 02.10.2026, nach Review implementiert auf Basis `572603f`. Die historischen Zeilennummern unten beziehen sich auf den Planungsstand; Funktionsnamen bleiben die stabilen Anker. Der Nutzer hat Tests und 9rooms-Läufe ausdrücklich ausgeschlossen und als Prüfung ausschließlich `exodus build --all` im AIML-Verzeichnis vorgegeben.

## Umsetzungsstand

- `ast_compiler.cpp`: Forward-Signaturen werden auch bei vorhandenen LLVM-Deklarationen vervollständigt; vorhandene Zusatzmetadaten bleiben erhalten. `this=ref<Class>` war im Basis-Commit bereits korrigiert. RC-Parameter mit `consumed` halten eine eigene Callee-Referenz; die Caller-Referenz bleibt bestehen.
- `ast_compiler_call.cpp` / `.h`: gemeinsame `ResolvedCallSignature` für freie Funktionen, Methoden einschließlich Feldketten und Vererbung, Interface-Methoden und `fn<...>`-Variablen. Die Typabfrage erzeugt kein Receiver-IR. Alle vier Aufrufwege verwenden den gemeinsamen Argument-Abschluss, einschließlich Interface-void/sret und indirekten Aufrufen.
- `context.h` / `ast_compiler_stmt.cpp`: typisierte Temp-Frames mit Entry-Alloca pro Temp-Stelle. Nullinitialisierung wird am Statement-Anfang und bei jeder While-Bedingung eingefügt. Ownership-Transfers und Releases setzen den Slot zurück. Unwind-Pads berücksichtigen die bereits erzeugten Temp-Slots zusätzlich zum Local-Snapshot; Cache-Schlüssel enthalten deren Adressen und Drop-Arten. Später im selben Statement erzeugte Slots gehören erst zu späteren Pads, da sie bei früheren Throws noch keinen Owner enthalten können.
- String-Erzeuger in `ast_compiler_expr.cpp`, `ast_compiler_call.cpp`, `builtin_registry.cpp` und `serialize_impl.cpp` sind an dieselben Temp-Slots angeschlossen. Container-, Modell- und eindeutige Box-Argumente werden unmittelbar nach ihrer Auswertung registriert, bevor das nächste Argument ausgewertet wird. Eindeutige Boxen werden beim Aufruf an den Callee übertragen.

Validierung: `exodus build --all` im AIML-Verzeichnis ist für den endgültigen Stand erfolgreich abgeschlossen (Exitcode 0; Build-Log: `/tmp/aiml-spec61-build-final.log`). 44 Regressionstestprogramme samt Import-Fixture sind unter `tests/lifetime/call_temporaries/` ergänzt und über ein gemeinsames Manifest in die funktionale Lifetime-Suite, das AOT-Leak-Gate und die Memory-Safety-Suite eingebunden. Abdeckung und Status stehen in `tests/lifetime/call_temporaries/README.md`. Die neuen Testprogramme wurden weder kompiliert noch ausgeführt. Keine Tests, Leak-Gates, Sanitizer-Läufe oder 9rooms-Builds/Messungen ausgeführt. Die unten beschriebenen Abnahmekriterien sind deshalb offen; kein Leak-Muster wird allein aufgrund der Bauprüfung als GREEN markiert.


### Abnahme (2026-10-02, Nachtrag)

Alle 44 Regressionen kompiliert und unter Valgrind ausgeführt: Exit 0, 0 definitiv/indirekt verlorene
Bytes, keine Speicherfehler. Dafür nötig waren Testkorrekturen (gültiges AIML, siehe README) und drei
Compiler-Fixes: `fn<…>`-Aufrufe übergeben bei `ref<T>`/`ptr<T>`-Parametern die Adresse; der `ptr<T>`-Move
wird erst beim Betreten des Callees wirksam (der Slot des Locals wird nach der Argumentauswertung
genullt, statt den Drop beim Übersetzen zu entfernen) — ein vorher werfendes Argument lässt die Box beim
Aufrufer; `spawn` verschiebt erst nach der Argumentauswertung. Zusätzlich erkennt der Borrow-Checker
schleifenübergreifendes use-after-move und Moves an Methoden-/Interface-Parameter (neg n20/n21).

## Ziel und Abgrenzung

Alle sechs bestätigten Fehlerbilder beheben: Methoden-Vorwärtsreferenz, Producer über verschachtelten Receiver, Producer über Funktionszeiger, Interface-Consumer, Funktionszeiger-Consumer und temporärer Caller-Owner bei Exception im Consumer. Vorrang hat spec/61_memory_management_DRAFT.md, besonders §§5, 8, 9.2 und 9.3. Der gültige Originalausdruck in 9rooms muss ohne zusätzliche lokale Variable leakfrei werden.

Keine Änderung des Sprachvertrags und keine zusätzlichen Pflicht-Annotationen in 9rooms. Containerargumente behalten ihre RC-Semantik. `ref<list<T>>` ist keine Lösung. `consumed` auf RC-Typen bedeutet nicht pauschal, dass die Caller-Referenz verschwindet: der Consumer hält seine eigene Referenz gemäß Vertrag. `ptr<T>`-Übergaben sind davon getrennt echte Moves.

## Voraussetzung — Arbeitsstand des Nutzers zuerst validieren und committen

Vor Beginn der Compileränderungen dieses Plans wird der laufende Umbau des Nutzers abgeschlossen, validiert und von ihm committet: ptr<T>-Move, yaml_doc-Freigabe, Signatur-Vorlauf sowie Interface-Listen und -Maps. Basis-Commit, Compiler-/Runtime-Hashes und Gate-Ergebnisse festhalten; danach Quellanker/Zeilen und rote Reproduktionen gegen genau diesen Stand aktualisieren. Keine konkurrierenden Änderungen an diesem noch offenen Umbau beginnen.

Zum Basis-Umbau gehört auch die im Review genannte fn-Lücke: fehlendes movePtrArg und fehlende consumed-Behandlung im indirekten fn-Pfad. Mit dem neuen Callee-Drop kann ein benannter ptr<T>-Owner sonst doppelt freigegeben werden. Diese Korrektur übernimmt der Nutzer vor seinem Commit; sie ist keine zusätzliche Implementierungsaufgabe dieses Plans. Ihre Regressionen bleiben bei der späteren Abnahme verpflichtend. Der separate RC-consumed-Vertrag wird unten ausdrücklich behandelt.

Vor dem ersten Compilerfix außerdem Compile-Zeit und Binärgröße eines 9rooms-Builds als Vergleichsbasis erfassen (Messprotokoll in Schritt 6).

## Aktueller Stand: bereits vorhandenen Fix berücksichtigen

`ast_compiler.cpp:3867` enthält inzwischen `registerForwardInfo`; Aufrufe stehen bei Methodenvorwärtsdeklarationen um 4164 und freien Funktionen um 4603. Den ursprünglichen Fix daher nicht nochmals unabhängig implementieren.

Zwei konkrete Konsistenzpunkte bleiben zu prüfen/korrigieren:

- Die Methodenvorregistrierung übergibt derzeit `ptr<Class>` als implizites this; `visitTopLevel` um 4721 fügt dagegen `ref<Class>` ein. this muss in beiden Phasen derselbe nicht-ownende Receiver sein.
- `registerForwardInfo` überspringt vorhandene FunctionInfo-Einträge, und die Vorwärtsdeklarationsschleifen überspringen vorhandene LLVM-Funktionen. Eine bereits existierende LLVM-Deklaration oder unvollständige FunctionInfo darf das Nachtragen von Signatur-/Ownership-Daten nicht verhindern. Das ist eine defensive Konsistenzprüfung, kein weiterer gemessener Hauptbefund: visitFunction überschreibt die Daten später vollständig; relevant ist das Zeitfenster vor dessen Body-Emission.

Die fünf späteren Minimalproben bleiben eigenständige Abnahmekriterien; vollständige Forward-Metadaten allein beheben sie nicht.

## Schritt 0 — Reproduktionen und Status nach dem Basis-Commit festhalten

Dateien: neue beschreibend benannte Dateien unter `tests/lifetime/`, Einbindung in `tests/run_lifetime_leak_gates.sh` (GREEN nach erfolgreichem Fix). Keine heute noch freien numerischen Test-IDs fest voraussetzen, da am Arbeitsbaum parallel gearbeitet wird.

Ausgangsartefakte:

- `../9rooms_v2/leak_probes/review_20261002/isolated/forward_method_1000/probe.aiml` und die grüne Gegenreihenfolge `self_chained_1000`.
- `../9rooms_v2/leak_probes/review_20261002_more/{chain,fnptr,throw,iface_consumer,fnptr_consumer}_1000/probe.aiml`, jeweils passende `_local`-Kontrolle.

Alle sechs Muster samt Kontrollen in einem gemeinsamen Testlauf ausführen und einzeln ausweisen. Die Forward-Probe als reine Compilerprobe ohne Spielimporte reduzieren, dabei rote/grüne Reihenfolge unter identischen Typen erhalten; falls noch keine _local-Kontrolle existiert, diese ergänzen. Die Gegenreihenfolge ersetzt den eigenen Local-Kontrollfall nicht. Aktuelle Baseline gegen einen festen Compiler-/Runtime-Stand aufnehmen. Produktionsdatenbanken werden nicht benötigt. Fehlerhafte Proben nicht als grüne Gates eintragen, bevor der zugehörige Fix tatsächlich besteht.

## Schritt 1 — Vollständige Signaturregistrierung vor Body-Codegen

Konkrete Stellen:

- `src/core/ast_compiler.cpp:3867`: `registerForwardInfo`.
- `src/core/ast_compiler.cpp:4120`: Methoden-Vorwärtsdeklarationen in `registerTypeDecl`.
- `src/core/ast_compiler.cpp:4562`: freie FunctionDecl-Vorregistrierung.
- `src/core/ast_compiler.cpp:4721`: implizites this bei `visitTopLevel`.
- `src/core/ast_compiler.cpp:5449`: FunctionInfo-Aufbau in `visitFunction`.
- `src/core/context.h:91`: `FunctionInfo`.

Änderung: Signaturaufbau in einen gemeinsam verwendeten Helfer ziehen. Rückgabetyp, Return-Ownership, Argumentnamen/-typen/-lifetimes sowie this=ref<Class> vollständig setzen. LLVM-Funktion bei Bedarf wiederverwenden. Bereits zugewiesene fptable_index-, Versions-, Source-/Importdaten erhalten; Signatur-Vervollständigung nicht mit Body-Definition oder Vergabe eines zweiten Funktionsslots verwechseln. Kein Body-Codegen während dieses Vorlaufs.

Abnahme: identisches Verhalten bei beiden Methodenreihenfolgen, bei freien Vorwärtsaufrufen, importierten/geerbten und generierten Classgroup-Methoden. Keine Abhängigkeit von kurzen Methodennamen in globalen Producer-Sets.

## Schritt 2 — Eine typisierte Call-Signaturauflösung

Konkrete Stellen:

- `src/core/ast_compiler_stmt.cpp:374`: `ownedContainerCallType`.
- `src/core/ast_compiler_stmt.cpp:384`: `resolveCallTarget`.
- `src/core/ast_compiler_stmt.cpp:413`: `ownedModelCallType`.
- `src/core/ast_compiler_call.cpp:75`: `parseFnType`.
- `src/core/ast_compiler_call.cpp:146–187`: Auflösung tiefer Receiver für die eigentliche Codegenerierung.
- `src/core/context.h:568`: `InterfaceMethod`, daneben `InterfaceInfo`.
- `src/core/ast_compiler.h:89–95`: bisherige Ownership-/Call-Helfer; neue Deklarationen hier ergänzen.

Vorschlag: `ResolvedCallSignature` als semantisches Ergebnis einführen: Dispatch-Art, Rückgabetyp, Return-Ownership, Parametertypen/-verträge und gegebenenfalls konkretes FunctionInfo-Ziel. Ein fn- oder Interface-Aufruf braucht keine erfundene konkrete FunctionInfo; sein statischer Signaturtyp genügt.

Auflösung muss freie Funktionen, einfache und verschachtelte Receiver, this/<., Vererbung, Interface-Signaturen sowie fn<...>-Variablen abdecken. Den vorhandenen fn-Typparser gemeinsam nutzbar machen; keine zweite abweichende Parserimplementierung.

Wichtig: Signaturanalyse darf Receiver/Argumente nicht erneut auswerten. `resolvePropertyWritePtr` erzeugt Adress-/IR-Code und darf deshalb nicht blind als zweiter Analyseaufruf verwendet werden. Typauflösung von Werteauswertung trennen oder das bereits ermittelte Call-Ziel wiederverwenden.

`ownedContainerCallType`/`ownedModelCallType` nutzen danach diese Signaturdaten. Builtin-Accessor-Semantik gesondert typisiert halten: keine pauschale Annahme, dass jede Methode namens get/at borrowed ist oder jeder undurchsichtige Pointer ein Container. Spec 61 §5.4/§5.7 unterscheidet Elementarten; bestehende Accessor-Tests müssen erhalten bleiben.

Abnahme: `chain_1000` und `fnptr_1000` sind grün; gleichnamige Methoden verschiedener Klassen dürfen einander nicht beeinflussen. Zählerprobe bestätigt genau eine Auswertung je Receiver/Argument.

## Schritt 3 — Die vier Normalpfad-Leaks zuerst beheben

Nach der typisierten Auflösung aus Schritt 2 die normalen Call-Abschlüsse vereinheitlichen. Dieser überschaubare Zwischenstand darf mit ausdrücklich dokumentierter roter Throw-Probe separat abgenommen und committet werden. Er ist noch keine vollständige §9.3-Abnahme.

Konkrete Stellen in `src/core/ast_compiler_call.cpp`:

| Pfad | Bestehende Stelle | Änderung im Normalpfad |
|---|---|---|
| Direkte freie Funktion | `ctrArgTemps/modelArgTemps` um 555; Releases um 786 | vorhandenen Abschluss als gemeinsame Funktion nutzbar machen |
| Direkte Methode | `ctrArgTemps/modelArgTemps` um 3442; Releases um 3578 | denselben Abschluss verwenden; Parameterversatz für this berücksichtigen |
| fn-Verbraucher | Block um 734, unmittelbarer Return um 778 | nach erfolgreichem Call durch den gemeinsamen Abschluss führen; die ptr-/Argumentkorrektur des Basis-Commits erhalten |
| Interface-Verbraucher | Vtable-Pfad um 850–1000, sret-Return um 987 und normaler Return um 999 | temporäre Argument-Owner sammeln und für alle Rückgabeformen einschließlich void/sret freigeben |

Zusammen mit Schritt 2 werden damit `chain`, `fnptr`, `iface_consumer` und `fnptr_consumer` grün. Nicht bloß das return im fn-Pfad entfernen, ohne die Ergebnis-/void-Behandlung zu erhalten.

Vor Cleanup das Call-Resultat sichern und korrekt weiterreichen. Aliasing erlaubter Containerreturns muss gültig bleiben: eine eigene Rückgabereferenz muss vor dem Release der Argumentreferenz bestehen. Bestehende String-, Model- und Move-Helfer erhalten; derselbe Owner darf nur einen Cleanup-Verantwortlichen haben.

Abnahme: vier rote Muster und alle zugehörigen _local-Kontrollen jeweils mit 10/1000 Aufrufen grün, benannte Caller-Container danach weiterbenutzbar, Ergebnisse unverändert. Throw-Tests weiterhin ausführen und als RED mit gemessenen Zahlen führen. Erfolg dieser Stufe nicht als vollständige Spec-61-Leakfreiheit ausgeben.

## Schritt 4 — RC-consumed als eigenen Vertrag korrigieren

Diese Lücke gehört ausdrücklich zum Umfang. Sie ist vom ptr<T>-Move-Fix des Nutzers zu unterscheiden.

Konkrete Stellen:

- `src/core/ast_compiler_call.cpp`: consumed-Auswertung um den direkten Funktionspfad (ab 555), direkte Methoden (ab 3442), Interfaces (ab 916) und den durch den Basis-Commit korrigierten fn-Pfad (ab 734).
- `src/core/ast_compiler_stmt.cpp`: `consumeLocalDrop`, `consumeStringTemp`; Aufrufer nach Typklasse unterscheiden.
- `src/core/ast_compiler.cpp`: `visitFunction` und Parameter-Incref/`pendingParamDrops`; §8-Call-Referenz und dauerhafte Speicherung konsistent behandeln.
- `src/core/ownership_analyzer.cpp` / `ownership_flow.h`: Argument-Move-Entscheidungen dürfen ein benanntes RC-Local nicht allein wegen consumed als Moved markieren. Diese Stellen nach dem Basis-Commit erneut lokalisieren.

Aktueller im Review beschriebener Zustand: Caller-Local verliert per consumeLocalDrop seine Referenz, Callee bekommt sie ohne incref. Das kann rechnerisch balanciert sein, verletzt aber §9.2 bei weiterer Nutzung des Caller-Locals: nach Freigabe durch den Callee hängt dessen Alias.

Ziel gemäß §9.2: benannte RC-Werte (string, list/map/buffer/cascade, shared) bleiben beim Caller gültig. Der Callee übernimmt eine zusätzliche eigene Referenz, nicht diejenige des benannten Callers. Speichert er sie dauerhaft, wird diese eigene Referenz in den Speicherort übertragen oder dort korrekt geteilt; sein Parameterdrop muss dazu passen. Nicht blind zusätzlich incref einbauen, wenn der Parameterpfad schon eine Call-Referenz hält: insgesamt genau eine zusätzliche Callee-Referenz herstellen und jede Speicherung gegen ihre vorhandene Retain-/Move-Semantik prüfen.

ptr<T> bleibt ein echter Move; ref<T> bleibt nicht-ownend. Für RC-Temporaries ist Referenztransfer als Optimierung nur zulässig, wenn wirklich kein überlebender Caller-Owner benötigt wird. Sichere Grundimplementierung: Callee-Referenz sichern und Caller-Temporary regulär droppen. Ein RC-consumed-Argument darf deshalb nicht pauschal aus der Caller-Temp-Cleanup-Liste ausgeschlossen werden.

Abnahme je betroffener RC-Klasse: Callee hält Wert nach Return; Caller liest/mutiert seinen benannten Wert weiter; Callee löscht/ersetzt später seine Referenz; Caller bleibt gültig und wird am Ende genau einmal freigegeben. Beide Freigabereihenfolgen testen, ebenso temporäre Argumente und Throw nach Übernahme. Für fn-Typen nur Verträge verwenden, die dessen statische Signatur tatsächlich trägt; nicht vom zufälligen aktuellen Funktionsziel ableiten oder neue Syntax als Teil dieses Fixes einführen.

## Schritt 5 — Ein typisierter Temp-Frame einschließlich Strings, Modelle und Unwind

Konkrete Stellen:

- `src/core/context.h:316–380`: `ScopeDrop`, `cleanupStack`, `stmtDropSnapshot`, `unwindPadCache`, bisherige string-only `stmtTempStack`.
- `src/core/ast_compiler_stmt.cpp:224`: `visitStatement` und Statement-Snapshot.
- `src/core/ast_compiler_stmt.cpp:471`: `releaseContainerHandle` / `releaseContainerCopyTemp`; `:793`: `makeHandleDrop`.
- `src/core/ast_compiler_stmt.cpp:4605`: `emitScopeDrop`; `:4796`: `aimlUnwindDest`.
- Weitere Integrationsstellen in derselben Datei: `consumeStringTemp`, Statement-Ende, `visitRetStmt`, `visitIfStmt`, `visitWhileStmt` und `visitThrow` mit den bisherigen stmtTempStack-Schleifen.
- `src/core/ast_compiler_call.cpp:20`: `callOrInvoke`; gemeinsame Abschlüsse aus Schritt 3 sowie Erzeugung/Adoption von String- und Model-Resultaten.
- `src/core/ast_compiler.h`: Deklarationen der typisierten Temp-Frame-Helfer.

### Feste Slots statt flusssensitiver Aktivierungsverwaltung

Pro statischer Temp-Stelle einen festen Slot im Funktions-Entry-Block anlegen. Temp-Typ und Drop-Deskriptor stehen vor der betreffenden Statement-Emission fest (typisierter Vorlauf über die Ausdrucksstellen, ohne IR-Auswertung). Der Frame enthält die vollständige Menge seiner Slots, nicht nur diejenigen, deren Ausdruck schon übersetzt wurde.

**Bei jedem dynamischen Statement-Beginn** alle Slots dieses Frames auf null/zero setzen, bevor eine Teilauswertung werfen kann. Einmalige Initialisierung beim Funktionseintritt reicht nicht. Für wiederholt ausgewertete while-Bedingungen erfolgt der Reset am Beginn jeder Bedingungsauswertung; Kurzschluss-/Verzweigungspfade dürfen keinen alten Temp-Wert wiederverwenden. Bei verschachtelten Statements nur deren eigenen Frame zurücksetzen, niemals noch benötigte äußere Owner. Vor erneutem Reset müssen Werte des vorherigen Durchlaufs freigegeben oder übertragen sein.

Container-/String-/Box-Handles verwenden Nullslots; Model-Resultate benötigen passend ausgerichtete, für ihren Drop sicher zero-initialisierte Slots. Die jeweilige Drop-Art muss den leeren Zustand tatsächlich sicher behandeln; nicht pauschal jeden Ressourcentyp wie einen nullbaren Pointer behandeln. Bestehende Typklassifikatoren und Drop-Deskriptoren wiederverwenden.

Nach erfolgreichem Producer-/Interpolationsergebnis den Owner sofort in seinen Slot schreiben. Wirft die Erzeugung selbst, bleibt der Slot leer. Auf Normalpfad nach Ende der Nutzung freigeben und leeren; bei echter Ownership-Übernahme durch Local/Return/Store nur den ursprünglichen Besitzslot leeren. RC-consumed nach Schritt 4 behandeln. Bei einem Move-Parameter Übergabe erst committen, wenn die notwendige Argumentauswertung abgeschlossen ist; ein vorheriger Throw muss die noch beim Caller liegenden Owner abbauen.

### Unwind-Pads und Cache

Ein Pad darf sämtliche Slots des gültigen Temp-Frames unbedingt, aber null-/zero-sicher droppen: nicht ausgeführte und bereits übertragene Temporaries haben leere Slots. Keine flusssensitive Aktivierungsmenge und keine pfadabhängigen SSA-Ergebnisse im Pad. Bei verschachtelten Frames die tatsächlich verlassene Frame-Kette in umgekehrter Erzeugungsreihenfolge behandeln; erhalten bleibende äußere Scopes nicht mitfreigeben.

`aimlUnwindDest` behält den Statement-Snapshotfilter für benannte Locals bei. Temp-Frames werden zusätzlich aufgenommen und nicht durch `if (snap && !snap->count(it->addr)) continue` ausgefiltert. So bleibt eine noch fehlgeschlagene Local-Initialisierung geschützt, während bereits erzeugte temporäre Argumente desselben Statements korrekt verschwinden.

Pad-Cache-Schlüssel: Funktion, Unwind-/Catch-Ziel, vorhandene normale Scope-Drops, statische Statement-/Auswertungsidentität und vollständige Temp-Slot-Menge einschließlich Drop-Arten/Frame-Kette. Innerhalb desselben Frames braucht er keinen Aktivierungszustand, weil die Nullslots diesen zur Laufzeit repräsentieren. Typischerweise ein wiederverwendbarer Pad je Cleanup-Konfiguration, nicht ein eigener Pad pro Temp-Stelle.

### Strings und Modelle sind Teil dieser Stufe

Die bisherige Verschiebung der String-Migration entfällt. `consume_throw("x-${i}")` und ein werfender Consumer eines heap-ownenden Model-Temporaries bekommen eigene rote Reproduktionen, bevor diese Stufe beginnt; sie sind beim Abschluss zwingend grün. String-Temps aus Interpolation, String-Rückgaben und Model-Temps laufen in denselben typisierten Frame. Nach Migration ihre bisherigen stmtTempStack-/modelArgTemps-Releases entfernen oder auf den Frame umleiten, damit kein doppelter Drop entsteht.

Gemeinsame Abschlusshelfer aus Schritt 3 auf den Frame umstellen, ohne bereits behobene Normalpfade wieder zu verschlechtern. C++-RAII stellt Codegeneratorzustand wieder her, ersetzt aber keine LLVM-Ausgabe von Laufzeit-Cleanup.

Abnahme: Container, String und heap-ownendes Model jeweils bei normalem Return, Consumer-Throw, Producer-Throw, später werfendem Argument, Schleifen und verschachtelten try/catch. `throw_1000` samt _local und allen neuen String-/Model-Kontrollen grün; kein UAF/Double-Free.

## Schritt 6 — Abnahme und Integration

Gezielte Regressionen zuerst, dann bestehende Gates:

- Einen gemeinsamen Gate-Lauf mit allen sechs Ausgangsmustern und sämtlichen _local-Kontrollen durchführen; die Forward-Reihenfolgekontrolle zusätzlich behalten. 0 definitely lost, 0 indirectly lost, keine Valgrind-Fehler, korrekte Resultate. Skalierung 10/1000 beibehalten. An der Endabnahme darf keiner dieser Fälle nur als erwartetes RED stehen.
- Dispatch × normal/throw × temporär/benannt abdecken; Zusatzfälle später werfendes Argument, verschachtelter Receiver, importierte/geerbte Methode, gleichnamige Methoden, void/sret, Rückgabe teilt einen Container mit dem Argument.
- ptr<T>-Argumente in allen vier Aufrufwegen (frei, Methode, Interface, fn) testen: benannter Owner und Temporary, normale Rückkehr und Throw; Caller-Move-Zustand korrekt, keine zweite Freigabe. Implizites this bleibt Borrow. Die vom Nutzer vorab behobene fn-Lücke muss dauerhaft abgesichert bleiben.
- RC-consumed-Tests aus Schritt 4 und String-/Model-Unwind-Tests aus Schritt 5 laufen im selben Abnahmepaket.
- List/map/buffer durch repräsentative Fälle prüfen; keine ausschließlich auf list<i32> zugeschnittene Typentscheidung.
- `bash tests/run_lifetime_leak_gates.sh` und `bash tests/run_memory_sanitizers.sh` gemäß vorhandenen Repo-Voraussetzungen ausführen. Einen Buildfehler oder nicht ausgeführten Sanitizer nicht als bestanden werten.
- Originalprobe `poison_tick_zone` mit unveränderter 9rooms-Ausdruckszeile neu bauen. Der zusätzliche lineare Verlust muss verschwinden. Die bekannte konstante 64-B-Allokation der alten Testvorlage separat ausweisen oder den Testaufbau bereinigen; nicht als tolerierten allgemeinen Leak ins Compiler-Gate übernehmen.
- Danach 9rooms neu bauen und einen normalen Match-/Phasenwechsel sowie Match-Ende prüfen. Native Minimalproben ersetzen keinen WASM-/Browserlauf.

### Compile-Zeit und Binärgröße messen

Vor dem ersten Fix auf dem validierten Basis-Commit und nach der Normalpfad-Stufe sowie nach dem Temp-/Unwind-Umbau denselben 9rooms-Build messen. Native Server- und WASM-Client-Ziele, Buildbefehl, Flags, Toolchain-Hashes, Quellstand und Zielartefakte im Protokoll festhalten. Ausschließlich passende Messpaare vergleichen; parallele Builds während der Messung vermeiden.

Jeweils sauberen Build und Build mit warmem Importcache getrennt messen, möglichst drei Läufe und Median für Wall-/CPU-Zeit; zusätzlichen Peak-RSS mit erfassen. Eigene Build-/Cache-Verzeichnisse verwenden, statt aktive fremde Caches zu löschen. Binärgröße der gleichen ungestrippten/gestrippten Artefakte sowie .text- und Exception-/Unwind-Sektionen (nativ) bzw. WASM-Dateigröße (Client) ausweisen. Ergänzend Zahl erzeugter Unwind-Pads/Temp-Slots aus IR oder Diagnostik zählen.

Absolute Werte und prozentuale Änderung dokumentieren. Eine deutliche Mehrung auf Pad-Duplizierung, übergroße Slot-Frames oder doppelte Signaturanalyse untersuchen; nicht mit einem unbegründeten neuen Grenzwert automatisch akzeptieren. Vorhandene Performance-Grenzen gelten, ansonsten Ergebnis transparent zur Bewertung vorlegen. Ohne diese Messung keine abgeschlossene Performance-Abnahme behaupten.

## Empfohlene Umsetzungseinheiten

0. Nutzer validiert und committet seinen bestehenden Umbau inklusive fn-ptr-Move-Fix. Danach Basis/Zeilen aktualisieren und Messbaseline aufnehmen.
1. Reproduktionen samt RED/GREEN-Status und verbleibende Signaturkonsistenz (Schritte 0–1).
2. Typisierte Auflösung und gemeinsamer normaler Call-Abschluss (Schritte 2–3): vier zusätzliche Normalpfad-Muster grün, Throw ausdrücklich weiterhin RED. Separat abnehmbar, kein Anspruch vollständiger §9.3-Erfüllung.
3. RC-consumed-Co-Ownership (Schritt 4) als eigener prüfbarer Vertragsschritt.
4. Feste typisierte Temp-Slots für Container, Strings und Modelle einschließlich Unwind (Schritt 5); Throw- und neue Unwind-Regressionen auf GREEN umstellen.
5. Gemeinsamer vollständiger Gate-Lauf, 9rooms-Abnahme und Buildkostenvergleich (Schritt 6).

Definition of done: alle sechs Muster und Kontrollen sowie RC-consumed-, ptr-Dispatch- und String-/Model-Unwind-Regressionen grün; identische Resultate unabhängig von Methodenreihenfolge/Dispatch; sichere normale und Exception-Ausgänge; gemessene Compile-Zeit/Binärgröße; keine erforderliche 9rooms-Umschreibung und keine Abweichung von Spec 61.
