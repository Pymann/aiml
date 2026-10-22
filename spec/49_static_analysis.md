# Chapter 49 — Static Analysis

> **Core idea:** AIML deliberately contains hard static structural rules so that
> code remains small, readable, and editable for both humans and AI.
> These rules are enforced at compile time, not at runtime.

---

## 1. Goal

The static analysis rules in AIML are intended to ensure primarily three things:

- small, editable files
- small, editable functions
- local, explicit exceptions instead of global opt-outs

The current compiler state in this chapter contains exactly the currently
implemented structural rules and the associated escape hatch.

---

## 2. Currently Implemented Rules

The compiler currently supports these static structural checks:

| Rule | Threshold | Result |
|------|-----------|--------|
| File size | `1000+` lines | Warning |
| File size | `1500+` lines | Compiler error |
| Function size | `301+` lines | Compiler error |
| Function arguments | `6+` arguments | Compiler error |
| Void-call as initializer | always-on | Compiler error |
| Non-`const` top-level decl | always-on | Compiler error |
| Project size without class | `3+` AIML files and `0` classes | Compiler error |
| Top-level type declarations per file | `4+` model/class/interface combined | Compiler error |
| 3+ file project without entry composition | `3+` files and `main` does not `new` an external class | Compiler error |
| String-literal comparison of a variable | same variable `2+` times (`eq`/`neq`) | Compiler error |
| String-literal comparison of a variable | same variable exactly `1` time | Warning |
| `ptr<T>` pushed into a value `list<T>` | always-on | Compiler error |
| Unused enum member (never referenced `Enum.Member`) | whole-program, gated | Warning |
| Blocking call reachable from a per-frame `engine.run` callback | whole-program, gated | Warning |
| Unknown statement key (`- foo: …` in a body) | always-on | Compiler error |

The current compiler also contains two hard OOP architecture rules:

- if a compile spans `3` or more AIML project files and defines no `class`, compilation fails
- if a compile spans `3` or more AIML project files, the entry file MUST declare a `main` function AND that function MUST instantiate (via `{ new: ClassName }`) at least one class declared in a different file (see §15)

Per-file declaration cap:

- a single AIML source file may declare at most `3` top-level types (model/class/interface combined); the 4th declaration is a compiler error (see §14)

The current `static_off`-controlled structural rules are:

- function size (`"lines"`)
- function argument count (`"args"`)
- shared-argument grouping (`"same_arg"`)
- string-literal comparison (`"str_cmp"`)
- pointer-indirection mismatch (`"ptr_indirection"`, see §19)
- blocking call in frame context (`"frame_blocking"`, see §20)

Additionally, one rule is controlled by an environment variable rather than
a per-`def` opt-out:

- top-level imperative statements (`AIML_STRICT_TOPLEVEL`)

The **void-call-as-initializer** rule cannot be disabled — storing a
`void` value into a typed location produces malformed IR.  See §12 below.

---

## 2a. Unknown statement keys

A statement is a map whose key names the statement: `decl`, `set`, `store`, `idx`, `call`,
`print`, `return`, `free`, `throw`, `if`, `while`, `ret`, `try`, `text`.  **Any other key is a
compiler error.**

Until 2026-10-15 it was not.  The statement builder returned `nullptr` for a key it did not
know, and the block builder silently dropped the result — the statement simply **vanished**,
with no diagnostic at any level.  Two things that cost real time:

- `- exit: [1]` was never a statement (the documented form is `call: [sys.exit, N]`, spec/23).
  54 places in the test corpus used it, so every `FAIL` branch printed its message and then
  returned **0** — the suite reported `PASS` for tests that had failed.
- `- continue: []` does not exist in aiml either.  Written inside a loop in another project, it
  vanished, both branches of a classification ran, and the bug looked like a runtime fault in
  the surrounding code.

The rule therefore is: **a key that is not a statement is an error, not a no-op.**  A dropped
statement is undetectable by reading the source — the text says what should happen, and it
simply does not.  That is strictly worse than a parse error, which at least names the line.

The diagnostic names the key, the line, and the nearest legal spelling where one exists
(`exit` → `call: [sys.exit, N]`).

### 2a.1 Dasselbe gilt fuer die OBERSTE Ebene (17.10.2026)

Die Regel oben deckte nur Statements ab.  Auf der obersten Ebene — dort, wo `def`, `class`,
`model`, `enum`, `interface`, `cascade`, `classgroup`, `alias`, `prefab`, `material`,
`namespace`, `import`, `config`, `text`, `extern` stehen — lief jeder unbekannte Schluessel in
einen `SkipBuilder` und **verschwand wortlos**.

Das ist dieselbe Fehlerklasse mit groesserer Wirkung: ein `- mdoel:` laesst nicht ein Statement
verschwinden, sondern **den ganzen Typ**, und der Fehler zeigt sich erst viel spaeter als
„Unknown type" an einer ganz anderen Stelle — oder gar nicht, wenn der Typ nur an einer Stelle
benutzt wird, die ebenfalls verschwand.

Gefunden beim Bauen von `spec/65`: die Probe `neg/n04_self_certify.aiml` schreibt ein
Top-Level `- clearance: 0`, das abgelehnt werden muss (die Freigabe setzt der Wirt, F65-6).
Sie wurde **angenommen**, weil der Schluessel einfach uebersprungen wurde.

Die Regel lautet daher: **ein Schluessel, der auf der obersten Ebene keine Deklaration ist, ist
ein Fehler.**  Geparst wird weiter, damit ein Lauf mehr als einen Fehler meldet.  Die Diagnose
nennt den Schluessel und, wo es eine gibt, die naechstliegende richtige Schreibweise
(`function` → `def:`, `struct` → `class:`/`model:`, `include`/`use` → `import:`,

#### Was die Regel sofort gefunden hat: `free:` lief auf oberster Ebene nie

Beim Einschalten der Regel schlugen **8 Korpus-Tests** fehl, alle mit demselben unbekannten
Schluessel: `free`.  Das war kein Fehlalarm, sondern der Befund.

`free: [p]` ist ein Statement (spec/20).  Die Weiche auf der obersten Ebene reichte
`decl`, `set`, `call`, `print`, `idx` und `store` an den StatementBuilder weiter — **`free`
fehlte**.  Ein `- free: [p]` auf oberster Ebene fiel damit in den `SkipBuilder` und wurde
wortlos verworfen: die Freigabe ist nie passiert.  Betroffen u.a.
`tests/test_oo.aiml:55` (`- free: [var_c]`), `tests/test_model_defaults.aiml`,
`tests/test_chan_class_member.aiml` und die drei
`*_class_member_heap_init_repro`-Tests.

Bei einer Sprache, deren Speichermodell ausdruecklich auf `new`/`free` steht (spec/61, spec/20),
ist ein stillschweigend verworfenes `free` kein Randfall: der Quelltext sagt, dass freigegeben
wird, und es wird nicht freigegeben — und kein Test konnte es merken, weil ein fehlendes `free`
nur leakt und nichts bricht.  Genau die Eigenschaft, die `exit:` so teuer gemacht hat.

Behoben am 17.10.2026: `free` steht in der Weiche.  `return:` und `throw:` bleiben auf der
obersten Ebene Fehler — sie haben dort keine Bedeutung — bekommen aber einen Hinweis statt
stiller Verwerfung.
`clearance` → gibt es nicht, das setzt der Wirt).

