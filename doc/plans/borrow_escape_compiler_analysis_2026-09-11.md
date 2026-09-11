# Borrow-escape: Compileranalyse und Implementierungspaket

Stand: 2026-09-11. Analyseauftrag, keine Compileränderung. Kein Git verwendet.

## Ergebnis

Die 14 Meldungen aus dem 9rooms_v2-Serverbuild entstehen in mitkompilierten
alternativen Initialisierungsmethoden. Die ServerRuntime benutzt Classgroup-
Konstruktoren, `init_bare`, `sees` und `on_wired`. Eine Ausnahme allein für
Classgroup-Verdrahtungen beseitigt die Warnungen in den Basismethoden nicht.

Die aktuelle Prüfung ist syntaktisch und zugleich zu streng und unvollständig:
Sie warnt auch bei `consumed`, erkennt aber eine Weitergabe über einen lokalen
Alias nicht. Eine Reproduktion mit Alias, explizitem `free` und anschließendem
Lesen kompiliert ohne Borrow-Warnung; Valgrind weist den ungültigen Zugriff nach.

## Konkrete Integrationsstellen

| Stelle | Gegenwärtige Funktion | Konsequenz |
|---|---|---|
| `src/core/ast_compiler_stmt.cpp:885`, `visitSetStmt` | Sofortige Warnung bei `this.member = ptr/ref-Parameter` | Durch strukturierte Befunde ersetzen; Parameterqualifikation und tatsächlichen Zuweisungspfad berücksichtigen. |
| `src/core/ast_compiler.cpp:4554`, `visitFunction` | Setzt `is_param` auch für konsumierende Parameter; liest danach `argMods` in `sym.lifetime` | `is_param` bedeutet nicht automatisch Borrow. |
| `src/core/ast_compiler.cpp:4746` | Erstellt `OwnershipPlan` pro Funktion | Kein vorhandener projektweiter Lebensdauerbeweis. |
| `src/core/ownership_analyzer.cpp:36,106,198` | Erkennt ausgewählte freizugebende Wert-Lokale; Calls und andere Statements verändern hier keine Ownership-Fakten | Nicht ungeprüft als Sicherheitsnachweis für gespeicherte Zeiger verwenden. |
| `src/core/ast_compiler_expr.cpp:132` | `new` alloziert über `calloc` | Derzeit kein allgemeiner automatischer Box-Drop. |
| `src/core/ast_compiler_stmt.cpp:338,1680` | Registriert/emittiert Drops für bestimmte Werte, Strings und Listen; explizite `ptr/ref` sind bei Modell-Drops ausgeschlossen | Die Lebensdauer eines Box-Pointees ist aktuell nicht die Scope-Lebensdauer der Zeigervariablen. |
| `src/core/ast_compiler_call.cpp:377` | Explizites `free` | Muss eine spätere Borrow-Provenienz ungültig machen. |
| `src/core/ast_compiler_call.cpp:403,667,2485` | Freie Aufrufe mit Hot-Reload-Tabelle, Interface-Dispatch, konkrete/vererbte Methoden | Aufgelöste semantische Ziele an diesen Stellen erfassen; LLVM-`call` allein reicht nicht. |
| `src/core/ast_compiler.cpp:1250,2431` | Classgroup-Prüfung und Synthese vor Body-Codegen | Besitzer-/Port-Beziehungen während der Synthese als Metadaten erhalten. |
| `src/core/context.h:389` | Registry enthält bislang Container-Namen | Keine fertige Instanz-/Besitzerrelation vorhanden. |
| `compiler.cpp:929,1202,1244` | Import-Header, separate Importübersetzung, danach Root-Codegen | Analyse muss Übersetzungseinheiten verbinden. |
| `compiler.cpp:3238,3281` | Cache-Hit überspringt Übersetzung; Imports bekommen eigene Compilerinstanzen | Fehlende Befunde bei Cache-Hits niemals als Beweis der Sicherheit/Unerreichbarkeit behandeln. |
| `compiler.cpp:1246,3327`, `compiler.h:91` | Vorhandenes Zusammenführen von Prefab-/Enum-Befunden | Architektonisches Vorbild für projektweite Borrow-Befunde, kein fertiger Callgraph. |

