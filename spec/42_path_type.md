# Builtin Path Type

## Motivation: Das String-Pfad-Problem

In den meisten Sprachen ist ein Dateipfad ein `string`. Das führt zu einer Reihe von
strukturellen Problemen:

**1. Keine Typ-Sicherheit.** Eine Funktion, die einen Pfad erwartet, bekommt einen String —
aber welchen? Einen absoluten? Einen relativen? Eine URL? Einen Dateinamen ohne Verzeichnis?
Der Compiler weiß es nicht, und der Programmierer vergisst es.

**2. Unnötiges Kopieren.** Jede String-Operation erzeugt eine neue Heap-Allokation.
`path + "/" + filename` allokiert drei Mal: erst `path + "/"`, dann das Ergebnis +
`filename`. Für etwas so Häufiges wie Pfad-Konkatenation ist das Verschwendung.

**3. Redundante Repräsentationen.** Der Pfad liegt als `std::string` in einer Map. Dann
wird er als `char*` an `open()` übergeben. Dann als `std::filesystem::path` für Operationen.
Dann zurück als `std::string` für Logging. Jede Konvertierung ist eine Kopie.

**4. Keine Normalisierung.** `"/usr/local/../local/bin"` und `"/usr/local/bin"` sind
als Strings unterschiedlich, als Pfade identisch. Ohne explizite Normalisierung landet
derselbe Pfad in unterschiedlichen Repräsentationen im System.

**5. Keine Compile-Time-Auflösung.** Statische Pfade (Konfig-Dateien, Templates,
bekannte System-Pfade) sind zur Compile-Zeit bekannt — aber werden trotzdem zur Laufzeit
immer wieder neu geparst und allokiert.

AIML löst das mit einem **ersten Klasse Pfad-Typen**: `path` ist ein Primitiv wie `i32`
oder `bool` — kein Wrapper-Struct, kein String-Alias, sondern ein eigener Typ mit
eigenen Garantien.

---

## Kernprinzipien

### 1. Interning — Ein Pfad, eine Speicheradresse

AIML interniert alle Pfade in einer globalen **Path-Intern-Table**: Zwei Pfade mit
demselben normierten Inhalt teilen *exakt denselben Speicher*. Ein `path`-Wert ist
intern nur ein 64-Bit-Zeiger in diese Tabelle — kein Heap-Buffer, keine Länge, kein
Capacity-Feld.

```
path p1 = "/usr/local/bin"   →  intern_table[42]  →  "/usr/local/bin\0"
path p2 = "/usr/local/bin"   →  intern_table[42]  (selber Pointer!)
path p3 = "/usr/local/../local/bin"  →  normiert → intern_table[42]  (selber!)
```

**Konsequenz:** Pfad-Vergleich ist ein **Pointer-Vergleich** (O(1), keine Zeichen-
schleife). Pfad-Kopie ist eine **Integer-Kopie** (8 Byte, kein malloc). Eine Pfad-Map
`map<path:T>` verwendet Pointer als Key — kein String-Hashing.

### 2. Immutabilität

Ein `path`-Wert ist unveränderlich. `path.join(...)` erzeugt einen *neuen* internierten
Pfad — ohne den alten zu modifizieren und ohne unnötige Zwischenpuffer (die Konkatenation
passiert einmalig, das Ergebnis wird sofort interniert).

### 3. Keine implizite String-Konvertierung

`path` kann nicht stillschweigend als `string` verwendet werden. Eine Funktion, die
`string` erwartet, bekommt keinen `path` — und umgekehrt. Explizite Konvertierung ist
immer sichtbar: `p.to_string()`.

### 4. Compile-Time-Interning für statische Pfade

Pfad-Literale, die zur Compile-Zeit bekannt sind, werden bereits beim Start des Programms
in die Intern-Table eingetragen — keine Laufzeit-Allokation, kein Parsing zur Laufzeit.

```yaml
# Diese beiden Pfade werden beim Programmstart EINMALIG interniert,
# danach ist set nur noch ein Integer-Store:
- decl: [cfg, path]
- set: [cfg, "/etc/myapp/config.toml"]   # compile-time interned literal
```

---

## Typ-Deklaration & Literale