---

## 3. File Size

### 3.1 Warning at 1000 Lines

An AIML file with `1000` or more lines produces a compiler warning.

Example message:

```text
...: AIML file large (1000 lines). 1000+ lines should be split.
```

### 3.2 Hard Error at 1500 Lines

An AIML file with `1500` or more lines produces a compiler error.

Example message:

```text
...: AIML file too large (1500 lines). Maximum is 1499 lines; 1500+ is a hard error.
```

### 3.3 Measurement Method

File size is counted as the number of **code lines** — the compiler reads the
file with `getline(...)` but **excludes** two kinds of lines from the count:

- **blank lines** (empty or whitespace-only), and
- **comment-only lines** (first non-whitespace character is `#`).

Every other line increments the count. Comments and blank lines are therefore
"free": a heavily documented file is measured only by its actual code, so the
`1000` / `1500` thresholds refer to code lines, not raw physical lines. (E.g. a
1656-line file with 217 comment lines and 80 blank lines counts as 1359.)

Implementation: `countFileLines` in `compiler.cpp`.

### 3.4 Scope

The file rule applies not only to the entry file, but also to:

- every AIML file in the entry file's transitive import closure

This makes large imported modules visible, not just the
top-level entry point file.

---

## 4. Function Size

### 4.1 Hard Error at 301 Lines

A function or method with `301` or more lines produces a
compiler error.

Example message:

```text
...:12: AIML function 'render_board_screen' too large (301 lines). Maximum is 300 lines; 301+ is a hard error.
```

There is currently no warning level for function sizes.

### 4.2 Scope

The function rule applies to:

- free functions (`def`)
- methods within classes

### 4.3 Measurement Method

Function size is computed as a source span:

- Start: `sourceLine` of the function
- End: last recognized source line within the body

The span is therefore:

```text
endLine - sourceLine + 1
```

### 4.4 Nested Blocks

When determining the end of a function, these nested statement types are
currently included recursively:

- `if`
- `while`
- `try/catch`

This means not only the last top-level statement in the body counts, but also
the deepest known source line in nested control flow blocks.

### 4.5 Non-Goal of the Measurement

The current rule is a pragmatic AST-based size check.
It is not a complete source-span API with an exact end marker for every
AST node.

This means:

- the rule is intentionally practical rather than perfect
- it serves as a structural hard boundary
- it is not intended as a formatting-precise pretty-print metric

---

## 5. `static_off`

`static_off` is the current escape hatch for individual static rules at the
function or method level.

### 5.1 Syntax

`static_off` is an optional field within `def`.

```aiml
- def:
    name: render_huge_legacy_block
    return: void
    static_off: ["lines"]
    body:
      - call: [print, "legacy"]
```

The same applies to methods:

```aiml
- class:
    name: BigScreen
    methods:
      - def:
          name: render
          return: void
          static_off: ["lines"]
          body:
            - call: [print, "ok"]
```

### 5.2 Type

`static_off` is a `list<string>`.

Each string designates exactly one static compiler rule to be disabled for
that single function.

### 5.3 Currently Defined Option

Currently these options are implemented:

- `"lines"`
- `"args"`
- `"same_arg"`
- `"str_cmp"`
- `"ptr_indirection"`
- `"frame_blocking"`

`"lines"` disables the function size check for the affected
function or method.

`"args"` disables the function argument-count check for the affected
function or method.

`"same_arg"` disables the shared-argument grouping check (see §6.7) for the
affected function.  The rule is enforced globally across all free functions
in a translation unit — a `static_off` must therefore be added to every
function that participates in the offending group, or to none of them; if
*every* function in a group opts out, the group is silently accepted.

`"str_cmp"` disables the string-literal comparison check (see §16) for the
affected function or method — both the multi-comparison error and the
single-comparison warning are suppressed for that `def`.

`"ptr_indirection"` disables the pointer-indirection mismatch warning (see §19)
for the affected function or method — use it for a deliberate `ptr<ptr<T>>`
out-parameter.

`"frame_blocking"` disables the blocking-call-in-frame-context warning (see §20)
for the affected function or method — use it for a deliberate one-shot blocking
call that cannot recur per frame (e.g. a synchronous load on a transition screen).

### 5.4 Semantics

- `static_off` acts only locally on the individual `def`
- `static_off` does not act file-wide
- `static_off` does not act project-wide
- `static_off` does not change any runtime semantics
- `static_off` does not change any IR or codegen path
- `static_off` only affects static compiler diagnostics

### 5.5 Unknown Entries

Unknown `static_off` entries are currently permitted but have no effect.

Example:

```aiml
static_off: ["unknown_rule"]
```

This still compiles, but currently disables no known rule.

This keeps the mechanism forward-compatible for future static checks.

---

## 6. Examples

### 6.1 File in Warning Range

A file with `1200` lines:

- continues to compile
- but produces a warning

### 6.2 File in Error Range

A file with `1500` lines:

- no longer compiles
- produces a hard error

### 6.3 Function Without Exception

A function with `301` lines:

- no longer compiles
- produces a hard error

### 6.4 Function with `static_off: ["lines"]`

A function with `301+` lines and

```aiml
static_off: ["lines"]
```

is not blocked by the function lines rule despite its size.

The file size rules remain unaffected.

### 6.5 Function Argument Count

A function or method with `6` or more arguments produces a
compiler error.

Example message:

```text
...:1: AIML function 'too_many' has too many arguments (6). Maximum is 5. Use class/model-based programming to group related state and behavior.
```

