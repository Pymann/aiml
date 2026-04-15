# Globber Builtin Implementation Plan

Stand: 2026-04-12

Basis:

- [42_1_globber.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/42_1_globber.md)
- [42_path_type.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/42_path_type.md)

Ziel:

- ein echter Builtin-`Globber`
- Wildcards `*`, `?`, `**`
- inkrementell, directory-by-directory
- begrenzte Allokation pro Schritt
- keine Vollmaterialisierung wie `glob.glob(...)`

---

## 1. Ausgangslage im bestehenden AIML-Code

### 1.1 Was schon vorhanden ist

**Pfad-Runtime**

- [path_runtime.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/path_runtime.h)
- [path_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/path_runtime.cpp)

Bereits vorhanden:

- `aiml_path_intern`
- `aiml_path_join`
- `aiml_path_parent`
- `aiml_path_name`
- `aiml_path_stem`
- `aiml_path_extension`
- `aiml_path_root`
- `aiml_path_relative_to`
- `aiml_io_exists`
- `aiml_io_is_dir`
- `aiml_io_is_file`
- `aiml_io_read_file`
- `aiml_io_write_file`
- `aiml_io_list_dir_fill`

**Compiler/Builtins**

- [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)

Bereits vorhanden:

- `path` als Builtin-Klasse
- `path`-Namespace (`from_string`, `cwd`, `home`, ...)
- `io.list_dir(path) -> list<path>`
- etablierte Builtin-Klassen mit opaque Runtime-Handles wie:
  - `process`
  - `socket_handle`

**Tests**

- [test_path.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_path.aiml)
- [test_io_write_listdir.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_io_write_listdir.aiml)

**AIML-seitiger Vorläufer**

- [tul/train/main.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/tul/train/main.aiml)

Dort existiert bereits `expand_glob(...)` als Workaround auf Basis von:

- `io.list_dir`
- `string.starts_with`
- `string.ends_with`

Das ist wichtig, weil die gewünschte Semantik im Projekt bereits praktisch gebraucht wird.

### 1.2 Was noch fehlt

Es gibt aktuell **keinen**:

- Runtime-Typ `Globber`
- C-API für inkrementelles Glob-State
- Compiler-Registrierung für `Globber`
- `io.globber(...)`-Konstruktor
- Tests für `*`, `?`, `**`

---

## 2. Architekturentscheidung

Die bestehende Codebasis legt drei mögliche Richtungen nahe:

1. Reines AIML-Glob auf `io.list_dir`
2. `io.glob(pattern) -> list<path>`
3. echter zustandsbehafteter Builtin-Typ

Die Spezifikation hat Variante 3 festgezogen. Das ist auch technisch die richtige Wahl.

### Warum nicht `io.glob(...) -> list<path>`

- große Bäume allokieren zu viel auf einmal
- keine sichtbare Traversal-Position
- kein natürliches Backpressure
- `**` würde leicht unkontrolliert teuer

### Warum `Globber`

- passt zur vorhandenen Builtin-Handle-Strategie
- passt zur `path`-Runtime
- inkrementelle Semantik ist klar testbar
- große Dateibäume bleiben beherrschbar

---

## 3. Ziel-API

### 3.1 Builtin-Klasse

```yaml
- class:
    name: Globber
    members:
      - [pattern, path, 0, { access: public }]
      - [location, path, 0, { access: public }]
      - [findings, list<path>, 0, { access: public }]
      - [done, i32, 0, { access: public }]
    methods:
      - def:
          name: init
          access: public
          args: [[pattern, path], [max_findings_per_step, i32]]
          return: void
      - def:
          name: next
          access: public
          args: []
          return: i32
```

### 3.2 Namespace-Konstruktor

```yaml
- decl: [g, Globber]
- set: [g, { call: [io.globber, "docs/**/*.md", 256] }]
```

`io.globber(...)` ist nur ein Convenience-Konstruktor und muss semantisch
äquivalent zu `decl + init(...)` sein.

---

## 4. Technische Kernentscheidung: Runtime-Handle statt reinem Struct-State

Die bestehende Builtin-Strategie in [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp) benutzt für zustandsbehaftete Builtins opaque Runtime-Pointer:

- `process`
- `socket_handle`

Für `Globber` sollte dasselbe Muster verwendet werden.

### Empfohlene interne Form

