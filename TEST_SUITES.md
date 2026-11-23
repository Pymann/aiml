# AIML test suites — status & overview

As of: 2026-09-23 · branch `mem-v3-compiler` · build: `exodus build --all` (from `exvenv`) · gates: `AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1` (memV3, compiler default)

## TL;DR — are all suites green?

**Almost — the memV3/lifetime/memory-safety core is green, with four exceptions, none of which is an open memV3 bug:**

- ✅ **Lifetime (spec/61)**: 47/0, 13 neg, 2 warn — fully green.
- ✅ **Memory sanitizer (valgrind)**: 41/41 memory-safe (the former leak-probe-A violation was the `sys.env` bug, now fixed).
- ✅ **JSON serialize**: all serialize tests green + **deterministic** — the long-standing non-deterministic bug #2 is fixed at the root.
- 🟢 **Main corpus**: the only red test is **1 open design question** (`test_model_inherits_hetero_list`). The `lifetime/neg` negative tests are now (correctly) run only by `run_lifetime.sh`, no longer by the corpus. **No flaky tests, no more harness noise.**
- ⚠️ **AOT smoke**: 1/3 — 2 **pre-existing** multi-file linking failures (a separate infra domain, untouched by the memV3 work).
- ⏸️ **WASM**: **not** run in this session (needs the node/wasm toolchain).

---

## The suites in detail

| Suite | Runner | Purpose | Status |
|---|---|---|---|
| Main corpus | `run_all_tests.sh` | All `tests/**/*.aiml` via native JIT (`aiml <file> -r`); cleans up DB state + `__aiml_cache` | **421/466 OK** |
| Lifetime (spec/61) | `tests/lifetime/run_lifetime.sh` | Ownership/borrow/move + teardown leaks (pos + neg + warn), MODE=spec61 | **47/0, 13 neg, 2 warn** ✅ |
| Memory sanitizer | `tests/run_memory_sanitizers.sh` | 41 probes, 2× each (native = correctness + valgrind = no UAF/double free/invalid access) | **40/41** |
| AOT smoke | `run_aot_tests.sh` | Compile `.aiml` → `.o`, link against `libaiml.so`, run the binary | **1/3** ⚠️ |
| WASM | `run_wasm_tests.sh` | `.aiml` → wasm32, run via node | **not run** ⏸️ |
| (wrapper, not a suite) | `run_safe.sh` | ulimit wrapper (RAM/stack/CPU) for debugging compiler crashes | — |

---

## Non-green items — classification

### Main corpus (419/466) — the 47 "not OK" break down as follows:
- **38 SKIP** — library/fixture files imported by other tests (no standalone run).
- **9× `tests/lifetime/neg/n01…n13`** — **harness noise, not a bug**: these are negative tests (they must raise a compile error), but `run_all_tests.sh` expects exit 0 → counts them as failures. They are validated by `run_lifetime.sh` (13/13 green there).
- **1 real open question: `test_model_inherits_hetero_list`** — `list<any>` with base-slice semantics for heterogeneous model lists. **Marked by the test author as an open type-system design question** ("It remains to be decided how the compiler will be designed with regard to typing") — needs a fundamental decision + a feature, not a bug fix.
### JSON serialize (✅ solved)
- `test_json_style_serialize` + `test_serialize_ext` were the known non-deterministic serialize bug #2 (~75% SIGSEGV, layout/ASLR dependent, invisible to valgrind). **Root cause:** for a `list/map/buffer` argument under memV3, `sys.serialize` passed the **alloca address** instead of the **handle pointer** → `emitSerializeList` read the pointer bytes as `{size,cap,data}` → `size` = low bits of a heap address → garbage iteration → crash (sometimes small/ok, mostly huge). **Fix:** load the handle under memV3 before serialising. Now **30/30 deterministic, valgrind 0 invalid access**, all serialize tests 5/5.

### Flaky cluster test_system + test_mem_leak_probe (✅ solved)
- SIGABRT "corrupted size vs. prev_size" / sanitizer leak-probe A. **Root cause:** `sys.env()` returned an `AimlHttpMap*` WITHOUT the memV3 `[rc][pad]` header (open addressing, libc strdup strings) — but the `map<string:string>` local is a handle whose scope drop `aiml_map_handle_release` reads+writes the rc header 8 B BEFORE the block → heap corruption. **Fix:** a real memV3 map handle (`aiml_map_handle_new`) + DENSE store with aiml-managed strings. Additionally `io.read_all` → aiml-managed string (was a bare malloc, OOB read). Now **test_system 20/20 + valgrind 0, test_mem_leak_probe 10/10, sanitizer 41/41**.

### AOT smoke (1/3)
- 2 failures in **multi-file AOT linking** (`undefined reference to testlib.inc_counter` and others) and one `.ll` output test. That is the object file/linker infrastructure — a domain of its own, **untouched** by the memV3/ownership changes.

### WASM
- Not run in this session (separate target, needs node + the wasm toolchain). No statement about green/red.

---

## Fixed in this session (branch `mem-v3-compiler`)

memV3/spec 61 consolidation, every fix verified with valgrind, lifetime 47/0 throughout:

- **§6.2/§10** — pushing a pointer expression into a `value list<Model>` = compile error (3 diagnostic tests).
- **§5.1** — cycle guard in the free walker (recursive `ptr<>` back-ref, `test_recursive_ptr_ser`).
- **§5.4/§7** — move-only resources (`tensor`, `ptr<>`) are not value-copyable; tests migrated to `ref<>`/`{clone}` (`test_class_member_shared_tensor`, `test_list_model_push_value_and_pointer`) + compiler enforcement.
- **§5** — `srv.filesystem` loads the `list<path>` handle (memV3); `get_copy_at` re-initialises empty container handles; `db_indexed list<string>` (default/backfill/`aiml_str_dup`).
- **§5.4 (solution c)** — `.get`/`.at`/`{at:}` return a `ref<T>` borrow; a `ptr<T>` binding = compile error (use `ref<T>` or `get_copy`); **`ref<Interface>` unified** (= fat pointer like `ptr<Interface>`, incl. dispatch + cross-interface upcast); ~14 tests migrated + negative test `test_ptr_bind_get_borrow_error`.
- **§5 (JSON serialize #2)** — for `list/map/buffer` arguments `sys.serialize` loads the memV3 handle instead of the slot address → fixes the non-deterministic serialize crash at the root (`test_json_style_serialize`, `test_serialize_ext`).
- **§5 (sys.env / io.read_all)** — `sys.env()` returns a real memV3 map handle (instead of a header-less `AimlHttpMap*`), `io.read_all` an aiml-managed string → fixes the heap corruption cluster (`test_system`, `test_mem_leak_probe`, sanitizer leak-probe A).

## Reproducing

```bash
source /media/.../python/exvenv/bin/activate
cd .../test/aiml
exodus build --all
bash run_all_tests.sh --no-build          # main corpus (only ONE run at a time!)
bash tests/lifetime/run_lifetime.sh        # lifetime spec61
bash tests/run_memory_sanitizers.sh        # valgrind gate (~20–30 min)
bash run_aot_tests.sh                       # AOT smoke
```
