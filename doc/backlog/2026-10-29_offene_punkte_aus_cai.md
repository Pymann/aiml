# Open AIML points from the work on cai

Date: 2026-10-29
Status: backlog
Source: project `Pymann/cai` (cascade AI in AIML). Part of it was found by cai itself
(`cai aiml lerne`: trial programs against the real compiler), the rest while building cai.
All points re-checked on 29.10.2026 on `mem-v3-compiler`; the probe is given in each case.

Already done (for classification only): hashmap<K:V> (spec/69), normalisation of import paths
(tests/import_norm), spec `==`/`!=` -> `eq`/`neq` and `neg` -> `-` (d7766b0), returning borrowed
container elements (`return: { call: [x.get, i] }`, tests/test_hashmap_string_return.aiml).

## High — silently wrong results

### 1. `select` does not exist, but is compiled without a diagnostic and yields 0

```yaml
- decl: [res, i64, { select: [false, 10, 20] }]   # res == 0, expected 20, no error
```
spec/04_1 §8 has been marked "not implemented" since d7766b0.  Decision open: remove it
(if/else is enough) or implement it.  At the very least: an unknown operator must not silently yield 0,
but must be a compile error — otherwise every typo produces the same wrong zero.

### 2. `shl` / `shr` / `ushr` with a negative shift width: undefined value

```yaml
- print: [{ shl: [6, -4] }]   # 0 as a standalone program, -1595265824 in a larger program
```
C behaviour (UB) shows through.  Proposal: mask the shift width (`& 63`) or throw
a runtime error for < 0 / >= 64, and record that in spec/04_1.

### 3. A list literal as a call argument is fanned out

```yaml
- decl: [got, i32, { call: [count_items, [1, 2, 3]] }]
# Module verification FAILED: Incorrect number of arguments passed to called function!
```
Also `[]` as an argument (zero arguments).  Workaround so far: create a local list.  The message
only comes from LLVM verification; what is needed is a real list argument or a clear
error message at the call site.

**OPEN (re-measured 31.10.2026)** — the fix for point 13 does NOT cover this, it only concerned `set`.
The cause, however, is the same place: `OnSequenceStart` only recognises the inner sequence for `decl`
and `set`; for `call` the elements travel one by one via `orderedCallArgs` into the argument list and
are fanned out as a result.  A fix belongs in the same place (recognise the inner sequence for `call` too
→ `BufferLiteralExpr` as ONE argument) and additionally needs a decision on what
the literal is materialised into at the parameter (a temporary list with an ownership rule per spec/61).

### 4. `Kv<string:i32>.get` on a missing key: SIGSEGV

```yaml
- decl: [store, Kv<string:i32>, { call: [db.open_kv, "probe_kv", { persist: false }] }]
- decl: [got, i32, { call: [store.get, "fehlt"] }]   # AIML CRASH (signal 11)
```
`Kv<string:string>` returns `""` in this case.  Expected: 0 or an exception.

## Medium — spec and compiler contradict each other

### 5. `and` / `or` with more than two operands

spec/04_1 lines 131/140: "Accepts 2 or more arguments: `{ and: [a, b, c] }`".
Compiler: "Binary operator 'and' expects exactly 2 operands, got 3."
Either implement it (left fold) or correct the spec.

**Spec contradiction fixed 01.11.2026, the function stays open.**  spec/04_1 (table lines 23/24 and
both sections) said "2 or more", spec/05_logic line 63 "exactly 2", the compiler followed
spec/05_logic.  spec/04_1 is now corrected to exactly two operands and names chaining
`{ and: [a, { and: [b, c] }] }` as the way (re-measured: works).  **What remains open is the
decision** whether the left fold is implemented — the spec no longer promises it.

### 6. `io.read(io.stdin, n)` reads past the end of the line

spec/23 line 20: "Read 100 bytes or until EOF/Newline".  Measured with `printf 'ab\ncd\n'`:
`io.read(io.stdin, 100)` returns `"ab\ncd\n"` (an fread block).  cai therefore reads character by character.
Either read line by line or correct the spec (and possibly offer an `io.read_line`).

**Spec contradiction fixed 01.11.2026, the function stays open.**  Re-measured: `printf 'ab\ncd\n'`
→ `io.read(io.stdin, 100)` returns 6 bytes, so it does not stop at the end of the line.  spec/23 now says
so and names the missing `io.read_line`.  **What remains open** is line-by-line reading.

### 7. `neq` on vectors is missing

`{ eq: [a, b] }` on vec2/3/4 works, `{ neq: [a, b] }` reports "Cannot apply eq/neq to struct type
without __eq__: vec3".  spec/43_0 names `{ not: [{ eq: [a, b] }] }` for now.

**No spec contradiction** (checked 01.11.2026): spec/43_0 line 345 documents the gap honestly.
spec/39 only listed `eq` in the vector table — there is now a `neq` row there with
"not implemented" and a reference, so that a reader of spec/39 does not assume it.  **Function open.**

### 8. `map<K:V>` has no `clear`

"Unknown map method: clear on map<string:i32>".  `hashmap` has `clear`.  For `map` either
add it or name it as missing in spec/10.

