# Implementation plan: chained field/method syntax in AIML

## Goal

The AIML compiler should support real chained accesses and methods consistently,
e.g.:

- `a.b.c`
- `a.b.c = 7`
- `a.b.len()`
- `a.items-0.name`
- `a.b.map.get("x")`
- `this.player.hand-2.mana_cost.get("1")`

Currently only partial forms or special cases work. That leads to
helper variables, workarounds and inconsistent behaviour between read, write
and method call.

## Core problem

Today the compiler does not treat access strings as a real segment chain,
but mostly as:

- `baseName`
- plus a remainder string from the first dot onwards

As a result, real chains break off early.

## Current findings in the C++ code

### 1. The read path splits only once

In [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L184), `visitVariable(...)` only processes the first separator (`.` or `-`).

Consequence:

- `a.b.c` becomes:
  - `baseName = "a"`
  - `propName = "b.c"`

That is no real recursive path resolution.

### 2. The write path likewise splits only once

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), `set` is treated structurally in the same way.

Consequence:

- deep targets such as `root.match.round` or `card.data.cost` are not robust

### 3. The method call path splits only at the first dot

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L62), `call->functionName` is split only once with `find('.')`.

Example:

- `a.b.map.get`
  - `baseName = "a"`
  - `methodName = "b.map.get"`

Then `visitObjectMethodCall(...)` tries to work on `a`, although the
actual receiver instance is only reached after several segments.

### 4. There are already partial special cases, but no general chain path

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L459) and [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1215) "chained method dispatch" already exists, but only as a local special case:

- isolate the field name
- then execute the sub-method

That is useful for simple builtin special cases such as:

- `resp.headers.put(...)`
- `c.mana_cost.put(...)`

but no general model for arbitrarily deep chains.

## Target picture

The compiler should split every access internally into **segments** and then
resolve them uniformly.

Example:

`this.player.hand-2.mana_cost.get("1")`

should internally become:

1. `base(this)`
2. `member(player)`
3. `member(hand)`
4. `index(2)`
5. `member(mana_cost)`
6. `method(get)`

Only with such a model can

- deep reads
- deep writes
- deep method calls
- mixed chains of members, indices and methods

be cleanly unified.

## Recommended architecture

### Architectural guard rails

If this rebuild is only patched selectively in `visitVariable(...)` and `visitCall(...)`,
the next parallel path will very probably arise.

Therefore these rules should apply:

1. There is exactly **one** parser for access chains.
2. There is exactly **one** resolver for read/write/call receivers.
3. `visitVariable(...)`, `visitSetStmt(...)` and `visitCall(...)` no longer duplicate
   any path logic.
4. Builtin methods get an already correctly resolved receiver and
   should not split string chains again themselves.
5. Property special cases such as `len` are integrated into the same chain path
   and not continued as a second system.

If these 5 points are kept, the rebuild not only straightens the syntax,
but at the same time reduces architectural debt in the compiler.

### Stage 1: access chain data model

New central types, e.g. in `utils.h/.cpp`:

- `enum class AccessSegmentKind`
  - `Base`
  - `Member`
  - `Index`
  - `Method`
- `struct AccessSegment`
- `struct AccessChain`

The parser should produce a normalised chain from an AIML path.

Examples:

- `a.b.c`
- `a.items-0.hp`
- `a.b.map.get`
- `this.player.cards-1.len`

### Stage 2: uniform chain parser

New function:

- `parseAccessChain(const std::string&)`

Requirements:

- understand `.` and `-` together
- normalise `this.` and `<.` beforehand
- not just string splitting, but produce real segment types
- cleanly separate the method name at the end from field segments

This logic must no longer be distributed over:

- `visitVariable(...)`
- `visitSetStmt(...)`
- `visitCall(...)`

## Central resolution

### Stage 3: shared resolver

New core function:

- `resolveAccessChain(ctx, chain, mode)`

`mode` for example:

- `Read`
- `Write`
- `CallReceiver`

The resolver walks segment by segment:

- load the start symbol
- descend into struct members
- respect pointers/references
- resolve list/array indices
- handle container properties
- deliver the final receiver for a method call

### Stage 4: switch read/write/call to the same resolver

#### `visitVariable(...)`

