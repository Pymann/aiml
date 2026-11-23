# AIML: prevent pointers in model value lists — 2026-09-05

## Change

`src/core/ast_compiler_call.cpp` now checks the actual LLVM representation of the argument for `list<T>.push()`. A pointer argument for a struct value element leads to a compile error regardless of the AST form. That covers variables, members and nested CallExprs such as `destination.push(source.get(index))`. After the diagnostic no faulty push IR is generated.

The earlier check only examined VariableExpr and could therefore miss the CallExpr causing the 9rooms reveal crash. The existing interface fat pointer upcast stays exempt and unchanged. List layout and get() semantics were not changed. The fix is limited to the pointer/aggregate conflict on push, not a complete check of all possible container type conflicts.

`compiler_change.patch` shows only this change relative to the user's already edited working state before the fix began. Other existing source changes were kept.

## Regression tests

- `tests/test_list_model_nested_get_push_repro.aiml`: nested get/push, now a compile error.
- `tests/test_ptr_push_into_value_list_error.aiml`: the existing direct pointer case stays a compile error.
- `tests/test_ptr_member_push_into_value_list_error.aiml`: a member expression is rejected.
- `tests/test_ptr_call_push_into_value_list_error.aiml`: a function with a ptr return is rejected.
- `tests/test_list_model_push_value_and_pointer.aiml`: valid model returns, member value copies, get via a local value variable, explicit dereferencing, pointer lists, list growth and copy/sharing semantics pass.

`run_all_tests.sh` recognises the four negative tests and requires the specific compile diagnostic text in addition to exit 1. A renewed runtime exit 1 of the old repro therefore cannot count as a passed test. Shell syntax was checked. The full global suite was not run; the focused selection is in results.json.

## Validation

- Compiler rebuilt successfully via `exodus build -c exodus.json`.
- 30 targeted native checks: **29 passed**, one existing heterogeneous model list repro crashes.
- Isolated before/after comparison for `test_model_inherits_hetero_list.aiml`: **both SIGSEGV (-11), byte-identical LLVM IR**. SHA-256 and exit codes in baseline_comparison.json. The isolated baseline compiler was built separately under /tmp with the previously saved ast_compiler_call.cpp and otherwise the same object files; no temporary reset of the working state.
- 4 wasm32 compile checks: three new pointer conflicts correctly rejected, the positive model/pointer test compiled successfully. No WASM browser run.
- Explicit dereferencing in the positive test finally also checked natively and for wasm32.
- `git diff --check` for the changed existing files succeeded.

The negative tests are counted as PASS when compilation aborts with the expected pointer/value error, not when the program crashes. The attached raw logs openly document the single remaining old failure.

## Effect on 9rooms_v2

The faulty call in `BoardScreen.cp_tick` is now rejected early at the next rebuild. For a buildable client again, the already documented explicit MockCard value copy is required there. This task changes AIML; the 9rooms workaround was not applied here and the web client was not redeployed.

## Re-check

From the AIML project directory:

```sh
./out/aiml/aiml tests/test_list_model_nested_get_push_repro.aiml -r
# Expected: exit 1 AND "pointer instead of a value" / "Compilation failed".
./out/aiml/aiml tests/test_list_model_push_value_and_pointer.aiml -r
# Expected: exit 0, PASS.
```