```yaml
# path ist ein Primitiv — wird wie i32/bool deklariert
- decl: [p, path]

# Pfad-Literal: normaler String-Syntax, aber Zieltyp ist path → kein String allokiert
- set: [p, "/usr/local/bin"]

# Relatives Pfad-Literal
- decl: [rel, path]
- set: [rel, "config/app.toml"]

# Leerer Pfad (Null-Äquivalent)
- decl: [empty, path]    # default: path.empty (= intern_table[0])
```

### Pfad-Typ-Varianten

```yaml
# path       — allgemeiner Pfad (absolut oder relativ)
# abspath    — Subtyp: garantiert absolut (Compiler-Fehler wenn relativ)
# relpath    — Subtyp: garantiert relativ
# dirpath    — Subtyp: endet nicht mit einer Dateikomponente (nur Verzeichnis)
# filepath   — Subtyp: hat eine Dateikomponente (Blatt-Knoten)

- decl: [abs, abspath]
- set: [abs, "/usr/local/bin"]     # OK
- set: [abs, "relative/path"]      # COMPILE ERROR: abspath requires absolute path

- decl: [dir, dirpath]
- set: [dir, "/usr/local/bin/"]    # OK (trailing slash oder explizit als dir)
- set: [dir, { call: [p.parent] }] # OK: parent() gibt immer dirpath zurück
```

---

## Operationen

### Komposition

```yaml
# path.join — verbindet Pfade, normiert das Ergebnis, interniert einmalig
- decl: [base, path]
- set: [base, "/usr/local"]

- decl: [full, path]
- set: [full, { call: [base.join, "bin", "aiml"] }]
# Ergebnis: "/usr/local/bin/aiml" — KEIN Zwischenpuffer für "/usr/local/bin"

# Operator / als Syntaxzucker für join (Compile-Time-Desugaring)
- set: [full, { path: [base, "bin", "aiml"] }]   # äquivalent zu join

# join mit path-Variablen (nicht nur Strings)
- decl: [suffix, path]
- set: [suffix, "bin/aiml"]
- set: [full, { call: [base.join, suffix] }]
# Wenn suffix absolut wäre, würde base verworfen (POSIX-Semantik)
```

### Zerlegung

```yaml
- decl: [p, path]
- set: [p, "/usr/local/bin/aiml.elf"]

# Verzeichnis-Komponente (parent)
- decl: [dir, path]
- set: [dir, { call: [p.parent] }]          # "/usr/local/bin"

# Dateiname (letzte Komponente)
- decl: [name, string]
- set: [name, { call: [p.name] }]           # "aiml.elf"

# Dateiname ohne Extension
- decl: [stem, string]
- set: [stem, { call: [p.stem] }]           # "aiml"

# Extension (mit Punkt)
- decl: [ext, string]
- set: [ext, { call: [p.extension] }]       # ".elf"

# Alle Komponenten als Liste
- decl: [parts, list<string>]
- set: [parts, { call: [p.parts] }]         # ["usr", "local", "bin", "aiml.elf"]

# Anzahl Komponenten
- decl: [depth, i32]
- set: [depth, { call: [p.depth] }]         # 4

# Root-Komponente
- decl: [root, path]
- set: [root, { call: [p.root] }]           # "/"  (oder "C:\" auf Windows)
```

### Eigenschaften

```yaml
- decl: [p, path]
- set: [p, "/usr/local/bin/aiml"]

- decl: [is_abs, bool]
- set: [is_abs, { call: [p.is_absolute] }]  # true

- decl: [is_rel, bool]
- set: [is_rel, { call: [p.is_relative] }]  # false

- decl: [is_empty, bool]
- set: [is_empty, { call: [p.is_empty] }]   # false

# Normierter Pfad (.. und . aufgelöst, doppelte Slashes entfernt)
# Passiert automatisch beim Internieren — aber explizit aufrufbar:
- decl: [norm, path]
- set: [norm, { call: [p.normalize] }]      # identisch zu p (schon normiert)

- decl: [messy, path]
- set: [messy, "/usr//local/../local/./bin"]
- set: [norm, { call: [messy.normalize] }]  # "/usr/local/bin"
# (normalize beim set bereits automatisch — norm == intern("/usr/local/bin"))

# Relativen Pfad von base zu p berechnen
- decl: [base, path]
- set: [base, "/usr/local"]
- decl: [rel, path]
- set: [rel, { call: [p.relative_to, base] }]   # "bin/aiml"
```

