# Collections & Complex Types

```yaml
# 1. Fixed-Size Arrays (Primitive)
# Allocated on Stack (via 'alloca' array type).
# Out-of-bounds access raises IndexOutOfBoundsException.
# Syntax: array<T:N>. (The deprecated `type-size`/`name-index` hyphen forms are removed — spec/01.)
- decl: [coords, "array<i32:3>"]
- idx: [coords, 0, 10]              # write element 0; read with { at: [coords, 0] }

# 2. Dynamic List (Strongly Typed)
# Syntax: list<T>
# Out-of-bounds access raises IndexOutOfBoundsException (F10-23, spec/71 §4): `at` reads, `idx`
# writes, `remove` (index in [0, len)), `insert` (index in [0, len]) and `pop` on an empty list;
# the message is "index 5 of 2".  Until 2026-10-29 `at` read past the end silently (garbage or 0).
# The names of every collection operation are spec/71 (the matrix); this chapter shows them on
# list and map.
- decl: [scores, list<i32>]

# List literal init: [v1, v2, ...] as the 3rd element of the decl. The compiler emits one push
# per element. Numeric elements are converted to the element type (spec/13, e.g. -1 → i32,
# 2 → f64); all other element types (string, models, …) are taken over exactly as with `push`
# (spec/61). `[]` is the empty list for every element type.
- decl: [ring_x, list<i32>, [0, 1, 1, 1, 0, -1, -1, -1]]
- decl: [kinds, list<string>, ["beast", "wraith", "construct"]]

# An element may be an ARBITRARY expression, just like a map literal value further
# down and just like the argument of `push` — a `{ call: ... }`, an arithmetic expression, an
# interpolation. Until 2026-11-04 an expression as an element was silently dropped: the parser only
# collected the SCALARS of the sequence, and the map ended up in the value slot of the decl. If it was at the end,
# the list was one element shorter; if it was at the FRONT, the decl took it as the whole initial value and
# wrote a string into the list slot — from then on `.len` was garbage and the heap damaged.
# Both without a warning. In cai it cost two log layers: `["P", {call: …}]`
# wrote only "P", on replay 731 and 736 propositions collapsed onto ONE sha256
# key (the two layers became a single row), while the facts
# of the same layers merged correctly — the bug was invisible as long as one only
# looked at the fact count. Safety net: tests/test_call_in_list_literal.aiml.
- decl: [tags, list<string>, ["P", { call: [serialize_row, row] }, "${name}"]]
- decl: [ratios, list<f64>, [1.0, { "/": [sum, count] }]]

# Assignment works the same way: `set: [list, [v1, v2, ...]]` REPLACES the content — the compiler
# emits a `clear` and then one `push` per element, with the same conversions as
# for the decl. `[]` empties the list. Applies equally to a member (`set: [<.names, ["a", "b"]]`)
# and to buffer<T>. Until 2026-10-31 `set` only took the FIRST element (the rest was silently
# dropped) and wrote it into the container slot — broken list, then SIGSEGV; `[]` did not
# compile at all.
- set: [kinds, ["golem", "spirit"]]
- set: [kinds, []]

# As a CALL ARGUMENT a list literal is ONE argument: the compiler creates a
# fresh list for it and fills it. The element type comes from the PARAMETER TYPE — only that way is
# `[]` determinable; otherwise it is derived from the first element. Until 2026-11-01 the
# literal was fanned out into individual arguments and only LLVM verification reported
# "Incorrect number of arguments".
- set: [num, { call: [count_items, [1, 2, 3]] }]
- set: [num, { call: [count_items, []] }]

# The same applies as a RETURN VALUE: `return: [a, b]` returns a fresh list, and
# the element type is given by the DECLARED return type (`return: list<f32>` turns `[1, 2]`
# into two floats, not two integers) -- that is why `return: []` works too.  The container
# is handed to the caller, not freed on the return path (spec/61 §6.2).
# Until 2026-11-21 the elements ended up in the parser's argument storage, the body got
# no value at all, and the function returned an UNDEFINED return value: `len`
# was garbage (measured 1768256100) and the first access crashed.
# Test: tests/test_return_list_literal.aiml.
- return: ["eins", "zwei"]
- return: []

# WHETHER a sequence after `return:` is a list literal is decided by the declared
# return type -- `return: [{ "+": [x, 1] }]` is the notation used everywhere
# for ONE value:
#   return type list<T>/buffer, one element of exactly this type    -> pass-through
#   return type list<T>/buffer, one element from a CALL             -> pass-through
#   return type list<T>/buffer, otherwise                           -> list literal
#   other return type, exactly one element                          -> pass-through
#   other return type, EMPTY (`return: []`)                         -> bare exit
#   other return type, several elements                             -> ERROR
# The second-to-last case used to be silent: the LAST element won, an i32 function returned
# `ret i32 9`.  Test: tests/test_return_list_no_list_error.aiml.
#
# The call row was added on 2026-11-21: the type of a call cannot be determined at this
# point, and `return: [{ call: [bytes.concat, ...] }]` in a
# `buffer<u8>` function was read as a container literal whose element type
# nobody knows -- nine tests failed (test_bytes, test_pipeline_*).  A call
# almost always returns the finished container, so pass-through applies; whoever really
# wants a container WITH a computed element writes it into a
# variable and returns that.
- return: [{ "+": [wert, 1] }]      # i32 function: the value itself
- return: [innen]                   # list<string> function, `innen` is list<string>: pass-through
- return: []                        # void function: simply leave (118 places in the corpus)

# Reading and WRITING one element.  There is no `list.set` method: a write is the `idx:`
# STATEMENT, the same form as for an array above.  `insert`/`remove` are for changing the
# LENGTH — reaching for them to overwrite an element shifts everything after it.
- idx: [scores, 0, 42]              # write element 0
- set: [first, { at: [scores, 0] }] # read element 0
# `Unknown list method: set` names `idx:` in its message (2026-11-03).

# Standard List Methods
- call: [scores.push, 100]       # Add to end
- call: [scores.pop]             # Remove from end (returns value); empty → IndexOutOfBoundsException
- call: [scores.insert, 0, 50]   # Insert at index — grows the list, shifts the rest
- call: [scores.remove, 0]       # Remove at index; raises IndexOutOfBoundsException if out of range
- call: [scores.clear]           # Remove all
- set: [len, scores.len]         # Element count (property, i32)
- set: [is_empty, scores.empty]  # 1 if there is no element (property, i32)
- set: [bytes, scores.byte_len]  # len × element size (property, i32)
- set: [scores.capacity, 100]    # at most 100 elements; 0 (the default) = unbounded (spec/71 §5);
                                 # push/insert/extend past it raise CapacityExceededException

# List Builtins (search, bulk, ordering)
# Equality uses the same semantics as `eq`: strings compare by content
# (null-safe), floats by `oeq`, ints/enums/pointers bitwise/by identity.
# Search methods (contains/index_of/count_of/remove_value) are NOT available for
# struct/model element types (no canonical model equality yet) — that is a
# compile error with a source location.
- set: [has,  { call: [scores.contains, 100] }]     # -> i32 1/0
- set: [idx,  { call: [scores.index_of, 100] }]      # -> i32 first index, or -1
- set: [hits, { call: [scores.count_of, 100] }]      # -> i32 number of matches
- set: [gone, { call: [scores.remove_value, 100] }]  # remove FIRST match; -> i32 1/0
- call: [scores.extend, more]    # append value-copies of another list<i32>, or of a
                                 # literal: [scores.extend, [1, 2]]
                                 # (self.extend(self) is defined; realloc-safe).
                                 # A different list<U> is a compile error.
                                 # Refcounted elements (string, models, nested
                                 # containers) become CO-OWNED: every appended copy
                                 # gets an incref (spec/61 §5.4), as with `slice`.
                                 # Otherwise the target holds freed pointers as soon as the
                                 # source dies.
- call: [scores.reverse]         # in-place reverse
- call: [scores.sort]            # in-place ascending, stable, O(n log n) (F10-20)
- call: [scores.sort_desc]       # in-place descending
- call: [scores.shuffle]         # in-place Fisher-Yates (uses math.random_int;
                                 # order is non-deterministic — not replay-safe)
# sort/sort_desc support only int, enum, float, bool and string element types.
# Strings sort by byte order (strcmp); unsigned element types compare unsigned.
- decl: [line, string, { call: [kinds.join, ", "] }]   # list<string> only: "beast, wraith, construct" (F10-21)
# Floats: ascending puts NaN toward the end.  shuffle/reverse/extend work for
# any element type (whole values are moved, no comparison needed).

# Slice: a fresh list with the elements at index 1 and 2
- set: [subset, { call: [scores.slice, 1, 3] }]

# Memory
- call: [scores.free]            # Early release; otherwise released at owner drop (spec/61)

# 3. Homogeneous Map
# Syntax: map<KeyType:ValueType>
# Note: The ':' inside '<>' is the type separator, not YAML key syntax.
- decl: [users, map<string:i32>]

# Map literal init: {key: value, ...} as the 3rd element of the decl.  The compiler emits
# one .set call per entry.  Key types: string, i32, i64, enum.  Values may
# be arbitrary expressions (literals, calls, ...).
- decl: [scores, map<string:i32>, {alice: 1, bob: 2, charlie: 3}]

# Enum-typed keys: the literal uses the enum VALUE names as keys; the
# compiler resolves them directly into the corresponding i32 constant at emit time.
# Advantage over `map<i32:i32>` with plain number keys: (1) review-friendly —
# one sees immediately what is meant, (2) typo safety — an unknown enum value
# → compile error instead of a silently wrong number.
- enum: { name: Color, values: [red, green, blue] }
- decl: [weight, map<Color:i32>, {red: 100, green: 200, blue: 300}]
- set: [wg, { call: [weight.get, Color.green] }]   # → 200

- call: [users.set, "alice", 1]
- set: [val, { call: [users.get, "alice"] }]      # the value; a missing key gives 0 (spec/71 §3)
- set: [val, { call: [users.get, "nobody", -1] }] # get(k, d): d when the key is missing
- set: [had, { call: [users.has, "alice"] }]      # i32 1/0
- set: [had, { call: [users.delete, "alice"] }]   # i32 1 if the key was there, else 0
- call: [users.clear]                             # all entries; storage stays, immediately reusable
- set: [len, users.len]
- set: [users.capacity, 1000]                     # at most 1000 keys (spec/71 §5)
- decl: [names, list<string>, { call: [users.keys] }]   # map<string:V> only: every key, byte order (F10-22)

# `get` never throws.  A missing key yields the zero value of V — 0 for numbers, the null
# pointer for strings and handles — or the default of get(k, d).  To tell a missing key from
# a stored zero, ask has(k):
- if:
    condition: { eq: [{ call: [users.has, "alice"] }, 0] }
    then:
      - print: ["Key not found"]

# 4. There is no heterogeneous map: `map<K:any>` is a compile error (spec/02 — `any` is never a
# container element).  Heterogeneous values go into an interface list (`list<ptr<Iface>>`,
# spec/19 §2) or a common base model.
```