## Speicherverwaltung: Korrektur der bisherigen Begründung

`AIML_LIFETIME_DROPS` ist im Compiler standardmäßig aus (`ast_compiler.cpp:1222`).
Der 9rooms-Game-Build setzt es in `exodus-game.json` nicht ausdrücklich; eine
geerbte Umgebung kann es trotzdem einschalten. Der Client setzt es ausdrücklich
auf 1. Der Memory-Testtreiber aktiviert es ebenfalls.

Auch bei aktiviertem Schalter enthält die erzeugte IR des vorhandenen Borrow-
Tests zwei `calloc`-Aufrufe und keinen entsprechenden Box-Drop. `g_runtime = runtime`
bewahrt momentan einen Zeiger auf die Runtime; daraus darf man keine bereits
implementierte rekursive RAII-Zerstörung des ganzen Dienstgraphen ableiten.

Das stützt die Einordnung der gemeldeten Serverstellen als UAF-Fehlalarme im
aktuellen Pfad. Es ist kein allgemeiner Nachweis, dass alle 9rooms-Speicherzugriffe
sicher sind. Die neue Analyse darf insbesondere nicht alle `new`-Objekte allein
wegen der derzeit fehlenden automatischen Freigabe als dauerhaft sicher markieren.

## Tatsächlich ausgeführte Prüfungen

Ergebnisse und isolierte Reproduktionen: `/tmp/aiml-borrow-audit-20260911/`.
Verwendet wurde das vorhandene Binary `out/aiml/aiml`, kein Compiler-Rebuild.

| Prüfung | Ergebnis |
|---|---|
| `borrow_escape_warn`, `classgroup_wiring`, `classgroup_sees_base_member`, `classgroup_post_wire`, `classgroup_subgroup_port`, `classgroup_cross_unit`, jeweils Drops 0 und 1 | 12/12 Läufe Exit 0. Borrow-Test jeweils eine Warnung; Classgroup-Tests keine. |
| `consumed_store.aiml`: Parameter erhält `lifetime: consumed` | Kompiliert, weiterhin eine falsche Borrow-Warnung. |
| `alias_store.aiml`: Parameter über lokale Zeigervariable speichern | Kompiliert, keine Borrow-Warnung. |
| `unused_store.aiml`: Aufruf vollständig entfernt | Kompiliert, weiterhin eine Borrow-Warnung. |
| `alias_uaf.aiml`: Alias speichern, Pointee explizit freigeben, Member lesen | Kompiliert ohne Borrow-Warnung; Valgrind Exit 99, `Invalid read of size 4`. |
| Vorhandener sicherer Borrow-Test unter Valgrind | Exit 0, `ERROR SUMMARY: 0 errors`. |
| Frischer 9rooms-Game-Compile nach `/tmp/.../server/main.ll`, Drops explizit 0, Strict-Top-Level 1, Asset-Roots `assets` | Exit 0; exakt 14 Borrow-Warnungen; 186 IR-Dateien einschließlich Imports. Server nicht gestartet. |

Die Server-IR ruft `ServerRuntime___ctor` auf; dieser benutzt
`EffectRuntime___ctor`, `LobbyService_init_bare` und `MatchService_init_bare`.
Über alle 186 IR-Dateien finden sich keine direkten Aufrufe von
`DeckService_wire_cards`, `LobbyService_init` oder `MatchService_init`. Die drei
manuellen Effekt-/Trigger-/Reaction-Wiring-Aufrufe haben ausschließlich
`MatchService_init` als direkten Aufrufer. Das ist eine Prüfung direkter Aufrufe,
kein vollständiger Erreichbarkeitsbeweis bei beliebigen dynamischen Aufrufen.
In `init_server_state` und den beiden Runtime-Konstruktoren gibt es keine
`free`-/`decref`-/`free_decoded`-Aufrufe; `runtime` wird in `g_runtime` gespeichert.