In [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L179)

should:

- keep the namespace/enum special case early
- but send normal variable/field reads completely to `resolveAccessChain(..., Read)`

#### `visitSetStmt(...)`

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

should:

- parse the target as a chain
- deliver the final lvalue via `resolveAccessChain(..., Write)`

#### `visitCall(...)`

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L62)

should:

- no longer split `call->functionName` only into `baseName` + `methodName`
- instead:
  - parse the chain
  - resolve the final receiver
  - treat the last segment as the method name

## Concrete method call rebuild

### Stage 5: receiver resolution for chains

Today:

- `a.b.map.get(...)` is started as a method on `a`

Goal:

- the receiver is `a.b.map`
- the method is `get`

New helper function:

- `splitChainForMethodCall(chain) -> receiverChain + methodSegment`

Then:

1. resolve the receiver chain
2. determine the final receiver type
3. execute the method dispatch for exactly this type

### Stage 6: do not treat builtin methods as special-logic strings

The previous "fieldName + subMethod" special case can then be simplified considerably.

Goal:

- the builtin method dispatch already gets the correct receiver
- no improvised partial resolution via string remainders

This particularly concerns:

- `list<T>`
- `map<K:V>`
- `Kv<K:V>`
- `Store<T>`
- `TimeSeries<T>`
- builtin classes with property handles

## Properties vs. methods

### Stage 7: distinguish container properties cleanly

A clean rule is needed for:

- `a.cards.len`
- `a.cards.len()`

Recommendation:

- internally map both forms to the same property/builtin logic
- but via the same chain resolver

Important:

- property special cases must not become a second parallel path again

## File layout

### `utils.h` / `utils.cpp`

New in here:

- `AccessSegmentKind`
- `AccessSegment`
- `AccessChain`
- `parseAccessChain(...)`
- `resolveAccessChain(...)`
- `splitChainForMethodCall(...)`

### `ast_compiler_expr.cpp`

Rebuild:

- `visitVariable(...)` onto the chain parser + resolver

### `ast_compiler_stmt.cpp`

Rebuild:

- `visitSetStmt(...)` onto the chain parser + resolver

### `ast_compiler_call.cpp`

Rebuild:

- `visitCall(...)` onto the receiver chain model
- `visitObjectMethodCall(...)` should no longer work with half-raw method names

## Implementation order

### sprint_task_1

Title: introduce access chain structures and the parser

Result:

- a parser for `a.b`, `a.b.c`, `a.x-0.y`, `a.b.get`

Affected places:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)

### sprint_task_2

Title: build the shared resolver for reads

Result:

- `visitVariable(...)` reads deep chains cleanly

Affected places:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)

### sprint_task_3

Title: build the shared resolver for writes

Result:

- `set: [a.b.c, ...]` works via the same path

Affected places:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

### sprint_task_4

Title: switch method call receivers to the chain model

Result:

- `a.b.map.get(...)`
- `this.player.hand-1.mana_cost.get(...)`
- `obj.nested.list.len()`

run via real receiver resolution

Affected places:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

### sprint_task_5

Title: clean up builtin dispatch and property accesses

Result:

- no more special-path sprawl
- container properties and methods run consistently

Affected places:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [utils.cpp](../../src/core/utils.cpp)

### sprint_task_6

Title: improve negative diagnostics for deep chains

Result:

- errors such as:
  - unknown intermediate member
  - method on the wrong end type
  - index on a non-container

with a clear message instead of a late IR or runtime error

Affected places:

- [context.h](../../src/core/context.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

## Test plan

Mandatory tests:

1. read `a.b.c`
2. `set: [a.b.c, 7]`
3. `a.items-0.hp`
4. `a.map.get("x")`
5. `a.b.map.get("x")`
6. `this.player.hand-1.name`
7. `this.player.hand-1.mana_cost.get("1")`
8. `a.cards.len`
9. `a.cards.len()`

Negative tests:

1. unknown intermediate member
2. method on a scalar
3. index on a non-list
4. write to a non-writable target

## Acceptance criterion

The change is only successful when users are no longer forced
to create helper variables or special workarounds for simple deep chains such as:

- `a.b.c`
- `this.player.hand-1.name`
- `card.data.cost.get("1")`
