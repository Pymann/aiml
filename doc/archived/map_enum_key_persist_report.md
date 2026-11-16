# `map<Enum:i32>` persist repro: current primary failure is map initialization

## Summary

The current repro
[test_map_enum_key_persist.aiml](../../tests/test_map_enum_key_persist.aiml)
still fails, but the strongest current finding is not the original
"string-only map serializer" theory.

With the current binary and a clean `/tmp/aiml_test_map_enum_key_persist/`,
the crash happens earlier: `bag.mana_cost.put(...)` is executed on a freshly
zero-initialized map field that was never initialized to a usable map storage.

So the current primary bug is:

- local/model map field initialization is broken or incomplete for `.put()`

The enum-key persistence question remains open after that, because this repro
hits the initialization bug first.

## Reproduced Behavior

Fresh run result:

- clean `/tmp/aiml_test_map_enum_key_persist/`
- current rebuilt `out/aiml/aiml`
- test exits with `code -1`

`gdb` shows the crash only as:

- SIGSEGV in `__aiml_user_main`

That is consistent with invalid generated code or invalid memory use in the
compiled AIML test path, not with a clean AIML-level `FAIL ...` assertion.

## Strongest Evidence

In the generated IR for
[test_map_enum_key_persist.ll](../../tests/test_map_enum_key_persist.ll):

- `bag` is allocated as zero-initialized storage in
  [test_map_enum_key_persist.ll](../../tests/test_map_enum_key_persist.ll#L61)
- then `bag.mana_cost.put(...)` is executed immediately
- but the map starts with:
  - `size = 0`
  - `capacity = 0`
  - `keysBase = null`
  - `valsBase = null`
  in
  [test_map_enum_key_persist.ll](../../tests/test_map_enum_key_persist.ll#L65)
- the generated grow path multiplies `capacity * 2`, so `0` stays `0` in
  [test_map_enum_key_persist.ll](../../tests/test_map_enum_key_persist.ll#L117)
- afterwards the append path writes through null key/value bases in
  [test_map_enum_key_persist.ll](../../tests/test_map_enum_key_persist.ll#L134)

That is immediate undefined behavior and is enough to explain the crash.

## What Changed Relative To The Earlier Theory

The earlier report version assumed the runtime serializer still hardcoded map
keys as strings. That was true for an older state, but the current source now
shows a more generic `AIML_TD_MAP` serializer path:

- encode uses recursive key serialization in
  [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L166)
- decode uses recursive key deserialization in
  [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L268)
- free uses recursive key cleanup in
  [ser_runtime.cpp](../../src/sys/ser_runtime.cpp#L360)

So the current repro no longer proves a serializer string-key bug by itself.

## Why `test_card_heap_persist.aiml` Can Still Pass

[test_card_heap_persist.aiml](../../tests/test_card_heap_persist.aiml)
passes because its `mana_cost` map is populated via `sys.deserialize(...)`,
not via `.put()` on a fresh zero-initialized model instance.

That means:

- deserialized map state can already be valid enough for later use
- while local model map fields can still be invalid until explicitly initialized

## Likely Root Cause Area

The most likely broken area is the model/container initialization path, not
typed `Kv` persistence itself.

Relevant places:

- top-level/model initialization semantics in
  [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- class/model field initialization in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- map runtime implementation in
  [map_impl.cpp](../../src/core/map_impl.cpp)

The specific runtime symptom is in `MapImpl::put(...)`:

- when `capacity == 0`, the grow path still leaves capacity at `0`
- and the append path assumes valid allocated key/value arrays

## Recommended Next Step

Before using this repro as evidence for enum-key persistence corruption, build a
second isolated test that:

1. explicitly initializes the map before `.put()`, or
2. obtains the map through a known-valid path like `sys.deserialize(...)`

Then test the typed `Kv` roundtrip again.

That will cleanly separate:

- map-initialization bugs
from
- enum-key serializer/deserializer bugs
