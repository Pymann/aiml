# Modules and Namespaces

## 1. Defining a Namespace
The `namespace` command sets the namespace for the **entire file**.
It is typically the first command in a file.

**Reserved namespaces** — the following prefixes cannot be used as user-defined namespaces;
any such namespace declaration raises a CompilationError:
- `sys`
- `time`
- `io`

```yaml
# Sets namespace for all subsequent definitions in this file
- namespace: my.math

- def:
    name: calc  # Becomes "my.math.calc"
    ...
```

## 2. Symbol Visibility

- **Namespace-level symbols** (top-level functions, classes, models, enums) are **public** by default.
- **Class members** are **private** by default (see `03_modifiers.md`).
- Duplicate FQNs across imported files raise `NamespaceCollisionError` at compile time.

### 2.1 Visibility Scope — the import closure

A file sees its own declarations plus the public declarations of every file in its
**transitive import closure** (the files it imports, the files those import, …) —
and nothing else.  Being part of the same program is not enough: if `main.aiml`
imports both `a.aiml` and `b.aiml`, then `a.aiml` sees `b.aiml` only if it imports it
(directly or transitively).  Using a symbol from outside the closure is a compile
error in the using file.

Consequently a file compiles to the same object in every program that includes it;
its compiled form depends only on its own closure (see the cache key,
`37_parsing_and_jit_internals.md` §3).

Class groups follow the same rule; for `groupmethods:` files see
`58_classgroups.md` §1.1.

## 3. Importing Modules
The `import` command loads another AIML file.
- Each file is compiled **at most once** — the `__aiml_cache` tracks already-compiled files.
- Circular imports are therefore **allowed**. The cache prevents infinite loops.
- Import paths are **relative to the importing file's directory**. `../` is supported.
- **Absolute paths in import statements are forbidden.**
- Imported symbols must be accessed via their **Fully Qualified Name (FQN)**.

```yaml
- import: "utils/math.aiml"

- def:
    name: main
    body:
      - call: [my.math.calc]
```

## 4. Execution vs Compilation

### 4.1 Direct Execution (`aiml main.aiml -r` or `aiml main.aiml --run`)
- **Behavior**: JIT compilation with Object Caching.
- **Imports**:
    1. Imported files are checked against valid cache in `__aiml_cache`.
    2. If missing or outdated, compiled to native object files (`.o`).
    3. The main file is compiled to LLVM IR in memory.
    4. Imported object files are loaded into the JIT execution engine.
- **Result**: Hybrid execution — dependencies pre-compiled, main script JIT-compiled.

### 4.2 Object Compilation (`aiml main.aiml -o output.o`)
- **Behavior**: Separate Compilation (used by `exodus build`).
- **Imports**:
    1. Checks if imported file needs recompilation (missing or older `.o`).
    2. Compiles imported file to a native object file if needed.
    3. Main file compiled to `.o`.
- **Linking**: Handled by the build system (exodus). The compiler writes a `.deps` sidecar
  file listing all transitive import object files. exodus reads this and links all objects
  with `g++`/`clang` plus libraries from `exodus.json` (e.g. `libaiml.so`, `libssl`, etc.).
- **Note**: Standalone `aiml file.aiml` (without `-r` or `-o`) only generates `.ll` (LLVM IR).
  Full AOT binaries require `exodus build` with an `exodus.json` config.

## 5. Paths and Caching

- **Import Paths**: Relative to the directory of the importing file.
    - `import: "src/utils/math.aiml"` → resolved from the importing file's directory.
- **JIT Cache Directory** (`-r` mode only): `__aiml_cache/` in the CWD (or `$AIML_CACHE` if set).
  Objects are content-keyed; key, layout, hit rule and publishing are specified in
  `37_parsing_and_jit_internals.md` §3.
- **AOT Cache** (`-o` mode): No `__aiml_cache` used. Import objects are stored in the build
  directory (same as the main `.o` file). The build system (exodus) handles incremental
  compilation via file timestamps.
- **Dependency Tracking**: JIT mode uses content hashes (spec/37 §3); AOT mode relies on exodus timestamps.

## 6. Scopes and Module-Level State

### 6.1 Scope Hierarchy

AIML has three scope levels:

| Scope | Lifetime | Visible to |
|-------|----------|------------|
| **Module-level** (top-level `decl:`) | Program lifetime | All functions in the same file |
| **Function-level** (inside `def:`) | Function call | That function only |
| **Block-level** (inside `if:/while:/for:`) | Block execution | That block only |

Inner scopes may not shadow outer names. Redeclaring a visible name in an inner
scope is a `CompilationError`.

### 6.2 Module-Level Declarations in Imported Files

When a file is imported, it is compiled as a **library** (separate `.o`).
Top-level `decl:` statements in the imported file become **module-level globals**:

```yaml
# utils/state.aiml
- namespace: state

- decl: [counter, i32]        # zero-initialized global

- def:
    name: inc
    body:
      - set: [counter, { +: [counter, 1] }]

- def:
    name: get
    return: i32
    body:
      - return: counter
```

```yaml
# main.aiml
- import: "utils/state.aiml"

- def:
    name: main
    body:
      - call: [state.inc]
      - call: [state.inc]
      - print: "${ { call: [state.get] } }"   # prints 2
```

**Rules:**

1. Module-level globals in libraries are **zero-initialized** at program start
   (integers = 0, pointers/strings = null).
2. Static initializers (`decl: [x, string, "value"]`) in imported files are
   **not guaranteed to execute** before the importer's `main`. Use explicit
   setter functions to configure library state.
3. Module-level globals have **internal linkage** — they are private to their
   compilation unit and do not collide with globals of the same name in other files.
4. Functions within the same file can freely read and write module-level globals.
5. **A module-level global is reported as a WARNING** (`AIML_LINT_GLOBALS=0` silences it).
   State belongs in a class or a classgroup instance; a global has no owner, and an **owning**
   global (`ptr<T>`) is the worst case of that — nothing says who frees it, and passing it to a
   function that takes `ptr<T>` moves it out from under every other reader.

   > Measured, not feared: in cosmos the client kept its live snapshots in
   > `g_live_cluster` / `g_live_agents` / `g_token_list`. Three call sites passed such a global
   > into an owning `ptr<T>` parameter; the parameter's drop freed the box and the global
   > dangled. Two of them crashed the WASM client (`RunsScreen_draw_grid`,
   > `LiveTable_agents`), the third was found by the compiler only after the ownership check
   > was repaired (2026-10-13/14). The server, which had been migrated to a classgroup
   > (doc/spec/93), had **two** globals left and neither could produce this failure.

   **A `const` primitive is exempt** (`i8`…`u64`, `f32`, `f64`, `bool`, `string`, `char`): it is
   never assigned, never passed on and owns nothing — a named literal, which is the opposite of
   the problem. Warning about it would punish naming a constant and reward a bare number in the
   code. A **non-const** primitive still warns: a writable global is shared mutable state even
   when it is only an `i32`.

   The warning is a **smell, not an error**: a global is still legal, and a few are legitimate —
   the dispatcher's per-request `g_current_peer_id` is the example (doc/spec/93 F93-7). The
   warning names the file and the variable so the exception is a decision someone made, not a
   habit nobody noticed.

#### `singleton: true` — der erklärte Anker eines Rückrufs

`engine.run(win, update, render)` nimmt **freie** Funktionen (`fn<f32:void>`,
spec/44_1 §4.1): ein Rückruf trägt keinen Zustand, und im Browser gehört die
Schleife ohnehin dem Wirt (`emscripten_set_main_loop`).  Also braucht jedes
Fenster-Programm **einen** Anker auf Modulebene — in cosmos `g_cosmos_app`, in
9rooms `g_app`.  Das ist keine Nachlässigkeit, sondern die Form, die die
Schnittstelle vorschreibt; eine Warnung, die keinen Ausweg nennt, trainiert nur
das Wegsehen.

```yaml
- decl: [g_cosmos_app, ptr<CosmosApp>, null, { singleton: true }]
```

- **F21-S1** `singleton: true` erklärt einen Modul-Global zum **einzigen Anker**
  des Programms: er wird einmal gesetzt und lebt, solange das Programm lebt.
  Die Warnung aus §6.2 entfällt für ihn — nur für ihn, nicht für die Datei.
- **F21-S2** Dafür **prüft** der Compiler genau das, was die Warnung befürchtet:
  ein so markierter Global darf **nicht als Argument an einen `ptr<T>`-Parameter**
  gehen.  Das ist ein **Fehler**, kein Hinweis — es ist der Zug, der den
  WASM-Client zweimal zum Absturz brachte (§6.2).  Lesen, Methoden aufrufen und
  `set` bleiben erlaubt; der Anker wird benutzt, nicht weitergereicht.
- **F21-S3** Es ist ein Tausch, kein Schalter: Schweigen gegen eine geprüfte
  Einschränkung.  `AIML_LINT_GLOBALS=0` schaltet dagegen **alle** Globals stumm,
  auch die, die man finden will.
- **F21-S4** Es sagt nichts über Lebenszeit oder Freigabe: wer den Anker
  freigeben will, tut das selbst (spec/61).

### 6.3 Variable Shadowing

```yaml
- decl: [x, i32, 10]

- def:
    name: example
    body:
      - print: "${x}"       # 10 (module-level)
      # - decl: [x, i32, 20]
      # -> CompilationError: shadowing is not allowed
```

## 7. Integrity and Consistency

The JIT import cache never serves an object whose inputs changed and never serves the
result of a failed compile; see `37_parsing_and_jit_internals.md` §3.