## Queries: `.len`, `.empty`, `.byte_len`, `.capacity`

| Property | Returns | Description |
|----------|---------|-------------|
| `.len` | `i32` | Element count |
| `.empty` | `i32` | `1` if there is no element |
| `.byte_len` | `i32` | Bytes of the elements (`list`: `len` × element size) |
| `.capacity` | `i32` | Most elements the collection takes; `0` = unbounded; settable with `set:` |

They are properties, never calls — `{ call: [xs.len] }` is a compile error that names `xs.len`.
Which type has which query is the table of spec/71 §1.1.  The size of a **type** in bytes is
`sys.size(x)` (spec/23), the way C's `sizeof` is.

## Struct/Model Elements in Lists

`list<T>` supports any element type `T` — primitives, strings, pointers, and user-defined
models/classes (structs). The storage semantics depend on `T`:

| Element type | Storage | push semantics | get semantics |
|-------------|---------|----------------|---------------|
| `i32`, `f32`, ... | value in array | value copy | value copy |
| `string` (`i8*`) | pointer in array | pointer copy | pointer copy |
| `ptr<MyModel>` | pointer in array | pointer copy (shared heap object) | pointer copy |
| `MyModel` (struct) | full struct in array | full struct copy (value semantics) | pointer to the element; a typed model local materializes a value copy |

