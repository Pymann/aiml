# JIT vs AOT

## Kurzfassung

Der Compiler hat aktuell keine einheitliche Semantik zwischen JIT- und AOT-Pfad.

Es gibt mindestens zwei deutlich verschiedene Compile-/Link-Pipelines:

- Datei-/AOT-Pfad über `compile(...)`
- String-/JIT-/Chunk-Pfad über `compileFromString(...)` und `compileChunkAndGetAddress(...)`

Diese Pfade setzen nicht denselben Compiler-Kontext auf, behandeln Imports unterschiedlich und laden externe Symbole/Objektdateien nicht gleich.

Das ist sehr wahrscheinlich eine reale Ursache für:

- Unterschiede zwischen normalem Build und `sys.bundle` / Hot-Reload
- Import-/Extern-/Global-Probleme nur im JIT-Pfad
- Unterschiede bei transitive imports, sibling imports und object linking

## 1. Größter Unterschied: `compile()` vs `compileFromString()`

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L104)
- [compiler.cpp](../../compiler.cpp#L431)

### AOT / Datei-Pfad

In [compiler.cpp](../../compiler.cpp#L104) bis [compiler.cpp](../../compiler.cpp#L426) macht `compile(...)` unter anderem:

- setzt `ctx.libraryMode`
- setzt `ctx.compilerPath`
- installiert `ctx.resolveImportCallback`
- sammelt `deferredImports`
- macht Header-Propagation für Imports
- registriert `libInitFuncNames`
- macht `siblingImports`-Injection
- registriert externe Globals aus importierten Dateien
- propagiert `ctx.objectFilesToLink`
- setzt `ctx.currentSourceFile`

### JIT / String-Pfad

In [compiler.cpp](../../compiler.cpp#L431) bis [compiler.cpp](../../compiler.cpp#L480) macht `compileFromString(...)` im Vergleich deutlich weniger:

- kein `ctx.libraryMode`
- kein `ctx.resolveImportCallback`
- keine `deferredImports`
- keine `siblingImports`
- keine Header-Only-Importpropagation
- keine separate Registrierung importierter Globals
- kein `ctx.currentSourceFile`

### Konsequenz

Gleicher AIML-Inhalt hat aus Datei und aus String nicht denselben Compile-Kontext.

Das ist kein kleiner Unterschied, sondern ein semantischer Split.

## 2. Importbehandlung ist nicht symmetrisch

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L135)
- [compiler.cpp](../../compiler.cpp#L263)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L417)

### AOT

Im Datei-Pfad wird Importauflösung aktiv vorbereitet:

- [compiler.cpp](../../compiler.cpp#L135) bis [compiler.cpp](../../compiler.cpp#L205)
- [compiler.cpp](../../compiler.cpp#L263) bis [compiler.cpp](../../compiler.cpp#L398)

Dabei werden:

- Typen aus Imports vorgezogen registriert
- Geschwisterimporte berücksichtigt
- globale `decl`s aus Imports in die Symboltabelle gehoben
- Objektdateien für Imports vorbereitet

### JIT

`compileFromString(...)` macht nur:

- `registerTypes(programAST.get())`
- `compile(programAST.get())`

ohne den Importvorbau.

### Konsequenz

Alles, was auf Importkontext angewiesen ist, kann im JIT-Pfad anders reagieren:

- globale Variablen aus Imports
- `extern`-artige Symbole
- transitive Modelle/Klassen
- sibling imports

## 3. JIT-Laufzeitbindung ist nicht einheitlich

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L723)
- [compiler.cpp](../../compiler.cpp#L1614)
- [compiler.cpp](../../compiler.cpp#L489)

### `jitRun()`

Der normale JIT-Lauf in [compiler.cpp](../../compiler.cpp#L723) bis [compiler.cpp](../../compiler.cpp#L1668):

- lädt Host-Prozess-Symbole per `LoadLibraryPermanently(nullptr)`
- preloadet Libraries aus `exodus.json`
- registriert sehr viele Runtime-Funktionen via `DynamicLibrary::AddSymbol`
- lädt `linkedObjects` per `engine->addObjectFile(...)`
- setzt `aiml_set_active_context(...)`

### `compileChunkAndGetAddress()`

Der Chunk-/Hot-Reload-Pfad in [compiler.cpp](../../compiler.cpp#L489) bis [compiler.cpp](../../compiler.cpp#L530):

- kompiliert via `compileFromString(...)`
- baut direkt eine JIT-Engine
- macht keine sichtbare Import-Objektladung
- macht keine breite `AddSymbol`-Registrierung wie `jitRun()`
- verwendet keinen äquivalenten Setup-Pfad für externe Runtime-Abhängigkeiten

### Konsequenz

Ein Symbol kann im normalen JIT-Lauf verfügbar sein, im Chunk-/Bundle-Pfad aber fehlen oder anders aufgelöst werden.

## 4. `currentSourceFile` nur im Datei-Pfad

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L259)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L166)

In [compiler.cpp](../../compiler.cpp#L247) bis [compiler.cpp](../../compiler.cpp#L256) wird im Datei-Pfad `ctx.currentSourceFile` gesetzt.

`compileFromString(...)` tut das nicht.

Das betrifft mindestens:

- `log.*`-Location-Injection in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L151)

### Konsequenz

JIT-/Bundle-Code hat andere oder fehlende Source-Locations in Logs.

Das ist kein Kernruntime-Bug, aber ein weiterer Beleg dafür, dass beide Pfade nicht dieselben Invarianten haben.

## 5. `libraryMode` ist nicht konsistent gesetzt

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L124)
- [compiler.cpp](../../compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L25)

Im Datei-Pfad wird `ctx.libraryMode = libraryMode` gesetzt in [compiler.cpp](../../compiler.cpp#L123).

Im String-Pfad fehlt diese Übernahme.

Das ist relevant, weil `libraryMode` Einfluss auf globale Symbolnamen und Import-/Lib-Initialisierungsverhalten hat, insbesondere bei:

- importierten `decl`s
- `__aiml_lib_init_*`
- extern/Datei-spezifischen Symbolpräfixen

### Konsequenz

Dasselbe Codefragment kann in String/JIT und Datei/AOT unterschiedliche globale Namen emittieren oder andere Linkage-Semantik bekommen.

## 6. Objektdatei-Linking nur im normalen JIT-Pfad sichtbar

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L1614)
- [compiler.cpp](../../compiler.cpp#L489)

In [compiler.cpp](../../compiler.cpp#L1614) bis [compiler.cpp](../../compiler.cpp#L1634) lädt `jitRun()` explizit `linkedObjects` in die Engine.

`compileChunkAndGetAddress()` macht diesen Schritt nicht.

### Konsequenz

Wenn ein JIT-Chunk auf Importcode oder importierte Objektdateien angewiesen ist, verhält er sich nicht wie der normale JIT-/AOT-Pfad.

Das ist besonders relevant für:

- `sys.bundle`
- Hot-Reload
- transitive Funktions-/Typnutzung
- importierte globale Zustände

## 7. AOT hat echte Linkerphase, JIT simuliert nur Teile davon

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L642)
- [compiler.cpp](../../compiler.cpp#L723)

Der AOT-Pfad über `compileToObject(...)` in [compiler.cpp](../../compiler.cpp#L642) bis [compiler.cpp](../../compiler.cpp#L720) endet in echten Objektdateien.

Der JIT-Pfad muss dagegen:

- Hostsymbole selbst registrieren
- Runtime-Symbole selbst registrieren
- Importobjekte selbst nachladen

Das ist grundsätzlich normal, aber hier ist es nicht zentralisiert. Stattdessen gibt es:

- einen umfassenderen `jitRun()`-Pfad
- einen verkürzten `compileChunkAndGetAddress()`-Pfad

### Konsequenz

JIT ist aktuell keine saubere Spiegelung von AOT, sondern ein eigener Modus mit anderer Infrastruktur.

## 8. `libraryMode` ändert den gesamten Top-Level-Codegen

Betroffene Stellen:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L153)

In [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20) bis [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L183) gibt es zwei grundsätzlich verschiedene Codegen-Modi:

- `ctx.libraryMode == true`
- `ctx.libraryMode == false`

### Im Library-Mode

- Top-Level-`decl`s werden zu LLVM-Globals
- es wird `__aiml_lib_init_<file>` erzeugt
- Initialisierung läuft über diese Lib-Init-Funktion
- es wird kein normales `__aiml_init` erzeugt
- `emitFptable(ctx)` läuft nicht
- `emitSchemaRegistry(ctx)` läuft nicht

### Im normalen Modus

- es wird `__aiml_init` erzeugt
- importierte `libInitFuncNames` werden dort aufgerufen
- danach wird `emitFptable(ctx)` erzeugt
- danach wird `emitSchemaRegistry(ctx)` erzeugt

### Konsequenz

Wenn `compileFromString(...)` `libraryMode` nicht korrekt übernimmt, ist das kein Randdetail, sondern führt zu völlig anderem Modulaufbau:

- andere Globalnamen
- andere Initialisierungsreihenfolge
- fehlendes `__aiml_init`
- fehlende fptable/schema-Emission

## 9. Hot-Reload-/fptable-Infrastruktur ist nicht überall gleich vorhanden

Betroffene Stellen:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L298)
- [fptable.cpp](../../src/core/fptable.cpp#L11)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L182)

Die indirekte Call-Infrastruktur hängt von `__aiml_fptable_current` ab:

- Call-Site-Nutzung in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L298) bis [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L337)
- Emission in [fptable.cpp](../../src/core/fptable.cpp#L11)

Da `emitFptable(ctx)` nur im Nicht-Library-Pfad erzeugt wird, hängt das Vorhandensein der Hot-Reload-Tabellen direkt an der Moduswahl.

### Konsequenz

String-/JIT-Code, der versehentlich in einem anderen Modus landet, kann:

- direkte Calls statt fptable-Calls erzeugen
- keine `__aiml_fptable_current`-Globale haben
- andere Hot-Reload-Semantik haben als der normale Build

## 10. Entry-Point-Erzeugung ist zwischen den Pfaden nicht symmetrisch

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L483)
- [compiler.cpp](../../compiler.cpp#L530)
- [compiler.cpp](../../compiler.cpp#L489)

`compileAndRun(...)` macht:

- `compileFromString(...)`
- `generateEntryPoint()`
- `jitRun()`

Siehe [compiler.cpp](../../compiler.cpp#L483) bis [compiler.cpp](../../compiler.cpp#L486)

`compileChunkAndGetAddress(...)` macht dagegen:

- `compileFromString(...)`
- sofort Engine bauen
- `finalizeObject()`
- Symbol lookup

Siehe [compiler.cpp](../../compiler.cpp#L489) bis [compiler.cpp](../../compiler.cpp#L530)

### Konsequenz

Der Chunk-Pfad läuft nie durch dieselbe Entry-Point-Logik wie der normale JIT-Lauf:

- kein standardisiertes `main`
- kein `__argc`/`__argv`-Store über `generateEntryPoint()`
- kein garantierter `__aiml_init()`-Call über den Entry-Point

Das ist besonders relevant, wenn Code implizit auf Top-Level-Init oder CLI-Globals angewiesen ist.

## 11. `aiml_set_active_context(...)` nur im normalen JIT-Lauf

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L1657)
- [exec_impl.cpp](../../exec_impl.cpp#L53)

In [compiler.cpp](../../compiler.cpp#L1657) bis [compiler.cpp](../../compiler.cpp#L1659) setzt `jitRun()`:

- `aiml_set_active_context(compiler_ctx_, engine)`

`compileChunkAndGetAddress()` tut das nicht.

### Konsequenz

Alles, was zur Laufzeit über den aktiven Compiler-/Engine-Kontext arbeitet, kann im Chunk-/Bundle-Pfad anders oder gar nicht funktionieren.

Der Kommentar in [compiler.cpp](../../compiler.cpp#L1656) nennt explizit:

- `aiml_exec_code`

Das macht den Unterschied direkt relevant für `sys.exec`-/Code-Objekt-Pfade.

## 12. Modulverifikation läuft nur im AOT-Objektemit explizit

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L695)
- [compiler.cpp](../../compiler.cpp#L705)

In [compiler.cpp](../../compiler.cpp#L695) bis [compiler.cpp](../../compiler.cpp#L715) verifiziert `compileToObject(...)`:

- jede Funktion
- das gesamte Modul
- schreibt bei Fehlern `/tmp/aiml_bad_module.ll`

Der normale JIT-Pfad hat diese explizite Diagnose nicht im selben Umfang.

### Konsequenz

Es kann IR geben, die im JIT später diffus scheitert, aber im AOT-Pfad früh und mit besserer Diagnose auffällt.

Das ist ein Unterschied in Debuggability und Fehleroberfläche, auch wenn derselbe Quellcode kompiliert wird.

## 13. Caching-/Objektstrategie unterscheidet sich nach Modus

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L1739)
- [compiler.cpp](../../compiler.cpp#L1757)
- [compiler.cpp](../../compiler.cpp#L1774)

In [resolveImport](../../compiler.cpp#L1739) bis [resolveImport](../../compiler.cpp#L1825) gilt:

- `noCache == false`: interner SHA-basierter Cache in `__aiml_cache`
- `noCache == true`: Buildsystem-/OutputDir-orientierter Objektpfad

`compileFromString(...)` nutzt diesen Import-/Cachepfad aber praktisch gar nicht.

### Konsequenz

Auch die Artefaktstrategie unterscheidet sich:

- Datei/AOT/JIT mit Imports baut reale Objektdateien und cached sie
- String/JIT-Chunk arbeitet viel direkter auf dem Modul und umgeht diese Stufe

Das ist ein weiterer Grund, warum Import-/Link-Bugs nur in einem der beiden Modi sichtbar sein können.

## 14. `sys.exec` hängt hart am aktiven JIT-Kontext

Betroffene Stellen:

- [exec_impl.cpp](../../exec_impl.cpp#L43)
- [exec_impl.cpp](../../exec_impl.cpp#L303)
- [compiler.cpp](../../compiler.cpp#L1657)

Die Runtime in [exec_impl.cpp](../../exec_impl.cpp#L43) bis [exec_impl.cpp](../../exec_impl.cpp#L108) arbeitet mit globalem Zustand:

- `g_active_context`
- `g_active_ee`
- gecachte fptable-Globale

Dieser Zustand wird nur im normalen JIT-Lauf explizit gesetzt:

- [compiler.cpp](../../compiler.cpp#L1657)

`compileChunkAndGetAddress()` setzt diesen aktiven Kontext nicht.

### Konsequenz

`sys.exec`-nahe Pfade sind nicht nur “JIT statt AOT”, sondern zusätzlich abhängig davon, ob überhaupt ein aktiver Engine-/Compiler-Kontext gesetzt wurde.

Das betrifft insbesondere:

- fptable-Lookup
- Hot-Reload
- `compileYamlChunk(...)`
- direkte Engine-Lookups in [exec_impl.cpp](../../exec_impl.cpp#L455)

## 15. `sys.bundle` hängt an Compiler-seitig gesammeltem Source-Metadatenzustand

Betroffene Stellen:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L886)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1424)

`sys.bundle` verwendet:

- `FunctionInfo.sourceYaml`
- `FunctionInfo.referencedTypes`
- `ClassInfo.sourceYaml`
- `ClassInfo.referencedTypes`

Siehe:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L886)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L939)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1365)

Da `compileFromString(...)` weniger Kontext setzt und weniger Importvorbau hat, ist sehr plausibel, dass die Closure für `sys.bundle` in JIT/String-Fällen unvollständiger ist als im Datei-/AOT-Pfad.

### Konsequenz

Selbst wenn derselbe Funktionsname gebundlet wird, kann der Inhalt des Bundles unterschiedlich sein:

- fehlende importierte Typen
- fehlende transitive Referenzen
- fehlende oder andere `sourceYaml`-Zuordnung

## 16. Isolated `sys.exec` benutzt wieder einen anderen Compilerpfad

Betroffene Stellen:

- [exec_impl.cpp](../../exec_impl.cpp#L225)
- [exec_impl.cpp](../../exec_impl.cpp#L276)
- [compiler.cpp](../../compiler.cpp#L483)

`aiml_isolated_exec(...)` in [exec_impl.cpp](../../exec_impl.cpp#L225) bis [exec_impl.cpp](../../exec_impl.cpp#L301) baut aus `code_chunks` neues YAML und ruft dann:

- `AIMLCompiler iso;`
- `iso.compileAndRun(yaml_source);`

Das heißt:

- Shared-Context-`sys.exec` nutzt den aktiven Context + fptable + Hot-Reload
- Isolated-Context-`sys.exec` kompiliert und läuft über einen frischen Compiler mit `compileFromString(...)`

### Konsequenz

Es gibt praktisch nicht nur JIT vs AOT, sondern mindestens drei relevante Semantikpfade:

- Datei/AOT
- Shared JIT
- Isolated JIT via `compileFromString(...)`

## 17. Exception-/Invoke-Verhalten hängt von compile-time Kontext und Runtime-Symbolen ab

Betroffene Stellen:

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L634)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L322)

Das Exception-Handling im Compiler nutzt:

- `ctx.catchBlock`
- `CreateInvoke(...)`
- `__gxx_personality_v0`
- `__cxa_throw`
- `__cxa_allocate_exception`
- `__cxa_begin_catch`
- `__cxa_end_catch`

Siehe:

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L634)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L322)

Zusätzlich gilt:

- Personality-Funktion wird nur auf Definitionspfaden gesetzt, nicht in Header-Only, siehe [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)

### Konsequenz

Exception-Semantik ist besonders empfindlich gegenüber Pfadunterschieden:

- Header-Only / Import-Kontext
- fehlende oder andere Personality-Setups
- JIT-Symbolverfügbarkeit für C++-Runtime-Funktionen

Das ist ein weiterer Bereich, in dem JIT und AOT nicht automatisch als äquivalent gelten sollten.

## 18. `compileChunkAndGetAddress()` umgeht `jitRun()`-Initialisierungslogik komplett

Betroffene Stellen:

- [compiler.cpp](../../compiler.cpp#L489)
- [compiler.cpp](../../compiler.cpp#L723)

`jitRun()` macht zusätzlich:

- Library-Preload aus `exodus.json`
- breite `AddSymbol(...)`-Registrierung für Runtime-Funktionen
- Import-Objektladung
- `aiml_set_active_context(...)`
- Aufruf von `main`

`compileChunkAndGetAddress()` macht nur:

- `compileFromString(...)`
- Engine erzeugen
- `finalizeObject()`
- Funktion lookup

### Konsequenz

Der Chunk-/Bundle-/Hot-Reload-Pfad ist nicht einfach “JIT ohne main”, sondern eine klar reduzierte Laufzeitumgebung.

Viele Probleme dort sind deshalb keine lokalen Bugs einzelner Features, sondern Folgen eines grundsätzlich anderen Execution-Setups.

## Priorisierte Problemstellen

### 1. `compileFromString(...)`

- [compiler.cpp](../../compiler.cpp#L431)

Hauptproblem:

- zu wenig Kontext im Vergleich zu `compile(...)`

### 2. `compileChunkAndGetAddress(...)`

- [compiler.cpp](../../compiler.cpp#L489)

Hauptproblem:

- benutzt den verkürzten String-Pfad und lädt die importierten Objekte/Runtime-Symbole nicht äquivalent zu `jitRun()`

### 3. `resolveImport(...)`

- [compiler.cpp](../../compiler.cpp#L1739)

Hauptproblem:

- dieser Pfad ist zentral für Datei/AOT und normalen JIT, aber praktisch außerhalb des String-/Chunk-Pfads

### 4. `libraryMode` / `ast_compiler.cpp`

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20)

Hauptproblem:

- Modusbit entscheidet über anderen Modulaufbau, aber wird nicht durch alle Compile-Pfade konsistent gesetzt

### 5. `compileChunkAndGetAddress(...)`

- [compiler.cpp](../../compiler.cpp#L489)

Zusätzliches Hauptproblem:

- kein äquivalenter Entry-Point-, Context- und Object-Link-Setup wie im normalen JIT-Lauf

### 6. `exec_impl.cpp`

- [exec_impl.cpp](../../exec_impl.cpp#L43)

Hauptproblem:

- `sys.exec`-Semantik hängt vom gesetzten aktiven Engine-/Compiler-Kontext ab und unterscheidet Shared vs Isolated JIT zusätzlich

### 7. Exception-Handling / Personality

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)

Hauptproblem:

- Invoke-/Exception-Pfade sind empfindlich gegenüber Header-Only-, JIT-Symbol- und Personality-Unterschieden

## Wahrscheinlichste reale Auswirkungen

Die Unterschiede erklären wahrscheinlich bereits beobachtete oder plausible Fehler bei:

- `sys.bundle` / `sys.exec`
- Hot-Reload
- importierten Globals
- `extern`-Symbolen
- importierten Klassen/Modellen, die im JIT anders aufgelöst werden
- Logging/Debugging mit fehlender `currentSourceFile`
- Hot-Reload/fptable-Verhalten
- `sys.exec` / `aiml_exec_code`-Kontext
- Top-Level-Initialisierung über `__aiml_init` vs `__aiml_lib_init_*`
- JIT-only oder AOT-only IR-Fehler durch unterschiedliche Verifikationspfade
- Unterschiede in `sys.bundle`-Inhaltsclosure
- Unterschiede in `sys.exec` Shared-vs-Isolated-Verhalten
- Unterschiede bei Exceptions, `invoke` und Personality-Funktionen

## Empfehlung

Die nachhaltige Richtung wäre:

1. einen gemeinsamen IR-Setup-Pfad für Datei und String schaffen
2. Import-/Header-/Sibling-Logik aus `compile()` in einen wiederverwendbaren gemeinsamen Aufbau extrahieren
3. `compileChunkAndGetAddress()` auf dieselbe Symbol-/Objekt-Ladepipeline wie `jitRun()` bringen
4. `libraryMode`, `currentSourceFile`, `resolveImportCallback`, `objectFilesToLink` in beiden Pfaden konsistent setzen

Solange das nicht passiert, sollte man JIT und AOT nicht als semantisch äquivalent behandeln.
