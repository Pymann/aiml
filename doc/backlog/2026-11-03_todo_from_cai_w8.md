# AIML to-do list from the cai work on W5–W8

Date: 2026-11-03
Status: **worked through** (see "Result" per point; as of the evening of 2026-11-03)
Source: cai branch `feature/weighted-symbolic-reasoning` (rule weights, causal export, TransE with the
tensor runtime). Every point was checked with a small program against the compiler at `43144207`
(`aiml FILE -r`); the programs are listed below. What was only observed and not reproduced small is listed
under "Observed".

## Result at a glance

| # | Point | Result |
|---|-------|----------|
| 1 | f32 literals in an f64 context | **fixed** — the literal is formed in the context type (spec/04_1 §7a table), `tests/test_f64_literals.aiml` |
| 2 | List of classes: get→remove→insert | **not reproducible** — kept as a regression test (`tests/test_list_model_mutate.aiml`, model AND class variant green) |
| 3 | Method call as a value argument of class type | **fixed** — the element pointer is loaded as in the free-function path (spec/10), test in the same file |
| 4 | `math.exp`/`log` only f32 | **fixed** — width by argument OR result context (spec/43_0 §7.1) |
| 5 | `string.parse_f64` | **added** (spec/09) |
| 6 | Fixed decimal places | **added** as `sys.format_fixed` (spec/23 F23-78) |
| 7 | Tensor randomness not seedable | **added** as `tensor.seed` + its own splitmix64 (spec/50 §17.1) |
| 8 | `begin_graph` frees user-created tensors | **documented + `t.keep`** (spec/50 §14.1) |
| 9 | Quotes in `${ { … } }` | **documented** (spec/09_1 §3.4) + the compiler names the affected lines |
| 10 | Element writing hard to find | **fixed** — `idx:` is now in the list section, and `Unknown list method: set` names it |
| 11 | `try` in a nested `if` | **not reproducible** — reduction green (`tests/test_try_in_nested_if.aiml`); the cai cause lay elsewhere |

Side findings on the `sys.arg_dump` fix of the same branch: two fields answering to the same flag
are now a compile error, and an alias `given` is reserved like `positional`
(spec/23 F23-63, cases in `tests/test_arg_dump_strict.aiml`).

**Correction of my earlier remarks in cai:**
- "There is no `parse_f64`" falls short. It works with `cast`: `{ cast: [{ call: [s.parse_f32] }, f64] }`.
  Only the precision is open, see point 5.
- "`list.set` is missing" is wrong. You write an element with `idx: [list, i, value]` (spec/10 §2). That
  also works for lists of classes (p2). The error I worked around in cai is a different one:
  point 2.

## Bugs (reproduced)

### 1. Floating-point literals are f32, even in an f64 context — high

```yaml
- decl: [val_a, f64, 0.1234567891]          # gives 0.123456791043282
- decl: [val_b, f64, { "/": [1.0, 3.0] }]   # gives 0.333333343267441
- decl: [one, f64, 1.0]
- decl: [three, f64, 3.0]
- decl: [val_d, f64, { "/": [one, three] }] # gives 0.333333333333333 (correct)
```

The literal is apparently produced as f32 and only widened to f64 afterwards. Computing with f64 variables is
exact, literals are not.

- **Consequence:** every constant in an f64 computation loses precision from the 8th digit on. In cai, for example,
  learning rates and thresholds are affected.
- **Expected:** a literal in an f64 context (`decl … f64`, operand of an f64 computation) is produced as f64,
  as in spec/04_1 §7 ("`f32` → `f64` is always exact" presupposes an f64 starting value).

### 2. List of classes: get → change → remove → insert destroys strings — high

```yaml
- decl: [cur, Item, { call: [items.get, row] }]
- set: [cur.part, "test${row}"]
- call: [items.remove, row]
- call: [items.insert, row, cur]
# afterwards: items[0].name and items[1].name are memory garbage ("���"), items[2] is right
```

Presumably `remove` frees the element's strings that `cur` still holds as well (`get` returns a
copy without an incref?). Rebuilding the list with `push` works; I did not check `idx` here.

- **Expected:** according to spec/61 `cur` holds its own references, and `remove` must not free them.

