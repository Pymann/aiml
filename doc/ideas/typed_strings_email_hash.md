# Idea: semantic string subtypes (`email`, `hash`, …) alongside `path`

Date: 2026-07-12

## Sketch

AIML already has `path` as a distinct primitive that is string-backed at the ABI
level (`context.h`: `typeName == "path"` → interned `char*`) but carries
semantic meaning the compiler/tools can key on. Extend that idea with more
semantic string subtypes, e.g.:

- `email`   — an address string (identity)
- `hash`    — a content/id digest
- (later) `url`, `uuid`, `token`, …

All lower to `i8*` (interned char\*) like `path`, so no runtime cost and full
interop with `string` where sensible.

## Why (concrete motivation)

The `str_cmp` static rule (spec/49 §16) flags a variable compared against ≥2
string literals as a hand-rolled enum switch. That is right for enum-ish tokens
(`"creature"`, `"combat_zone_1"`), but a **false positive** for legitimate
runtime-string identity checks:

```aiml
- if: { condition: { eq: [owner_email, "alice@example.com"] }, ... }
- if: { condition: { eq: [owner_email, "bob@example.com"]   }, ... }
```

Emails/hashes are the identity — there is no enum to migrate to. Today the only
recourse is `static_off: ["str_cmp"]` per function.

If `owner_email` were typed `email` (not `string`), `str_cmp` could **skip
comparisons where the compared operand is a semantic-string subtype** (email,
hash, path, …) and only flag `string`/enum-ish comparisons. The type carries the
"this is identity data, not an enum" signal — no heuristic on literal contents
needed, no opt-out.

## Rough shape

- Register `email`/`hash` in the type resolver (mirror the `path` branch),
  each → interned `char*`.
- Allow implicit `string` ↔ subtype where ergonomic (or require explicit cast).
- `str_cmp` (ast_compiler.cpp `checkStringComparisons`): when the non-literal
  operand's declared type is a semantic-string subtype, do not count it.
- Optionally, string builtins (`.len`, `.substr`, `index_of`, interpolation)
  all work unchanged since the ABI is identical.

## Open questions

- Coercion rules string↔email↔hash (implicit vs explicit cast).
- Do model/wire fields of type `email` serialize identically to `string`? (Yes —
  same ABI — so it is backward compatible on the wire.)
- Which subtypes are worth first-classing vs. left as `string`.

## Related

- spec/49 §16 (str_cmp)
- `path` primitive (context.h type resolver)
