# Refs and Pointers

This document specifies AIML's pointer-like types. The key takeaways:

1. **Today**: `ref<T>` and `ptr<T>` are functionally identical
   (both lower to `T*` opaque pointers, neither is borrow-checked).
2. **Soon**: `ref<T>` will get real borrow semantics — no `free`, no
   ownership transfer, must not outlive the storage it points into.
3. **Therefore**: new code MUST follow the borrow-safe convention below.
   Code written under the current alias may break when borrow-checking lands.

## Status

| Type    | LLVM lowering        | Borrow-checked | Lifetime-tracked | Free required |
| ------- | -------------------- | -------------- | ---------------- | ------------- |
| `ptr<T>`| `T*` (opaque ptr)    | no             | no               | yes (manual)  |
| `ref<T>`| `T*` (opaque ptr)    | no             | no               | yes (manual)  |

There is no functional difference today:

- The LLVM type resolver maps both to the same opaque pointer
  ([src/core/context.h](../src/core/context.h) lines 449-470).
- Every place in the compiler that recognises a "pointer target" checks
  `ptr<...> || ref<...>` — for example interface-upcast source detection
  ([src/core/ast_compiler_stmt.cpp:358-361](../src/core/ast_compiler_stmt.cpp)),
  the `kv.get` view check ([line 316](../src/core/ast_compiler_stmt.cpp)),
  the serialiser branch in
  [serialize_impl.cpp](../src/core/serialize_impl.cpp), and method dispatch in
  [ast_compiler_call.cpp:516](../src/core/ast_compiler_call.cpp).
- No borrow checker, lifetime tracker, or move semantics exists.

## Borrow-safe convention (mandatory for new code)

Use this table to pick `ref<T>` vs `ptr<T>`. Code written to these rules
will keep compiling once borrow-checking lands.

| Pattern                                            | Choose      |
| -------------------------------------------------- | ----------- |
| Implicit method `this` (compiler-injected)         | `ref<T>` ✓  |
| Function parameter — borrowed, no free, no store   | `ref<T>` ✓  |
| Function parameter — taking ownership / freeing    | `ptr<T>`    |
| `decl: [x, _, { new: T }]`                         | `ptr<T>`    |
| Return value from a function                       | `ptr<T>`    |
| Element type of `list<...>` / `map<K:...>`         | `ptr<T>`    |
| Struct field holding a heap object                 | `ptr<T>`    |
| KV view: `decl: [v, _, { call: [kv.get, k] }]`     | `ptr<T>`    |

**Rule of thumb**: anything that might outlive the scope it was created in
must be `ptr<T>`. A `ref<T>` should only ever flow *into* a function (as a
parameter), never *out of* it (return value, stored field, list element).

## Implicit `ref<T>` injection — the `<.` operator

The class-method `this` parameter is injected automatically by the compiler
as `ref<ClassName>` (see
[src/core/ast_compiler.cpp:906](../src/core/ast_compiler.cpp)):

```yaml
- class:
    name: Counter
    models: [CounterModel]
    methods:
      - def:
          name: bump
          args: []                          # user wrote no args
          # Compiler injects: { name: "this", typeName: "ref<Counter>" }
          # at args[0] before this entry.
          body:
            - set: [<.value, { +: [<.value, 1] }]
            # ^^                ^^
            # `<.` desugars to `this.` — i.e. reads through the implicit
            # ref<Counter>.
```

Every `<.field` and `<.method()` reads through the implicit `ref<Counter>`
argument. This is a genuine borrow: `this` doesn't outlive the call.

## Interface upcast — `ptr<Concrete>` → `ptr<Interface>`

An interface pointer is a **fat pointer** `AimlFatPtr_<Iface> = {i8* obj,
i8* vtable}`. Assigning a concrete-class pointer into an interface-typed
slot upcasts it: the compiler emits the `{obj, __vtable_<Class>_<Iface>}`
struct (see `Compiler::buildInterfaceUpcast`). This happens implicitly in
every context that has both the source class name and the target interface:

- **decl** — `decl: [b, ptr<CardBase>, concrete]`
- **method / call argument** — passing `concrete` to a `ptr<Iface>` param
- **container insert** — `list<ptr<Iface>>.push(concrete)`, `map.put(k, concrete)`
- **return** — `return: obj` where `obj` is `ptr<Concrete>` and the function
  returns `ptr<Iface>`. The fat pointer is built at the return site; a bare
  `return` of a concrete pointer does **not** reinterpret the object's first
  bytes as a fat pointer (that produced a garbage vtable → SIGSEGV before the
  2026-07-10 fix). Canonical: `test_peq_iface_ptr_null.aiml`.

`return: null` for an interface return type yields a zero fat pointer
`{obj=null, vtable=null}`; test it with `peq [x, null]` / `pneq [x, null]`
(plain `eq` has no `__eq__` for fat pointers). `peq`/`pneq` compare the
`obj` slot, so a freshly-built interface pointer is non-null and a
`return: null` one is null.

## Serialisation

As of 2026-05-23, `list<ptr<T>>` is **serialisable** when `T` is a
registered struct. The serialiser dereferences each slot, writes the inner
struct inline as a JSON object, and on deserialisation allocates a fresh
heap struct per element. See `test_iface_list_ptr_serialize.aiml` for the
canonical pattern.

`list<ref<T>>` also works today (the compiler treats both the same), but
it is **not borrow-safe** — pushing into a list is an ownership transfer.
Use `list<ptr<T>>` for any list that survives the scope it was filled in.

Plain `ptr<T>` / `ref<T>` **fields** of a struct remain non-serialisable —
the round-trip would lose pointer identity. Use a `list<...>` wrapper if
you need to pass referenced data over the wire.

## Known technical debt

Existing test files use `ref<T>` for owning pointers (because the alias
permitted it). These will need migration when borrow-checking lands:

- `tests/test_chan_class_member.aiml` — `decl: [box, ref<ChannelBox>, { new: ... }]`
- `tests/test_buffer_class_member_heap_init_repro.aiml` — same pattern
- `tests/test_list_class_member_heap_init_repro.aiml` — same pattern
- `tests/test_map_class_member_heap_init_repro.aiml` — same pattern
- `tests/test_oo.aiml` — multiple sites

New tests should follow the borrow-safe convention above. The four
`test_iface_*` files (post-2026-05-23) already do.

## Roadmap

Planned future work — currently neither scheduled nor designed:

- **Borrow checking for `ref<T>`** — disallow `free` on `ref<T>` values,
  enforce that no `ref<T>` outlives the storage it points into, forbid
  storing `ref<T>` into longer-lived containers (lists, struct fields).
- **Ownership transfer / move semantics for `ptr<T>`** — track unique
  ownership where useful.
- **Lifetime annotations** — optional, opt-in via attribute syntax.

Until that lands, `ref<T>` documents intent; it does not enforce it.

## See also

- [`16_classes.md`](16_classes.md) — class declarations and `<.` syntax.
- [`17_classes_examples.md`](17_classes_examples.md) — `models:` and
  `implements:` composition.
- [`45_heap_serialization.md`](45_heap_serialization.md) — what struct
  fields are and are not serialisable.