## Testsuiten: Aussagekraft und Lücken

- `tests/run_memory_sanitizers.sh` nutzt Valgrind, nicht ASan. Ein absichtlicher
  Double-free kontrolliert, dass die Instrumentierung tatsächlich Fehler sieht.
  Korrektheit und Speicherzugriffe werden getrennt geprüft; Leaks sind kein
  Fehlerkriterium. Ohne Valgrind meldet das Script SKIP mit Exit 0.
- Der Borrow-Probe-Aufruf bei Zeile 162 prüft weder Vorhandensein noch Abwesenheit
  der Warnung. Die Fixture liest den gespeicherten Zeiger nicht einmal zurück.
  Der konsumierende Parameter-Test deklariert ebenfalls mehrere Methoden, ohne
  sie aufzurufen. Beide sind als alleinige Diagnose-/Semantikabnahme unzureichend.
- `run_all_tests.sh` prüft überwiegend Exitcodes, schließt `_...`-Unterordner aus
  und führt deshalb `_mem_sanitize` nicht aus. Am Ende fehlt ein expliziter
  Fehler-Exit anhand von `FAILED`; die letzte Ausgabe kann trotz Fehlern Exit 0
  liefern. Zudem löscht das Script Cache und DB-Verzeichnisse. Es wurde für diese
  Analyse nicht gestartet.
- Classgroup-Tests decken Verdrahtung, Basis-Member, Post-Wire, Untergruppen,
  Ports, Interfaces und Cross-Unit-Nutzung ab; sie liefern keinen allgemeinen
  Besitzer- oder Freigabebeweis.
- 9rooms `run_relevant_tests.sh` deckt den betroffenen Cluster ab, prüft aber nur
  Laufzeiterfolg. Fehlende Dateien werden als MISS ausgegeben, ohne `fail` zu
  erhöhen. Für eine verbindliche Abnahme muss auch Vollständigkeit geprüft werden.
- Die vollständigen Compiler-, Memory- und 9rooms-Suiten wurden hier nicht
  ausgeführt. Die zwölf Läufe sind eine fokussierte Baseline.

## Überarbeitete Implementierungsreihenfolge

### A. Diagnosevertrag und belastbare Regressionen

Dedizierten Diagnose-Testtreiber hinzufügen: erwarteter Exit, Diagnose-ID,
Vorhandensein/Abwesenheit, Anzahl und Quellort. Nicht nur nach dem Wort PASS suchen.
Die obigen Reproduktionen übernehmen, echte UAF-Repros nur kompilieren oder
kontrolliert unter Valgrind starten. `consumed` von Borrow unterscheiden.

`spec/61` muss explizit zwischen einem nachgewiesenen Lebensdauervertrag, einem
unbekannten Borrow-Escape und einem nachgewiesenen ungültigen Zugriff unterscheiden.
Keine automatischen Drops, Kopien oder Besitztransfers durch diesen Diagnoseschritt ändern.

### B. Eigenständige Borrow-Befunde und Methodenzusammenfassungen

Neue LLVM-unabhängige `borrow_analysis.h/.cpp` statt Ausbau des Drop-Gates zum
universellen Borrow-Checker. Stabile Schlüssel aus Modul, qualifizierter Methode
und Parameterposition; gespeicherter Memberpfad und Quellort als Werte. Keine
rohen AST-Zeiger über die Lebensdauer einer Subcompilerinstanz hinaus speichern.

