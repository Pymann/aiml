# Spec 61: container leaks and memory errors

Targeted regressions for the [implementation plan](../../../doc/spec61_container_leak_fix_plan_2026-09-29.md).
They complement the existing lifetime suite: its tests for list remove, shared lists and
discarded pops do not cover the map/string pop errors shown here.

## Running

From the AIML project root, with a built compiler and `libaiml` that match each other:

```bash
python3 tests/lifetime/container_leaks/run.py
python3 tests/lifetime/container_leaks/run.py --only map_shared_control map_shared_string_key_control
python3 tests/lifetime/container_leaks/run.py --iterations 200 --output /tmp/new-container-audit
```

`--output` must be a new directory. Without it, a new directory is created under `/tmp`
and printed. `--root` overrides the automatically determined AIML project path.
Requirements: Python 3.11+, g++, Valgrind and native AIML build artifacts. The runner does not build
the compiler; it builds every repro AOT and links against `out/libaiml/libaiml.so`.

The runner sets `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`, `AIML_MODEL_REFCOUNT=0` and runs every
probe natively as well as under Valgrind. Default: 200 and 400 iterations. The sources contain
exactly one marked loop bound; only that bound is varied for the test run.
String payloads stay constant, so that scaling does not depend on their length.

**Goal of every test: correct contents, exit 0, 0 definite/indirect lost and no memory errors.**
Historical failures in `cases.json` are diagnostic metadata, not expected successful XFAILs.
As long as a failure exists, the runner returns exit 1. A missing tool is a failure too.
A crash, a compiler error or a missing complete Valgrind log do not count as proof of a leak.
The runner distinguishes LEAK, SAFETY_FAIL, ASSERTION_OR_RUN_FAIL and infrastructure errors.
Binary hashes before/after the run must be identical, otherwise the acceptance is invalid.

Artifacts are kept: per variant the source/build/link/native/Valgrind logs, the generated binary,
`results.json` and `provenance.json`. Native runs produce no core dumps. Definite/indirect
are only read from complete Valgrind reports; RSS is not a gate here.

## Cases

| Probe | Finding | Isolated difference / check |
|---|---|---|
| `map_nested_control` | B1 safety | map with a list value, ordinary scope teardown; despite the name NOT a green control |
| `map_nested_self_put` | B1 safety / P4 | additionally store the same value again via `get` |
| `list_string_insert` | B2 safety | insert a local string; content/size correct, check ownership on drop |
| `map_shared_control` | B3 leak | numeric keys and a shared model; despite the name a failing case |
| `map_shared_string_key_control` | control for B3 | the same ownership path with a string key |
| `map_nested_remove` | B4 leak | remove an inner list value from the map; map empty afterwards |
| `map_model_remove` | B4 leak | remove an inline model with a string field; map empty afterwards |
| `map_model_replace` | B5 leak | replace a model under the same key; map stays at one entry |
| `map_model_get_borrow` | B7 safety (review) | separate model get as `ref<Item>` with a field read assertion |
| `map_model_control` | control for B4/B5 | store once, check size, normal scope teardown |
| `map_string_control` | control for B4 | remove a direct string value; map empty afterwards |
| `list_pop_bound` | B6 leak | bind a pop to a local string; check text and empty list |
| `list_pop_arg` | B6 leak | pass a pop to a function; callee checks text, caller the empty list |
| `list_pop_discard` | control for B6 | discard a pop directly; list empty |
| `list_nested_control` | layout control | nested list instead of a nested map |

The names keep the mapping to the original investigation. The `finding` mapping
in `cases.json`, not the word `control` in the file name, marks green counter-probes.

The subdirectory location prevents the normal `tests/lifetime/[0-9]*.aiml` glob from collecting open cases
as green tests. The existing leak gate registers confirmed failures as RED and
clean counter-probes as GREEN. **Its exit 0 alone is not enough:** it deliberately does not let RED
fail. For this work always use the strict runner above. After a repair, move the
respective cases to GREEN in the general gate; this runner needs no
loosening of its expectations for that.

The larger alias/move/WASM test matrix in the implementation plan is additional later
fix acceptance. These minimal tests claim neither complete spec 61 coverage nor a
WASM/browser acceptance.

The model get read assertion showed an additional crash that occurs independently of replace. It therefore lives in `map_model_get_borrow`, instead of hiding the B5 leak behind an earlier crash. B5 and its control check map size and complete scope teardown in isolation; the stored field contents are covered separately by B7.