**Spec contradiction fixed 01.11.2026, the function stays open.**  spec/10 line 102 listed
`call: [users.clear]` as valid, although the compiler rejects it (re-measured).  The line is
now marked as missing, with a reference to `hashmap` (spec/69) and the route via `remove`.
**What remains open** is a `clear` for `map`.

## Low — ergonomics / design

### 9. Ownership by short name (`ownsReturnMatches`)

Only the return case for string elements is fixed (`isBorrowingAccessor`, type of the receiver).
Other places still decide purely by the short name of the called function whether a
result is fresh or borrowed (ast_compiler_stmt.cpp: decl of models/shared ~line 1622 and
~line 1858; ast_compiler_call.cpp ~line 2244 at push).  A user function with the same short name
(`get`, `at`, `first`, `last`) can trigger the same confusion there.  Proposal: use
`isBorrowingAccessor` or the receiver type everywhere.

### 10. No escaping for `${` in strings

A program that generates AIML source (cai) cannot write `"${x}"` as text; nested
quotes in `${ { … "x" … } }` also break the YAML.  Proposal: `$${` as a
literal `${`.

### 11. Method call on a call result

`{ call: [{ call: [f] }.len] }` is not valid YAML/AIML; it always needs an intermediate variable.
Possibly a chain form (`{ call: [f], then: len }`) or a `pipe`.

### 12. Borrow checker: copying a `ptr<T>` member into another member

Is not flagged (only locally), but leads to double ownership.  cai therefore uses `shared<T>`.

### 13. Assigning a list literal to a member: crash

```yaml
- class:
    name: Holder
    members:
      - [names, list<string>, null, { access: public }]
    methods:
      - def:
          name: fill
          args: []
          body:
            - set: [<.names, ["Knoten", "Text"]]
```

`holder.fill` crashes (SIGSEGV in `aiml_list_handle_incref`).  Found in cai (`AimlGraphLearner.setup`).
Workaround: create a local list (`decl: [names, list<string>, ["Knoten", "Text"]]`) or a
function that returns the entry.  Related to point 3.

**Addendum 31.10.2026 (cai, `ProcTree.branch_step`):** the error does not only affect members, but
every `set:` of a list to a list literal, also locally, with text or variable elements.  `decl`
with a literal works.  Minimal (`aiml d.aiml -r`, build of mem-v3-compiler 0b53eb8):

```yaml
- def:
    name: main
    return: i32
    body:
      - decl: [pick, list<string>, []]
      - set: [pick, ["x", "y"]]
      - print: ["${pick.len}"]      # prints 1818624120 instead of 2, then SIGSEGV
      - return: 0
```

With `- set: [pick, [one, two]]` (two `string` variables) the same: a nonsensical length (7929976), then a crash.
In cai it did not show up as a crash: in a deeply nested loop the code afterwards read
`pick.len` = 1718968690, the condition `pick.len == 4` was false, and a result was silently missing.

Besides that, assigning an empty list does not compile at all:

```yaml
      - decl: [ctx, list<string>, ["a"]]
      - set: [ctx, []]               # Failed to evaluate value expression for assignment to: ctx
```

Workaround in cai: `clear`, then `push` per element.

**DONE 31.10.2026** (this addendum = point 36 in cai's `NOTES.md`).  Both minimal examples
above are reproduced and fixed; they are now test cases in the corpus.
The cause lay in the parser, not in codegen: `StatementBuilder::OnSequenceStart` only recognised the inner
sequence for `decl`.  For `set`, `expectingValue` stayed set, `OnScalar` consumed the
**first** list element as a scalar value and returned early — the rest was silently dropped, and
the scalar ended up in the container slot (garbage in `.len`, then SIGSEGV).  `set: [x, []]` yielded
no value at all for the same reason ("Failed to evaluate value expression").
Fix: the parser builds a `BufferLiteralExpr` for `set`, `visitSetStmt` resolves it into `target.clear` +
one `target.push` per element — via the ordinary method dispatch, so that members,
memV3 handles, string ownership and number conversion apply without a second path.
Spec: spec/10 (semantics: REPLACE, `[]` empties) and spec/02 (`set` takes exactly two elements;
a container takes no scalar).  Tests: `tests/test_set_list_literal.aiml` (6 cases incl.
members), `tests/test_set_arity_error.aiml`, `tests/test_set_container_scalar_error.aiml`.
Along the way it turned out that the existing check against `set: [list, idx, value]` was **dead code**
(it required `!exprArg`, which `expectingValue` always occupied) — now the parser counts the real
scalars and aborts.  Demonstrated: corpus of 997 tests without new failures, the 9rooms game +
client build without a single violation, leak gate 180/180 at 0/0.

## Test suite (for information)

On mem-v3-compiler 12–13 tests fail independently of these points, among them
`test_bundle_*`, `test_pipeline_*`, `test_netfs_smb`, `test_widget_model_bind`, `tests/venv/run.sh`
(moved venv, F68-32 / F68-1).  Each also measured on the state before the cai changes.

**Updated 01.11.2026** (corpus of 997 tests on the merged state): there are **8**, not 12–13 —
five `test_pipeline_*`, `test_bundle_exec_semantics`, `tests/closures/run.sh` (red spec/64 lab)
and `tests/venv/kontor_stub.aiml` (new, time limit 600 s).  `test_netfs_smb`,
`test_widget_model_bind` and `tests/venv/run.sh` no longer fail.
