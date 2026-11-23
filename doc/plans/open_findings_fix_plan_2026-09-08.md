# Open findings: concrete remaining work plan

As of: 2026-09-08. Line numbers refer to the working tree as read during the review, which was being edited continuously; function names serve as stable anchors. This is an implementation plan, not proof of tests run. No runtime/compiler changes as part of this review.

## Authoritative lifetime approach (correction 2026-09-08)

According to the user, WP-C is discarded. Authoritative are `spec/61_lifetime_ownership.md` (still marked as PROPOSAL) and `doc/plans/lifetime_qualifier_2026-09-08.md`: locally determined single-owner semantics with `scope`/`persistent`/`consumed`, without whole-program escape analysis and without runtime drop flags. This remaining work plan does not reactivate WP-C. The decoder and KV findings are independent of it; their implementation status must be re-checked each time because of ongoing changes.

## 1. Cleaned-up status

- F1: The originally criticised scalars and string lengths are corrected (`src/sys/ser_runtime.cpp:164–174,319–354`). The decoder as a whole is not released yet: for remaining gaps see P0.1 and P0.2.
- F2: The statement "no allocations" in the spec table is corrected. A contradictory runtime summary remains; see P2.
- F3: Error path coverage and return value checking in the measurement loop are still missing; see P0.4.
- F4/F5: The concretely reported contradictions and the blanket UAF safety claim are corrected in the ownership document. That is not an acceptance of the ownership semantics still to be implemented.
- F6: The criticised `exodus-asan.json` no longer exists. Still open is the proof that memory accesses in the generated AIML code are instrumented as well; see P1.2.

## 2. P0.1 — pass the actual blob boundary to the decoder

**Locations:** `src/sys/ser_runtime.h:84` (`aiml_ser_decode`), `src/sys/ser_runtime.cpp:719–760`; `src/sys/db_runtime.cpp:117–120` (`AimlKvValueLoc`), `:1678–1710` (`aiml_db_kv_get_copy_at`).

**Finding:** the decoder receives a pointer, but no actual memory length. It already reads the four-byte prefix unchecked and uses its value as the boundary. The new field checks therefore do not protect against a prefix that claims more bytes than the stored value contains. A concrete crash was not run in this review.

**Proposed change:**

1. Add a new internal, length-checked entry point, e.g. `int aiml_ser_decode_checked(const char* blob, size_t blob_size, void* out, const AimlTypeNode* nodes, int32_t n_nodes)`. Keep the existing ABI for now; the unsized API must not remain the checking path for untrusted stored data.
2. Check `blob_size >= 4` before accessing the prefix. After that check `data_len >= 0` and `data_len <= blob_size - 4`. Keep the existing contract for empty payloads for now; do not change format semantics in passing.
3. Read the fingerprint/guard only within the payload range checked this way. Keep the existing legacy compatibility; no new mandatory guard format in this fix.
4. KV callers pass `loc.length` or `it->second.size()`. For the WAL additionally ensure `offset <= wal_mmap_size` and `length <= wal_mmap_size - offset` before forming the pointer. Do not use an addition prone to overflow as the check.
5. Switch internal position/length checks to checked remaining lengths: `needed <= end - pos` after `pos <= end`; in particular `r_i32`, scalars and strings. `pos + len` can currently overflow as a signed addition.
6. Record further decoder entry points: slot decode (`db_runtime.cpp:290`), deep copy (`ser_runtime.cpp:715`), mirror (`mirror_runtime.cpp:424,1110,1382`). Carry the real frame/blob lengths available there through to the checked entry point. These are additional audit points; their complete error contracts were not checked here.

**Acceptance:** prefix lengths of 0–3 bytes, a negative/excessive payload length and an excessive string length lead to a controlled error; no out-of-bounds accesses under ASan/UBSan. Valid guard blobs and supported legacy blobs stay readable.

## 3. P0.2 — close the remaining decode branches

**Locations:** `src/sys/ser_runtime.cpp`, function `dec`.

| Location | Current error | Concrete change |
|---|---|---|
| `:419–423`, MAP | `if (pos >= end) break` reports no error despite further announced entries. | On a missing required entry, set an error and abort. If zero-byte types are allowed, take their valid representation into account instead of requiring payload bytes across the board. |
| `:428–445`, BUFFER | A negative size is taken over; allocation happens before the payload check; missing data sets no error. The cast of `bytes` to `int` can distort the check. | Check size and stride, check the multiplication for overflow, require the complete payload before allocation. On error leave the buffer empty and set an error. No cast to `int` for the range check. |
| `:449–454`, RAWBYTES | An incomplete range is silently skipped. | Report an invalid length or missing bytes as an error; only copy after the remaining length check. |
| `:456–460`, PTR_STRUCT | A missing marker byte is treated like a valid null. | Report a missing marker byte as an error. Validate marker values according to the encoder contract; treat a valid null separately. |

