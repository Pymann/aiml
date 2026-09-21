# Spec 61: Plan zur Behebung der drei Befunde aus Review-Runde 3

Stand: 2026-09-20. Status: geplant, noch nicht umgesetzt.

## Ziel und Umfang

Die drei nachgewiesenen Fehler aus den Probes 30–32 beheben. Maßgeblich sind Spec 61 §5.1 (Deep-Clone und Erhalt der Graphstruktur) und §6.2 (Borrow statt Ownership-Transfer bei `ref<T>`).

Der letzte vollständige Testlauf ergab 42 erfolgreiche Prüfungen und drei Fehlschläge. Die älteren Regressionstests bestanden bereits. Da parallel Änderungen am Compiler stattfinden, muss dieser Ausgangsstand vor der Umsetzung erneut geprüft werden.

| Befund | Ist-Verhalten | Erwartetes Verhalten | Regressionstest |
|---|---|---|---|
| F1: Listen-Clone ist flach | Mutation am Clone verändert das Original von 7 auf 99 | Alle besessenen veränderlichen Elemente werden unabhängig geklont | [30_round3_list_clone_deep.aiml](../tests/lifetime/30_round3_list_clone_deep.aiml) |
| F2: Container-DAG geht verloren | Zwei ursprünglich identische Listen werden getrennt geklont; `right.len` bleibt 2 | Beide Kanten zeigen auf genau einen geklonten Container; `right.len` wird 3 | [31_round3_clone_container_dag.aiml](../tests/lifetime/31_round3_clone_container_dag.aiml) |
| F3: Borrow-Zuweisung wird als Move gewertet | `set: [view, second]` führt anschließend zu `use-after-move` für `second` | `view: ref<T>` leiht aus; `second: ptr<T>` bleibt ein gültiger Owner | [32_round3_ref_rebind_borrow.aiml](../tests/lifetime/32_round3_ref_rebind_borrow.aiml) |

Die Implementierung bleibt hinter dem bestehenden Spec-61-Schalter `AIML_MEM_V3`. Die Semantik von `copy` bleibt flach. Ältere Fehler und andere Ownership-Baustellen gehören nur dann in diesen Umfang, wenn sie die drei Korrekturen unmittelbar blockieren.

## 1. Ausgangsstand sichern

- Aktuellen Diff prüfen und parallele Änderungen erhalten; die unten genannten Funktionen erneut lesen.
- Einen Compiler aus dem aktuellen C++-Stand bauen und verwendeten Commit beziehungsweise Arbeitsstand sowie Buildzeit dokumentieren.
- `ONLY=round3 bash tests/lifetime/run_lifetime.sh` ausführen. Bereits behobene Fälle nicht erneut implementieren.
- Bestehende Kontrollfälle zu Container-Aliasing, flachem Copy, Graph-Clone und Borrowing als Vergleich verwenden.

## 2. F2: Container im Graph-Clone memoisierten

**Betroffene Stellen:** [ser_runtime.cpp](../src/sys/ser_runtime.cpp), `clw`, insbesondere `AIML_TD_LIST_PTR`, `AIML_TD_MAP_PTR`, `AIML_TD_BUFFER_PTR`, sowie `aiml_clone_deep`.

Die drei Container-Fälle erzeugen bisher bei jeder eingehenden Kante einen neuen Handle. Sie müssen dieselbe Zuordnung Originalobjekt → Clone verwenden wie der übrige Graph-Walker.

1. Vor jeder Container-Allokation die quelladressbasierte Memo-Tabelle prüfen.
2. Bei einem Treffer den bereits geklonten Handle übernehmen und dessen **Container-Refcount** für die zusätzliche besitzende Kante erhöhen. Hier nicht `aiml_model_incref` verwenden: Modell- und Container-Header sind unterschiedliche Laufzeitverträge.
3. Bei einem neuen Objekt den passenden Listen-, Map- oder Buffer-Handle anlegen und **vor** dem rekursiven Kopieren seiner Inhalte in die Memo-Tabelle eintragen.
4. Alle Unterobjekte innerhalb desselben Clone-Aufrufs mit derselben Memo-Tabelle bearbeiten. Null-Handles erhalten ihre bestehende Semantik.
5. Sicherstellen, dass jede besitzende Kante genau einmal gezählt wird und temporäre Hilfshandles keine zusätzlichen Referenzen zurücklassen.

**Abnahme:** Probe 31 besteht. Mutation über `duplicate.left` ist über `duplicate.right` sichtbar, während das Original unverändert bleibt. Zusätzlich entsprechende DAG-Fälle für Map und Buffer prüfen, da sie denselben geänderten Switch-Zweig verwenden. Bei Scope-Ende dürfen keine mehrfachen Freigaben auftreten.

## 3. F1: Listen-Clone an den Deep-Clone-Walker anbinden

**Betroffene Stellen:** [ast_compiler_stmt.cpp](../src/core/ast_compiler_stmt.cpp), `emitListHandleDecl`; [string_runtime.cpp](../src/sys/string_runtime.cpp), `aiml_list_handle_copy`; die Erzeugung der Clone-Typbeschreibungen in [ast_compiler.cpp](../src/core/ast_compiler.cpp).

`emitListHandleDecl` führt `copy` und `clone` bisher gemeinsam über `aiml_list_handle_copy` aus. Diese Runtime-Funktion kopiert absichtlich nur den Container und erhöht die Refcounts seiner gemeinsam genutzten Elemente.

