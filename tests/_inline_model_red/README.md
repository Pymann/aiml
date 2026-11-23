# Red contract tests: inline `list<Model>` element release (package 1)

Belongs to `doc/spec61_inline_value_model_release_plan_2026-09-24.md`. These tests encode the
target contract (spec 61 §5.4) for inline value-model elements. They deliberately live under `_inline_model_red/`
(prefix `_` → excluded from the `run_all_tests.sh` corpus), because they are **RED until package 5** and should not
break the green corpus. Once packages 3+5 are done, the green cases move to
`tests/lifetime/[0-9]*` (content) or are taken into the mem-sanitizer acceptance with a hard leak gate.

## Running
```sh
# Content (JIT):
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 out/aiml/aiml -r tests/_inline_model_red/49_value_survives_remove.aiml
# Leak (standalone valgrind):
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 out/aiml/aiml tests/_inline_model_red/49_value_survives_remove.aiml -o /tmp/r/main.o
g++ /tmp/r/*.o -Lout/libaiml -laiml -lyaml-cpp -lpthread -lssl -lcrypto -ldl -Wl,-rpath,$PWD/out/libaiml -o /tmp/r/bin
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 valgrind --leak-check=full /tmp/r/bin
```

## Findings at creation (2026-09-24, aiml @ pop/clear fix state)

| Test | Content (JIT) | Leak (valgrind) | Meaning |
|---|---|---|---|
| `49_value_survives_remove` | **PASS** | **RED: 47 def + 16 indirect lost** | The copy survives `remove` content-wise — BUT the removed elemKind-2 element is not released (remove defers kind 2). |
| `50_share_semantics` | **PASS** | **RED: 16 B def lost** (Codex re-measurement 2026-09-24) | Container-shared + scalar-independent is observably correct, BUT `copy.label = "changed"` leaks the old string (`aiml_str_dup ← main`). → **SECOND leak path: scalar/string field reassignment on a value-model view**, not only the release leak. Test 50 is NOT green. |

## Central insight (empirical, important for the implementation)

The content in test 49 is green today **only because** `remove`/`clear` do NOT release the value-model element
(the leak IS the reason the shared view stays valid). A **naive**
activation of element release (without CopyRetain) would leave the view dangling → content would break
(double-free/UAF). That supports the plan sequence: **CopyRetain materialisation first, then release.**
In the end test 49 requires BOTH together: PASS + 0 lost.

⚠️ **Correction (Codex §11):** It is NOT "the only gap is the release leak". Test 50 shows
a second leak (field reassignment on an unbalanced value copy). A pure release patch
is therefore not enough. Share semantics are observably correct, but the value copy TODAY does not materialise
its own member ownership (the decl path only memcpy's struct bytes, without retain — ast_compiler_stmt.cpp
~1990). Precise distinction: (1) independent struct bytes with unsecured field references vs.
(2) a real `ref` to the slot. "copy-on-write-at-set" is NOT established in the respective path.

## Package 2 — retain/release audit (2026-09-24)

| Test | Result | Meaning |
|---|---|---|
| `p2_retain_coverage` | **GREEN** (PASS + 0 lost + 0 errors, 200 iter) | `aiml_ser_retain` co-owns string + list<i32> + nested value model + map correctly via the existing `store: [ref<Model>, at(list,i)]` path. Linchpin validated for the common field kinds. |
| `p2_array_field_leak_RED` | **RED** (6780 B / 400 blocks lost, no double-free) | `array<string:N>` as a model field → serFieldSubtree RAWBYTES → retain/release do not see the elements → pure leak. §9.6 gap confirmed + quantified. |

**Open package 2/3 CODE work (with risk):** (a) array field lifecycle descriptor (STRUCT composition) — ⚠️ changes serFieldSubtree → **schema fp/wire risk** (like the matches KV FATALs); consider a separate lifecycle descriptor. (b) Derive the `isCopyableValue` trait from `modelIsMoveOnly` (§9.5). (c) Wire up CopyRetain materialisation at decl/set/return/push (§9.2/§9.3, package 3) — the broad ABI intervention, needs the full behaviour gate.

## Package 3 — CopyRetain mechanics PROVEN in isolation (2026-09-24)

| Test | Result | Meaning |
|---|---|---|
| `p3_copyretain_survives_source_drop` | **GREEN** (PASS + 0 lost + 0 errors, 200 iter) | A retain copy (memcpy + `aiml_ser_retain` via the store-retain path) survives the kind-2 RELEASE of the source list element (aiml_list_free scope drop = the same `aiml_ser_free_decoded` that remove/clear use) with intact content (string/list/map/nested model) and balanced rc — **no double-free, no leak.** |

**Conclusion:** The runtime mechanics (retain copy ⊕ element release) are validated risk-free. The earlier remove deep-free regression was NOT due to the release, but to the missing retain copy (view). Remaining (risky) work: wire CopyRetain into the IMPLICIT `decl copy = at(..)` path (instead of is_view_alias) + activate remove/clear/pop release — affects the core structures (ZoneEntry/MockCard/ActiveAbility), needs the full game+client behaviour gate.

## Package 3 — stage A attempt (decl-only CopyRetain) DISCARDED (2026-09-24)

Attempt: emit CopyRetain only at the `decl copy = at(..)` init (memcpy → `aiml_ser_retain` +
owned drop + `is_view_alias=false`), escape sinks (set/idx/return/push) UNCHANGED.
**Result: mem-sanitizer FAIL (1 check).** → **Empirical proof: decl alone is NOT enough.**
The escape paths (ast_compiler_stmt.cpp 2313/2471/3655, 4662, ast_compiler_call.cpp:1545 push) have
their own is_view_alias/deep-copy logic; when decl makes the copy owned+retain, the
escapes contradict it (double retain/copy or a wrong move → double-free/imbalance). Confirms plan §3/§4:
value materialisation must be consolidated across ALL sinks AT THE SAME TIME (decl + set + idx +
return + push + store), with a uniform ownership fact instead of the is_view_alias heuristic. Stage A
was reverted (green state restored). The actual package 3 implementation is therefore a
coordinated multi-sink rework — not an isolated decl patch.