**Öffentliche AIML-Sicht**

- `pattern: path`
- `location: path`
- `findings: list<path>`
- `done: i32`

**Interner Runtime-State**

```cpp
struct AimlGlobber;
```

mit intern z. B.:

- `pattern_raw`
- `max_findings_per_step`
- `std::deque<GlobTask>` oder `std::vector<GlobTask>`
- current overflow state für große Einzelordner
- parsed pattern segments
- `done`

Wichtig:

- der Traversal-Queue/Stack gehört **nicht** in AIML-Felder
- nur sichtbarer Snapshot-Zustand gehört in AIML-Felder

---

## 5. Runtime-Design

### 5.1 Neue C-API in `path_runtime.h`

Neue Funktionen:

```cpp
void* aiml_globber_new(void);
void  aiml_globber_free(void* g);
int   aiml_globber_init(void* g, const char* pattern, int max_findings);
int   aiml_globber_next(void* g, const char** out_location, void* list_ptr);
int   aiml_globber_done(void* g);
const char* aiml_globber_pattern(void* g);
```

Optional:

```cpp
int aiml_glob_match_segment(const char* pattern_seg, const char* name);
```

### 5.2 Interner Traversal-State

Empfohlene Struktur:

```cpp
struct GlobTask {
    std::filesystem::path dir;
    size_t segment_index;
};

struct DirCursor {
    std::filesystem::path dir;
    size_t segment_index;
    std::vector<std::filesystem::directory_entry> entries;
    size_t offset;
};

struct AimlGlobber {
    std::string pattern;
    std::vector<std::string> segments;
    int max_findings_per_step;
    int done;
    std::deque<GlobTask> pending;
    std::optional<DirCursor> active_dir;
};
```

### 5.3 Matching-Semantik

Pro Segment:

- `*` -> beliebig viele Zeichen
- `?` -> genau ein Zeichen
- `**` -> rekursive Directory-Wildcard

Wichtig:

- Matching geschieht **pro Segment**
- nicht über den Gesamtpfadstring

### 5.4 `next()`-Semantik in der Runtime

`aiml_globber_next(...)`:

1. leert die vom Compiler vorbereitete `list<path>`
2. setzt `out_location`
3. verarbeitet genau einen Directory-Step
4. schreibt Treffer in die Ergebnisliste
5. hält `findings.len <= max_findings_per_step`
6. gibt `1` bei Fortschritt, `0` bei vollständigem Ende

Wenn ein einzelner Ordner mehr Treffer hat als `max_findings_per_step`:

- `location` bleibt auf diesem Ordner
- die Runtime merkt sich einen Cursor
- der nächste `next()`-Aufruf setzt dort fort

Das ist wichtig, sonst wäre der Per-Step-Limit bei großen Ordnern nur Schein.

---

## 6. Compiler- und Builtin-Integration

### 6.1 Neue Builtin-Klasse in `builtin_registry.cpp`

Analog zu `socket_handle` und `process`:

- `ctx.classes["Globber"]`
- `isBuiltin = true`

Methoden:

- `init(pattern, max_findings)`
- `next()`

Feldzugriffe:

- `pattern`
- `location`
- `findings`
- `done`

### 6.2 Schwieriger Punkt: Sichtbare Felder + hidden handle

Für `Globber` reicht ein nackter `i8*` wie bei `socket_handle` nicht, weil
die Spezifikation sichtbare Member fordert.

Darum braucht es entweder:

1. einen echten LLVM-Struct-Typ für `Globber` mit:
   - hidden handle pointer
   - visible AIML fields

oder

2. eine bestehende Mechanik für builtin-backed class layouts wiederverwenden, falls vorhanden

Empfehlung:

- `Globber` als normaler Klassenlayout-Typ mit erstem hidden Feld:
  - `__globber_handle: i8*` (private/internal)
- sichtbare Felder regulär daneben

Dann können Methoden:

- den Handle nutzen
- und danach `location`, `findings`, `done`, `pattern` synchronisieren

### 6.3 `io.globber(...)`

In `io`-Namespace ergänzen:

```cpp
io.builtinFuncs["globber"]
```

Semantik:

- allokiert einen `Globber`
- ruft `init(...)`
- gibt das Objekt zurück

Wenn Konstruktion von Klassenwerten im Builtin-Pfad zu teuer ist:

