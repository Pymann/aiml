# WASM Vtable Struct-Return — Stub-Wrapper Approach Followup

Date: 2026-06-06
Status: backlog (a working fix is shipped; this lists follow-on hardening)

## Summary

The WASM linker / codegen demotes >8-byte struct returns to sret out-pointer
ABI internally, but for indirect calls through function pointers (interface
vtables) the IR-level signature has no auto-lowering. This caused
`list<string>` returns through `view.method()` to corrupt the data pointer
(size+cap survived as packed i64, data ptr was garbage) →
"index out of bounds" trap on the first `at:` access.

The shipped fix:

- `src/core/ast_compiler.cpp::getOrCreateSretStub` emits a stub per struct-
  returning method that calls the real method directly and memcpy's the
  result through an explicit sret out-pointer.
- Vtables point to the stub instead of the real method.
- `src/core/ast_compiler_call.cpp` vtable-call branch uses the sret form
  when retTy is a struct.

This works for the DataCreatures hot path (`ItemBehavior.tile_tags` via
`ptr<Tileable>`) and for the multi-file mirror test
(`tests/wasm/test_mirror_rpc_client_multifile.aiml`).

## What's still loose

1. **`fn<...>` indirect calls** (`src/core/ast_compiler_call.cpp:500`) still
   use direct struct-return signature. If anyone stores a struct-returning
   method into a `fn<>` variable and invokes it, same bug applies. We
   reverted an earlier patch that broke direct calls; the proper fix is
   the same sret-form as vtable.

2. **Direct method calls in WASM are still vulnerable** if LLVM's auto-
   lowering and our IR-level types disagree. Today they agree because both
   sides go through the same `define %list_string @method()` shape, but
   any future where the definition is emitted in a different translation
   unit with different attributes could re-open the gap. The principled
   fix is "all struct-returning methods use sret out-param explicitly,
   everywhere" — i.e., what I sketched in the morning but didn't ship
   because the scope was too big.

3. **Stubs are emitted per translation unit as `private` linkage**, so the
   same stub exists multiple times in different `.ll` files. Vtables in
   each unit point to their own copy. This works (linker keeps each
   private) but bloats the binary. Could be linkonce_odr instead.

## Tests in place

- `tests/test_iface_list_return.aiml` (native + WASM)
- `tests/wasm/test_mirror_rpc_client_multifile.aiml` (multi-file WASM)

## Estimated effort for hardening

- Apply the sret-form to `fn<>` indirect calls: 1 h
- Switch stubs to `linkonce_odr`: 30 min
- Optional: full "struct returns always use sret" refactor (definitions,
  direct calls, indirect calls — everything explicit): 4-6 h

Total minimal hardening: ~1.5 h. Full refactor: ~6 h.
