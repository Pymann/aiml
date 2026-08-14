# No-op Elision for Bodyless Interface Methods

Date: 2026-07-10
Status: backlog (optimization, low priority)
Context: 9rooms_v2 Card-Refactor — CardBase interface with many methods that
         most card types leave bodyless (no-op / default). See spec/19 §5.

## What works today

A class method declared WITHOUT `body:` satisfies its interface and the
compiler synthesizes a default return (void → nothing, i32 → 0, ptr → null,
list/map/struct → empty).  Verified: `tests/test_iface_bodyless_method.aiml`.

## The optimization gap

A bodyless method still compiles to a real (near-empty) function placed in the
vtable.  Calling it is a **full virtual call**: vtable-slot load + indirect
call + prologue/epilogue + trivial default return.  Cheap, but not zero.

- Direct calls to empty functions get inlined away at `-O1+`; **virtual**
  (interface) calls do NOT — LLVM cannot devirtualize without whole-program
  info, so the indirect call stays even at `-O2`.
- Projects that build at `-O0` (e.g. 9rooms) pay it unconditionally.

For non-hot paths (card play/equip/display — once per action) this is
irrelevant.  It would only matter if a bodyless method sat in a per-frame,
per-object loop.

## Possible approaches (when it matters)

1. **Null-slot + call-site skip**: emit a null vtable slot for a bodyless
   VOID method; the caller does `if (slot) call(slot, args)`.  Conflicts with
   the current "no partial/null-slot vtable" safety policy (null slots caused
   silent-null SIGSEGVs → LinkOnceODR fix); trades a call for a branch.
2. **Shared sentinel no-op**: point all bodyless-void slots at one shared
   `__aiml_noop` function; the caller compares the slot against the sentinel
   and skips.  Still a load+compare+branch, but no per-class stub.
3. **Whole-program devirtualization pass**: when the concrete type is known
   (monomorphic call site), replace the virtual call with a direct one that
   LLVM can inline/elide.

Value-returning bodyless methods can never be fully elided (the caller needs
the value); at best route them to a shared "return default" stub.

## Recommendation

Leave as-is for now — bodyless already gives the clean-syntax / no-boilerplate
win.  Revisit only if profiling shows a bodyless method on a hot path.
