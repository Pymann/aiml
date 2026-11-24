# Review and proof tests — container leaks, 2026-09-29

> Raw probe logs, sources and build logs were removed on 2026-11-24; they are in the git history (commit `6e7c9e8`).

[Plan](../../spec61_container_leak_fix_plan_2026-09-29.md) · [Tests/runner](../../../tests/lifetime/container_leaks/README.md)

## Result

Compiler and native runtime were freshly built with `exodus build -c exodus.json -j 4` and `exodus build -c exodus2.json -j 4` respectively; both builds succeeded. After that, native AOT programs with size/content assertions were checked under Valgrind. Flags: `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`, `AIML_MODEL_REFCOUNT=0`. Binary hashes identical before/after every run. Existing compiler changes were not altered; the source state and build logs are recorded here.

**15 permanent probes × N=200/400 = 30 final measurements:** 10 PASS (five control cases), 12 LEAK (six variants of the four known leak classes), 8 SAFETY_FAIL (four variants of three safety causes, including the new model get crash). All variants classified as LEAK: native exit 0, complete Valgrind report, definite/indirect lost > 0. All confirmed leaks double the lost bytes at double the iteration count. Strings use constant payloads for that.

The strict runner returns exit 1 for the open failures; that is a demonstrated red target state, not a successful fix. Historical expectations do not loosen the gate. The general leak gate registers the ten failing probes as RED and the five controls as GREEN; RED still does not affect the exit code there. So use the strict runner in addition.

## Review corrections to the plan

- P1 safety/layout separated from the self-put reference balance that is only completed in P4.
- Strict acceptance added: the existing RED-reporting gate alone is not enough.
- With a new SHARE contract, map append and update as well as surplus key temporaries must be switched over together.
- Tests 86/87 (now GREEN) and 88, already edited by others, kept.
- B7 added as an additional precondition for model get content checks: the get borrow and value materialisation must not use the same misinterpreted struct value.

## Discovered in addition: B7

The new assertion `decl [view, ref<Item>, {call: [index.get, "key"]}]` followed by `view.text` already causes an invalid read in `strcmp` without any remove/replace, then SIGSEGV. The address contains bytes of the string content, e.g. `0x3d64616f6c796170` ("payload="). The exact first faulty IR operation has not yet been conclusively located. Not a leak finding: access to the borrow view is broken.

At first this read assertion in `map_model_control` and `map_model_replace` triggered the earlier crash and hid their scope teardown. Both first measurements are kept under `initial/`. The assertion now lives in isolation in `map_model_get_borrow`; the two leak/control probes check map size and complete teardown specifically. They were the only changed probes and were re-run together with B7 (`targeted/`). `results.json` and the table below use only the final source state per case/iteration; no crash was counted as a 0-byte success.

## Measured final variants

All figures are bytes. Safety cases can report 0 lost bytes but still failed because of an invalid read/signal.

| Probe | N | Status | Definite | Indirect | Native Exit | Evidence |
|---|---:|---|---:|---:|---:|---|
| map_nested_control | 200 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |
| map_nested_control | 400 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |
| map_nested_self_put | 200 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |
| map_nested_self_put | 400 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |
| list_string_insert | 200 | SAFETY_FAIL | 0 | 0 | 0 | source, Valgrind |
| list_string_insert | 400 | SAFETY_FAIL | 0 | 0 | 0 | source, Valgrind |
| map_shared_control | 200 | LEAK | 3200 | 3800 | 0 | source, Valgrind |
| map_shared_control | 400 | LEAK | 6400 | 7600 | 0 | source, Valgrind |
| map_nested_remove | 200 | LEAK | 4800 | 3200 | 0 | source, Valgrind |
| map_nested_remove | 400 | LEAK | 9600 | 6400 | 0 | source, Valgrind |
| map_model_remove | 200 | LEAK | 3800 | 0 | 0 | source, Valgrind |
| map_model_remove | 400 | LEAK | 7600 | 0 | 0 | source, Valgrind |
| map_model_replace | 200 | LEAK | 3400 | 0 | 0 | source, Valgrind |
| map_model_replace | 400 | LEAK | 6800 | 0 | 0 | source, Valgrind |
| list_pop_bound | 200 | LEAK | 3400 | 0 | 0 | source, Valgrind |
| list_pop_bound | 400 | LEAK | 6800 | 0 | 0 | source, Valgrind |
| list_pop_arg | 200 | LEAK | 3400 | 0 | 0 | source, Valgrind |
| list_pop_arg | 400 | LEAK | 6800 | 0 | 0 | source, Valgrind |
| map_model_control | 200 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_model_control | 400 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_string_control | 200 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_string_control | 400 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_shared_string_key_control | 200 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_shared_string_key_control | 400 | PASS | 0 | 0 | 0 | source, Valgrind |
| list_nested_control | 200 | PASS | 0 | 0 | 0 | source, Valgrind |
| list_nested_control | 400 | PASS | 0 | 0 | 0 | source, Valgrind |
| list_pop_discard | 200 | PASS | 0 | 0 | 0 | source, Valgrind |
| list_pop_discard | 400 | PASS | 0 | 0 | 0 | source, Valgrind |
| map_model_get_borrow | 200 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |
| map_model_get_borrow | 400 | SAFETY_FAIL | 0 | 0 | -11 | source, Valgrind |

## Repeating

```bash
python3 tests/lifetime/container_leaks/run.py
```

Further options and test mapping in the test README. The measurement artifacts here contain sources and logs, no binaries. `source_state.json`, `preexisting_source_changes.patch` and the two run `provenance.json` files document the examined state. The large existing suites as well as WASM/browser were not re-run for this test/plan review; their acceptance remains implementation work. This report claims neither leak freedom of the language nor complete spec 61 conformance.
