# Static Analysis Rule Implementation Plan

Date: 2026-04-09

## Goal

Introduce further static structure rules for AIML without disturbing the ongoing
9rooms development unnecessarily.

The plan prioritises:

- minimal compiler interventions
- clear rule definitions
- testable, local AST analyses
- a controlled introduction with possible escape hatches via `static_off`

---

## Starting point

Already implemented:

- file size warning from `1000` lines
- file size hard error from `1500` lines
- function size hard error from `301` lines
- `static_off: ["lines"]` to switch off the function line rule locally

Relevant implementation places:

- static structure diagnostics in [compiler.cpp](../../compiler.cpp)
- AST structure in [ast_nodes.h](../../src/core/ast_nodes.h)
- statement dispatch in [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

Important architecture point:

- The new static analysis should still take place before or in parallel with the actual
  codegen in `compiler.cpp`.
- New rules should not be mixed into `visitDeclStmt`, `visitIfStmt`, `visitWhileStmt`
  or other IR-level code paths.

---

## Candidate rules

### 1. Maximum number of local variables per function

Technical difficulty: low to medium

What can be counted:

- all `DeclStmt` in the function body
- including nested blocks
- excluding function arguments
- excluding class members
- catch variables optional, but better excluded at first

Why the rule makes sense:

- many local variables are unfavourable for AI-assisted editing
- the rule measures real cognitive load better than pure file size

Recommended initial semantics:

- count only `DeclStmt`
- no special cases for types
- no scope weighting, a pure count

### 2. Maximum nesting depth

Technical difficulty: low

What can be counted:

- `if`
- `while`
- `try`

Optionally later:

- `catch` as a level of its own
- deeper expression nesting separately

Why the rule makes sense:

- high nesting depth is a direct readability and maintenance barrier
- the AST structure for it already exists

Recommended initial semantics:

- starting depth `0`
- `+1` on entering `if`, `while`, `try`
- take the maximum over the whole function body

### 3. Maximum number of statements per function

Technical difficulty: low to medium

Status:

- not the first priority
- can later serve as a more robust structure metric alongside the line rule

Why later:

- the line rule already exists
- variable count and nesting depth deliver more new value first

---

## Recommended introduction order

### Phase 1

Only prepare the plan, the specification and targeted tests.

No productive activation of new rules.

Goal:

- do not block 9rooms
- define the semantics precisely
- have repro tests in advance

### Phase 2

Implement nesting depth.

Why first:

- smallest intervention
- clearest semantics
- simplest AST walk
- lowest risk of unexpected side effects

### Phase 3

Implement the local variable count.

Why afterwards:

- likewise determinable locally in the AST
- needs somewhat more design clarification than nesting

### Phase 4

Optional:

- statement count
- further structure metrics
- possibly differentiate warning and error levels

---

## Concrete target design

## A. New analysis helpers in `compiler.cpp`

Structure analogous to today's:

- `maxStmtSourceLine(...)`
- `maxBlockSourceLine(...)`
- `logAimlFunctionSizeDiagnostic(...)`

Add:

- `countDeclsInStmt(...)`
- `countDeclsInBlock(...)`
- `maxNestingDepthStmt(...)`
- `maxNestingDepthBlock(...)`
- `logAimlFunctionVarCountDiagnostic(...)`
- `logAimlFunctionNestingDiagnostic(...)`

Important:

- pure AST functions
- no dependency on the IR generator
- no side effects

## B. Extend `static_off`

Currently exists:

- `"lines"`

Recommended new keys:

- `"vars"`
- `"nesting"`

Semantics:

- `"vars"` disables only the variable count rule
- `"nesting"` disables only the nesting depth rule

Not recommended:

- a global `"all"`
- file-wide switch-offs
- project-wide switch-offs

## C. Compiler diagnostics

Format analogous to existing messages:

Examples:

```text
...:42: AIML function 'foo' has too many local declarations (37). Maximum is 32.
...:42: AIML function 'foo' is nested too deeply (6). Maximum is 4.
```

Important:

- always report with the file and the function start line
- name the rule clearly in the text

---

## Recommended thresholds for the first version

Do not activate right away, but first measure against the real code base.

Proposed starting values:

- local variable count warning from `24`, hard error from `32`
- nesting depth warning from `4`, hard error from `6`

Why conservative:

- 9rooms and the existing AIML tests should not collapse immediately
- measure the real distribution first, then tighten

If maximum leniency for 9rooms is prioritised:

- warnings only at first
- hard errors only after a clean-up phase

---

## Recommended introduction strategy without disturbing 9rooms

### Stage 1: only analysis functions + tests

Implement:

- AST walker for the `decl` count
- AST walker for the nesting depth
- no active compiler diagnostics yet

Purpose:

- verify the logic in isolation
- zero risk for 9rooms

### Stage 2: opt-in logging behind a debug switch

Possibility:

- with the debug flag set, only `LOG_DEBUG` or `LOG_WARNING`
- no build blocking yet

Purpose:

- collect real measurements from 9rooms and the AIML suite

### Stage 3: activate warnings

After reviewing the data:

- activate only the warning levels at first
- no hard error

Purpose:

- make problem spots visible
- let 9rooms keep developing

### Stage 4: activate hard errors

Only when:

- the biggest outliers are cleaned up
- `static_off` exceptions have been set deliberately
- the tests stay stable

---

## Test plan

### 1. Unit-level compiler repros

New small AIML test cases:

- a function with many `decl` within a flat structure
- a function with `decl` in nested `if`/`while`
- a function with a deep nesting chain
- a function just below and just above the threshold
- a function with `static_off: ["vars"]`
- a function with `static_off: ["nesting"]`

### 2. Negative/positive pairs

Examples:

- `test_static_analysis_vars_ok.aiml`
- `test_static_analysis_vars_fail.aiml`
- `test_static_analysis_nesting_ok.aiml`
- `test_static_analysis_nesting_fail.aiml`

### 3. Suite protection

After every activation stage:

- `./run_all_tests.sh --no-build`

And for real effects:

- targeted builds of the affected 9rooms targets

---

## Open design questions

To settle finally before implementation:

1. Do function arguments count towards the variable count?
Recommendation: no

2. Does `catchVar` count as a local variable?
Recommendation: no, at first

3. Does `else` count as a nesting level of its own?
Recommendation: no, only the enclosing `if`

4. Do `try` and `catch` count together as one level or separately?
Recommendation: one level for `try` at first

5. Should new rules be hard errors immediately?
Recommendation: no

---

## Minimal implementation cut

If only a small, low-risk first step is wanted:

1. implement the nesting depth analysis
2. provide `static_off: ["nesting"]`
3. no active error rule yet
4. only tests and optional debug logging

That is the smallest sensible entry with minimal danger for 9rooms.

---

## Recommendation

Concrete recommendation for the next work package:

1. do not extend the spec for new rule keys in `49_static_analysis.md` yet
2. implement the measurement and analysis functions first
3. build only the nesting depth as the first pilot
4. the variable count directly afterwards
5. let new rules run only as a warning or debug metric at first
6. activate hard errors only after looking at real 9rooms outliers

That keeps 9rooms development agile while the static analysis
is expanded step by step.