### Vergleich

```yaml
# Vergleich ist O(1) Pointer-Vergleich
- decl: [a, path]
- decl: [b, path]
- set: [a, "/usr/local/bin"]
- set: [b, "/usr/local/../local/bin"]   # normiert → selber Pointer wie a

- decl: [eq, bool]
- set: [eq, { binary: [a, ==, b] }]    # true — kein String-Vergleich, nur ==ptr

# Lexikografische Ordnung (für Sortierung in Listen)
- decl: [lt, bool]
- set: [lt, { binary: [a, <, b] }]
```

### Konvertierung

```yaml
# Explizit zu string (einzige erlaubte String-Konvertierung)
- decl: [s, string]
- set: [s, { call: [p.to_string] }]         # allokiert nur wenn nötig (lazy)

# String → path (parst und normiert)
- decl: [from_str, path]
- set: [from_str, { call: [path.from_string, s] }]

# Zu OS-nativer Darstellung (auf Windows: Backslashes)
- decl: [os_str, string]
- set: [os_str, { call: [p.to_os_string] }]
```

---

## Filesystem-Integration

Der `io`-Namespace akzeptiert ausschließlich `path`, niemals `string`:

```yaml
# KORREKT:
- decl: [p, path]
- set: [p, "/etc/hosts"]
- decl: [content, string]
- set: [content, { call: [io.read_file, p] }]

# COMPILE ERROR — string ist kein path:
- set: [content, { call: [io.read_file, "/etc/hosts"] }]
#                                        ^^^^^^^^^^^^ Type mismatch: string != path
# Ausnahme: path-Kontext-Inferenz — Literale im path-Kontext werden als path geparst:
# (wenn io.read_file path erwartet, ist "/etc/hosts" ein path-Literal, kein string-Literal)

# Filesystem-Operationen
- decl: [exists, bool]
- set: [exists, { call: [io.exists, p] }]

- decl: [is_dir, bool]
- set: [is_dir, { call: [io.is_dir, p] }]

- decl: [is_file, bool]
- set: [is_file, { call: [io.is_file, p] }]

- decl: [size, i64]
- set: [size, { call: [io.file_size, p] }]

# Verzeichnis-Inhalt (gibt list<path> zurück — keine Strings!)
- decl: [entries, list<path>]
- set: [entries, { call: [io.list_dir, p] }]
- for_each:
    var: entry
    in: entries
    body:
      - print: ["${entry}"]     # path hat eine implizite Display-Darstellung in Interpolation

# Verzeichnis erstellen
- call: [io.mkdir, p]
- call: [io.mkdir_all, p]    # mkdir -p

# Umbenennen / Verschieben (atomic auf POSIX)
- decl: [dest, path]
- set: [dest, "/etc/hosts.bak"]
- call: [io.rename, p, dest]

# Löschen
- call: [io.remove, p]
- call: [io.remove_all, p]   # rm -rf (Verzeichnisbaum)
```

### File-Descriptor-Caching (`file_ref`)

Ein `file_ref` kombiniert einen `path` mit einem offenen File-Descriptor. So muss
der Pfad nicht für jeden I/O-Aufruf neu aufgelöst werden (kein wiederholtes `open()`):

```yaml
# file_ref — path + offener fd, kein erneutes Lookup beim Lesen/Schreiben
- decl: [f, file_ref]
- set: [f, { call: [io.open, p, "r"] }]   # öffnet einmal, hält fd offen

- decl: [line, string]
- set: [line, { call: [f.read_line] }]

- call: [f.seek, 0]
- call: [f.write, "new content"]

# path aus file_ref zurückbekommen (zero-cost, ist gespeichert)
- decl: [orig_path, path]
- set: [orig_path, { call: [f.path] }]

- call: [f.close]
```

---

## Pfad-Sicherheit

### Traversal-Schutz

```yaml
# path.safe_join — lehnt Komponenten ab, die außerhalb des base-Verzeichnisses führen
# Schützt vor Path-Traversal-Angriffen (../../etc/passwd)

- decl: [base, path]
- set: [base, "/var/www/uploads"]

- decl: [user_input, string]
- set: [user_input, "../../etc/passwd"]    # Angreifer-Eingabe

- decl: [safe, path]
- set: [safe, { call: [base.safe_join, user_input] }]
# RUNTIME ERROR: "../../etc/passwd" escapes base "/var/www/uploads"

# Explizites Erlauben von relativen ../-Komponenten (opt-in):
- set: [safe, { call: [base.join_unchecked, user_input] }]  # opt-in: kein Schutz
```