This rule applies to:

- free functions (`def`)
- methods within classes

It does not apply to:

- external ABI declarations inside `extern`

The intent is to push AIML code toward:

- class-based programming
- model-based programming
- grouping related state and behavior instead of long flat signatures

### 6.6 Function with `static_off: ["args"]`

A function with `6+` arguments and

```aiml
static_off: ["args"]
```

is not blocked by the argument-count rule.

The file size and function size rules remain unaffected.

### 6.7 Shared Argument Grouping (`same_arg`)

If three or more free functions in the same translation unit share an
argument with **identical name and identical type**, the compiler emits a
hard error on every function in the group (except those carrying
`static_off: ["same_arg"]`).

```text
...:2: function 'damage_creature' shares arg 'creature:DataCreature' with
2 other function(s) (damage_creature, heal_creature, kill_creature).
These should be methods on a class.  Add `static_off: ["same_arg"]` to
this def to bypass.
```

The argument may appear at *any* position in the argument list — not just
the first slot.  A creature passed as second arg in one function and as
first in another still triggers the rule:

```aiml
- def: { name: damage,  args: [[creature, DataCreature], [amount, i32]] }
- def: { name: heal,    args: [[creature, DataCreature]] }
- def: { name: poison,  args: [[delay, i32], [creature, DataCreature]] }
```

The check applies to:

- free functions (`def`)

It does *not* apply to:

- methods within classes (already grouped by class)
- class methods' implicit `this` argument

The intent is to push AIML code toward grouping related state and behavior
in a class — the moment three functions naturally want to operate on the
same shape, the shape itself is asking to be lifted into a class.

#### Opt-out semantics

The bypass works *per group* — if every function in the offending group
carries `static_off: ["same_arg"]`, no error is emitted.  This allows
legacy / migration code to opt out coherently without polluting unrelated
helpers.  A partial opt-out (only some functions in the group marked)
still produces errors on the unmarked functions.

### 6.8 Function with `static_off: ["same_arg"]`

A trio of free functions sharing `(creature, DataCreature)` with

```aiml
- def: { name: legacy_a, args: [[creature, DataCreature]], static_off: ["same_arg"] }
- def: { name: legacy_b, args: [[creature, DataCreature]], static_off: ["same_arg"] }
- def: { name: legacy_c, args: [[creature, DataCreature]], static_off: ["same_arg"] }
```

is not blocked by the shared-argument rule.

The file size, function size, and argument-count rules remain unaffected.

---

## 7. Rule Priority

The rules are independent of each other:

- `static_off: ["lines"]` only disables the function size check
- `static_off: ["args"]` only disables the function argument-count check
- the file size rules remain active

Example:

- File has `1600` lines
- A function in it has `static_off: ["lines"]`

Result:

- the function is spared from the function rule
- the file still fails the file size rule

---

## 8. Recommendations

- split large files instead of setting exceptions
- break up large functions instead of routinely using `static_off`
- use `static_off: ["lines"]` only for deliberately accepted legacy or
  migration cases
- new functions should generally respect the 300-line limit

---

## 9. Non-Goals

This chapter does not describe general type, parser, or
semantic errors such as:

- unknown types
- wrong operator arity
- undefined variables
- invalid member accesses

Such compiler errors also exist, but do not belong to the structural
and static analysis rules described here.

---

## 10. Implementation Status

The current compiler state implements:

- file warning at `1000` lines
- file hard error at `1500` lines
- function hard error at `301` lines
- function hard error at `6` arguments
- shared-argument grouping check (`same_arg`, 3+ free functions)
- top-level imperative statement check (env-gated, see §11)
- void-call-as-initializer check (always-on, see §12)
- non-`const` top-level `decl` check (always-on, see §13)
- `static_off` as `list<string>` on `FunctionDecl`
- the concrete `static_off` options `"lines"`, `"args"`, `"same_arg"`,
  `"str_cmp"`, `"ptr_indirection"`, `"frame_blocking"`
- `ptr<T>`-into-value-`list<T>` push check (always-on, see §17)
- unused enum member whole-program warning (gated, see §18)
- pointer-indirection mismatch warning (always-on, `ptr_indirection`-opt-out,
  see §19)
- blocking-call-in-frame-context warning (gated, `frame_blocking`-opt-out,
  see §20)

Additional static rules can later be added via further
`static_off` keys or additional fixed compiler checks.

---

## 11. Env-Gated Rules

Some checks are **opt-in via environment variable** rather than per-`def`
`static_off`.  They are off by default so existing code keeps compiling, and
become hard errors when the env var is set to any truthy value (`1`, `on`,
`true`, …).  An explicit `0` / `off` / `false` keeps them off.

### 11.1 `AIML_STRICT_TOPLEVEL` — Top-Level Imperative Ban

When set, any of the following at the top level of a source file is a
compile error:

- `- call:`  (`ExprStmt` carrying a `CallExpr`)
- `- set:`
- `- if:`
- `- while:`
- `- idx:`  (indexed write)
- a `- decl:` whose initializer is a `call(...)` expression

Allowed at the top level: `import`, `class`, `def`, `extern`, `model`,
and `decl` with literal/null initializer.

```text
AIML_STRICT_TOPLEVEL=1 ./aiml my_script.aiml
# → ERROR: top-level imperative statement (Call(log.info)) — move into
#   a `main` function (auto-called by compiler).
```

