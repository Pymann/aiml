# AIML: fix plan for field chains in interpolation and call arguments

## Problem

The new chain support now works for:

- deep field reads/writes
- list index chains like `a.items-0.hp`
- method chains like `a.items-0.cost.get("1")`

But one important remaining break is left:

- field chains in string interpolation like `${hc.iid}`
- the same chain expressions in certain call argument contexts

In the current state this means that expressions that are actually string or ptr based
can internally be treated as the wrong type (`i32`).

Practical effect in `9rooms`:

- `${hc.iid}`
- `${g_client_info.player_name}`
- `${g_client_info.deckset_id}`

triggered frontend build errors, although the same accesses work as normal
expressions outside interpolation.

## Core finding

This is no longer a general field chain bug, but an integration hole
between:

- the AST/expression builder for interpolation
- `visitStringInterpolation(...)`
- and the type/value conventions of `visitVariable(...)`

In other words:

- `hc.iid` as a normal expression: ok
- `${hc.iid}` as an interpolation part: not yet robust

## Most probable cause

String interpolation today works type-sensitively via `visitExpression(...)`
and then decides based on the LLVM type how a part is brought into string form.

The break probably sits in one of these points:

1. an interpolation part is not built as a normal `VariableExpr("hc.iid")`
2. or `visitVariable(...)` does not return the final field value in this path,
   but an intermediate value with wrong type information
3. or `visitStringInterpolation(...)` does not treat the return value of a
   chained field access the same way as that of a simple variable read

## Relevant C++ places

The hottest places for the fix are:

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
  - `visitStringInterpolation(...)`
  - `visitVariable(...)`
  - `visitExpression(...)`
- [ast/expression_builder.cpp](../../src/core/ast/expression_builder.cpp)
  - in case `${...}` contents are not built as normal expressions there
- [string_utils.h](../../src/core/string_utils.h)
- [expression.cpp](../../src/core/expression.cpp)
  - in case there is special handling for variable/path resolution there
- optionally for diagnosis:
  - [compiler.cpp](../../compiler.cpp)
  - [context.h](../../src/core/context.h)

## Target behaviour

These cases must work equally:

```aiml
- decl: [iid, string, hc.iid]
- print: ["iid=${iid}"]
```

and

```aiml
- print: ["iid=${hc.iid}"]
```

likewise:

```aiml
- call: [log.info, "player=${g_client_info.player_name} deckset=${g_client_info.deckset_id}"]
```

and analogously for:

- `ptr<T>.field`
- `list<T>-idx.field`
- `obj.deep.field`
- `obj.deep.map.get("x")` inside `${...}` as soon as a string or
  numeric value results there

## Concrete implementation plan

### sprint_task_1: repro tests for interpolation and direct string contexts

Create new tests first.

Path:

- `tests/test_field_chain_interpolation.aiml`
- optionally `tests/test_field_chain_call_args.aiml`

At least these cases:

- `${obj.field}`
- `${obj.deep.field}`
- `${obj.items-0.name}`
- `${ptrObj.name}`
- `log.info("x=${obj.field}")`
- assignment into a `string` variable via interpolation

Acceptance:

- all run green
- no LLVM verifier errors
- no type degradation to `i32`

Affected places:

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- `tests/`

### sprint_task_2: normalise the AST tree for `${...}`

Check how `${hc.iid}` is built internally.

If the content does not end up as a normal expression, this must be unified:

- `${...}` should get the same AST expression type as outside
  interpolation
- no separate simplified "variable name only" path for chain strings

Affected places:

- [ast/expression_builder.cpp](../../src/core/ast/expression_builder.cpp)
- possibly [ast_builder.cpp](../../src/core/ast_builder.cpp)

### sprint_task_3: harden `visitStringInterpolation(...)` against chain results

`visitStringInterpolation(...)` must not implicitly assume that a
part is only:

- a simple int
- a simple string ptr
- a float

Instead:

- take the result of `visitExpression(part)` as the authoritative source
- treat chained field reads exactly like normal VariableExprs
- if necessary: distinguish pointer-to-string / loaded string cleanly

Affected places:

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)

### sprint_task_4: extract a shared "value-to-string" conversion

Currently the type handling for interpolation sits locally in
`visitStringInterpolation(...)`.

Better:

- introduce a central helper, e.g. `stringifyValue(IRValue*)`
- make the same helper usable for `print`, `log.*` or later template paths

That prevents chain expressions from working in one place and not
in another.

Affected places:

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [string_utils.h](../../src/core/string_utils.h)
- possibly a new implementation file in `src/core/`

### sprint_task_5: regressions for real 9rooms patterns

After the generic fix, cover exactly the previously problematic patterns as tests,
but hermetically:

- `${hc.iid}`
- `${client_info.player_name}`
- `${obj.items-0.name}`
- interpolation inside `log.info(...)`
- composite strings with several chain parts

Acceptance:

- 9rooms-like patterns compile
- no more fallback to temporary alias variables needed

## Architectural guideline

The fix should **not** be built as a special case "if StringInterpolation and the name
contains a dot".

The right direction is:

- interpolation uses the same expression logic as all other contexts
- stringification is a downstream step
- no separate mini-resolver just for `${...}`

Otherwise AIML gets parallel semantics again:

- normal field read
- interpolation field read
- call arg field read

That is exactly what should be avoided.

## Recommendation

If you cut it pragmatically:

1. test package first
2. then the AST check for `${...}`
3. then harden `visitStringInterpolation(...)` centrally
4. after that 9rooms-like regressions

That is small enough for a focused rework and big enough to remove the
current fallback to intermediate variables again.
