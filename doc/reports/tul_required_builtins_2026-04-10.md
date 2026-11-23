# Fehlende AIML-Builtins fuer tul GenAI

Datum: 2026-04-10

## Kontext

Der `tul` GenAI-Implementierungsplan benoetigt String- und Math-Operationen,
die in AIML aktuell nicht als Builtins existieren. Ohne diese muesste jede
Einzeloperation manuell als zeichenweise while-Schleife implementiert werden,
was den Code unnoetig aufblaeht.

## Ist-Zustand: Was existiert bereits

String-Methoden sind in `ast_compiler_call.cpp` ab Zeile 706 hardcoded:
- `string.substr(start)` und `string.substr(start, len)` — via strdup/strndup
- `string.starts_with(prefix)` — via strncmp
- `string.index_of(needle)` — via strstr
- `string.len` — als Property (strlen)

Math-Namespace in `builtin_registry.cpp` ab Zeile 2911:
- `math.sqrt` existiert bereits (via `sqrtf`)
- `math.sin`, `math.cos`, `math.exp` etc. existieren

## Fehlende String-Builtins

### 1. `string.lower() -> string`

Alle ASCII-Grossbuchstaben in Kleinbuchstaben umwandeln.

Benoetigt fuer: Tokenizer-Normalisierung. Jede Eingabe muss vor dem Tokenisieren
normalisiert werden.

Implementierung: Analog zu bestehenden String-Methoden in `ast_compiler_call.cpp`:
- C-Runtime: `aiml_string_lower(const char*) -> char*` — `strdup` + `tolower`-Loop
- Compiler: neuer Branch `methodName == "lower"` im `className == "string"` Block
- JIT: `AddSymbol("aiml_string_lower", ...)`

### 2. `string.split(delimiter) -> list<string>`

String an Delimiter aufteilen und als `list<string>` zurueckgeben.

Benoetigt fuer: Tokenizer-Split (Whitespace), Tag-Parsing, allgemeine Textverarbeitung.
Ohne diesen Builtin muss jeder Tokenizer eine manuelle while-Schleife mit
`index_of` + `substr` bauen — das sind ~15 Zeilen pro Aufrufstelle.

Implementierung: Komplexer als andere String-Methoden wegen `list<string>` Rueckgabe.
- C-Runtime: `aiml_string_split(const char* str, const char* delim, int32_t* out_count) -> char**`
  Gibt Array of `char*` zurueck + Elementanzahl via out-Pointer.
- Compiler: `initList` + fill-Loop, analog zum `io.list_dir` Pattern in `ast_compiler_call.cpp`
  (stack-alloc AimlList, `aiml_string_split` aufrufen, Elemente per `ListImpl::push` einfuegen)
- Rueckgabe: `list<string>` als Stack-Struct, memcpy in Ziel-Alloca (wie `visitDeclStmt` fuer listen)
- JIT: `AddSymbol("aiml_string_split", ...)`

### 3. `string.trim() -> string`

Fuehrende und abschliessende Whitespace-Zeichen entfernen.

Benoetigt fuer: Normalisierung vor Tokenisierung, robuste Eingabeverarbeitung.

Implementierung:
- C-Runtime: `aiml_string_trim(const char*) -> char*` — skip leading whitespace,
  find last non-whitespace, `strndup(start, len)`
- Compiler: neuer Branch `methodName == "trim"` im String-Block
- JIT: `AddSymbol("aiml_string_trim", ...)`

### 4. `string.replace(old, new) -> string`

Alle Vorkommen von `old` durch `new` ersetzen.

Benoetigt fuer: Satzzeichenbehandlung (z.B. Komma/Punkt entfernen oder separieren).

Implementierung:
- C-Runtime: `aiml_string_replace(const char*, const char* old, const char* new) -> char*`
  Iterativ mit `strstr` + `memcpy` in wachsenden Buffer
- Compiler: neuer Branch `methodName == "replace"` im String-Block, 2 Args
- JIT: `AddSymbol("aiml_string_replace", ...)`

## Prioritaet

| Builtin | Prioritaet | Grund |
|---------|-----------|-------|
| `string.split` | Kritisch | Ohne das ist Tokenisierung extrem umstaendlich |
| `string.lower` | Kritisch | Normalisierung ist Pflicht |
| `string.trim` | Hoch | Robuste Eingabeverarbeitung |
| `string.replace` | Mittel | Satzzeichen-Handling, kann in Phase 1 ggf. umgangen werden |
| ~~`math.sqrt`~~ | ~~Existiert~~ | Bereits als `sqrtf` in math-Namespace registriert |

## Implementierungsort

Alle neuen String-Methoden:
- C-Funktionen: neue Datei `src/sys/string_runtime.h` + `src/sys/string_runtime.cpp`
- Compiler-Branches: `src/core/ast_compiler_call.cpp` im `className == "string"` Block (ab Zeile 707)
- JIT-Symbole: `compiler.cpp` — `AddSymbol` fuer jede neue C-Funktion
- exodus.json: `string_runtime.cpp` in Sources aufnehmen

## Implementierungsreihenfolge

1. `string.lower` + `string.trim` (einfachste, char* -> char*, kein Listenhandling)
2. `string.replace` (iterativer Buffer, mittelschwer)
3. `string.split` (list-Rueckgabe, aufwaendigstes Pattern)

## Aufwand

Geschaetzt 2-3h fuer alle 4 Builtins inkl. Tests.
`string.split` ist am aufwaendigsten wegen list-Rueckgabe.