1. Die Behandlung von `isCopy` und `isClone` aufteilen. `copy` behält den flachen Runtime-Pfad und seine Move-only-Prüfung.
2. `clone` über den memoisierten Deep-Clone-Walker aus Abschnitt 2 ausführen. Vorhandene Hilfsfunktionen für Clone-Ausdrücke und Typbeschreibungen wiederverwenden beziehungsweise gezielt gemeinsam nutzbar machen.
3. Die tatsächliche Repräsentation beachten: Eine lokale Liste enthält unter memV3 einen **Handle-Pointer**, keinen inline gespeicherten Listenheader. Quelladresse, Zieladresse und Root-Typbeschreibung müssen übereinstimmen; ein `LIST_PTR`-Descriptor benötigt die Adresse des Handle-Slots.
4. Quellausdruck genau einmal auswerten. Einen gültigen Clone-Ausdruck nicht stillschweigend durch einen Null-Handle ersetzen, wenn die Quelle kein einfacher Variablenname ist.
5. Eine gemeinsame Memo-Tabelle für die gesamte Liste benutzen. Ein separater Clone-Aufruf pro Element würde Aliase zwischen Elementen erneut zerstören.
6. Den Ergebnis-Handle als genau eine neue besitzende Referenz in den Zielslot übernehmen und mit dem vorhandenen Scope-Drop verbinden. Keine zusätzliche flache Element-Incref-Schleife nach dem Deep-Clone ausführen.

**Abnahme:** Probe 30 besteht. Ergänzend prüfen:

- Zwei Listeneinträge mit demselben `shared<T>` ergeben genau ein neues gemeinsames Zielobjekt innerhalb des Clones.
- `list<ptr<T>>` erhält neue besessene Objekte; keine Pointeradresse wird mit dem Original geteilt.
- Ein flaches `copy` einer Liste mit `shared<T>` teilt weiterhin die Elemente, besitzt aber einen eigenen Container.
- Die bestehenden skalaren Copy-/Clone-Probes und Probe 15 bleiben grün.

## 4. F3: Move-Diagnostik am Zieltyp ausrichten

**Betroffene Stelle:** [borrow_check.cpp](../src/core/borrow_check.cpp), `BorrowChecker::stmt`, Zweig für `SetStmt`.

Die bisherige Fallunterscheidung behandelt einen unbekannten beziehungsweise nicht als `ptr` erfassten Zielslot trotzdem als besitzenden Empfänger. So wird bei einem `ref<T>`-Ziel die Pointerquelle fälschlich auf `Moved` gesetzt.

1. Den statischen Typ des Zielslots verlässlich ermitteln. Für lokale Variablen die Typinformationen aller Deklarationen und Parameter verwenden; ein fehlender Eintrag in `ptrLocals` beweist keinen besitzenden Zieltyp.
2. Bei `ref<T>`-Zielen die Quelle auf gültige Benutzung prüfen, aber **nicht** als verschoben markieren. Die Borrow-Herkunft beziehungsweise bestehende Invalidierungsinformation aktualisieren, soweit der Checker diese erfasst.
3. Dieselbe Entscheidung für auflösbare `ref<T>`-Felder verwenden. Ein Feldziel allein darf nicht automatisch einen Move auslösen.
4. Bei echten besitzenden Zielslots die bestehende Move-Regel erhalten. Verbotene Cross-kind-Konversionen müssen weiterhin diagnostiziert werden; die Korrektur darf sie nicht durch eine pauschale Ausnahme freigeben.
5. Beim Lowering gegenprüfen, dass eine Borrow-Zuweisung weder den Quellslot leert noch dessen Drop entfernt oder Eigentum an das `ref` überträgt. Probe 32 muss auch nach erfolgreicher Diagnosephase korrekt laufen.

**Abnahme:** Probe 32 besteht. Ergänzend Borrow-Feldzuweisung prüfen. Die negativen Tests für echte Moves, illegale Konversionen und Weiterverwendung eines an einen besitzenden Parameter übergebenen Pointers bleiben wirksam.

## 5. Reihenfolge und Abschlussprüfung

Empfohlene Reihenfolge: F2 → F1 → F3. F1 benötigt die korrekte Container-Memoisierung; F3 ist davon unabhängig. Pro Änderung einen kleinen, separat prüfbaren Diff erstellen.

Nach den Korrekturen aus dem AIML-Projektverzeichnis:

```bash
ONLY=round3 bash tests/lifetime/run_lifetime.sh
bash tests/lifetime/run_lifetime.sh
```

- Alle drei Ausgangsprobes und die gezielten Ergänzungen bestehen.
- Die komplette Spec-61-Lifetime-Suite besteht einschließlich Negativ- und Warnprüfungen. Erwartungen nicht abschwächen, um fehlerhaftes Verhalten grün zu machen.
- Die geänderten Clone-Pfade mit einem frisch gebauten ASan-Compiler auf ungültige Speicherzugriffe und doppelte Freigaben prüfen. Für Leak-Prüfungen azyklische Probes verwenden; absichtliche Ownership-Zyklen separat behandeln.
- Relevante modellunabhängige Kontrolltests auch mit `MODE=off` ausführen, um die Begrenzung auf memV3 zu prüfen. Nicht die gesamte Spec-61-Suite im alten Modus als grün voraussetzen.
- Bei neuen Fehlern nur die betroffenen Prüfungen erweitern; erfolgreiche unveränderte Prüfungen nicht ohne Anlass wiederholen.
- Abschlussbericht mit geändertem C++-Pfad, Buildstand, Testbefehlen und Ergebnissen erstellen.

## Erledigt, wenn

Die neuen Tests 30–32 und alle bisherigen Lifetime-Prüfungen bestehen, Listen-Clones besessene Elemente unabhängig kopieren und interne Container-Aliase erhalten, und Borrow-Zuweisungen keine Ownership-Transfers auslösen. Der bestehende flache `copy`-Vertrag und echte Move-Diagnosen bleiben erhalten.
