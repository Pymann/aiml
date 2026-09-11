# Review: be8c023 und 69b0f34

Datum: 2026-09-10. Beide Commits heißen `ownership dataflow`. Geprüfter Bereich: `HEAD~2..HEAD`, Endstand `69b0f34`. Alle 15 geänderten Pfade hinsichtlich Implementierung, Proben und dokumentierter Abnahme eingeordnet. Git ausschließlich lesend benutzt, wie für den Commit-Vergleich angefragt. Kein Compiler-/Runtime-Code geändert, keine neuen Tests gebaut oder ausgeführt.

Die zwei untracked Proben im Arbeitsbaum sind kein Bestandteil dieser Commits. Deshalb sind lokale Erfolgsmeldungen nicht automatisch für einen sauberen Checkout reproduzierbar.

## R1 — P1: Return über eine View-Variable kann den tatsächlichen Owner freigeben

Stelle: `src/core/ast_compiler_stmt.cpp:1400–1414,1465`.

Die Ausnahme vom Return-Cleanup nimmt nur die OwnershipId der zurückgegebenen Variable bzw. ihres textuellen Basenamens. Beispiel: lokales owned Modell `owner` mit einer Liste; lokales `view` erhält flach `owner.items`; `return view`. Der Analyzer markiert `view` als Borrowed/Unknown, hält aber keine Beziehung zu `owner`. Return schützt ausschließlich `view`; `owner` wird freigegeben. Die zurückgegebene Listenhülle enthält damit einen freigegebenen Buffer. Gleiches Problem bei einem geliehenen Modellpointer über einen anderen lokalen Namen.

Die Prüfung `retVar` beweist keine Alias-Sicherheit. Benötigt: Ressourcenherkunft bis zum wirklichen Owner verfolgen, unabhängige Copy vor Cleanup oder bis dahin Cleanup bei ungeklärter Heap-Rückgabe unterdrücken. Der Fall wird erst durch die jetzt aktivierte Return-Freigabe laufzeitkritisch.

## R2 — P1: Member-Deep-Copy liest bei ptr<Model> den Pointer-Slot als Modell

Stelle: `src/core/ast_compiler_stmt.cpp:1284–1296`; Helfer `src/core/ast_compiler.cpp:4994–5018`.

`emitLValueAddress` liefert für eine einfache ptr-Variable deren Alloca. Nur `is_byref_arg` wird dereferenziert; ein gewöhnlicher `ptr<Model>`-Parameter oder lokaler Pointer fällt nicht darunter. Der neue Fast-Path übergibt diese Adresse direkt an `aiml_ser_deep_copy` mit dem Descriptor des Zielmodells.

Bei einer Zuweisung `set: [<.model, source_ptr]` wird dadurch der acht Byte große Pointer-Slot statt des Pointees serialisiert. Je nach Modellfeldern entstehen ungültige Pointer oder Reads außerhalb des Slots. Der bisherige untere Pointerpfad benutzte dagegen den ausgewerteten Pointerwert.

Korrektur: `lvTy` und LLVM-Repräsentation auswerten; für `ptr<T>` den tatsächlichen Pointee übergeben, für Value-T das Struct-Lvalue. Null-/Typvertrag vor Zugriff prüfen.

## R3 — P1: Neuer Member-Fast-Path umgeht die Prüfung inkompatibler Modelle

Stelle: `src/core/ast_compiler_stmt.cpp:1284–1314`.

Die Deep-Copy kehrt bei :1296 zurück; die bisherige Diagnose für eine Quelle eines fremden Modelltyps beginnt erst bei :1299. Für einen borrowed Modellparameter mit anderem Layout greift der Fast-Path, obwohl `lvTy` nicht gegen `finalTypeName` geprüft wird. Der Ziel-Descriptor wird zum Lesen des falschen Quelllayouts verwendet. Das kann über einen zuvor diagnostizierten Programmfehler hinaus zur Speicherverletzung führen.

Korrektur: sämtliche Kompatibilitätsprüfungen vor jedes Copy-/Move-Lowering setzen. Layoutverträglichkeit nicht durch das bloße Vorhandensein einer Adresse ersetzen.

## R4 — P1: Derselbe Owner kann mehrfach in verschiedene Member bewegt werden

Stelle: `src/core/ownership_analyzer.cpp:133–141`; `consumeLocalDrop` in `src/core/ast_compiler_stmt.cpp`.

Ein als Owned klassifizierter Local bleibt nach geplantem Member-Move unverändert Owned/Live. Die Quelle wird weder im Analyzer Moved noch mit den Zielressourcen verknüpft. Beispiel: frisches `x`, dann `set:[a.field,x]` und `set:[b.field,x]`. Beide Statements werden Move, beide speichern flach. `consumeLocalDrop` entfernt lediglich die direkte Freigabe von x. Die rekursiven Drops von a und b können trotzdem denselben inneren Heap doppelt freigeben; Return-Cleanup macht dies auch beim üblichen abschließenden Return sichtbar.

Korrektur: echten Konsum pfadbezogen erfassen; erneute Verwendung erkennen. Warning-first benötigt eine sichere Behandlung der gesamten beteiligten Besitzerkette, nicht nur das Löschen des Quell-Drops. Branch-/Schleifen-Merges können ohne solche Transferzustände keine Move-Sicherheit beweisen.

