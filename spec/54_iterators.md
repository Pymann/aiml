# Chapter 54 — Iterators

## 1. Overview

`iterator<T>` is a generic builtin type for lazy, one-element-at-a-time traversal
of collections and database handles. It traverses the source collection directly
without copying — zero allocation beyond the iterator handle itself.

The type parameter `T` denotes the element type returned by `next()`. For example,
`iterator<string>` yields `string` values (KV keys), a future `iterator<i32>` could
yield entity IDs from a `Store<T>`.

```aiml
- decl: [it, "iterator<string>", { call: [kv.iter] }]
- while:
    condition: { call: [it.has_next] }
    body:
      - decl: [key, string, { call: [it.next] }]
      - print: ["key: ${key}"]
- call: [free, it]
```

---

## 2. LLVM Representation

`iterator<T>` is an **opaque heap pointer** (`i8*`), regardless of `T`.
The backing C struct (`AimlIterator`) is allocated by the creation function and
owned by the `iterator<T>` local (spec/61 class 4, unique resource).

---

## 3. Creation

Iterators are created through methods on the source collection:

| Source type | Method | Returns | Description |
|---|---|---|---|
| `Kv<K:V>` | `kv.iter()` | `iterator<string>` | Live iterator over KV keys |

Future sources (not yet implemented):

| Source type | Method | Returns |
|---|---|---|
| `list<T>` | `list.iter()` | `iterator<T>` |
| `Store<T>` | `store.iter()` | `iterator<i32>` (entity IDs) |
| `map<K:V>` | `map.iter()` | `iterator<K>` |
| `buffer<T>` | `buf.iter()` | `iterator<T>` |

---

## 4. Methods

| Method | Signature | Description |
|---|---|---|
| `has_next()` | `() -> i32` | Returns `1` if more elements remain, `0` otherwise |
| `next()` | `() -> T` | Returns the next element. Returns `""` (for strings) or zero value when exhausted |
| `reset()` | `() -> void` | Rewinds the iterator to the beginning, allowing a second pass without re-creating it |

---

## 5. Lifecycle and `free`

An `iterator<T>` is a **class-4 unique resource** (spec/61 §2): it is released
exactly once when its owner drops — at scope exit, on every return path.

- `- call: [free, it]` is an optional **early release**; it nulls the slot, so the
  owner drop is a no-op. After `free`, any further call to `next()`, `has_next()`
  or `reset()` is **undefined behavior**.
- Passing an iterator to a parameter **moves** it: the callee owns and releases it,
  the caller must not use it afterwards.

---

## 6. Mutation during Iteration

The iterator holds a **live reference** into the source collection — it does not
copy any data. This means:

- **Read-only iteration is always safe.** The iterator walks the internal data
  structure directly with zero overhead.
- **Mutating the source during iteration is undefined behavior.** For `Kv<K:V>`,
  calling `kv.set()` or `kv.delete()` while an iterator is active can trigger
  a rehash of the underlying `unordered_map`, invalidating the iterator silently.

**Rule:** finish iterating (or call `free`) before modifying the collection.

This is the standard trade-off: copy-free iteration in exchange for a simple
contract. The same rule applies in C++ (`unordered_map::iterator`), Rust
(borrow checker enforces it), and Go (`range` over map snapshots internally).

---

## 7. Why there is no `is_valid()` method

A validity check (`it.is_valid() -> i32`) is intentionally not provided.

There are exactly two ways to invalidate an iterator:

1. **Calling `free`.** This is a programming error (use-after-free). Detecting it
   at runtime would require a global registry of live handles, adding overhead to
   every `next()` call. AIML follows C/C++ convention: once freed, do not use.

2. **Mutating the source during iteration.** This is also a programming error
   (see §6). The internal C++ iterator becomes silently invalid — no check can
   reliably detect this without per-call verification overhead.

Both cases are **contract violations by the caller**, not runtime conditions to
handle. An `is_valid()` method would give a false sense of safety while adding
cost to the hot path.

**Rule of thumb:** let the owner drop release the iterator; use `free` only to release it early.

> **TODO:** An `is_valid()` method may be needed in the future when iterators are
> used in more complex scenarios (e.g. long-lived iterators across function
> boundaries, or iterators passed between threads). For current use cases
> (local, read-only, short-lived iteration) it is not required. When implemented,
> it should check both use-after-free (via a generation counter or tombstone flag)
> and source-mutation (via a modification counter on the source collection).

---

## 8. Iteration Order

For `Kv<K:V>`, the iteration order is **unspecified** (determined by the internal
`unordered_map` bucket layout). Do not rely on insertion order or alphabetical
sorting. If you need ordered iteration, collect and sort manually.

---

## 9. Thread Safety

Creating an iterator (`kv.iter()`) is lock-free — it only stores a reference
to the map's `begin()` and `end()`. After creation, the iterator is a
thread-local object — no locking is needed for `next()`, `has_next()`, or
`reset()`. Multiple threads can safely hold independent iterators over the
same KV **as long as no thread modifies the KV concurrently** (see §6).

---

## 10. Complete Example

```aiml
- decl: [store, Kv<string:f32>]
- set: [store, { call: [db.open_kv, "scores", {persist: true}] }]
- call: [store.set, "alice", 95.0]
- call: [store.set, "bob", 87.5]
- call: [store.set, "carol", 91.2]

# Iterate all keys, look up values
- decl: [it, "iterator<string>", { call: [store.iter] }]
- while:
    condition: { call: [it.has_next] }
    body:
      - decl: [name, string, { call: [it.next] }]
      - decl: [score, f32, { call: [store.get, name] }]
      - print: ["${name}: ${score}"]

# Second pass — just count
- call: [it.reset]
- decl: [n, i32, 0]
- while:
    condition: { call: [it.has_next] }
    body:
      - call: [it.next]
      - set: [n, { +: [n, 1] }]
- print: ["Total: ${n} entries"]

- call: [free, it]
```
