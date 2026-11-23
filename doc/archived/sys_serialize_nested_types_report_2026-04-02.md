# AIML sys.serialize / sys.deserialize Nested Types Report

Date: 2026-04-02

## Short conclusion

Nested data types are currently only partly robust.

There are two separate serialisation paths:

- the JSON-based path for `sys.serialize` / `sys.deserialize` in [serialize_impl.cpp](../../src/core/serialize_impl.cpp)
- the binary descriptor path `aiml_ser_*` in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp)

The JSON path is usable for some important nested cases:

- struct with a nested struct
- struct with `list<T>`
- struct with `list<struct>`
- struct with `list<list<primitive>>`

But it is not generally recursively correct. Maps in particular are the biggest weak point.

The binary descriptor path is conceptually more generic and much closer to real recursion for `struct/list/map`, but has its own open points with `buffer<T>` and with error hardening.

## Verified runtime findings

Verified positively:

- [test_serialize_struct.aiml](../../tests/test_serialize_struct.aiml) runs successfully
- [test_serialize_card.aiml](../../tests/test_serialize_card.aiml) runs successfully
- [test_serialize_ext.aiml](../../tests/test_serialize_ext.aiml) runs successfully

So it is currently concretely demonstrated that:

- `list<struct>` works for the JSON path
- `list<list<i32>>` works for the JSON path
- a card-like nested case with `list<Ability>` and `Ability.effects: list<list<i32>>` works

Verified negatively:

- `map<string:list<i32>>` in the JSON path leads to a runtime crash
- `map<ManaColor:i32>` in the JSON path leads to a runtime crash

I ran these two repros locally, directly with the current AIML binary.

## Findings

### Critical: JSON maps are effectively only implemented for string->primitive/string

The JSON layer claims recursion, but does not implement it cleanly for maps.

Evidence:

- the map write path for struct fields only knows `string`, `i64`, `f32`, `f64`, everything else falls back to `i32` in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L252)
- the top-level map write path has the same restriction in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L489)
- the map read path for struct fields only knows primitive/string values in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L714)
- the top-level map read path has the same restriction in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L900)

Consequence:

- `map<string:list<i32>>`
- `map<string:struct>`
- `map<string:map<...>>`

are not implemented correctly in the JSON layer despite `isSerializable(...)` and can crash or serialise wrongly.

### Critical: JSON maps ignore the key type and effectively treat keys as `string`

The JSON layer reads and writes map keys as `i8*`/string.

Evidence:

- When writing, `kp` is always read as `i8pT` in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L284) and [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L535)
- When reading, `ctx.getLLVMType("string")` is hard-coded as the key type for maps in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L716) and [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L906)

Consequence:

- `map<ManaColor:i32>` is structurally inconsistent in the JSON path
- the runtime crash for the repro with an enum key map is thereby technically explainable

That is also problematic because [test_serialize_card.aiml](../../tests/test_serialize_card.aiml) already uses `map<ManaColor:i32>`, but only covers the deserialize-only case without a round trip or a map content test.

### High: `isSerializable()` is too permissive for the JSON path

`isSerializable()` recursively allows `list<T>` and `map<K:V>` as long as the value type is recursively serialisable, see [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L958).

That does not match the real JSON implementation, which:

- for lists visibly only handles `list<primitive>`, `list<struct>`, `list<list<primitive>>`
- for maps only handles primitive/string values correctly

Consequence:

- The compiler accepts types that the runtime path cannot process robustly.
- That leads to crashes instead of clean compiler errors.

### High: nested lists are only specially implemented one level deep in the JSON path

There are targeted special cases for `list<list<T>>`, but no general recursive list implementation.

Evidence:

- write path: special case `isListType(elemTn)` in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L183) and [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L424)
- read path: the same special case in [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L668) and [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L850)

Consequence:

- `list<list<list<i32>>>` is not secured as a generic case
- `list<list<struct>>` is not visibly supported cleanly either

### Medium: spec and implementation diverge for the JSON path

