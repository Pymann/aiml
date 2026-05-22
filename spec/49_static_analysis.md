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

The current `static_off`-controlled structural rules are:

- function size
- function argument count

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

File size is counted as the simple physical line count of the source file.

The compiler reads the file directly and counts each line read by
`getline(...)`.

### 3.4 Scope

The file rule applies not only to the entry file, but also to:

- directly imported AIML files
- sibling files loaded in the compile context

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

`"lines"` disables the function size check for the affected
function or method.

`"args"` disables the function argument-count check for the affected
function or method.

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
- `static_off` as `list<string>` on `FunctionDecl`
- the concrete `static_off` option `"lines"`
- the concrete `static_off` option `"args"`

Additional static rules can later be added via further
`static_off` keys or additional fixed compiler checks.
