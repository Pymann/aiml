# Bodyless method with struct return → UndefValue (FIXED 2026-07-10)

## Symptom
A BODYLESS interface method (declared `- def: { name: give, return: map<string:i32> }`
with no `body:`) crashed the program at runtime — often at *program exit* or on
first mutation of the returned value — with SIGSEGV/SIGABRT at a tiny/garbage
address.

## Root cause
`Compiler::visitFunction` synthesizes a default return when the body falls
through without a terminator (`ast_compiler.cpp` ~2147). The scalar cases
(int/ptr/float) returned a well-defined zero, but the aggregate case
(struct/list/map) fell into the `else` branch → `llvm::UndefValue::get(retTy)`.

An undef `map`/`list` has undef `{data, size, cap}`. The caller reads those:
`contains`/`get` sometimes survive (undef materialised as 0), but `put`
reallocs `data` (garbage pointer) and scope-exit `free`s it → crash. The bug
was non-deterministic because undef may lower to 0 or to leftover register
contents.

## Fix
Return `llvm::ConstantAggregateZero::get(retTy)` for struct/array/vector return
types — a well-defined empty container (`{null, 0, 0}`), safe to read, mutate
(realloc(null)=malloc) and free (free(null)=no-op).

## Repro
`tests/test_bodyless_struct_return.aiml` — bodyless `give → map<string:i32>`,
then `got.put("9", 9)`. RED before fix (SIGABRT), GREEN after.

## Discovered via
9rooms_v2 Increment 3: `CreatureCard.get_produces` is a bodyless no-op (only
ManaCard produces mana). `test_card_base_engine_api.aiml` called it and crashed
on exit.
