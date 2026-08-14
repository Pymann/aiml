# Extern-Globals Pattern: Sharper Failure Mode, Better Sharing Primitive

Date: 2026-06-06
Status: backlog

## Summary

We discovered (2026-06-05) that AIML's `{extern: true}` decl pattern silently
miscompiles when only some referencing files mark a global as `extern` and
the owner does not. The non-extern owner emits a unique-per-file symbol
(`__lib_<file>_<name>`) while extern referencers expect a weakly-shared
symbol (`__lib_<name>`). Linker resolves both, no error, but they point to
different storage — assignments by one side don't reach the other. This bit
DataCreatures' login: client_main.aiml owned `g_session` without `extern`,
login_panel.aiml referenced it with `extern: true`, and the panel saw a
permanently-null session.

The shipped fix: main-mode also respects `is_extern` in
`src/core/ast_compiler.cpp` Phase 1. Now both sides agree on the symbol.

## What's still loose

1. **Silent failure mode**. There's no link-time or compile-time check that
   the same global name is declared consistently across files. A future
   refactor that re-introduces a missing `extern: true` won't be caught.
   Possible solutions:
   - Compile-time check: aggregate decls from all sibling-imports and
     verify "if any decl has extern, all do".
   - Link-time check: emit a marker symbol per extern decl (e.g.
     `__lib_<name>__extern_marker`) and assert it exists when emitting an
     extern reference. Linker fails clean if it's missing.

2. **The pattern itself is a code smell**. Cross-file shared globals are
   semantically an application "service" or "context" object. Putting them
   in a class instance (e.g. `ClientApp`) and threading the instance via
   setter into each panel eliminates the entire family of bugs:
   - No `{extern: true}` boilerplate
   - No symbol-name fragility (private member vs global)
   - Reasonable testability (can construct multiple `ClientApp` instances
     in different test scenarios)

DataCreatures will adopt the ClientApp pattern in a separate refactor
(see `datacreatures/doc/...` once started).  Other AIML projects that
share globals across files should be encouraged to use the same pattern.

## Suggested compiler change

Add `--strict-extern` flag (or similar) that:
1. Tracks every declared global in every compiled file.
2. After all files are compiled, for each global name with at least one
   `{extern: true}` decl, verify ALL decls have it.
3. Emit a compile-time error pointing to the inconsistent decl.

Effort: ~2-3 h with test cases.

## Test plan

- `tests/test_extern_consistency_check.aiml`: two files declaring the
  same global, one with extern, one without → expect compile error.
- `tests/test_extern_consistency_ok.aiml`: same with both extern → OK.
- Update existing tests that rely on the current lax behavior.
