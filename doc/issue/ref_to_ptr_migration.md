# Migration: legacy `ref<T>` usages → `ptr<T>` (borrow-safe)

**Discovered:** 2026-05-23 — during the `list<ref<T>>` serialiser work.
**Severity:** Medium. Will break compilation once borrow-checking for
`ref<T>` lands. No runtime impact today.
**Status:** Open. Tests listed below still use `ref<T>` for owning
pointers (allowed today via the `ref<T> == ptr<T>` alias, but inconsistent
with the borrow-safe convention defined in
[`spec/18_refs_and_pointers.md`](../../spec/18_refs_and_pointers.md)).

---

## Background

As of 2026-05-23 the AIML compiler treats `ref<T>` and `ptr<T>` as
synonyms — both lower to the same opaque pointer and no borrow check is
performed. Older tests were written under that lax regime and use `ref<T>`
for owning pointers (e.g. `decl: [box, ref<Box>, { new: Box }]`).

The borrow-safe convention now in [`spec/18`](../../spec/18_refs_and_pointers.md)
requires:

- `ref<T>` only for borrowed function parameters and the compiler-injected
  method `this`.
- `ptr<T>` for everything that may outlive its creation scope — `new`
  results, list/map elements, struct fields, function return values, KV
  views.

When borrow-checking lands the legacy usages will fail to compile.

## Affected files (test corpus)

Tests that bind `new T` (or another owning expression) to a `ref<T>` slot,
or push a `ref<T>` into a list/struct field:

- `tests/test_chan_class_member.aiml` — `decl: [box, ref<ChannelBox>, { new: ChannelBox }]`
- `tests/test_buffer_class_member_heap_init_repro.aiml`
- `tests/test_list_class_member_heap_init_repro.aiml`
- `tests/test_map_class_member_heap_init_repro.aiml`
- `tests/test_oo.aiml` — multiple sites

Tests that use `ref<T>` as the function-return type also count once
borrow-checking forbids escaping borrows; audit with
`grep -rn 'return: ref<' tests/`.

There may be additional usages in:

- `src/sys/*.cpp` builtin registrations (search for `"ref<"`).
- Other downstream projects (`datacreatures`, `9rooms`, `tul`, `tpx`).

Audit command:

```bash
grep -rn 'ref<' tests/ src/ 2>/dev/null | grep -v 'this' | grep -v 'spec/'
```

## Migration steps (when borrow-checking lands)

1. For each affected file: replace `ref<T>` with `ptr<T>` on declarations
   bound to `new`, on list-element types, on struct fields, on function
   returns, on KV views.
2. Keep `ref<T>` only on function parameters that:
   - Do not call `free` on the argument.
   - Do not store the argument into anything longer-lived (list, map,
     struct field, global).
   - Do not return the argument (or any pointer derived from it).
3. Re-run the full test suite; expect borrow-check errors at the original
   `ref<T>` sites, fix them by widening to `ptr<T>`.
4. Update `spec/18_refs_and_pointers.md` to remove the "Known technical
   debt" section once all listed files migrate.

## Why not migrate now?

- Borrow-checking does not exist yet. Migrating today is mechanical churn
  without enforcement — a future reader could re-introduce the mistake
  before the checker catches it.
- The current test corpus is a useful baseline for the borrow-checker
  implementation: it shows real owning-vs-borrowing usage patterns that
  the checker must classify correctly.

When the borrow-checker design lands (separate spec doc, not yet
written), revisit this issue and migrate the affected tests as the
first consumer of the new rules.

## See also

- [`spec/18_refs_and_pointers.md`](../../spec/18_refs_and_pointers.md) —
  the borrow-safe convention this issue tracks compliance against.
