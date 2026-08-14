# AIML Unit-Test-Generator: Konkreter Implementierungsplan

Dieses Dokument konkretisiert den Ansatz aus `aiml_unit_test_implement.md` zu einem umsetzbaren Plan.

Ziel ist **nicht** ein magischer Vollautomat, der inhaltlich korrekte Tests errät. Ziel ist ein
**sicherer Generator für isolierte Test-Harnesses**, der:

1. eine AIML-Datei analysiert,
2. externe Abhängigkeiten kontrolliert stubbt,
3. aufrufbare Test-Skelette erzeugt,
4. deterministisch kompiliert,
5. und spätere manuelle Assertions oder Golden-Checks sauber vorbereitet.

## 1. Zielbild

CLI:

```bash
./out/aiml -t src/foo/bar.aiml -o tests/test_bar_generated.aiml
```

Erwartetes Ergebnis:

- alle `import`-Abhängigkeiten werden im generierten Testartefakt gekappt oder explizit ersetzt
- fehlende Klassen/Funktionen werden als **kompilierbare Stubs** ergänzt
- für öffentlich aufrufbare `def`-Blöcke und Klassenmethoden entstehen `test_*`-Skelette
- ein `main`-Runner ruft alle generierten Tests deterministisch auf
- der Generator produziert **valide AIML-Ausgabe**

Nicht-Ziel in MVP:

- automatische semantisch richtige Assertions
- perfektes Mocking von Seiteneffekten
- vollständige Erkennung aller nativen Laufzeitverträge
- automatische Konstruktion sinnvoller Testdaten für jede beliebige Signatur

## 2. Technische Entscheidung

Empfehlung: **nativ im Compiler**, nicht als separates Python-Tool.

Begründung:

- Der Compiler kennt bereits das echte Symbolsystem.
- Builtins, Namespaces, Klassen, Modelle und Methoden sind bereits in der Auflösungsschicht vorhanden.
- Fehler wie „fehlendes Symbol vs. builtin“ müssen nicht in zweiter Logik nachgebaut werden.
- Der gleiche AST kann für Analyse, Stub-Injektion und Export verwendet werden.

Pragmatischer Rollout:

1. interner Compiler-Pass
2. CLI-Flag `--generate-tests`
3. Export als `.aiml`
4. erst danach optional Hilfsskript für Batch-Generierung

## 3. Architektur

Es werden fünf neue Bausteine eingeführt.

### 3.1 `TestGenConfig`

Kleine Konfiguration für den Pass:

- Eingabedatei
- Ausgabedatei
- `strip_imports: bool`
- `generate_method_tests: bool`
- `stub_missing_symbols: bool`
- `emit_todo_asserts: bool`
- `target_namespace_prefix: string`

### 3.2 `SymbolInventory`

Sammlung aller in der Quelldatei selbst definierten Symbole:

- Klassen
- Modelle
- Interfaces
- freie `def`
- Klassenmethoden
- Namespaces/FQN-Namen

### 3.3 `MissingSymbolReport`

Ergebnis des Analysepasses:

- fehlende Typen
- fehlende freie Funktionen
- fehlende methodisch referenzierte Member, soweit belastbar erkennbar
- nicht-stubbfähige Referenzen als Warnungen

Wichtig:
Nicht jede unbekannte Method-Chain darf blind gestubbt werden. `obj.foo.bar()` kann ohne Typwissen
nicht sicher rekonstruiert werden. Solche Fälle müssen in MVP als **Warnung** markiert werden, nicht
als automatisch „gelöst“.

### 3.4 `GeneratedTestPlan`

Beschreibung der zu emittierenden Tests:

- `test_<function>`
- `test_<class>_<method>`
- optional `smoke_<type>` für Konstruktion/Init

### 3.5 `TestAstEmitter`

Baut aus Original-AST plus Stubs plus Test-Skeletten die finale Datei.

## 4. Implementierungsphasen

## Phase 1: CLI und Exportpfad

Ziel:
Das Tool akzeptiert `--generate-tests`, lädt AIML, und schreibt einen unveränderten AST wieder aus.

Arbeitspakete:

1. CLI-Flag in der bestehenden `main`/Argumentlogik ergänzen
2. Dateiladen und Parse-Pfad wiederverwenden
3. JSON/YAML-Emitter-Pfad für AST-Export festziehen
4. Ausgabe in Datei `-o ...`

Definition of Done:

- `./out/aiml --generate-tests file.aiml -o out.aiml` erzeugt eine Datei
- erzeugte Datei ist parsebar
- Roundtrip ohne inhaltliche Änderung funktioniert für einfache Testdateien

Risiko:

- Exportformat darf keine semantisch relevanten YAML-Strukturen zerstören

## Phase 2: Symbolinventar

Ziel:
Der Compiler kann eine Datei ohne Imports inventarisieren.

Arbeitspakete:

1. Top-Level-Nodes sammeln:
   - `class`
   - `model`
   - `interface`
   - `def`
   - `namespace`
2. Methoden pro Klasse inventarisieren
3. FQN-Auflösung für lokale Datei definieren
4. Builtin-Symbole explizit aus dem bestehenden Compilerkontext abfragen, nicht doppelt pflegen

Definition of Done:

- Für eine gegebene Datei kann der Pass alle lokal definierten Symbole als Report ausgeben
- Builtins werden nicht als „fehlend“ markiert

## Phase 3: Referenzanalyse

Ziel:
Der Compiler findet belastbar die **wirklich relevanten** externen Referenzen.

Arbeitspakete:

1. Rekursiver AST-Walk über:
   - `decl`
   - `set`
   - `return`
   - `if` / `while` / `try`
   - `call`
   - `new`
   - Member-/Method-Chains
2. Referenzklassen trennen:
   - Typreferenz
   - Funktionsreferenz
   - Konstruktorreferenz
   - Method-Chain-Referenz
3. Conservative Mode:
   - Nur Referenzen stubbbar markieren, die sicher klassifizierbar sind
   - Ambige Chains nur warnen

Definition of Done:

- `MissingSymbolReport` unterscheidet sicher zwischen:
  - lokal vorhanden
  - builtin
  - sicher fehlend
  - unklar/warnend

Wichtige Abgrenzung:

Der ursprüngliche Plan ist hier zu optimistisch. „Fehlende Symbole finden“ ist nicht nur ein
einfacher rekursiver YAML-Scan, sobald Member-Chains und Methoden ohne vollständige Typinformation
im Spiel sind. In MVP muss dieser Pass deshalb bewusst konservativ sein.

## Phase 4: Stub-Generierung

Ziel:
Aus sicher fehlenden Referenzen werden minimale, kompilierbare Stubs.

Arbeitspakete:

1. Typ-Stubs:
   - fehlende `class` mit kleinem Dummy-Member
   - fehlende `model` optional ebenfalls als Dummy-Struct
2. Funktions-Stubs:
   - Name
   - generische leere Arg-Liste nur dann, wenn die Signatur wirklich unbekannt ist
   - besser: Parameteranzahl aus Callsite ableiten
3. Rückgabetypen:
   - `void` nur für echte Statement-Calls
   - für expression-context Calls muss ein Platzhaltertyp gewählt werden
4. Stub-Namenskonvention:
   - Originalname beibehalten
   - optional Kommentar/Metadaten `AUTO-GENERATED STUB`

Definition of Done:

- fehlende einfache Klassen/Funktionen werden kompilierbar ergänzt
- Ausdruckskontexte erzeugen keine offensichtlichen Typbrüche

Wichtiger Punkt:

Die Behauptung aus dem Ursprungsdokument „Typen können meist weggelassen werden“ ist für die
Compilerrealität zu locker. Für Ausdruckskontexte braucht der Generator einen brauchbaren
Rückgabetyp, sonst produziert er nur andere Compilerfehler.

## Phase 5: Import-Isolation

Ziel:
Generierte Testdateien sind bewusst isoliert.

Arbeitspakete:

1. Alle Top-Level-Imports standardmäßig entfernen
2. Optionaler Modus `--keep-imports` für spätere Integrationstests
3. Originaldateipfad als Kommentar/Metadatum in Testdatei schreiben
4. Warnblock erzeugen, wenn gekappte Imports nicht vollständig stubbbar waren

Definition of Done:

- generierte Datei enthält keine produktiven Imports im Default-Modus
- Compiler meldet stattdessen nur noch echte Generatorlücken

## Phase 6: Test-Skeleton-Erzeugung

Ziel:
Alle lokal testbaren Ziele bekommen ein konsistentes `test_*`-Gerüst.

Arbeitspakete:

1. Freie Funktionen:
   - `test_<name>`
2. Klassenmethoden:
   - `test_<class>_<method>`
3. Testbody-MVP:
   - Start-Print
   - TODO-Print oder Kommentar
   - optional minimaler Smoke-Call
4. Problematische Ziele überspringen:
   - private Methoden
   - generierte/native Bindings ohne sichere Aufrufbarkeit

Definition of Done:

- generierte Datei enthält für jeden geeigneten Kandidaten ein testbares Skelett
- Namenskollisionen werden vermieden

Empfehlung:

In MVP zuerst nur **Smoke-Skeletons**, noch keine Auto-Assertions. Ein halbkorrekter Assert-Generator
ist gefährlicher als ein ehrlicher Teststub.

## Phase 7: `main`-Runner und Ergebnisaggregation

Ziel:
Die generierte Datei ist direkt ausführbar.

Arbeitspakete:

1. `main` erzeugen, falls nicht vorhanden
2. alle `test_*` in stabiler Reihenfolge aufrufen
3. einfacher Exitcode:
   - `0`, wenn alle Testfunktionen durchlaufen
   - `1`, wenn Generator selbst einen fatalen TODO-Block markieren will
4. optional später:
   - Zähler `passed/failed/skipped`

Definition of Done:

- generierte Tests lassen sich direkt kompilieren und starten

## Phase 8: Validierung und Compiler-Regressionstests

Ziel:
Der Generator selbst bekommt eigene Tests.

Pflichttests:

1. einfache freie Funktion ohne Abhängigkeit
2. Datei mit Importen
3. Datei mit fehlender Klasse
4. Datei mit fehlender freier Funktion
5. Datei mit expression-call, der Nicht-`void` braucht
6. Datei mit Klassenmethoden
7. Datei mit `try/catch`
8. Datei mit Member-Chain, die nur als Warnung endet
9. Golden-Output-Test für stabile Emission

Definition of Done:

- Generatorpfad ist selbst regressionstestbar
- Output ist ausreichend stabil für Golden-Vergleiche

## 5. Konkrete Implementierungsorte im Compiler

Wahrscheinliche Eingriffspunkte:

- CLI-/Argumentparser in der Compiler-`main`
- AST-Parse-Pfad wiederverwenden
- Symbolauflösung/Builtin-Registry lesen, nicht kopieren
- neuer Pass z. B. `TestGeneratorPass`
- AST-Export über vorhandenen JSON/YAML-Writer

Sinnvolle neue Dateien:

- `src/core/test_generator_pass.h`
- `src/core/test_generator_pass.cpp`
- `src/core/test_generator_types.h`
- `tests/test_generate_unit_tests_*.aiml`

## 6. Reihenfolge fuer die reale Umsetzung

Empfohlene Reihenfolge:

1. CLI + AST-Export
2. Symbolinventar
3. MissingSymbolReport
4. einfache Typ-/Funktions-Stubs
5. Test-Skelett-Generator
6. `main`-Runner
7. Golden-Tests
8. erst danach anspruchsvollere Expression-/Method-Chain-Fälle

Das ist wichtig, damit der Generator früh nutzbar wird, ohne zu früh „smart“ werden zu wollen.

## 7. Sicherheitsgewinn fuer die Entwicklung

Der Generator verbessert die Entwicklungssicherheit auf drei Ebenen:

1. Schneller isolierter Kompiliertest einzelner AIML-Bausteine
2. Frühere Sichtbarkeit von API-/Typschnittstellenproblemen
3. Standardisierte Test-Harnesses statt ad-hoc Testdateien

Der eigentliche Sicherheitsgewinn kommt aber **nicht** von automatisch erfundenen Assertions,
sondern von:

- reproduzierbarer Isolation
- konsistenten Stubs
- stabilem Test-Runtime-Entrypoint
- und späteren gezielten manuellen Assertions

## 8. Bewusste Nicht-Ziele in MVP

Diese Punkte sollten ausdrücklich **nicht** im ersten Wurf gelöst werden:

- automatische semantische Testfallgenerierung
- intelligente Mock-Rueckgaben aus Quelltextinterpretation
- vollständige Inferenz aller Method-Signaturen
- Abdeckung privater/intern gekapselter Methoden
- Snapshot-/Golden-Assertions auf inhaltliche Programmausgaben

## 9. Definition of Done fuer das Gesamtfeature

Das Feature ist erst fertig, wenn:

1. `--generate-tests` für einfache AIML-Dateien stabil läuft
2. generierte Dateien ohne Imports kompilieren
3. fehlende einfache Typen/Funktionen automatisch gestubbt werden
4. `test_*`-Skelette plus `main` korrekt emittiert werden
5. der Generator selbst eigene Regressionstests hat
6. Warnfälle explizit als Warnfälle gemeldet statt still falsch „gelöst“ werden

## 10. Nächster konkreter Schritt

Direkt sinnvoll ist jetzt:

1. `Phase 1` und `Phase 2` bauen
2. einen ersten Debug-Report `--generate-tests --dump-symbols`
3. dann an einer kleinen Datei wie `tul/math/matrix.aiml` oder einer reinen Core-Testdatei validieren

So bleibt der erste Wurf klein, aber belastbar.
