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
# Out-of-bounds access raises IndexOutOfBoundsException.
- decl: [scores, list<i32>]

# List-Literal-Init: [v1, v2, ...] als 3. Element des decl. Der Compiler emittiert einen push
# pro Element. Numerische Elemente werden in den Elementtyp konvertiert (spec/13, z.B. -1 → i32,
# 2 → f64); alle anderen Elementtypen (string, Modelle, …) werden genau wie bei `push` übernommen
# (spec/61). `[]` ist für jeden Elementtyp die leere Liste.
- decl: [ring_x, list<i32>, [0, 1, 1, 1, 0, -1, -1, -1]]
- decl: [kinds, list<string>, ["beast", "wraith", "construct"]]

# Standard List Methods
- call: [scores.push, 100]       # Add to end
- call: [scores.pop]             # Remove from end (returns value)
- call: [scores.insert, 0, 50]   # Insert at index
- call: [scores.remove, 0]       # Remove at index; raises IndexOutOfBoundsException if out of range
- call: [scores.clear]           # Remove all
- set: [len, scores.len]         # Element count (property, i32)
- set: [is_empty, scores.empty]  # Check if empty (property, bool)

# List Builtins (search, bulk, ordering)
# Equality uses the same semantics as `eq`: strings compare by content
# (null-safe), floats by `oeq`, ints/enums/pointers bitwise/by identity.
# Search methods (contains/index_of/count/remove_value) are NOT available for
# struct/model element types (no canonical model equality yet) — that is a
# compile error with a source location.
- set: [has,  { call: [scores.contains, 100] }]     # -> i32 1/0
- set: [idx,  { call: [scores.index_of, 100] }]      # -> i32 first index, or -1
- set: [hits, { call: [scores.count, 100] }]         # -> i32 number of matches
- set: [gone, { call: [scores.remove_value, 100] }]  # remove FIRST match; -> i32 1/0
- call: [scores.extend, more]    # append value-copies of another list<i32>
                                 # (self.extend(self) is defined; realloc-safe).
                                 # A different list<U> is a compile error.
- call: [scores.reverse]         # in-place reverse
- call: [scores.sort]            # in-place ascending, stable insertion sort
- call: [scores.sort_desc]       # in-place descending
- call: [scores.shuffle]         # in-place Fisher-Yates (uses math.random_int;
                                 # order is non-deterministic — not replay-safe)
# sort/sort_desc support only int, enum, float and string element types.
# Floats: ascending puts NaN toward the end.  shuffle/reverse/extend work for
# any element type (whole values are moved, no comparison needed).

# Access
- set: [val, scores-0]           # Get element at index 0
- set: [subset, scores-1-3]      # Slice: elements at index 1 and 2
- set: [scores-0, 99]            # Set element at index 0

# Memory
- call: [scores.free]            # Explicit free (if no RAII)

# 3. Homogeneous Map
# Syntax: map<KeyType:ValueType>
# Note: The ':' inside '<>' is the type separator, not YAML key syntax.
- decl: [users, map<string:i32>]

# Map-Literal-Init: {key: value, ...} als 3. Element des decl.  Compiler emittiert
# einen .put-Call pro Eintrag.  Key-Typen: string, i32, i64, enum.  Values dürfen
# beliebige Expressions sein (Literale, calls, ...).
- decl: [scores, map<string:i32>, {alice: 1, bob: 2, charlie: 3}]

# Enum-typed keys: das Literal nutzt die Enum-VALUE-Namen als Schlüssel; der
# Compiler löst sie beim Emit direkt in die zugehörige i32-Konstante auf.
# Vorteil ggü. `map<i32:i32>` mit reinen Zahl-Keys: (1) Review-freundlich —
# man sieht sofort was gemeint ist, (2) Typo-Safety — unbekannter Enum-Wert
# → Compile-Error statt still falscher Zahl.
- enum: { name: Color, values: [red, green, blue] }
- decl: [weight, map<Color:i32>, {red: 100, green: 200, blue: 300}]
- set: [wg, { call: [weight.get, Color.green] }]   # → 200

- call: [users.put, "alice", 1]
- set: [val, { call: [users.get, "alice"] }]   # returns i32 or null pointer if missing
- call: [users.remove, "alice"]                # raises MissingKeyError if key not found
- call: [users.clear]
- set: [len, users.len]

# Existence check via get() + null comparison:
- set: [maybe_alice, { call: [users.get, "alice"] }]
- if:
    condition: { eq: [maybe_alice, null] }
    then:
      - print: ["Key not found"]

# 4. Heterogeneous Map — map<string:any>
# Values are stored as tagged variants: the runtime keeps the real type alongside the value.
# get() auto-unboxes: the returned value has the REAL runtime type.
# set: infers the correct type automatically.
# Comparisons work on the returned value because the real type is known at runtime.
- decl: [meta, map<string:any>]

- call: [meta.put, "count", 42]          # stored as i32 (tagged)
- call: [meta.put, "label", "hello"]     # stored as string (tagged)
- call: [meta.put, "ratio", 3.14]        # stored as f64 (tagged)

# Auto-unboxed retrieval — set: infers real type:
- set: [count, { call: [meta.get, "count"] }]  # count is i32
- set: [label, { call: [meta.get, "label"] }]  # label is string

# Comparisons work because the real type is returned:
- if:
    condition: { eq: [count, 42] }
    then:
      - print: ["count matches"]

# Null check still required for missing keys:
- set: [missing, { call: [meta.get, "nonexistent"] }]
- if:
    condition: { eq: [missing, null] }
    then:
      - print: ["Key not found"]
```

## Size vs Len

| Attribute | Syntax | Returns | Description |
|-----------|--------|---------|-------------|
| `.len` | `obj.len` (property) | `i32` | Element/character count |
| `.size` | `{ call: [obj.size] }` (method) | `u64` | Byte count in memory |

`.len` is available on `list`, `map`, and `string`.
`.size` is an intrinsic method available on all types.

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

A pointer expression cannot be pushed directly into an aggregate value list. This is
a compile-time error for variables, members and nested calls alike. For example,
`destination.push(source.get(index))` is invalid when both lists hold model values:
`get()` returns an element pointer. Declare a local `MyModel` initialized from the
`get()` result, then push that value; use `list<ptr<MyModel>>` when pointer sharing
is intended. Calls returning actual model values remain valid push arguments.

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
changes, push/pop/put/remove, and any data-buffer growth all propagate
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
      - call: [users.put, "bob", 7]

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
  `xs.get(i)`, `${xs.len}`, …).
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
value** — push/put inside a function silently did not propagate.  Code
written under that model is unaffected if it relied solely on element-
value mutation via the shared `data` pointer (which already worked).
Code that pushed onto an arg and **returned the list** (the old
work-around) still works — the return path is unaffected.  The new
behaviour only *adds* propagation for size/capacity changes.
