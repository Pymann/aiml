# Two compiler robustness gaps (open, 2026-07-10)

Both surfaced while migrating 9rooms_v2 off the fat `Card`. Neither is a
correctness bug in *valid* programs, but both turn a small mistake into a
hard-to-diagnose crash instead of a clear error.

## 1. Corrupt `.o` cached when module verification fails → poisons later runs

When a per-file module fails `verifyModule` ("... verification FAILED before
compileToObject"), the incremental object cache under `__aiml_cache/` still ends
up with a **zero-byte / partial `.o`** under that source's content hash. The
next `--run` of any test that imports that file loads the corrupt `.o`
(`Failed to create ObjectFile from buffer` / "not recognized as a valid object
file") or JITs garbage → SIGSEGV — masking the *real* compile error.

Workaround today: `find __aiml_cache -name '*.o' -size -100c -delete` before
re-running.

Fix direction: only write the `.o` to cache **after** a successful verify +
compileToObject; write to a temp path and atomically rename, so a failed/killed
compile never leaves a readable-but-invalid cache entry.

## 2. Member access on an undefined variable silently resolves to `i32 0`

`c_res.card` where `c_res` is not declared (e.g. removed during a refactor)
compiled to a literal `i32 0` instead of erroring. It then flowed into a call
expecting a struct (`TriggerManager_register_card(..., %Card, ...)`), and the
mismatch only showed up at *module verification* ("Call parameter type does not
match function signature") — far from the real cause.

Fix direction: `visitMemberExpr` (property read) should hard-error when the base
identifier resolves to no symbol / no field, with the source location, rather
than emitting a zero fallback. See [[feedback_no_fallbacks]] — laut fail statt
stiller Fallback.
