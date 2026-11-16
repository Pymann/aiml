# AIML Spec vs Implementierung

## Kurzfazit

Die AIML-Implementierung deckt einen brauchbaren Kern der Sprache ab, ist gegen die aktuelle Spec aber nicht vollstaendig. Der groesste Punkt ist nicht nur fehlende Implementierung, sondern auch, dass die Spec selbst laut [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md) noch 71 offene Inkonsistenzen, Luecken oder Widersprueche hat.

Deshalb muss die Luecke in zwei Ebenen betrachtet werden:

- die Spec ist an mehreren Stellen noch nicht normiert
- die Implementierung deckt einige spezifizierte Komfort- und Striktheitsregeln nur teilweise oder gar nicht ab

## Was bereits als belastbarer Kern wirkt

- Grundsyntax fuer `def`, `call`, `decl`, `set`, Kontrollfluss und Basistypen
- Modelle als Datenlayout und Serialisierungsbasis
- Mirror-Grundpfad mit registrierten Objekten, Deltas und RPC-Queues
- Persistenz- und Runtime-Builtins in einem produktiv nutzbaren, aber uneinheitlichen Zustand

## Groesste Luecken gegen die Spec

### 1. Funktionsaufrufe und Aufrufsemantik

Die Spec in [07_functions.md](../../spec/07_functions.md) verspricht mehr als der aktuelle Compiler belastbar absichert:

- Default-Argumente und benannte Trailing-Overrides sind spezifiziert
- `map<string:any>` als Eingabe fuer Funktionssignaturen mit Validierung und Auto-Unboxing ist spezifiziert
- klare `TypeMismatchException`-Semantik ist spezifiziert

Der aktuelle Call-Pfad ist hier nach bisheriger Analyse nur teilweise strikt:

- falsche Parametertypen fuehren nicht allgemein zu einem sauberen Frontend-Fehler
- Casts und Typanpassungen passieren teils implizit
- der Unterschied zwischen User-Funktionen und Builtin-Sonderfaellen ist nicht sauber normiert

Betroffene Stellen:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [call_dispatcher.cpp](../../src/core/call_dispatcher.cpp)
- [ir_generator.cpp](../../src/core/ir_generator.cpp)

### 2. Modelle, strukturelle Typisierung und tiefe Member-Zugriffe

Die Spec in [15_models.md](../../spec/15_models.md) ist deutlich reicher als der heute robuste Alltagsumfang:

- Zuweisung aus Maps mit Laufzeitvalidierung
- strukturelle Typisierung fuer Modellparameter
- `ptr<Model>` mit Auto-Dot-Dereferenzierung
- tiefer Zugriff auf verschachtelte Felder

Die bekannte praktische Luecke ist vor allem der Zugriffspfad:

- tiefe Member-Ketten wie `a.b.c` sind nicht konsistent fuer Read/Write/LValue
- dadurch entstehen viele Hilfsvariablen und Whole-Object-Reassigns

Betroffene Stellen:

- [member_chain_plan.md](../../member_chain_plan.md)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [utils.cpp](../../src/core/utils.cpp)

### 3. Mirror-Spec gegen Runtime-Verhalten

Die Mirror-Spec in [44_5_mirror.md](../../spec/44_5_mirror.md) beschreibt eine relativ glatte transportagnostische API.

Die Runtime ist funktional, aber nicht voll auf diesem Niveau:

- `session.update()`-Semantik war im WS-Pfad nicht so robust wie die Spec suggeriert
- Queue-/Poll-/Wait-Ergonomie fuehrt auf AIML-Seite zu viel Boilerplate
- einige reale Mirror-Faelle mussten erst ueber Repros und Reporte abgesichert werden

Betroffene Stellen:

- [mirror_ws_crash_report.md](../../mirror_ws_crash_report.md)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp)

### 4. JIT- und AOT-Verhalten ist nicht aequivalent

Sprachsichtlich sollte AIML-Quelltext moeglichst dieselbe Semantik haben, egal ob Datei-/AOT-Pfad oder String-/Chunk-/JIT-Pfad.

Der aktuelle Compiler hat hier bekannte Unterschiede:

- anderer Compilerkontext
- andere Importbehandlung
- andere Objekt- und Linkpfade
- andere Initialisierungs- und Symbolregistrierung

Betroffene Stellen:

- [jit_vs_aot.md](../../jit_vs_aot.md)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)

### 5. Die Spec selbst ist noch kein harter Vertragsstand

Die Vollstaendigkeitsfrage ist ohne Normierung der Spec nur begrenzt beantwortbar. Der bestehende Review zeigt offene Punkte bei:

- Typnamen
- YAML-Syntaxvarianten
- Fehlersemantik
- `any`-Verhalten
- Collection-Zugriff
- reservierten Namespaces

Betroffene Stelle:

- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md)

## Prioritaet

Wenn das Ziel echte Spec-Naehe ist, ist diese Reihenfolge am sinnvollsten:

1. Spec normalisieren und priorisieren
2. Funktionsaufrufe und Typpruefung haerten
3. Model-/Member-Zugriffe vereinheitlichen
4. Mirror-Verhalten und API-Ergonomie an die Spec angleichen
5. JIT/AOT-Paritaet herstellen

## Ergebnis

AIML ist gegen die Spec heute am ehesten "teilweise implementiert, aber nicht durchgaengig normkonform". Der Kern funktioniert, die Sprache als spezifiziertes Ganzes ist aber noch nicht geschlossen genug, um von hoher Vollstaendigkeit zu sprechen.