### 3. Method call as a value argument of class type: wrong IR — medium

```yaml
- print: [{ call: [<.line, { call: [<.scores.get, 0] }] }]   # line(score: Score)
# Function 'Holder_show' verification FAILED: Call parameter type does not match function signature!
#   %call_method = call ptr @Holder_line(ptr %this_ptr, ptr %12)
```

With an intermediate variable (`decl: [score, Score, { call: [<.scores.get, 0] }]`) it works.

- **Expected:** the same behaviour as with the intermediate variable, or a compiler error with a location instead of
  "LLVM verification failed".

### 4. `math.exp`, `math.log` etc. only compute in f32 — medium

```yaml
- decl: [val_exp, f64, { call: [math.exp, 1.0] }]   # 2.71828174591064 (e = 2.718281828459045)
```

spec/43_0 §7 only documents f32. For statistics and learning (logistic regression in cai W3)
f64 versions are missing.

- **Proposal:** `math.exp`/`log`/`sqrt`/`pow` with an f64 argument compute in f64 (overload), or separate
  names (`math.exp64` …).

## Missing building blocks

### 5. `string.parse_f64`

`parse_f32` + `cast` works, but loses precision from the 8th digit: `"0.123456789"` → `0.123456791043282`. What is needed
is a `strtod` counterpart, like `parse_f32` (spec/09 "`string.parse_f32()`").

### 6. Formatting numbers with a fixed number of decimal places

Interpolation writes f64 with up to 15 digits (`0.333333333333333`), f32 with 6. For reports something
like `{ call: [sys.format_fixed, value, 3] }` → `"0.333"` is missing. Today cai computes via whole
thousandths for that.

### 7. The tensor runtime's randomness cannot be seeded

`tensor.randn`/`randn_ex` use `rand()` (Box-Muller in `aiml_tensor_randn`). There is no seed, so
no reproducible starting values across processes and platforms. cai therefore builds its own starting values with
an LCG.

- **Proposal:** `tensor.seed(n)` with its own platform-independent generator.

### 8. Docs: `begin_graph` also frees user-created tensors

`aiml_tensor_begin_graph` collects tensors with `is_param=1`, without a gradient, without a graph node and with
`alloc_epoch < epoch-1`. That also hits index or constant tensors the user created before the loop,
for example `tensor.zeros(n)` for identifiers. In the second round `embedding_lookup` then crashes
with a nonsensical shape.

- spec/50 §14 only says "resets the graph".
- **Proposal:** describe there how Adam states are protected (`adam_step` refreshes the epoch).
  Or a way to mark a tensor as permanent (`t.keep`).

## Diagnostics and usability

### 9. Nested quotes in `${ { call: … } }`

`"… ${ { call: [<.f, "x"] } } …"` ends as a YAML error ("end of map not found"), often lines away from the
error. The remedy is an intermediate variable.

- **Proposal:** name the case in spec/09_1, and have the compiler point to the cause for YAML errors in lines with `${ {`.

### 10. Element writing hard to find

`idx:` is only in the header of spec/10 §2. Whoever looks for `list.set` finds nothing; I used
`remove`/`insert` instead and ran into point 2.

- **Proposal:** on `Unknown list method: set` point to `idx: [list, i, value]`.

## Observed, not reproduced small

### 11. `try` in a nested `if` of a long method

In cai `Cli.run` (over 250 lines) a `try` inside an `if` afterwards stopped `this.out` and other methods
from resolving ("Unknown method"). After moving it out into a method of its own it worked. A small
program for it is still pending.

## Programs

The programs are `p1.aiml` to `p5.aiml` in the scratchpad of the cai session. The necessary parts are here:

- **p1 (point 2):** `class Item {name, part: string}`. Push three elements, then per row get → change `part`
  → remove → insert, then print.
- **p2 (correction):** `idx: [items, 0, neu]` and after that `{ at: [items, 0] }`. Result `neu`, correct.
- **p3/p4 (points 1, 4, 5):** see the blocks above.
- **p5 (point 3):** `class Holder { scores: list<Score>; line(score: Score); show() }`. In `show`:
  `{ call: [<.line, { call: [<.scores.get, 0] }] }`.