**Implementation**: `list.push(val)` does `CreateStore(val, &data[size])` — the LLVM value
is stored directly into the data array. For structs this is a full byte-for-byte copy
(`sizeof(MyModel)` bytes per element). For pointers it is a target-pointer-sized copy (4 bytes on wasm32, 8 bytes on a 64-bit native target).
A numeric value is first converted to the element type: `[vals.push, 1.5]` on a `list<f64>`
stores a double (a float literal is `f32`), an integer pushed into a float list converts its value.

A pointer expression — a `ptr<T>`/`ref<T>`/`shared<T>` variable, member or call result —
cannot be pushed directly into an aggregate value list. This is a compile-time error for
variables, members and nested calls alike. `{ at: [source, index] }` is different: pushed
into a value list it is **copied** (a value read of the element), pushed into a
`list<ref<MyModel>>` it is the element's **address** (a borrow, spec/61 §5.4).  Returned from
a function whose return type is `ref<MyModel>` / `ptr<MyModel>`, or `set:` into such a
variable, it is the address as well.  Use `list<ptr<MyModel>>` only for boxes the list owns.
Calls returning actual model values remain valid push arguments.

**As an argument**, by contrast, an element pointer **is** accepted where the parameter is a
model by value: the element is loaded and the callee gets a value copy, exactly as it would
through an intermediate local.  This holds for a method and for a free function alike.

```yaml
# `line(score: Score)`; `scores` is a list<Score>
- call: [<.line, { at: [<.scores, 0] }]      # ok — the element is loaded
```

