# String Refcount Phase 1: Compiler-side Ownership Tracking

Date: 2026-06-06
Status: backlog
Predecessor: Phase 2 (runtime allocator migration) completed 2026-06-06.

## Summary

The refcount infrastructure (`aiml_str_alloc/dup/ndup/incref/decref` in
`src/sys/string_runtime.cpp`) is in place and all runtime allocators are
migrated. `aiml_ser_free_decoded` decref's strings, and `visitAtExpr` for
`list<string>` incref's the borrowed pointer. This is enough to prevent the
mirror-close UAF (repro: `tests/test_mirror_close_string_uaf.aiml` PASS).

What's still missing: the compiler does **not** emit auto-decref on `set:`
rebind nor auto-decref at scope-exit. Strings produced by runtime allocators
(`__aiml_str_concat`, `aiml_str_*`, deserialised lists, etc.) leak when the
local variable is reassigned. The leak is bounded per session but real.

## The naive approach is wrong

```cpp
// In visitSetStmt, before `createStore(val, baseAlloca)`:
if (sym->typeName == "string") {
    aiml_str_decref(old);   // release previous binding
    aiml_str_incref(new);   // retain new binding
}
```

Failure modes observed when this was tried directly (2026-06-06 sprint):

1. **`aiml_str_incref` segfault** on literals pointing very close to memory
   base. `s - sizeof(AimlStrHeader)` underflows or reads past a section
   boundary. Tests broken: `test_string_len`, `test_ffi`, `test_compress`,
   `test_tul_required_builtins` (all SIGSEGV in `aiml_str_incref`).

2. **Double-decref leaks/UAF** for fresh allocations: e.g.
   `decl: [x, string, { call: foo }]` where `foo()` returns rc=1. Naive
   incref-on-store bumps to rc=2, then variable scope-exit decref's to 1,
   but no other holder exists → leak. If a sibling op also decref's, UAF.

## The right shape

Every expression that produces a string falls into one of these classes:

| Expression                                       | Class      | Action on store      |
| ------------------------------------------------ | ---------- | -------------------- |
| String literal (`"foo"`)                         | none       | no-op                |
| Function call returning string                   | transfer   | no incref (it owns)  |
| String interpolation (`"${x}"`)                  | transfer   | no incref            |
| Variable read (`set: [a, b]` reads `b`)          | borrow     | incref               |
| `at: [list<string>, i]`                          | borrow     | (already inc'd in visitAtExpr) |
| Property access (`obj.field` where field=string) | borrow     | incref               |
| Numeric → string conversion (`__aiml_*_to_string`) | transfer | no incref            |

The compiler needs to classify each producer expression. A clean way:

1. Add `bool ownsResult` (or equivalent) to the IRValue / expression-result
   metadata, set during `visitExpression` by every producer.
2. In `visitSetStmt` / `visitDeclStmt` for `string`:
   - Decref the old binding (if rebinding into an initialised slot).
   - If `!ownsResult`, incref the new binding.
3. Track all string locals per function in a vector; at every `ret`,
   decref them. Skip the local that's actually being returned.

## Subtleties

- **`set: [x, x]` self-assignment** must not free `x` before we re-take it.
  Incref-first / decref-second ordering matters.
- **Function arguments** of type string: caller transfers a borrowed ref;
  callee should incref on entry (treats arg as borrow), decref on exit /
  rebind. Or: caller transfers ownership; callee decref's at exit only.
  Pick one convention and document it.
- **Return values**: function emits incref before `ret` so caller takes
  ownership; OR convention is "return value is owned (rc=1)" and the
  scope-exit decref skips the returned local.  The latter is simpler.
- **Class member strings** (struct field of type `string`): on `set:
  obj.field, x`, decref the old field value; incref/transfer x.  On struct
  destruction, every string field must be decref'd. This needs class
  destructor emission, which AIML doesn't have today.
- **List/map element strings**: `list<string>.push(s)` should incref `s`
  (the list takes ownership of a ref).  `list<string>.clear()` should
  decref every element.  Currently `aiml_list_push_string` stores raw
  pointer; `clear` does no per-element cleanup.  Both need to be updated
  if we want full ownership symmetry.

## Test plan

- `tests/test_string_rebind_no_leak.aiml`: 10_000 iter `set: [x, "${y}"]`,
  read RSS via `/proc/self/status` before/after, assert delta < 100 KB.
- `tests/test_string_scope_exit_no_leak.aiml`: call a fn that allocs a
  local string 10_000×, assert no growth.
- `tests/test_string_return_ownership.aiml`: function returns a string;
  caller stores in var; verify rc=1 after return (single owner).
- `tests/test_string_list_push_clear.aiml`: push 1000 strings into a list,
  clear, push 1000 more. Assert no growth.
- `tests/test_string_self_assign.aiml`: `set: [x, x]` doesn't free `x`.

## Estimated effort

- Expression classifier on `IRValue`/`Expression`: 2-3 h
- visitSetStmt / visitDeclStmt logic + scope-exit emission: 2-3 h
- Class destructor / list-clear destructor emission (optional, for full
  symmetry): 3-5 h
- Test plan above + validation: 2 h

Total: ~10 h for a clean implementation. Until then, the existing partial
refcount (visitAtExpr + ser_runtime) covers the practical DataCreatures
hover hot-path; rebind leaks are bounded and benign for short sessions.