The spec describes nested maps and nested lists much more generally, see [29_serialize.md](../../spec/29_serialize.md#L90).

The real implementation is narrower:

- map keys practically `string`
- map values practically primitive/string
- lists nested only with limited special cases

That leads to a false sense of support.

### Medium: the binary descriptor path is more generic for `struct/list/map`, but `buffer<T>` is not serialised deeply

In the binary path `struct`, `list` and `map` are traversed recursively via `enc/dec/fre`, see [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L147), [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L157) and [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L171).

For `buffer<T>`, however, only a raw byte block is copied:

- encode does `w_raw(...)` in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L189)
- decode does `memcpy(...)` in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L309)
- free, however, traverses `buffer` recursively over its elements in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L401)

Consequence:

- `buffer<primitive>` is plausibly ok
- `buffer<string>` or `buffer<struct-with-heap>` is inconsistent and very probably wrong

### Medium: binary decode only validates list counts hard

Lists have a sanity check in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L264).

Maps and buffers have no comparable hard count/size check:

- map in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L287)
- buffer in [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L309)

Consequence:

- broken or manipulated blobs can trigger needlessly large allocations

### Low: debug output is still active in the binary decode implementation

In [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L248) there is still `fprintf(stderr, "[ser-pos] ...")`.

That is noisy for production paths and should go or be put behind a debug flag.

## Current support state

### JSON `sys.serialize/sys.deserialize`

Robust or practically confirmed:

- primitive
- enum
- struct/class/model with primitive fields
- nested struct
- `list<primitive>`
- `list<struct>`
- `list<list<primitive>>`

Only partly or not robust:

- `map<string:primitive>`: rather yes
- `map<non-string-key:V>`: no
- `map<string:struct/list/map>`: no
- `list<list<struct>>`: unclear and not generic
- `list<map<...>>`: no
- `buffer<T>`: currently not intended at all as a real JSON type path via `sys.serialize`

### Binary `aiml_ser_*`

Appearing robust:

- struct
- nested struct
- list recursively
- map recursively

Open/problematic:

- `buffer<T>` only shallow
- missing hard plausibility checks for map/buffer sizes
- leftover debug output

## Recommended fix order

### Phase 1: secure the JSON map path immediately

Effort:

- about 0.5 to 1 day

Content:

- align `isSerializable()` for the JSON path with the real implementation
- reject unsupported map forms as compiler errors for `sys.serialize/sys.deserialize`
- block at least these types for now:
  - `map<K:V>` with `K != string`
  - `map<string:V>` if `V` is not primitive/string

Benefit:

- prevents the currently demonstrated runtime crashes immediately

### Phase 2: make JSON maps really recursive

Effort:

- about 1.5 to 3 days

Content:

- parse the key type cleanly instead of implicitly `string`
- serialise/deserialise map values recursively, analogous to lists
- support for:
  - `map<string:list<T>>`
  - `map<string:struct>`
  - optionally `map<string:map<...>>`

Benefit:

- brings the JSON path up to the level that the spec and `isSerializable()` currently suggest

### Phase 3: generalise JSON list recursion

Effort:

- about 1 to 2 days

Content:

- replace the hard special cases `list<struct>` and `list<list<T>>` by a general recursive emitter/reader

Benefit:

- less ad hoc code
- better behaviour for more deeply nested lists

### Phase 4: define binary `buffer<T>` cleanly

Effort:

- about 0.5 to 1.5 days

There are two clean options:

- restrict `buffer<T>` to POD/raw bytes and reject everything else at compile time
- or serialise `buffer<T>` recursively like `list<T>`

My proposal:

- short term: hard-forbid non-POD `buffer<T>`

### Phase 5: harden binary decode

Effort:

- about 0.25 to 0.5 days

Content:

- count/size limits for map and buffer
- remove the leftover debug `fprintf`

## Recommendation

The most important immediate measure is not "more features", but closing the wrong permissions in the JSON path.

My pragmatic proposal:

1. Restrict JSON maps immediately to the range that is really supported
2. only after that build real recursive map support
3. in parallel only allow the binary path for the types that are really serialised deeply and correctly there

## Assessment

If you only want a safe state:

- 0.5 to 1 day for guardrails and compiler errors

If you want really good nested type support in the JSON path:

- roughly 2.5 to 4.5 days in total

## Methodology

This is a combination of:

- static code analysis
- runtime tests of the existing serialisation tests
- two additional crash repros for problematic map cases