Direkte Parameterstores, lokale Aliase und Weiterleitungen an andere Methoden
erfassen. Branches/Schleifen zusammenführen, rekursive Aufrufe per Fixpunkt;
unbekannte Konstrukte bleiben unbekannt. Zusammenfassungen am Ende auswerten,
unabhängig von der Reihenfolge der Methodendefinitionen.

### C. Projektweiter Kontext einschließlich Cache

In `AIMLCompiler` einen pro Build geteilten Analysekontext etablieren und in
`resolveImport` weiterreichen. Befunde über den vorhandenen Header-AST-Pfad auch
bei Cache-Hits erfassen oder versionierte Analyse-Sidecars einführen. Für die
erste Version ist der Header-Pfad vorzuziehen: Er wird ohnehin für Typen gelesen.
Dabei synthetisierte/verschobene Methoden und doppelte Imports deduplizieren.

Objekt-/IR-Ausgabe, JIT mit kaltem und warmem Cache und isolierte Bibliotheks-
übersetzung müssen konsistente Diagnosen liefern. Bei unvollständiger Analyse
bleibt die Warnung bestehen. Keine unbedingte Unterdrückung in `libraryMode`.

### D. Begrenzte Nachweise und Erreichbarkeit

1. Zeigerherkunft und tatsächliche Objektidentität verfolgen; gleiche Klasse,
   globale Zeigervariable oder gemeinsamer Scope allein sind kein Beweis.
2. Classgroup-Instanz-, Untergruppen- und Portbeziehungen bei der Synthese
   speichern. Unterschiedliche Instanzen desselben Runtime-Typs unterscheiden.
3. Sichere, lokal vollständig sichtbare Nutzungen sowie unveränderte gemeinsame
   Besitzer prüfen. Explizites `free`, Rebinding, unbekannte Mutationen und
   externe Aufrufe entwerten Nachweise. Nicht unterstützte Fälle bleiben Warnungen.
4. Unerreichbare Methoden nur im nachweislich geschlossenen Programm ausblenden.
   Roots umfassen Entry, Modulinitialisierung, Konstruktoren, Hooks und erreichbare
   Callback-/Interface-/dynamische Registrierungen. Quellsprachlich bekannte
   Aufrufe vor dem Hot-Reload-Lowering erfassen. Unbekannte dynamische Nutzung
   oder eine offene Bibliotheks-API darf keinen falschen Unerreichbarkeitsbeweis ergeben.

Für die 14 konkreten 9rooms-Meldungen ist Punkt 4 zentral: Die gemeldeten
Basismethoden werden durch Classgroup-Verdrahtung umgangen. Lebensdauermetadaten
allein lösen deren definitionsseitige Warnungen nicht.

### E. Abnahme

- Sichere und unsichere Aufrufer derselben Methode, Parameter-Aliase, transitive
  Weitergabe, Rekursion, Branches, Scope-Ende, explizites Free und Besitzerwechsel.
- Vererbung, Interface-Aufrufe, Callback-Erreichbarkeit, externe Bibliotheksnutzung.
- Zwei getrennte Runtime-Instanzen, Untergruppen-Port, global gespeicherte Runtime.
- Ein-Datei- und Cross-Unit-Fälle mit Importreihenfolge/diamantförmigen Imports,
  kaltem/warmem Cache, JIT und Objekt-/IR-Ausgabe.
- Warnungsänderung darf Codegen und Speicherverwaltungsverhalten nicht ändern;
  IR strukturell und relevante Laufzeittests vergleichen.
- Die 14 9rooms-Warnungen dürfen erst nach dokumentiertem Nachweis verschwinden.
  Offene dynamische Fälle bleiben sichtbar, auch wenn das Ziel sonst verfehlt wird.
- Danach dedizierter Diagnosetreiber, Classgroup-Auswahl, vollständige Memory-
  Suite, 9rooms-Cluster und frischer Game-Build. Vollständige Suites nur mit
  verlässlicher Fehler-/Skip-Auswertung als Abnahme zählen.
