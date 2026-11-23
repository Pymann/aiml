# Store Typed Persist Fix Report

## Summary

The persistent `Store<T>` path for heap fields was switched over to the same safe serialize/deserialize approach that typed `Kv<K:V>` already uses.

Before:

- persistent `Store<T>` entries were written as raw struct bytes on `insert`/`update`
- that was dangerous for types with `string`, `list<...>`, `map<...>` or other heap-based fields
- on reopen, stale pointers could be reused as a result

Now:

- for persistent `Store<T>` with heap fields the compiler automatically chooses a typed store path
- the runtime path writes WAL records as serialised blobs
- when loading, these blobs are materialised again into owned decoded slots

## Changed places

### Compiler

In [builtin_registry.cpp](../../src/core/builtin_registry.cpp) `db.open("...", {persist: true})` was extended for `Store<T>`:

- if `T.hasHeapFields` and `T.serNodesVar` are set
- then no longer `aiml_db_store_open_persist(...)`
- but `aiml_db_store_open_typed_persist(...)` is called

### Runtime API

In [db_runtime.h](../../src/sys/db_runtime.h) the new runtime API was added:

- `aiml_db_store_open_typed_persist(...)`

### Runtime implementation

In [db_runtime.cpp](../../src/sys/db_runtime.cpp) the following points were implemented:

- `AimlStore` got `ser_nodes`, `ser_n` and `typed_persistent`
- for typed persistent stores there is now decoded slot storage instead of raw mmap slot data
- `store_wal_write(...)` serialises typed store entries as a blob
- `store_wal_replay(...)` decodes typed store entries back into owned slots
- `insert` / `update` / `get` / `delete` handle typed persistent stores separately
- `checkpoint` now actively compacts typed store WALs instead of only `fsync`

## New regression tests

Newly added:

- [test_aiondb_store_heap_reopen_write.aiml](../../tests/test_aiondb_store_heap_reopen_write.aiml)
- [test_aiondb_store_heap_reopen_read.aiml](../../tests/test_aiondb_store_heap_reopen_read.aiml)

These tests cover a `Store<T>` with:

- `intent: string`
- `dependencies: list<string>`

and check:

- `insert`
- `checkpoint`
- reopen in the second run
- correct return of the heap fields

## Verification

After the changes the following were run successfully:

- rebuild of `aiml-tests`
- [test_aiondb_store_heap_reopen_write.aiml](../../tests/test_aiondb_store_heap_reopen_write.aiml)
- [test_aiondb_store_heap_reopen_read.aiml](../../tests/test_aiondb_store_heap_reopen_read.aiml)
- [test_aiondb_store_reopen_write.aiml](../../tests/test_aiondb_store_reopen_write.aiml)
- [test_aiondb_store_reopen_read.aiml](../../tests/test_aiondb_store_reopen_read.aiml)
- [test_card_heap_persist.aiml](../../tests/test_card_heap_persist.aiml)

## Remaining difference to the old store path

The classic persistent scalar `Store<T>` stays mmap-slot based.

The new heap-safe path for typed stores is deliberately different:

- snapshot/replay via serialised blobs
- no more raw heap-containing struct bytes in persistent slots

That is the actual cause of the fix.