Rationale: top-level execution **is** ordered correctly (source order,
container init first — see [08_entrypoint.md](08_entrypoint.md#top-level-execution-order)),
so strict mode is no longer needed to avoid hoisting bugs.  It exists as
a **style discipline**: imperative startup hidden between `def`s and
`decl`s is harder to find with `grep` and harder to step through with a
debugger than a single explicit `main` function.

Inside a user-defined `main` the order is also deterministic, and the
compiler auto-invokes `main` from the C entry-point — so no explicit
`- call: [main]` at the top level is required (and would actually trigger
a double-call warning under strict mode).

> **History:** earlier versions of the compiler hoisted `decl`-with-call
> initializers above sibling imperative statements, causing real
> ordering bugs.  The 2026-06-04 source-order refactor removed that
> behavior in both main and library files; the env-gated check survives
> because the style argument stands on its own.

#### Migration

For existing code the env var is *off by default*.  Strict mode is intended
for new projects or migrated modules where the cleaner main-function style
is enforced.

---

## 12. Void-Call as Initializer (always-on)

A `decl` or `set` whose right-hand side is a direct call to a function
that returns `void` is a **compile-time error**:

```yaml
- def:
    name: log_it
    args: [[v, i32]]
    body: [- print: ["${v}"]]   # no return: → void

- decl: [_a, i32, { call: [log_it, 1] }]    # ERROR
- set: [some_i32, { call: [log_it, 2] }]    # ERROR
```

Diagnostic:

```text
foo.aiml:18: cannot initialize '_a' of type 'i32' from a void-returning
call 'log_it()' — void values cannot be stored into a typed location.
Add `return: <T>` + a `- return: ...` to 'log_it', or use an explicit
`- call: [log_it, ...]` statement (no init).
```

### Why It Is Non-Negotiable

Storing the result of a void call produces a malformed LLVM IR type ring.
In one observed case (libraryMode regression, 2026-06-04) the resulting
type-layout query recursed into `getPrefTypeAlignment` until the host
process stack overflowed — and the trace was unrecoverable because the
stack itself was corrupted.  An always-on AST-level reject was the
cheapest, narrowest fix.

### Scope

The check covers direct, unqualified function calls
(`- decl: [_, T, {call: [fn, ...]}]`).  Method calls (`a.b()`) and
namespace calls (`sys.foo()`, `log.info()`) go through different visit
handlers and are checked by their own typing rules.

### Workarounds

If the side effect is what you wanted, detach the call:

```yaml
- call: [log_it, 1]                 # imperative — no value produced
- decl: [_a, i32, 0]                # honest sentinel
```

If you wanted a return value, give the function one:

```yaml
- def:
    name: log_it
    args: [[v, i32]]
    return: i32                     # ← declared
    body:
      - print: ["${v}"]
      - return: v                   # ← supplied
```

---

## 13. Non-`const` Top-Level `decl` (always-on)

A top-level `decl` is only allowed when its 4th slot contains a modifier object with `{ const: true }`.
Any top-level `decl` without `{ const: true }` in that modifier position is a compile-time error in every AIML module.

### Scope

The check applies to every module, including:

- entry modules with `main`
- imported library modules
- shared support modules

The check is limited to the AIML `decl` instruction at top level. It does not
apply to local declarations inside function or method bodies.

### Allowed

```aiml
- decl: [MAX_PARTY_SIZE, i32, 6, { const: true }]
```

```aiml
- def:
    name: main
    body:
      - decl: [local_score, i32, 0]
```

### Forbidden

```aiml
- decl: [combat_score, i32, 0]
```

```aiml
- decl: [game_state, GameState, null]
```

### Rationale

Mutable module state hides ownership and lifetime. Persistent state should be
owned by an object instance, not by a file. If state must outlive a single
function activation, it should be moved into a class and the owning instance
should be created with `new` so the state lives on the heap.

### Diagnostic

The diagnostic for this rule must explicitly mention the `new` keyword and
recommend moving the state into a class instance allocated with `new`.

Example diagnostic:

```text
foo.aiml:12: top-level decl 'combat_score' is not allowed unless it uses the modifier object `{ const: true }`.
Move this state into a class instance and create the owning object with `new`
so the state lives on the heap.
```

### Relationship to Other Rules

This rule is always on and cannot be disabled via `static_off`.
It is independent from `AIML_STRICT_TOPLEVEL`: even when strict top-level mode
is off, top-level non-`const` `decl` remains forbidden.

---

## 14. Top-Level Type Declarations per File (always-on)

A single AIML source file may declare at most **`3`** top-level types,
counted as the sum of `- model:`, `- class:`, and `- interface:` items.
The 4th such declaration in the same file is a hard compiler error.

### 14.1 Rationale

Without this rule, projects drift toward "kitchen-sink" files
(`models.aiml`, `types.aiml`) that gather dozens of declarations.  Such
files are hard to navigate, hard to rename, and produce huge merge
conflicts.  A 3-declaration cap forces logical splitting at the
declaration level — independent of total line count.

### 14.2 Measurement Method

The compiler scans each AIML source file's raw text for lines starting
with `- model:`, `- class:`, or `- interface:` at column 0.  Nested
declarations (e.g. an interface inside a class body — not currently
supported, but reserved) do not count.

### 14.3 Diagnostic Message

```text
<path>: file declares 4 top-level types (3 model, 1 class, 0 interface).
Maximum is 3; split this file into smaller modules.
```

### 14.4 Scope

The rule fires for the entry file AND every file in its transitive import
closure in the same way the file-size rule does.  Each path is reported
at most once per compile.

### 14.5 No `static_off` Escape

This rule cannot be disabled.  If a project truly needs to colocate
more than 3 declarations (e.g. a generated DTO module), the convention
is to split into `foo_a.aiml` + `foo_b.aiml` etc. and re-export via a
thin barrel file.

### 14.6 Negative Test

See `tests/test_static_too_many_decls_per_file.aiml` — declares 3
models + 1 class in one file and must exit with code `1`.

---

## 15. `main` Composition for Multi-File Projects (always-on)

When a compile spans **`3` or more** AIML project files (the entry
file plus its transitive import closure), the entry file MUST:

1. declare a top-level `- def: name: main` function, AND
2. that `main` function must instantiate (via `{ new: ClassName }`)
   at least one class declared in **another** file of the project.

If either condition fails, the compile aborts with a hard error.

### 15.1 Rationale

The simpler "3+ files require at least one class anywhere" rule (§2)
catches projects with zero behavior-carrying object structure.  But it
does not prevent the degenerate case where a project HAS classes in
imports yet the entry file ignores them — every class colocated with
its caller, imports unused or used only for trivial helpers.

This rule forces the entry point to actually compose modules.  The
3-file threshold is the same as the related OOP rule so the two stay
in lockstep.

### 15.2 Diagnostic Messages

If `main` is missing:

```text
<entry>: project spans N AIML files but does not declare a `main`
function. Projects with 3+ files must use a `main` entry point.
```

If `main` exists but only instantiates local classes:

```text
<entry>: project spans N AIML files; `main` must instantiate at least
one class declared in another file. Without external composition the
extra files are dead weight — split the entry point and wire it from
a separate module.
```

### 15.3 Detection Method

The compiler:

1. counts project files: `1` (entry) + `len(importedSourceFiles)`
2. raw-text-scans the entry file for `- def:` ... `name: main` to
   confirm a `main` function exists
3. raw-text-scans the entry file for `new:` patterns and collects all
   `ClassName` identifiers
4. raw-text-scans ALL project files for `- class: name: X` and builds
   a `{className → sourceFile}` map
5. checks at least one collected `ClassName` resolves to a class
   declared in a file other than the entry file (canonical-path
   comparison)

Notes:

- `- model:` and `- interface:` declarations do not satisfy the rule;
  only real `- class:` qualifies (because only classes carry behavior).
- The scan is line-prefix-based, not full AST traversal, so `new:`
  inside string literals or comments will produce false positives.
  In practice this is benign — string-literal `new:` is extremely rare
  in AIML source.

### 15.4 Scope

Fires once per compile of the entry file.  Does not fire when compiling
an individual library import in isolation (because that build only
sees itself, not the full project).

### 15.5 No `static_off` Escape

The rule cannot be disabled.  A 3-file project that genuinely does not
need external composition is structurally suspect — either fold the
files back together (drop to <3 files) or restructure the entry point.

### 15.6 Negative Test

See `tests/test_static_main_no_external_compose.aiml` — entry file
imports two siblings (3-file project), declares `main` that
instantiates only a local `LocalRunner` class.  Must exit with code `1`.


---

## 16. String-Literal Comparison (always-on, `str_cmp`-opt-out)

Comparing a variable against a **quoted string literal** via `eq` / `neq` is a
code smell that this rule surfaces at two severities, scoped **per function /
method**:

| Situation | Result |
|-----------|--------|
| the *same* variable compared against a string literal **`2`+ times** | Compiler error |
| a variable compared against a string literal **exactly `1` time** | Warning |

### 16.1 Rationale

Repeatedly branching a single variable on string values is a hand-rolled
`switch` that the type system cannot check — a typo in one arm silently never
matches, and adding a case means editing scattered `if`s.  Two or more such
comparisons of the same variable almost always want to be an **enum**
(compile-checked, exhaustive) or **polymorphic dispatch** (a method per
subtype).  A single string comparison is milder — often a genuine
membership/format test — so it is only a warning, hinting at a **regular
expression** for real string matching.

### 16.2 What Counts

- The operator must be `eq` or `neq`.
- Exactly one operand must be a **quoted** string literal (YAML tag `!`,
  i.e. `"combat"`).  Interpolated strings (`"${x}"`) do not count.
- The other operand must be a plain variable reference (`VariableExpr`),
  including a dotted member read such as `mat.current_phase`.
- Grouping key is the variable's textual name; `mat.current_phase` in two
  comparisons counts as two hits on the same variable.

Crucially, **unquoted** scalars — including enum members like
`GamePhase.combat_phase` — are `VariableExpr`, not string literals, and never
trigger the rule.  Migrating a string switch to an enum comparison therefore
resolves the error, which is the intended forcing function.

### 16.3 Scope

The check runs over:

- free functions (`def`)
- methods within classes

Each function/method is analysed independently: the counter resets per
function, so the same variable name in two different functions does not
accumulate.

Unlike `same_arg` (entry-file only), this check runs for **both the entry file
and every imported/library module** — an imported file with a 2+ string switch
fails the whole build. This makes the rule effective codebase-wide, since most
code lives in imported modules. (A cached object from a prior successful build
is not re-scanned; a clean build re-checks everything.)

### 16.4 Diagnostics

Error (2+ comparisons):

```text
foo.aiml:12: variable 'kind' is compared against string literals 2 times in
'classify' — this hand-rolled string switch should be an enum or polymorphic
dispatch.  Add `static_off: ["str_cmp"]` to this def to bypass.
```

Warning (single comparison):

```text
foo.aiml:12: variable 'kind' is compared against a string literal in 'is_fire'
— consider a regular expression for string matching.
```

### 16.5 `static_off`

`static_off: ["str_cmp"]` on a `def` suppresses **both** the error and the
warning for that function or method.  Use it for deliberately accepted string
dispatch (e.g. a thin boundary parser) or migration code.

### 16.6 Negative Test

See `tests/test_static_string_switch_error.aiml` — a function compares `kind`
against `"fire"` and `"water"` and must exit with code `1`.  The companion
`tests/test_static_string_single_warn.aiml` compares once, warns, and still
compiles (exit `0`).

---

## 17. `ptr<T>` Pushed into a Value `list<T>` (always-on)

Pushing a `ptr<T>` into a `list<T>` whose element type is the **value** struct
`T` is a compiler error.

```yaml
- decl: [lst, list<Foo>]              # value list — each slot is a full Foo struct
- decl: [fpp, ptr<Foo>, { new: Foo }]
- call: [lst.push, fpp]              # ERROR: pushing an 8-byte pointer into a struct slot
```

### 17.1 Rationale

A `list<Foo>` allocates one full `Foo` struct per element. Pushing a
`ptr<Foo>` writes only the **8 pointer bytes** into that struct-sized slot and
leaves the rest as garbage. The list looks fine until something walks it in
full — most notably `aiml_ser_encode` (heap serialization, ch. 45), which
recurses into the slot's nested `list`/`map` fields, follows garbage pointers,
and crashes with a `SIGSEGV` far from the offending `push`. This is exactly the
class of bug that produced a hard-to-bisect card-save crash in a downstream
project. The fix is always local and mechanical:

- **dereference** at the push site — `{ '?': [fpp] }` — to copy the value in, or
- **declare a pointer list** — `list<ptr<Foo>>` — if by-reference storage is intended.

### 17.2 What Counts

- The call must be a `.push(...)` on a `list<T>` receiver.
- The list element type `T` must be a **value** type (not itself `ptr<...>`,
  and not an interface fat-pointer element — `list<SomeInterface>` stores fat
  pointers and is exempt).
- The pushed argument must be a plain variable whose resolved type is exactly
  `ptr<T>` for that same `T`.

A `list<ptr<Foo>>.push(fpp)` is therefore **allowed** (pointer into a pointer
slot), as is `list<Foo>.push({ '?': [fpp] })` (value into a value slot). Only
the type-mismatched `list<Foo>.push(ptr<Foo>)` fails.

### 17.3 Diagnostics

```text
foo.aiml:12: list<Foo>.push() got a ptr<Foo> ('fpp'): an 8-byte pointer would
be written into a value-struct slot (heap corruption / SIGSEGV on serialize).
Dereference it — { '?': [fpp] } — or declare the list as list<ptr<Foo>>.
```

### 17.4 Negative Test

See `tests/test_ptr_push_into_value_list_error.aiml` — a function pushes a
`ptr<Foo>` into a `list<Foo>` and must exit with code `1`.

---

## 18. Unused Enum Members (always-on, whole-program **warning**)

An enum member that is **declared** but never **referenced** as `Enum.Member`
anywhere in the build produces a non-blocking compiler **warning** (not an
error — the build proceeds and runs).

```text
enums.aiml:12: enum member 'PassiveAbility.armored' is never referenced in this build — remove the unused enumerator or wire it up.
```

### 18.1 Rationale

Dead enumerators accumulate as card/effect vocabularies evolve: a value is added
to an enum, the code path that used it is refactored away, and the member lingers
— readers can no longer tell whether it is load-bearing. This is the enum analogue
of the Tier-3 dead-prefab check (`44_8_prefab_static.md`) and reuses the same
whole-program machinery.

### 18.2 What Counts as a Use (V1)

V1 is deliberately conservative — only **explicit symbolic references** to a
concrete member count:

- `Enum.Member` as an expression (the value, an argument, a `decl`/`set` initializer,
  a condition operand, …).

Both resolution paths are hooked: the primary `Enum.Member` expression path and
the member-access fallback (`resolvePropertyOrEnum`).

**Reflective whole-enum uses** additionally mark **every** member of an enum as
referenced — the construct consumes the entire member vocabulary, so warning on
any single member would be a false positive:

- `enums.name(T, …)` / `enums.value(T, …)` — enum reflection (see
  [14_enums.md](14_enums.md#reflection-enums-namespace)); lowered from
  `ctx.enums[T]` at compile time.
- `enum: T` binding on a dropdown/radio widget — the widget's options ARE the
  enum's members and the selection is read back via `dropdown_enum`; recorded in
  `checkPrefabUsage` (per prefab child's `enum_type`).

### 18.2a `wire: true` — ein Vokabular, das über eine Grenze geht

Ein Enum, dessen Mitglieder über ein **Protokoll**, eine **Datei** oder eine
**Datenbank** gehen, wird in *einem* Build nur teilweise benannt und im anderen
vollständig: `JobState.agent_lost` schreibt der Server, liest der Client, und
der Client-Build sieht das Wort nirgends.  Der Hinweis „remove the unused
enumerator" wäre dort das genaue Gegenteil des Richtigen — das Mitglied ist
**der Vertrag**.

```aiml
- enum:
    name: JobState
    wire: true          # Vokabular einer Schnittstelle, nicht dieses Builds
    values: [queued, running, agent_lost, finished]
```

- **F49-W1** `wire: true` markiert **alle** Mitglieder des Enums als verwendet —
  dieselbe Wirkung wie eine reflexive Ganz-Enum-Nutzung (§18.2), nur erklärt
  statt erschlossen.
- **F49-W2** Es ist eine **Behauptung des Autors**, keine Messung: wer es setzt,
  sagt „dieses Vokabular wird auswärts benannt".  Darum steht es am Enum und
  nicht an einem Schalter des Builds — ein `AIML_LINT=0` hätte alle Enums
  stummgeschaltet, auch die, deren tote Mitglieder man finden will.
- **F49-W3** Es ändert **nichts** an der Kodierung: `wire` ist kein Format,
  keine Stabilitätszusage für Zahlenwerte und keine Erlaubnis, Mitglieder
  umzusortieren.  Es schaltet genau eine Warnung ab.

### 18.3 What Does **Not** Count (V1)

These are intentionally **not** treated as a use, so an otherwise-unreferenced
member still warns:

- a raw integer literal that happens to equal a member's value,
- implicit use only through persisted / networked data (dafür gibt es `wire: true`, §18.2a),
- using the enum **type** (e.g. as a field or argument type) without naming a member.

(A `enum: SomeEnum` dropdown/radio binding *does* now count — see the reflective
whole-enum uses in §18.2 — as does `enums.name`/`enums.value`.)

This keeps false positives out of the "is used" set at the cost of some
false-warnings on members reached only via the excluded channels (common in
bytecode-heavy code that writes effect/trigger codes as raw ints). Future
versions may widen what counts.

### 18.4 Whole-Program Semantics & Gating

Declared members and referenced members are unioned across the entry module and
every transitive import (merged up the sub-compiler hierarchy exactly like
`allPrefabDecls` / `allPrefabUses`), then diffed once at the top level. The
warning is gated identically to dead-prefab — emitted only when the use-set is
complete:

- top-level (non-library) build, and
- not `--prefab-report` mode, and
- a real object/exe build (`noCache`) **or** a single file with no imports.

A warm-cache multi-file JIT run (which may skip re-compiling imports) stays
silent to avoid false "unused" noise. The location prefix points at the enum's
**declaration block** — `EnumDecl` carries no per-member source node, so all
members of one enum share the block position.

### 18.5 Tests

- `tests/test_unused_enum_member.aiml` — one enum with a partially-used member set
  (warnings for the unused members) and one fully-used enum (no warning); the
  program still runs (exit `0`).
- `tests/test_unused_enum_import.aiml` + `tests/_enum_import_lib.aiml` — an enum
  declared in an imported module with one member used in the entry: the merge
  reports the other members as unused in an object build, and the entry-used
  member is not warned.

## 19. Pointer-Indirection Mismatch (always-on **warning**, `ptr_indirection`-opt-out)

A call argument whose **source pointer-depth** exceeds the callee parameter's
declared depth is *over-indirected*: the callee dereferences one (or more) levels
too few and reads/writes through a bogus address.  Because LLVM uses opaque
pointers, `ptr<ptr<T>>` and `ptr<T>` are indistinguishable in the IR and the
verifier accepts the call — so this class of bug reaches runtime as a corruption
or segfault.  This rule catches it at the source level, scoped **per function /
method**, as a **warning** (the program still compiles).

The motivating bug: `record_death({'&': [mat]}, iid)` inside a helper whose `mat`
parameter is already `ptr<MatchSession>`.  `{'&': [mat]}` yields `ptr<ptr<…>>`;
`record_death` wrote through the extra indirection and crashed on the first
creature death caused by an effect.

### 19.1 Pointer-Depth

The **depth** of a declared type is its count of leading `ptr<` wrappers:
`i32`/`string`/`Thing`/`list<T>` → `0`, `ptr<Thing>` → `1`, `ptr<ptr<Thing>>` → `2`.

The **depth of an argument expression** is computed for variable-based forms only:

| Argument form | Depth |
|---------------|-------|
| plain variable `x` | depth of `x`'s declared type |
| `{'&': [x]}` / `&x` (`N` `&`s) | depth(`x`) + `N` |
| `?x` / `??x` (`N` `?`s) | depth(`x`) − `N` |
| anything else (call result, literal, `{at:}`, …) | *unknown → skipped* |

### 19.2 What Is Warned

Only **over-indirection** — argument depth **strictly greater** than the
parameter depth.  Examples that warn:

- `{'&': [p]}` where `p` is `ptr<Thing>` into a `ptr<Thing>` param (2 > 1).
- `p` (a `ptr<Thing>`) into a value `Thing` param (1 > 0).

**Under-indirection is never warned**: passing a value where a `ptr` parameter is
expected (depth 0 into depth 1) is the language's implicit **by-reference** pass
(`list`/`map`/model args are addressed automatically — see spec/22).  Warning on
it would flood every legitimate by-ref call.

### 19.3 Callee Resolution

- **Free functions** (no `.` in the call name) are resolved from the compiled
  program's declarations, so the *exact* parameter depth is known and compared.
- **Method / builtin / cross-file calls** (dotted name, or a callee not in this
  file's closure) cannot be resolved per-file.  They fall back to the
  callee-independent signal: `&x` (single or multi `&`) where `x` is **already a
  pointer** — an unambiguous over-indirection regardless of the target parameter.

### 19.4 Scope

Runs over free functions (`def`) and class methods, for **both the entry file and
every imported/library module** (like `str_cmp`), so the rule is codebase-wide.
Each function is analysed independently.

### 19.5 Diagnostics

Resolved free-function call:

```text
foo.aiml:12: call to 'record_death': argument 1 ('mat') is over-indirected — it
has pointer-depth 2 but the parameter expects depth 1 (ptr<MatchSession>).  The
callee will dereference too few levels; drop a '&' or add a '?'.  Suppress with
`static_off: ["ptr_indirection"]`.
```

Unresolvable callee (heuristic):

```text
foo.aiml:12: call to 'obj.record': `&mat` takes the address of 'mat' which is
already a pointer (depth 1) — this passes an over-indirected pointer (depth 2).
Pass 'mat' directly, or suppress with `static_off: ["ptr_indirection"]`.
```

### 19.6 `static_off`

`static_off: ["ptr_indirection"]` on a `def` suppresses the warning for that
function/method — use it for a genuine `ptr<ptr<T>>` out-parameter.

### 19.7 Test

See `tests/test_ptr_indirection_warning.aiml` — a method over-indirects a free
function (warning), a second suppresses it via `static_off`, a third passes the
pointer correctly (no warning); the program compiles + runs (exit `0`).

## 20. Blocking Call in Frame Context (always-on **warning**, `frame_blocking`-opt-out)

A **blocking** call (one that suspends the thread — an RPC await, a sleep, a
blocking socket read) that is statically reachable from a **per-frame callback**
(the `update`/`render` functions handed to `engine.run`) is a latent stall: the
render loop cannot make progress while the call is parked.  This is the AIML
analogue of the Linux-kernel `might_sleep()`-in-atomic-context check — a
"can-this-block?" **effect** propagated over the call graph, intersected with a
"does-this-run-every-frame?" **context**.

```text
lobby_screen.aiml:143: blocking call `session.send_and_await` runs inside the
per-frame render loop (engine.run → mm_menu_update → MainMenuApp.frame →
LobbyScreen.render → refresh_presence).  A blocking RPC/sleep in the frame loop
stalls rendering — under WASM asyncify it can freeze the page.  Make it async
(`send_rpc` + `poll_rpc` across frames) or suppress with
`static_off: ["frame_blocking"]`.
```

### 20.1 Rationale

`engine.run` is portable: native runs a frame-limited `while` loop, the browser
uses `emscripten_set_main_loop`.  A blocking primitive lowers, under WASM, to an
`emscripten_sleep` (asyncify) that **unwinds the whole main-loop callback** and
depends on a later rewind.  Doing this deep inside the per-frame call chain is
fragile: it stutters the loop (each blocked call is dead wall-clock) and, under
load, the asyncify rewind can fail to resume — the page freezes with no crash.
Native tolerates the same code as a mere hitch (a real `usleep`), so the acute
failure is WASM-only, but a blocking call in a render loop is bad UX on every
target.  The fix is always the same: don't block the loop — fire the request
(`send_rpc`) and consume the response over subsequent frames (`session.update`
+ `poll_rpc` + `decode_rpc`).

### 20.2 The Blocking Effect

A call is a **blocking primitive** if it is one of the compiler's known
thread-suspending builtins.  The V1 seed set:

- `session.send_and_await(...)` — the RPC request/await poll-loop (§44_5),
- `time.sleep_ms(...)` / `time.sleep(...)`,
- `ws.recv(...)` / any WebSocket op that blocks until a frame/open arrives.

A user `def` is **blocking** if its body (transitively) calls a blocking
primitive or another blocking `def`.  The property is inferred as a
fixed-point over the direct-call graph — the same whole-program machinery used
for the unused-enum and dead-prefab checks — so `refresh_presence` is blocking
because it calls `net_list_online`, which calls `send_and_await`.

### 20.3 The Frame Context

A function is in **frame context** if it is a per-frame callback or is
(transitively) called by one.  The seed is precise: `engine.run(win, update,
render)` names its `update` and `render` callbacks as arguments, so the compiler
marks exactly those two functions as frame-context roots and propagates the mark
along direct calls (`<.method`, `obj.method`, free `fn`).  `LobbyScreen.render`
is frame-context because `MainMenuApp.frame` calls it and `frame` is the
`engine.run` update callback.

### 20.4 The Violation

The check reports the intersection: a **blocking** call lexically inside a
**frame-context** function.  The diagnostic anchors at the offending call site
and prints the reachability chain from the `engine.run` callback down to the
blocking primitive, so the author sees exactly which frame path is at fault.
Severity is a non-blocking **warning** (V1) — the build proceeds and runs; a
future revision may escalate to an error on WASM targets.

### 20.5 V1 Scope & Limits

V1 is deliberately sound-by-under-approximation (no false positives, may miss):

- **Direct calls only.** Both the blocking-effect closure and the frame-context
  closure follow statically resolvable calls.  Calls through `ptr<Interface>`,
  a `classgroup` component slot, a vtable, or a callback stored in a field are
  **not** followed — such a hidden path is neither flagged nor propagated.  This
  catches the common direct render-chain (the case that bit us) with zero false
  alarms, at the cost of missing indirection-hidden blocking.
- **V2 (documented, not implemented):** let an `interface` / `rpc:` method
  declare the `blocking` effect in its signature (à la async-trait / Koka
  effects); implementations must conform, and indirect calls then propagate the
  effect precisely.

### 20.6 Gating

Like the other whole-program checks, the intersection is computed only when the
call graph is complete: a top-level (non-library) real object/exe build, or a
single file with no imports.  A warm-cache multi-file JIT run that may skip
recompiling imports stays silent, to avoid a false "not reachable" (or false
"not blocking") from an unseen edge.

### 20.7 `static_off`

`static_off: ["frame_blocking"]` on a `def` suppresses the warning for that
function/method — use it for a deliberate one-shot blocking call that is
acceptable to run on a dedicated loading/transition screen (e.g. a synchronous
asset load guarded so it cannot recur per frame).

### 20.8 Tests

- `tests/test_frame_blocking_warning.aiml` — an `engine.run` update callback
  whose transitive callee calls `time.sleep_ms` (warning naming the chain); a
  second callback chain that opts out via `static_off: ["frame_blocking"]` (no
  warning); a non-frame `def` that blocks freely (no warning — blocking is only
  illegal *in* frame context); the program compiles + runs (exit `0`).

---

## 21. Value Round-Trip Through a Call (always-on **error**)

```yaml
- set: [mat, { call: [advance_phase_auto, mat] }]     # error
```

Assigning a call result back to a variable that is itself passed **by value** to that call is a
hard compiler error when the variable is a **value model with heap-owning members** (a model
whose fields include a `string`, a container, a nested model with such fields, …).

### 21.1 Rationale

The idiom "copy in, modified copy out" hands the whole aggregate to the callee by value (a
member-wise share, `61_memory_management_DRAFT.md` §7), lets the callee mutate its parameter and
return it, and rebinds the caller's variable to the result. Under spec/61 the ownership of that
result is ambiguous: the returned value still shares the caller's heap, so releasing the old
value (§5.5) frees what the result references, and not releasing it leaks the whole aggregate on
every call. It also copies the entire aggregate twice per call.

The intended forms are:

- a `ref<T>` parameter that mutates in place and returns nothing:
  `- call: [advance_phase_auto, {'&': [mat]}]`, or
- a method on the model's class.

### 21.2 What Counts

- `set: [X, { call: [F, …] }]` where `X` is a plain local or parameter (no `.` / `<.` path),
- `X`'s declared type is a value model with heap-owning members (not `ptr<>`, `ref<>`,
  `shared<>`, a refcount model, a scalar, `string` or a container),
- and `X` appears **by value** as a direct argument of `F` (a bare `X`; `{'&': [X]}` is a borrow
  and does not count). `F` may be a function or a method.
- Cascade handlers are covered by `57_function_cascades.md` §3a (carrier by reference, no return).

### 21.3 Diagnostics

```
<file>:<line>: 'mat' (MatchSession) is passed by value to 'advance_phase_auto' and reassigned
from its result — pass ref<MatchSession> and mutate in place (spec/49 §21)
```

### 21.4 `static_off`

None — the rule cannot be disabled.

### 21.5 Negative Test

`tests/test_static_value_roundtrip_error.aiml` must fail with the diagnostic above.

## 22. Symbol Clearance Flow (**opt-in**, see spec/65)

`spec/65_symbol_clearance.md` defines levels for symbols at a trust boundary (a console, a
bundle that travelled, a loaded library).  Its **flow rule** — for every call edge `f → g`,
`level(g) >= level(f)` (F65-9) — is a static check and therefore belongs in this chapter, but it
is **off by default** and enabled with `AIML_STATIC_CLEARANCE=1` (F65-17).

It is opt-in rather than always-on because the normal case is one binary written by one party
(F65-1), where the rule has nothing to say.  What *is* always on is the syntax of the
annotations themselves (`invalid-level`, `invalid-grants`), so an annotation can never be
silently ignored and rot.

The identifiers in spec/65 §10 (F65-21) are fixed strings, assigned once.  They are a small,
local instance of the open item at the end of this chapter — they do not solve it for the rest
of the compiler, and a caller matching them must not assume any other check offers the same.

## `--check` — machine-readable diagnostics

```
aiml --check <file.aiml>
```

Analysis only: walks the whole import graph, emits no objects and no binary — the same shape as
`--prefab-report`.  Human log output is suppressed, so the output is **only** records, one per
line, tab-separated:

```
<severity>\t<file>\t<line>\t<column>\t<origin>\t<message>
```

| Field | Meaning |
|---|---|
| `severity` | `error` or `warning` |
| `file` | source file, or `-` when the diagnostic carries no location |
| `line` | 1-based, `0` when unknown |
| `column` | 1-based, `0` when unknown (see below) |
| `origin` | the compiler source position that raised it, e.g. `ast_compiler.cpp:5110` |
| `message` | the text, with the location prefix stripped |

Tab-separated because messages contain colons, spaces and quotes; a caller must not have to guess
where the location ends.  Exit code is `1` when any error was emitted, `0` otherwise.

### The column is partial, and that is not a formatting problem

`fmtLoc(node)` now emits `file:line:column`, the column coming from the YAML mark — it had been
available all along and was simply never printed (cosmos `doc/spec/98` F98-9).  Where the AST
retains the mark, a caller can underline the exact token:

```
error  pipe.aiml  1  3  ast_compiler.cpp:5110  prefab 'BadScene' child 'img_one': texture stem …
```

But roughly half the diagnostics are raised through `fmtLoc()` **without** a node and fall back to
`ctx.currentLine`, which has no column companion — those report `column 0`.  Closing that gap is
not a change to this mode: the AST would have to carry a source **column** alongside
`sourceLine`, set wherever `capturedLine` is set today.  Until then a caller highlights the line
and, where a column is present, the token.

### Offen: stable diagnostic codes

`origin` is the compiler file and line that raised the finding.  It is stable enough to group by
and unstable across refactors, so it is a diagnostic aid, not an identifier.  A caller that must
react to a *specific* check still has to match message text, which breaks when the wording
improves.  Real codes (`AIML-E0042`-style, assigned once and never reused) are open.