Additionally end LIST/MAP recursion (`:377–424`) when an error is already set, so that damaged counts do not cause further decode allocations needlessly. Keep the release invariants: pointers null-initialised, MAP occupied only set for releasable slots, sub-objects completely cleanable with `aiml_ser_free_decoded`. Do not just abort loops and leave cleanup unchecked.

**Acceptance:** for every type named, at least one valid boundary case and one truncated/invalid case; detect errors even when the affected field is the last field of the object. Be able to free already decoded nested fields after an error without a leak/double free.

## 4. P0.3 — do not publish faulty decode results

**Locations:** `src/sys/db_runtime.cpp:1576,1597` (`aiml_db_kv_get`), `:1637,1651` (`aiml_db_kv_get_copy`), `:2520` (index build). Reference for existing error handling: `:1698–1708` (`aiml_db_kv_get_copy_at`).

**Finding:** the five calls named ignore the decode status. Partially/invalidly decoded values can get into the cache and be passed on as a valid pointer or index source.

**Proposed change:**

- Decode each time into a null-initialised temporary slot. Only take it into the cache on success and update the LRU.
- On error deep-free the partial heap; leave no seemingly valid cache entry behind. `get`/`get_copy` return `nullptr`. Check AIML callers/codegen for this error representation; do not cover it up with a pointer to an empty object.
- Index build: do not pass a faulty record to `extract_and_add`; cleanup and a traceable error diagnostic. Whether the whole build should abort or may deliver a documented partial index is to be decided by the existing index contract.
- `get_copy_at` keeps `1 / 0 / -1` and the existing cleanup/nulling logic, but gets the length-checked entry point from P0.1.
- Separate follow-up audit: `aiml_ser_deep_copy` (`ser_runtime.cpp:702–717`) also ignores the status and returns `void`. For an error-proof deep copy/replace per chapter 61 a checked success contract is needed; do not mix a broad ownership switch into this decoder fix.

**Acceptance:** faulty data is neither cached nor indexed. After an error a subsequent valid query works. Repeated errors do not produce proportionally growing live heap.

## 5. P0.4 — demonstrate specifically that error paths work

**Existing location:** `tests/test_kv_get_copy_at.aiml:65–68,81–104`.

**Proposed new files:** `tests/test_ser_decode_checked.cpp`, `tests/test_kv_get_copy_at_errors.cpp` (native probes for targeted blob manipulation); a shared runner can come about in P1.2. Do not add a production API for injecting broken blobs just for tests.

1. In `probe_reuse` check the so far unused `okr`: any value other than `1` must make the probe fail unambiguously, e.g. via an additional error counter that `main` evaluates. A negative measurement alone must not serve as an error code, since RSS differences can also be negative regularly.
2. On refill also check `ok2`; currently only the follow-up data is checked.
3. Native decoder cases for P0.1/P0.2, including schema mismatch and an error after an already allocated string/list part.
4. KV integration for persistent and in-memory: success = `1`; missing = `0` and the target unchanged; corrupt/foreign schema = `-1` and a completely cleaned, nulled target; after that a valid refill again = `1`.
5. Also check the error paths via `get`/`get_copy`/index from P0.3. For persistence use only a temporary test WAL.
6. Compare success and error cycles N/2N under leak checking. Keep the existing RSS probe in addition; flat RSS alone is no proof of memory safety.

**Gate:** P0.1–P0.4 must be passed before the server live test. The tests are planned here, not already written or run.

## 6. P1.1 — re-measure the server before big ownership work

**Locations:** `../9rooms_v2/sources/match/match_scheduler.aiml:37–63`, `../9rooms_v2/sources/match/match_service_base.aiml:855`; the existing main plan `memory_lifetime_fix_plan_2026-09-07.md:193–196`.

The scheduler already checks `tok == 1` and marks the key as to be skipped on `-1`. Do not re-implement this existing mitigation.

After the decode gate is passed, measure for at least about 50 minutes with a documented, comparable match/connection profile. Record the RSS curve, live heap where available, match/player/connection counts, decode errors and actually successful ticks. Undecodable records must neither be ticked nor written back. Flat RSS with game logic that has failed does not count as success.

Before the run, record the warm-up and the evaluation criterion for residual growth. Confirmed stability assesses the effectiveness of the scheduler mitigation; it does not replace the safety acceptance of a later implementation of chapter 61. WP-C stays discarded. File the result with the binary/source state and measurement data as a new report under 9rooms `doc/analysis/memory_checks/`.