## R5 — P1: Calls können den freigegebenen Slot verändern, ohne den Plan zu aktualisieren

Stelle: `src/core/ownership_analyzer.cpp:168–194` (Bedingungen, Calls und übrige Statements).

Ein frisch deklarierter Listen-Local bleibt laut Analyzer owned, auch wenn eine Funktion ihn über einen by-ref-Parameter auf einen fremden Buffer umbindet. Weder Call-Argumente noch Bedingungen liefern Mutation-/Retain-Effekte. Das neue Return-Cleanup kann diesen fremden Buffer nun freigeben. Deep-Copy allein für Modell-Member-Set behebt diesen Fall nicht.

Korrektur: bekannte Call-Effekte auswerten, unbekannte Mutationen/Retention konservativ behandeln. Der im Bericht behauptete konservative Schutz der nicht modellierten Funktionsgrenzen ist damit nicht allgemein gegeben.

## R6 — P1: Fehlende Analyse fällt weiterhin auf ungeprüfte normale Drops zurück

Stelle: `src/core/ast_compiler_stmt.cpp:1642–1646`; `ownership_analyzer.cpp:58–72` und Statement-Dispatch.

Das Gate läuft nur bei vorhandenem Plan, Block-Exit und OwnershipId. Fehlt eines, wird trotzdem `emitScopeDrop` ausgeführt. Try/Catch wird vom Analyzer nicht besucht; entsprechende Locals erhalten keine IDs/Exit-Fakten und umgehen das Gate. Die fehlende Abdeckung war schon beim Watch-Review W1 notiert und ist in diesen Commits nicht geschlossen.

Korrektur: bei fehlendem Beweis konservativ unterdrücken oder den jeweiligen Block/Parameter explizit modellieren. Das ist eine verbleibende Schutzlücke der neu eingeführten Analyse, nicht die Behauptung, jede davon betroffene Freigabe sei erst in diesen Commits hinzugekommen.

## R7 — P2: Eingecheckte Harness referenziert nicht eingecheckte Testdateien

Stelle: `tests/run_memory_sanitizers.sh:143–146`.

Folgende referenzierte Dateien fehlen im Baum von `69b0f34` und erscheinen lokal nur als untracked:

- `tests/_mem_sanitize/ownership_shared_member_void.aiml`
- `tests/_mem_sanitize/return_cleanup.aiml`

Ein sauberer Checkout kann die neue Harness nicht vollständig bestehen. Korrektur: die tatsächlich verwendeten Proben Bestandteil der Änderung machen; nicht auf den lokalen Arbeitsbaum als Testvoraussetzung vertrauen.

## R8 — P2: Lokaler Modell-Copy-Fast-Path umgeht const-Prüfung

Stelle: `src/core/ast_compiler_stmt.cpp:899–922` gegenüber const-Prüfung `:974`.

Bei kompatiblen Heap-Modellen erfolgt Deep-Copy samt frühem Return vor `sym->is_const`. Eine lokale Zuweisung in ein const-Modell kann damit akzeptiert werden, die bisher verboten war. Auch der Selbstzuweisungs-Shortcut liegt davor.

Korrektur: Schreibbarkeitsprüfung vor allen Fast-Paths. Kein Runtime-/Ownership-Optimierungspfad darf bestehende Sprachprüfungen überspringen.

## Probe- und Dokumentationsbefunde

- `assign_local_copy.aiml` kommentiert, dass das innere `yrec` gedroppt wird. Der Analyzer setzt es beim Set jedoch auf Unknown und das Gate unterdrückt gerade diesen Drop. Die Probe prüft Inhalt und späteren Quellzugriff, beweist aber nicht allein die behauptete Cleanup-Unabhängigkeit. Gezielt Freigabe/Mutation der Kopie nachweisen.
- `map_string_drop.aiml` prüft lediglich Aufbau ohne Absturz; trotz Kommentar keine Inhaltsassertion. Map-Drop bleibt ausdrücklich zurückgestellt und wird dadurch nicht validiert.
- Das Datenmodell/Join-/mayDrop-Modul ist als Baustein sinnvoll. Es liefert aber ohne Aliasressourcen, Moved-Transfers und Call-Effekte keinen vollständigen Ownership-Beweis.
- Die dokumentierten lokalen Verbesserungen (direkter View-Rebind, bestimmter borrowed Modell-Member-Store, einfacher Return mit eigener Liste) sind enger als die Aussage, das gesamte Kern-Akzeptanzkriterium sei erfüllt.
- Bewusst zurückgestellte Replace-Lecks und Map-/Box-Drops sind bekannte Restarbeit; sie werden hier nicht als neue unerwartete Regression doppelt gezählt.

## Ergebnis

Beide Commits verbessern einzelne bekannte Fälle. Eine allgemeine Freigabe des aktiven Return-Cleanups ist durch die aktuelle Analyse nicht gedeckt. R1–R5 vor dieser Freigabe schließen; R6 als konservative Gate-Regel korrigieren. R7 macht außerdem den dokumentierten Harness-Erfolg aus dem Commit allein nicht reproduzierbar.

Alle Fehlerpfade oben sind aus dem Quelltext abgeleitet; konkrete neue Crash-Repros wurden in dieser Review nicht ausgeführt. Dokumentierte vorherige Suite-/Valgrind-Erfolge wurden nicht als eigene Verifikation ausgegeben.
