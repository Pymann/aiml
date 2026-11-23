# Console in the host process and access levels for symbols

As of: 16.10.2026. Source-based architecture review, triggered by the cosmos question
*"a console for the server itself, one thread or a separate JIT per user — are the
server's symbols available there, and can every symbol be given a level?"*
No implementation. The measurements below were taken on the built `cosmosd` and on the compiler source,
not estimated.

> **Result as a spec:** `spec/65_symbol_clearance.md` (PROPOSAL).

## 1. Are the host's symbols available in the JIT?

Today **no**, and for one single reason: the missing linker flag.

| Measurement on the built `cosmosd` | Value |
|---|---|
| `nm cosmosd` → aiml functions (type `T`) | **38** (`StepScheduler_claim`, `StepScheduler_report`, …) |
| `nm -D cosmosd` → of those in the dynamic directory | **0** |
| State symbol | `__lib_sources_server_server_state_aiml_g_runtime` (type `B`) present, not exported |
| Link flags | `-Wl,-rpath,…`; **no** `-rdynamic` |

The JIT calls `llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr)`
(`compiler.cpp:1353`, `:1406`, `:1817`, `:3057`) and thereby resolves everything that is in `.dynsym`:
the whole `libaiml` (`aiml_*`), libc, OpenSSL. The host's functions are not
there. The state is addressable, but not findable.

Two ways to change that, with very different consequences:

- **`-rdynamic`** — exports **everything**, every internal too. Built in an hour and from then on
  permanently the reason why the host can no longer be rebuilt.
- **Export table** — the host registers by name what it offers, via
  `DynamicLibrary::AddSymbol` (already used in-house: `exec_impl.cpp:175`,
  `compiler.cpp:1375`, `:1879–1899`), the script side declares `- extern: "C"`
  (`tests/test_ffi.aiml`, preparation in `src/core/ast/extern_builder.cpp`). That is a
  contract that can be versioned and checked.

## 2. One thread per user is not possible

`aiml_isolated_exec` builds a fresh `AIMLCompiler` with its own `LLVMContext` — but it has to
**save and write back** the caller's global state around it
(`exec_impl.cpp:335–362`):

```cpp
auto* saved_ctx = g_active_context;  auto* saved_ee = g_active_ee;
auto* saved_fptable = g_fptable_current;
{ AIMLCompiler iso; … rc = iso.compileAndRun(yaml_source); }
g_active_context = saved_ctx;  g_active_ee = saved_ee;  g_fptable_current = saved_fptable;
g_fptable = nullptr;  g_fptable_versions = nullptr;  g_func_names = nullptr;  g_func_count = 0;
```

That only works **sequentially**. Two threads compiling at the same time overwrite
these globals; the second one breaks the state of the first while cleaning up. That is
not a missing mutex but missing context binding — the same root as the comment in
`exec_impl.cpp` about several isolated contexts: *"but need a rebindable `ctx.module()`
— today a reference at 601 places."*

**Conclusion: one console per user as a process of its own, not as a thread.** That costs nothing
that was wanted, and additionally provides crash containment, a memory/CPU limit, `kill` and
compliance with the KV single-writer rule.

## 3. The JIT's symbol table has no views

Decisive for the level idea: `DynamicLibrary::AddSymbol` writes into a **process-wide**
LLVM table without tenant separation. And `compileYamlChunk` pushes *all* functions of the
active context into it before every bundle compilation (`exec_impl.cpp:168–176`):

```cpp
for (auto& [nm, fi] : g_active_context->functions) {
    uint64_t addr = g_active_ee->getFunctionAddress(nm);
    if (addr) llvm::sys::DynamicLibrary::AddSymbol(nm, (void*)addr);
}
```

A symbol that becomes visible for user A is thereby resolvable for **every** JIT in the process.

**Conclusion: levels cannot be built as visibility in the JIT.** They have to be a
**name admission at compile time**. That is no weakening — it only moves the check
to the place where the names are resolved anyway.

## 4. Most of the scaffolding for levels already exists

`FunctionInfo` (`src/core/context.h:92`) already carries everything a flow check needs:

```cpp
AccessLevel access = AccessLevel::Public;    // exists — never checked on FUNCTIONS
std::set<std::string> referencedFuncs;       // the call graph (spec/29_2 §16)
std::vector<BuiltinUse> referencedBuiltins;  // builtins called (spec/62 §10.2)
std::set<std::string> referencedGlobals;     // globals touched (spec/29_2 §22)
```