## 7. P1.2 — demonstrate sanitizer coverage of the generated code

**Locations:** `compiler.cpp:1655–1699` (`compileToObject`, so far pure code output without a recognisable ASan instrumentation pass); `main.cpp:165–193` (existing `.ll` export including imports and an optional entry point). Build flags alone do not automatically instrument AIML-generated LLVM loads/stores.

**Minimal proposal:** a new runner `tests/run_memory_sanitizers.sh` for native AOT probes:

1. Use the existing LLVM IR export including imported modules and the required entry point.
2. Explicitly ASan-instrument all generated modules with a toolchain matching the project's LLVM version. Determine pass/function attributes based on the LLVM version actually installed; `-fsanitize` at bare link time is not enough.
3. Also build the C++ runtime with ASan, debug symbols and frame pointers and link the matching sanitizer runtime; enable LSan. Additionally check decoder arithmetic under UBSan.
4. Prove the instrumentation by IR/object inspection AND an isolated, deliberately faulty control probe: a direct memory access from generated AIML code must trigger an ASan error. The control probe runs separately with an expected error status.
5. After that, let the real probes pass without sanitizer findings and with traceable N/2N live heap results.

For now no mandatory rework of the productive JIT pipeline. The AOT check run only proves the instrumented native path; do not claim identical coverage for divergent JIT/WASM paths. If a compiler switch is needed later, plan instrumentation before `addPassesToEmitFile` and take the sanitizer mode into account in the import cache key.

## 8. P2 — small documentation corrections

- `spec/31_db_integration.md:826–829`: after decode success `1`, on error free the partial heap + null the target + `-1`; missing unchanged `0`.
- `src/sys/ser_runtime.h:81–83`: state the return contract correctly: `0` success, `-1` structural error, `-2` schema mismatch. The error target is not a valid value; name the cleanup responsibility.
- Optionally the adjacent comments in 9rooms (`match_scheduler.aiml:46–47`, `match_service_base.aiml:852`): replace "no per-tick allocation" by "reused shell, nested fields are freed and reallocated".

## 9. Order and limits

P0.1 → P0.2 → P0.3 → P0.4 with a native sanitizer check → server re-measurement. P2 can run along in the respective change package. Complete AIML instrumentation from P1.2 is binding at the latest before acceptance of the ownership changes; it should not turn the small runtime fix into a big compiler rework.

The new decoder/caller findings are derived from the current source. Their concrete effect on the observed server OOM is therefore not proven. No additional language semantics change is part of this remaining work plan.

## 10. Open specification questions in chapter 61

Source state of the chapter review: 2026-09-08, chapter with 144 lines. These are design gaps, not demonstrated implementation errors.

1. **`consumed` without hand-over** (`:33,73–88,121–123`): the table promises no drop, non-consumption/inconsistent consumption are only warnings. So no safe cleanup is defined for a path that keeps compiling. Proposal: drop never-consumed values like `scope` despite the warning; treat divergent ownership at the merge as an error for `consumed` too, as long as no explicit static normalisation is defined. That keeps the no-runtime-flags rule.
2. **`persistent` needs a concrete owner** (`:25,32,61–62`): a local persistent object suppresses the drop without stating how it gets into the longer-lived owner. A copy to another owner does not dispose of the original. Proposal: only allow persistent with a provably bound owner store, or define an explicit release contract; reject a free local declaration without an owner/release.
3. **Copy/move is contradictory** (`:65–66,104–115`): assignment is a deep copy in one place and optionally a move in another; the definition of "consumable" is missing. In addition the copy exemption for persistent (`:61`) against "owned box storing always move" (`:112`). Proposal: one binding table for target kind, source ownership and operation with an unambiguous precedence; add examples for local assignment, member set and persistent box. Do not presuppose implicit last-use behaviour.
4. **Borrow safety within the same scope** (`:43–46,100,124`): an escape ban alone does not prevent an invalid view after `clear`, `remove`, reallocation, replacement or release of the owner. Proposal: define local invalidation/mutation rules as well as returning and passing on to functions explicitly. No whole-program escape analysis is needed for that.
5. **Function boundaries are missing** (`:43,55–56,133–135`): consuming parameters are mentioned, but their declaration and their relation to by-ref lists/maps and by-value models are not defined. Proposal: define a signature contract for borrow/consume/copy and returns; in particular by-ref parameters must not be dropped at the callee's end by the general rule "value type = owned".

Sanitizer coverage stays relevant for this new approach too; the new lifetime plan demands it itself in §8. No return to the discarded WP-C follows from these questions.
