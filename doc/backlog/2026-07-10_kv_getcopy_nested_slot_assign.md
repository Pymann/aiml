# `kv.get_copy` Assigned Into a Nested Struct Field Skips Deep-Copy

Date: 2026-07-10
Status: FIXED 2026-07-10 (ast_compiler_stmt.cpp member-write branch now emits
        aiml_ser_deep_copy for kv.get_copy into a model/class field, and memcpy
        for a plain model pointer — see also the 4 sibling fixes + the
        conservative assignment-compat guard added in the same pass).
        Regression: tests/test_kv_getcopy_nested_slot.aiml now PASSES.
Discovered by: 9rooms_v2 Card-Refactor (Phase C) — `CardService.load_env`
                returned a `CardEnvelope` whose nested typed-data slot had
                corrupt heap fields (card_id="", name=garbage), while a
                direct `get_copy` into a top-level variable was fine.

## Summary

`kv.get_copy(key)` returns a pointer into the KV `decoded_cache` (stable
until the next mutation, per spec/45 §7.1).  When its result is assigned to
a **top-level variable**, the compiler emits a deep-copy of the heap fields
(string/list/map) — this is the `visitSetStmt` get_copy deep-copy path and
it works.

When the SAME result is assigned to a **nested struct field**
(`set: [outer.inner, { call: [kv.get_copy, key] }]`), the deep-copy path is
NOT taken.  The nested slot ends up with the raw (cache-owned / shallow)
heap pointers, which are then corrupt/garbage after the statement — the
string field reads as garbage bytes, the i32 field reads 0.

## Minimal Reproduction

`tests/test_kv_getcopy_nested_slot.aiml` (exits non-zero until fixed).

```yaml
- model: { name: Inner, fields: [[name, string], [val, i32]] }
- model: { name: Outer, fields: [[tag, string], [inner, Inner]] }
# ... seed Inner{name:"Alpha", val:42} into Kv<string:Inner> "k1" ...
- decl: [outer, Outer]
- set: [outer.tag, "T"]
- set: [outer.inner, { call: [store.get_copy, "k1"] }]   # <-- BUG
# outer.inner.name reads garbage, outer.inner.val reads 0
```

### Matrix (what works vs. not)

| Case | Pattern | Result |
|------|---------|--------|
| V1 | `outer.inner = <local Inner>` (whole-slot, local source) | OK |
| V2 | `outer.inner = kv.get_copy(key)` (whole-slot, get_copy source) | **BUG** — heap fields corrupt |
| V3 | `tmp = kv.get_copy(key); outer.inner.name = tmp.name; ...` (field-by-field) | OK (workaround) |

The bug is independent of function return — V2 corrupts in-place, no
`return` involved.  V1 shows a whole-slot assign per se is fine; the
trigger is specifically the `get_copy` source combined with a nested
field target.

## Root Cause (hypothesis)

`visitSetStmt` special-cases `kv.get_copy` (and likely `kv.get`) to emit a
recursive deep-copy of heap fields, but only recognizes a plain
identifier LValue as the target.  A member-access LValue
(`base.field`) falls through to the default memcpy/shallow assignment,
copying the cache-owned pointers instead of duplicating the heap data.
The cache pointer is then reused/invalidated → garbage.

## Fix Direction

Apply the same deep-copy logic when the assignment target is a nested
struct field (member-access LValue), not just a top-level variable.  I.e.
route member-access targets through the same `get_copy` deep-copy codegen.

## Workaround (in use)

Read into a top-level `tmp` variable first, then copy fields individually
into the nested slot (V3).  `9rooms_v2 CardService.load_env` uses this.