Until 2026-11-03 only the free-function path did this.  A method call reached LLVM as
`call @Holder_line(ptr, ptr)` against a by-value parameter and the module failed verification
("Call parameter type does not match function signature"), while the same call written through
an intermediate local worked.

**Consequence**: `list<MyModel>` and `list<ptr<MyModel>>` are both valid but have
fundamentally different semantics:

```yaml
# Value semantics — each element is an independent copy.
# Modifying the original after push does NOT affect the list element.
- decl: [items, list<MyModel>]
- decl: [m, MyModel]
- set: [m.name, "alpha"]
- call: [items.push, m]
- set: [m.name, "beta"]      # items-0.name is still "alpha"

# Pointer semantics — elements share the heap object.
# Modifying via the pointer affects ALL references.
- decl: [items, list<ptr<MyModel>>]
- decl: [m, "ptr<MyModel>"]
- set: [m, { new: MyModel }]
- set: [m.name, "alpha"]
- call: [items.push, m]
- set: [m.name, "beta"]      # items-0.name is now "beta" (same heap object)
```

**When to use which**:
- `list<MyModel>` — lightweight data records, no shared mutation needed
- `list<ptr<MyModel>>` — shared objects, objects returned from runtime APIs (VecResult, zip_entry)

## Built-in Generic Types

The generic type parameters (`list<T>`, `map<K:V>`, `ptr<T>`, `chan<T>`) are **built-in only**.
User-defined generic types or generic functions are not supported in AIML v1.

## Pass-by-Reference for `list<T>` / `map<K:V>` Function Arguments

When a `list<T>` or `map<K:V>` is declared as a function parameter, the
compiler passes it **by reference** — analogous to Python and Java
semantics.  The callee operates on the caller's struct directly; size
changes, push/pop/set/delete, and any data-buffer growth all propagate
back to the caller.

```yaml
- def:
    name: push_to
    args: [[xs, list<i32>]]
    body:
      - call: [xs.push, 99]
      # xs.len here reflects the modified state.

- def:
    name: main
    body:
      - decl: [scores, list<i32>]
      - call: [scores.push, 1]
      - call: [scores.push, 2]
      # scores.len == 2

      - call: [push_to, scores]
      # scores.len == 3  ← change propagated
```

The same applies to maps:

```yaml
- def:
    name: register
    args: [[users, map<string:i32>]]
    body:
      - call: [users.set, "bob", 7]

- decl: [m, map<string:i32>]
- call: [register, m]
# m has {"bob": 7} after the call.
```

### Internal Representation

At the LLVM level:

- A by-value `list<T>` variable is a stack-allocated 3-field struct
  `{ i32 size, i32 capacity, ptr data }`.
- A `list<T>` **function argument** is lowered to an opaque pointer
  (`ptr`) in the LLVM signature.  At the call-site the compiler passes
  the *address* of the caller's struct (`alloca`).  Inside the callee
  the parameter symbol carries `is_byref_arg = true` and the auto-deref
  is inserted at every read/write site (`xs.len`, `xs.push`,
  `{ at: [xs, i] }`, `${xs.len}`, …).
- Other functions called with the same parameter as argument
  (chained calls) receive the *same* pointer — the by-ref semantics
  flow through the whole call chain without copy.

### Need a copy instead?

When a function genuinely wants a local working copy that must not leak
mutations back, use [`sys.clone`](29_3_sys_clone.md) explicitly:

```yaml
- def:
    name: sorted_copy
    args: [[xs, list<i32>]]
    return: list<i32>
    body:
      - decl: [out, list<i32>, { call: [sys.clone, xs] }]
      - call: [out.sort]
      - return: out
```

After this call, the caller's `xs` is unchanged and the returned `out`
is the independently-sorted copy.

### Comparison to Other Types

| Argument type      | Passing semantics       |
|--------------------|-------------------------|
| `i32`, `f64`, …    | by value (primitive)    |
| `string` (`i8*`)   | by reference (interned/shared)  |
| `list<T>`          | **by reference (implicit)** |
| `map<K:V>`         | **by reference (implicit)** |
| `MyStruct` (model) | by value (struct copy)  |
| `ptr<MyStruct>`    | by reference (explicit) |
| `ref<MyStruct>`    | by reference (explicit, future borrow-checked) |

Class methods follow class semantics: a class's own member methods
implicitly receive `ref<ClassName>` as their `this` argument (see
[16_classes.md](16_classes.md)).

### Migration from Earlier AIML

Earlier compiler versions passed `list<T>`/`map<K:V>` arguments **by
value** — push/set inside a function silently did not propagate.  Code
written under that model is unaffected if it relied solely on element-
value mutation via the shared `data` pointer (which already worked).
Code that pushed onto an arg and **returned the list** (the old
work-around) still works — the return path is unaffected.  The new
behaviour only *adds* propagation for size/capacity changes.
