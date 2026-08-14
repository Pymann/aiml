# Compiler Dispatch / Lookup Cleanup

**Status**: Backlog (2026-06-07)
**Scope**: AIML compiler hot paths (`src/core/context.h`, `ast_compiler_expr.cpp`, AST dispatch)
**Motivation**: Der Compiler enthält mehrere große String- und RTTI-Dispatch-Pfade, die im laufenden Compile-Prozess sehr oft getroffen werden. Ein Teil davon ist nur unübersichtlich; ein Teil ist auch ein echter Performance-Kandidat.

## Ziel

Die größten wiederholten linearen Dispatch-Pfade im Compiler systematisch auf passendere Lookup-Mechanismen umstellen:

- String-`if`-Ketten -> tabellarischer Lookup / `unordered_map`
- Operator-Stringvergleiche -> einmalige Normalisierung auf `enum` + `switch`
- große `dynamic_cast`-Ketten -> `NodeKind` / Tag-Dispatch statt RTTI-Kaskaden

Nicht jeder Umbau soll aus Performance-Gründen passieren. Priorität haben die Pfade, die im normalen Compile-Lauf häufig aufgerufen werden.

## Priorität 1 — `CompilerContext::getLLVMType`

**Datei**: `src/core/context.h`

Aktuell steckt hier ein großes String-Dispatch-Grab für:

- primitive Typen (`i32`, `f32`, `bool`, ...)
- opake Handle-Typen (`gl_texture`, `prefab_instance`, `audio_sound`, ...)
- einige Spezialstrukturen (`vec2`, `mat4`, `mirror_event`, `ui_style`, ...)

### Problem

`getLLVMType(typeName)` wird an sehr vielen Stellen aufgerufen. Die aktuelle Form ist eine lange lineare Folge von `if (typeName == "...")`-Vergleichen.

### Zielbild

1. Spezialfälle am Anfang behalten:
   - `ptr<T>`
   - `ref<T>`
   - `list<T>` / `map<K:V>` / `fn<...>` / `iterator<T>`
2. Danach tabellarischer Lookup:
   - `unordered_map<string, llvm::Type*>` für triviale cached Typen
   - oder `unordered_map<string, std::function<llvm::Type*()>>` für lazy create
3. Komplexere Struktur-Typen (`mat4`, `mirror_event`, `ui_style`) als kleine dedizierte Builder-Funktionen auslagern

### Erwarteter Nutzen

- weniger wiederholte Stringvergleiche
- kleinerer, lesbarer Hot Path
- weniger Risiko für versehentliche Doppeldefinitionen / vergessene Alias-Pfade

## Priorität 2 — Binary Operator Dispatch

**Datei**: `src/core/ast_compiler_expr.cpp`

Aktuell wird `bin->op` mehrfach über Stringvergleiche ausgewertet:

- mathematische Struct-Ops
- float arithmetic / comparisons
- integer fallback
- pointer equality special cases

### Problem

Ein einzelner Operator (`+`, `eq`, `gte`, `pneq`, ...) wird im selben Compile-Pfad mehrfach als String geprüft.

### Zielbild

1. Operator früh auf `enum class BinOpKind` normalisieren
2. Synonyme direkt dort vereinheitlichen:
   - `+` / `add`
   - `-` / `sub`
   - `le` / `lte`
   - `ge` / `gte`
3. Danach Dispatch per `switch(opKind)`

### Erwarteter Nutzen

- weniger Stringarbeit im Hot Path
- klarerer Kontrollfluss
- weniger verstreute Alias-Behandlung

## Priorität 3 — AST Node Dispatch ohne RTTI-Kaskaden

**Dateien**:

- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- teilweise `compiler.cpp`

### Problem

Viele Pfade arbeiten mit langen `dynamic_cast`-Ketten:

- top-level decl dispatch
- expr dispatch
- stmt dispatch
- diverse Registrierungs- und Header-Pässe

### Zielbild

- `NodeKind` oder äquivalenter Tag direkt auf AST-Basis
- zentrale `switch(node->kind())`-Dispatches
- nur dort `dynamic_cast`, wo wirklich konkrete Datenfelder gebraucht werden

### Erwarteter Nutzen

- weniger RTTI-Rauschen
- klarerer Dispatch
- potentiell geringerer Overhead bei großen ASTs

## Was **nicht** primär gemeint ist

### `builtin_registry.cpp`

Der File ist groß, aber der Haupt-Dispatch ist dort bereits weitgehend tabellarisch organisiert (`builtinFuncs`, `builtinMethods`). Das ist eher ein Volumen-/Wartbarkeitsthema als ein klassisches `if/else`-Performanceproblem.

### Kleine seltene If-Ketten

Import-/Fehler-/One-shot-Logik in `compiler.cpp` ist nicht der erste Hebel. Nur umbauen, wenn sie strukturell ohnehin angefasst wird.

## Konkreter Arbeitsplan

1. `getLLVMType` zerlegen
   - Spezialparser vorne
   - primitive/opaque lookup table
   - strukturierte helper für komplexe Typen
2. `visitBinary` auf `BinOpKind` umstellen
3. Expr-/Stmt-Dispatch auf `NodeKind` vorbereiten
4. Danach top-level decl dispatch in `registerTypeDecl` / verwandten Pfaden anfassen

## Messidee

Vorher/Nachher messen auf:

- kompletter Build des AIML-Repos
- kompletter Build von DataCreatures client/server
- optional instrumentierte Counter:
  - Anzahl `getLLVMType`-Calls
  - Häufigkeit der wichtigsten Operatoren

## Erfolgskriterium

- Keine Semantikänderung im erzeugten IR / Verhalten
- spürbar weniger String-/RTTI-Dispatch in den Hot Paths
- Compiler-Code wird zugleich klarer, nicht nur „cleverer"