- zuerst nur `decl + g.init(...)` unterstützen
- `io.globber(...)` in zweiter Teilphase ergänzen

---

## 7. Testplan

### 7.1 Neue Tests

Neue Testdatei:

- `/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_globber.aiml`

Pflichtfälle:

1. `*.txt`
   - flaches Matching im aktuellen Ordner
2. `test_?.json`
   - `?`-Wildcard
3. `docs/*.md`
   - segmentiertes Matching mit Basisverzeichnis
4. `docs/**/*.md`
   - rekursive Traversal
5. `max_findings_per_step = 1`
   - Overflow-Cursor für großen Einzelordner
6. kein Treffer
   - `done` korrekt, `findings` leer
7. ungültiges oder nicht lesbares Startverzeichnis
   - sauberer Endzustand statt Crash

### 7.2 Regressionen

Bestehende Tests müssen weiter laufen:

- [test_path.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_path.aiml)
- [test_io_write_listdir.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_io_write_listdir.aiml)

### 7.3 TUL-Smoke-Test

Bestehenden AIML-Workaround in [tul/train/main.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/tul/train/main.aiml) später ersetzen oder parallel prüfen:

- `"docs/*.md"` mit altem `expand_glob(...)`
- `"docs/*.md"` mit neuem `Globber`

Das ist ein guter Praxistest.

---

## 8. Thread-Safety

Aus [47_thread_safety.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/47_thread_safety.md) folgt:

- `path`-Interning ist bereits global geschützt
- `io.list_dir` ist als reine Funktion einsortiert

Für `Globber` gilt:

- ein einzelner `Globber`-Handle sollte als **Kategorie 3 / per-handle**
  behandelt werden
- also **nicht implizit thread-safe**
- parallele Nutzung desselben `Globber`-Objekts aus mehreren Threads ist kein Ziel

Das vereinfacht die Runtime deutlich.

---

## 9. Implementierungsreihenfolge

### Phase 1 — Runtime-Grundlage

1. `path_runtime.h`
   - C-API für `AimlGlobber`
2. `path_runtime.cpp`
   - `AimlGlobber`-State
   - Segmentparser
   - Segmentmatcher für `*` / `?`
   - Traversal-Engine für `**`
   - bounded `next()`

### Phase 2 — Compiler/Builtin

3. `builtin_registry.cpp`
   - `Globber` Builtin-Klasse
   - Methoden `init`, `next`
   - sichtbare Felder + hidden handle
4. optional `io.globber(...)`

### Phase 3 — Tests

5. `test_globber.aiml`
6. ggf. `test_globber_recursive.aiml` wenn der erste Test zu groß wird
7. TUL-Smoke-Test oder temporärer Beispieltest

### Phase 4 — Doku

8. `aiml_reference_guide.md`
   - `Globber` ergänzen
9. ggf. `00_reference_guide.md`
   - Unterkapitel bereits gelistet, nur wenn nötig Kurzverweis ergänzen

---

## 10. Nicht-Ziele für V1

Nicht Teil der ersten Implementierung:

- character classes `[abc]`
- brace expansion `{a,b}`
- shell-expansion von `~`
- symlink policy beyond default filesystem iteration
- Sortiergarantie über Plattformen hinaus
- parallele Traversal eines einzelnen `Globber`

---

## 11. Definition of Done

Das Feature ist fertig, wenn:

1. `Globber.init(pattern, max_findings_per_step)` funktioniert
2. `Globber.next()` directory-by-directory iteriert
3. `*`, `?`, `**` korrekt unterstützt werden
4. `findings.len` pro Schritt begrenzt ist
5. große Einzelordner ohne Vollmaterialisierung verarbeitet werden
6. `location`, `done`, `pattern` korrekt synchronisiert werden
7. neue Tests grün sind
8. bestehende `path`- und `io.list_dir`-Tests grün bleiben

---

## 12. Empfohlene erste Umsetzungsentscheidung

Die riskanteste technische Stelle ist **nicht** das Wildcard-Matching, sondern
die Frage, wie `Globber` im Compilerlayout repräsentiert wird.

Darum sollte der erste konkrete Implementation Spike sein:

1. hidden handle + sichtbare Felder als Builtin-Klasse sauber modellieren
2. danach Runtime-`next()` anschließen

Wenn dieser Teil sauber sitzt, ist der Rest vergleichsweise geradlinig.