`AccessLevel` is today enforced **only on class members** (`src/core/utils.cpp:40`,
`ast_compiler.cpp:6862/6956`); on functions the field is dead code. The three `referenced*` sets
are collected anyway for bundling and the purity check.

From this follows the part that turns the idea from decoration into a checking tool: not only the access
must be checked, but the **privilege flow along the call edges**. If a function open to
everyone internally calls a restricted one, every user gets the restricted effect via the
detour. The static rule against it can be checked with the existing data; the
model for the table form is `src/core/purity.h` (default per namespace, exceptions per
function).

## 5. The four escape routes

| Escape route | Why it bypasses levels | Countermeasure |
|---|---|---|
| `- extern: "C"` | The script declares the symbol itself; **no** resolution against the level table takes place | `extern` only for names of the released export list (`extern_builder.cpp`) |
| `sys.exe`, `sys.lib_load`, `sys.bundle`, `db.open_kv`, `io.*`, `net.*` | Builtins, not context symbols — bypass in one line | Levels **also** in the builtin registry, as a table like `purity.h` |
| `ptr<T>` + `cast:` + buffer | Computing and writing addresses reaches everything in-process, `g_runtime` too | No raw pointers/pointer casts in the restricted dialect |
| nested `aiml_exec_code` | A script compiles a new one — with which clearance? | The clearance is **inherited and cannot be raised** |

## 6. The limit that remains

Levels are a check at compile time, **not a sandbox**. They prevent accidents, make
intent readable and yield a versionable API surface. They do not protect against a malicious participant
in the same process; there only the process boundary helps and, in the end, real
isolation (rootless/userns). The existing condition — no sandbox in the MVP, so only
trusted participants — is **not** lifted by levels.

## 7. And the normal case: one binary, no need

The user's objection (16.10.2026) and a requirement for the spec: normally **one** binary from
**one** author runs; there something like this is simply not needed. Levels are therefore not a property
of a program but of a **boundary** between a host and foreign code entering it.
Without this boundary the feature must not merely be switched off — it must not occur
at all: no annotation, no pass, no diagnostic, no runtime cost, nothing to
learn. The spec therefore starts with this sentence and not with the mechanics.

## 8. Recommended order

1. Console as a **process of its own** per user, via the existing authenticated RPCs. Covers the
   cases named (creating jobs, hooks, settings, queue/agents) without `-rdynamic`, without a
   second KV writer, without crash risk for the host.
2. Levels as a language means with three check points (function resolution, builtin registry,
   `extern` admission) **plus** a flow check over the call edges — `spec/65`.
3. Do **not** build a host-internal JIT with access to its own symbols as long as 1 and 2 are not in place;
   then via an export table, never via `-rdynamic`.

## 9. Addendum 16.10.2026: two findings while sharpening the specs

Going through the `sys.exec` page, two things came out that were not written down anywhere before:

1. **`sys.get_arg` returns `""` for every index in a JIT run.** `main(argc, argv)` writes the
   globals `__argv`/`__argc` — that only happens in the AOT path (`compiler.cpp`). In an
   `aiml -r` run nobody writes them, `__argv` stays null, and `SysGetArg` takes the
   safety branch (the comment there literally reads *"if argv is null (JIT)"*). A program
   that reads its own arguments therefore behaves differently in the JIT than when built. Added to
   `spec/23_system.md`.

2. **The isolated object cache has no version key in the AOT case.**
   `aiml_isolated_exec` only takes over `compilerPath` `if (g_active_context)`; in AOT that is
   null, `compilerBinaryRev()` returns `"unknown"`, `cacheRevDir()` ends up in `rev_unknown`.
   Compiled scripts of a console would therefore lie under a key that does not
   distinguish compiler versions. Added to `spec/29_2_sys_exec.md` §12, with the
   direction: hang the key on the revision of `libaiml.so`, and a missing key should
   **switch off** the cache instead of opening a shared bucket.

Plus a correction in `spec/29_2` §3.2: the isolated context was described there as *"complete
sandboxing"* and *"No connection to the host program"*. That directly contradicted the result
of this analysis (§3, §6) — same process, same address space, same process-wide
symbol table. Had spec/65 relied on this wording, the console would have been built as
"sandboxed".

**Consequence for the order in §8:** point 1 (console as a process of its own) stays; before point 3
the cache key must be fixed, because the isolated context is the only available
path in the AOT host (spec/65 F65-25).

## 10. Addendum: §2 is measured, not just read

The statement from §2 ("one thread per user is not possible") at first rested on reading the
code. It has since been **measured**, by a test that exists exactly for this:

`tests/test_concurrent_isolated_exec.aiml` has four threads each compile and run three **different** bundles
(different sources, so that no cache hides the process). Its
header calls itself "the measurement" for `spec/62` F62-99 — the requirement that serialises
compilation *as long as the answer is unknown*. Expected return value: 0.

Measured on 16.10.2026 on a `--clean` full build: **SIGSEGV (139)**. In the whole corpus (846
tests, 730 passed, 78 skipped) that is the **only** failure outside the two
spec-before-code labs (`tests/closures/` 21, `tests/clearance/` 16).

So the answer is no longer "would be the next step", but: concurrent isolated
compilation crashes today. The serialisation in spec/62 F62-99 is thus no longer a precaution
but the only possible form — and `spec/65` F65-25 (one process per user) rests on
a measurement.

## 11. Two infrastructure findings on the side

1. **Five corpus tests have never run.** `run_all_tests.sh` collects with
   `find tests/ -name "*.aiml" | grep -v "build" | grep -v "out" | …`. The filters are meant to exclude the
   directories `build/` and `out/`, but hit every **name** that contains the
   string — and "lay**out**" like "r**out**es" contain it:
   `tests/lifetime/45_store_view_outparam.aiml`, `tests/test_event_system_layout_repro.aiml`,
   `tests/test_http_routes.aiml`, `tests/test_layout_helpers.aiml`,
   `tests/test_ptr_i32_out_param.aiml`. Fix: exclude path components
   (`-not -path "*/out/*"`), not substrings.
2. **Faulty `sys.exe` arguments crash the compiler instead of being diagnosed.** A
   `call: [sys.exe, "id", []]` — first argument a string instead of `list<string>` (spec/23 §5.1) —
   ended in SIGSEGV. That was a mistake in one of my probes, but a crash is the wrong
   answer to it; what would be expected is "rather fail loudly" with a type message.

### Correction to §10 (17.10.2026)

The wording in §10 was too strong. Re-measured: the crash occurred **once** in the full
corpus run; after that the same test passed in the next full run and in **six**
consecutive single runs. The error is thus **sporadic and load-dependent**, not
deterministic.

That is the more unpleasant kind: a test that is green six times out of seven invites the
conclusion that concurrent isolated compilation is fine. It is not fine — it is
unproven, and one observed crash is enough to keep the serialisation from spec/62 F62-99 and
the one-process rule (spec/65 F65-25). Corrected accordingly in spec/65.

## 12. ⚠️ HALTED: spec vs. corpus conflict with the restored `free:`

After `free` is routed again at the top level (§11 point 1 was only half the
story), it actually runs there — and **5 of 8** affected tests now fail with
the same, correct diagnostic:

```
tests/test_oo.aiml:55: free of a ref<T> ('var_c'):
    a borrow never owns and must not be freed (§10).
```

The pattern in the corpus:

```yaml
- decl: [box, ref<ListBox>, { new: ListBox }]    # heap allocation bound to a BORROW
...
- free: [box]                                    # rejected: a borrow never owns
```

Affected: `test_oo.aiml:55`, `test_chan_class_member.aiml:37`,
`test_buffer_class_member_heap_init_repro.aiml:20`,
`test_list_class_member_heap_init_repro.aiml:21`,
`test_map_class_member_heap_init_repro.aiml:28`.

**Both states are wrong, not just the new one.** Before, the `free` was discarded: the
allocation leaked, and the test was green. Now the compiler correctly rejects the invalid
release, and the test is red. In both cases the source was not spec-conformant.

The three other tests (`test_model_defaults`, `test_inherited_defaults`,
`test_chan_ptr_model_repro`) now pass and for the first time really free what they
claim to free.

### Why this is halted here

This is exactly the conflict for which the working rule "enforce the spec, adapt tests to be spec-conformant,
do NOT soften spec/impl — **halt on such conflicts**" applies. Two routes,
and the choice is not mine:

1. **Make the tests spec-conformant:** `ptr<ListBox>` instead of `ref<ListBox>` for a `new` allocation
   that is freed later. That corresponds to the goal ("`new`/`ptr<Model>` is the heap path,
   `ref` is a borrow") and leaves §10 untouched. Five files.
2. **Loosen §10**, so that a `ref<T>` initialised directly from `{ new: T }` counts
   as an owner. That would be an intervention in the ownership model and contradicts the condition
   not to soften spec/impl — I consider it the wrong route, but name it because otherwise the
   decision would not be described completely.

Until the decision the routing fix stays in: the five red tests are **correct
diagnostics on faulty source**, not a compiler regression. Reverting would be done
in a minute, but would hide the error again.