### Berechtigungs-Checks

```yaml
# Prüfe ob der aktuelle Prozess Zugriff hat (nutzt access(2))
- decl: [can_read, bool]
- set: [can_read, { call: [io.can_read, p] }]

- decl: [can_write, bool]
- set: [can_write, { call: [io.can_write, p] }]

- decl: [can_exec, bool]
- set: [can_exec, { call: [io.can_exec, p] }]
```

---

## Pfade in Datenstrukturen

Da `path` intern ein 8-Byte-Pointer ist, sind Pfade in Collections extrem effizient:

```yaml
# map<path:T> — Lookup ist O(1) Pointer-Vergleich, kein String-Hashing
- decl: [cache, map<path:string>]
- call: [cache.set, p, "cached content"]
- decl: [hit, string]
- set: [hit, { call: [cache.get, p] }]   # O(1), kein String-Compare

# list<path> — 8 Byte pro Eintrag (kein char*, kein length, kein capacity)
- decl: [paths, list<path>]
- call: [paths.push, "/usr/local/bin/aiml"]
- call: [paths.push, "/usr/local/lib/libaiml.so"]

# set<path> — deduplizierung per Pointer (intern garantiert eindeutig)
- decl: [visited, set<path>]
- call: [visited.add, p]
- decl: [seen, bool]
- set: [seen, { call: [visited.contains, p] }]   # O(1)
```

---

## Plattform-Abstraktion

`path` ist intern platform-agnostisch: `/` als Separator auf allen Plattformen,
`to_os_string()` konvertiert für Plattform-APIs:

```yaml
# Plattform-spezifische bekannte Pfade
- decl: [home, path]
- set: [home, { call: [path.home] }]         # ~/  (expandiert aus $HOME / USERPROFILE)

- decl: [tmp, path]
- set: [tmp, { call: [path.temp_dir] }]      # /tmp  oder  C:\Users\X\AppData\Local\Temp

- decl: [cfg, path]
- set: [cfg, { call: [path.config_dir, "myapp"] }]
# Linux: ~/.config/myapp  |  macOS: ~/Library/Application Support/myapp  |  Win: AppData\Roaming\myapp

- decl: [exe, path]
- set: [exe, { call: [path.current_exe] }]   # Pfad des laufenden Programms (readlink /proc/self/exe)

- decl: [cwd, path]
- set: [cwd, { call: [path.cwd] }]           # Aktuelles Arbeitsverzeichnis
```

---

## Interner Aufbau (Implementation)

```
path-Wert (Laufzeit):
  ┌─────────────────────────────┐
  │  u64: pointer → intern_entry │   8 Byte — das ist alles
  └─────────────────────────────┘

intern_entry (Intern-Table):
  ┌──────────┬─────────┬─────────────────────────────┐
  │  u32 len │ u32 hash│  char[] normalized_utf8\0   │
  └──────────┴─────────┴─────────────────────────────┘

Intern-Table:
  - Hash-Map: FNV-1a(normalized_str) → intern_entry*
  - Lock-freies Insert (CAS auf Bucket-Pointer)
  - Einträge niemals gelöscht (Pfade sind Programmlebensdauer-Daten)
  - Compile-Time-Pfade werden in Read-Only-Sektion eingebettet (kein Laufzeit-malloc)
```

**Vergleich mit string:**

| Operation          | `string`          | `path`                  |
|--------------------|-------------------|-------------------------|
| Kopie              | malloc + memcpy   | 8-Byte Integer-Copy     |
| Vergleich          | O(n) memcmp       | O(1) Pointer-Vergleich  |
| Hash               | O(n) berechnen    | Bereits im Entry gespeichert |
| Join               | 2+ Allokationen   | 1 Allokation (Ergebnis) |
| Als Map-Key        | String-Hash + cmp | Pointer-Hash + ==       |
| Normalisierung     | Manuell / nie     | Automatisch beim Intern |
| Typ-Sicherheit     | Keine             | Compile-Time            |
