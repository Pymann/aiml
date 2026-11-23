# DELTA, replica ownership and WS9: concrete implementation plan

As of: 2026-10-03. Status: **draft for implementation, not an implemented fix**.
Supplements section 11 of `rpc_implementation_fix_plan_2026-10-02.md`.

The snippets are concrete change proposals. Newly introduced functions are
marked as such; they are not to be understood as already available APIs.
Line numbers are orientation, function names the stable search anchors.

## 1. Defined behaviour

1. DELTA is decoded completely, bounded and transactionally. No partial
   takeover on errors; a schema deviation is not a successful snapshot.
2. Mirror **borrows** the object initialised by the AIML caller. Registration
   and session end do not destroy its fields. The object must live until
   unregister/close. The normal AIML lifetime path stays the owner.
3. Readiness is indicated by a successfully taken-over snapshot,
   not by REGISTER, a pointer assignment or the number of `update` calls.
4. The WS9 test waits for a snapshot + expected values and only ends the host
   after an explicit receipt confirmation from the client.
5. Model version migration stays outside this fix. Identical wire schemas
   from different compiler contexts are supported; different schemas
   are rejected.

## 2. Change matrix

| File / anchor in the current state | Change |
| --- | --- |
| `src/sys/ser_runtime.cpp`: `aiml_ser_encode`, `aiml_ser_rpc_fp`, `aiml_ser_decode_sized_tx` (~1539) | explicit wire encode API and a mandatory envelope/schema check for DELTA |
| `src/sys/ser_runtime.h:92` | new declarations and an unambiguous ownership contract |
| `src/sys/mirror_runtime.cpp`: `AimlMirrorRegistered` (~163) | snapshot counter and error status instead of `has_decoded` as an ownership switch |
| `mirror_packet_length` (~440) | recognise the marked DELTA format, reject unknown flags |
| `send_delta_to_peer` (~520), `send_delta_pkt` (~545), `session_update` (~1126), `register_id_typed` (~1312) | uniform encoding including dirty tracking |
| `process_packet`, DELTA (~590), pending blob apply (~1287) | a shared transactional apply helper |
| `PKT_UNREGISTER` (~674), `session_close` (~1057), `session_unregister` (~1234) | only free registration resources, remove all lookup entries |
| `src/core/builtin_registry.cpp`: `MIRROR_FIND_OBJ_PTR`, `register`, `register_id` (~9920–10065) | bind the real long-lived object; register all serialisable models typed; status builtins |
| `src/core/ast_compiler.cpp`: `initClassContainerMembers` (~131), global initialisation (~1595/~1783) | investigate the initial state and correct it specifically at the proven cause |
| `src/core/ast_compiler_stmt.cpp`: model initialisation (~1711), defaults (~2078), ScopeDrop | initialise validly exactly once and release correctly later |
| `tests/test_mirror_ws_9rooms_imports.aiml` | state machine, ACK, no unprotected host global reads |
| `tests/ws_context_*`, `test_mirror_ws_shared_module_context_mismatch` | separate the positive context regression from the negative schema regression |

## 3. Isolate the initial state first (commit 1)

The original initialisation cause is not yet proven. Do not build in a blanket
`memset` or a renewed initialisation in `register_id`: both can
overwrite live strings/containers and hide leaks.

### 3.1 Minimal test before any mirror operation

New `tests/test_mirror_replica_defaults.aiml` with the same model as the
problem case: a string without a default, a string with a default, a list, a map, a nested
model. Before the first network call, check `.len`/defaults, then serialise
and leave the scope. Variants: local variable, imported global,
`ptr<Model>`, enabled refcount representation. Use the existing refcount switches;
do not introduce a new model representation for mirror.

Check the generated LLVM IR for this order:

```cpp
// Target order in the existing value model construction path.
// Only on FIRST CONSTRUCTION, not on register_id or on every DELTA.
ctx.builder.CreateStore(llvm::Constant::getNullValue(modelTy), objectAddr);
initClassContainerMembers(objectAddr, modelName);
// Apply explicit defaults only once; respect existing string decrefs.
// Afterwards a ScopeDrop for the same address and the same descriptor.
```

`initClassContainerMembers` already initialises strings and containers. Therefore
first check whether the call is missing, is overwritten by a later store
or points to the wrong address. For globals, check that the initializer
runs before a host thread starts. For `ptr<>`/refcount models, mirror must
receive the object address, not the address of the pointer slot.

### 3.2 Exclude temporary receivers

`MIRROR_FIND_OBJ_PTR` currently creates a temporary alloca for some expressions.
A long-lived registration must not keep this temporary object.
Replace the macro with a shared helper for both builtins:

```cpp
// New compiler helper, schematic signature (reuse symbol lookup).
struct MirrorObjectBinding {
    llvm::Value* address;
    std::string modelType;
    uint64_t size;
};
// resolveMirrorObjectBinding(ctx, expr):
// - only a provably addressable object that lives until unregister/close;
// - normalise the symbol type (ptr/ref/refcount);
// - load exactly once for a pointer slot;
// - size from the DataLayout and the concrete model type;
// - reject temporary model values, never silently register reg_tmp.
```

For initialised value models: registration takes no additional
ownership of heap fields. For explicit pointer/borrow variants,
extend the lifetime analysis so that an object does not end before the registration.
At least bound complex, unprovable lifetimes with a clear diagnostic.

**Gate:** the default/teardown test under ASan or the existing memory sanitizer
must pass before `release_old = 1` becomes binding in commit 3.

## 4. A marked, schema-checked DELTA format (commit 2)

### 4.1 Wire format and compatibility

For `PKT_DELTA`, `reserved = 2` is defined:

```text
[net_id:u32 LE][type=0x01:u8][format=2:u8]
[payload_len:u32 LE][wire_magic:u32 LE][schema_fp:u32 LE][encoded fields...]
```

Reuse of the existing structural RPC envelope
(`0xA1F5C0DF`, fingerprint, payload), explicitly documented for DELTA too.
No change to persisted KV blobs. The marker distinguishes new DELTAs from
old untagged `reserved=1` packets.

This is a **coordinated client/server upgrade**. Old binaries are not
compatible and can misinterpret marker 2 as raw data. Deployment therefore
only with jointly updated peers. Mixed operation is not part of this
fix; if needed, a separate DELTA capability must be negotiated
beforehand. RPC version 3 alone does not prove support for DELTA format 2.
No automatic best-effort fallback.

For models on the new typed path, `reserved=0/1` are rejected.
Raw DELTA remains at most for the explicit legacy C API; it is not documented as
schema-checked or platform-independent.

### 4.2 Serializer APIs

New declarations behind `AimlTypeNode` in `ser_runtime.h`:

```cpp
char* aiml_ser_encode_wire(const void* obj,
                          const AimlTypeNode* nodes, int32_t n_nodes);
int aiml_ser_decode_delta_tx(const char* blob, size_t blob_size,
                            void* out, size_t out_size,
                            const AimlTypeNode* nodes, int32_t n_nodes);
```

Implementation in `ser_runtime.cpp`; uses the existing envelope mode and
restores it even on C++ exceptions:

```cpp
extern "C" char* aiml_ser_encode_wire(const void* obj,
                                     const AimlTypeNode* nodes, int32_t n) {
    struct RestoreMode {
        int previous = aiml_ser_set_rpc(1);
        ~RestoreMode() { aiml_ser_set_rpc(previous); }
    } restore;
    return aiml_ser_encode(obj, nodes, n);
}

// Private helper function in ser_runtime.cpp.
static uint32_t wire_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

extern "C" int aiml_ser_decode_delta_tx(
    const char* blob, size_t len, void* out, size_t out_size,
    const AimlTypeNode* nodes, int32_t n) {
    if (!blob || !out || !nodes || n <= 0 || out_size == 0 || len < 12)
        return -1;
    const auto* b = reinterpret_cast<const uint8_t*>(blob);
    if (size_t(wire_u32(b)) != len - 4) return -1;
    if (wire_u32(b + 4) != AIML_RPC_MAGIC) return -2;
    if (wire_u32(b + 8) != aiml_ser_rpc_fp(nodes, n)) return -2;
    // Precondition: out is a completely initialised, owned model.
    return aiml_ser_decode_sized_tx(blob, len, out, out_size, nodes, n, 1);
}
```

`aiml_ser_decode_sized_tx` already provides complete consumption, budget/depth
limits and a temporary decode. The additional mandatory magic check here
is necessary because the existing general decoder accepts legacy blobs.

**Check the schema limits explicitly:** the current fingerprint describes
serializer kinds, not field names or all semantic types (`i32`/`f32`
share a kind). It is not an authentication mechanism. For this fix,
wire schema compatibility is guaranteed, not arbitrary semantic
model equality. Recursive descriptors (`PTR_REF`) must not simply be compared
by local type ID: resolve them canonically or, for sync, reject them for now
with a diagnostic. The same applies to non-serialisable resources.

### 4.3 Switch all sender/comparison paths together

In `send_delta_to_peer`, `send_delta_pkt`, the dirty tracking of `session_update`
and the initialisation of `shadow_blob` in `register_id_typed`:

```cpp
char* blob = aiml_ser_encode_wire(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
if (!blob) return; // In the update loop: continue.
const size_t len = size_t(read_u32_le(
    reinterpret_cast<const uint8_t*>(blob))) + 4;
// Check len against the same frame upper limit as on receipt.
std::vector<uint8_t> pkt(6 + len);
write_u32_le(pkt.data(), uint32_t(reg->net_id));
pkt[4] = PKT_DELTA;
pkt[5] = 2;
memcpy(pkt.data() + 6, blob, len);
// Send + take over the existing shadow ownership or free blob.
```

If possible, introduce a single `encode_delta(reg)`/`make_delta_packet` for all
four places. Dirty tracking must compare exactly the same blob
that is sent later; otherwise the new header alone produces permanent synchronisation.

Compiler builtins `register` and `register_id`: replace the condition
`hasHeapFields && serNodesVar` with `serialisable model && serNodesVar`.
Pure number models also get format 2 and the schema check.
Diagnose non-serialisable models, do not fall back to a raw memcpy.

### 4.4 Parser

In `mirror_packet_length`, `case PKT_DELTA`, use explicit flags:

```cpp
if (d[5] == 2) {
    if (avail < 10) return more;
    const uint32_t payload = read_u32_le(d + 6);
    if (payload < 8 || payload > kMaxRpcFrame) return PKT_INVALID;
    t = 10 + size_t(payload);
    // The existing shared tail checks the complete packet and WS remaining bytes.
} else if (d[5] == 0) {
    // Explicit previous raw legacy branch, not as a fallback for typed.
} else {
    return PKT_INVALID; // including untagged format 1 in the new protocol
}
```

Also reject raw→typed in `process_packet`, so that a manipulated marker
cannot overwrite heap pointers through `memcpy`. Likewise reject typed→untyped.
Pending data additionally stores the format; marker 0 must not accidentally become the initial state
for a replica that is bound typed later.

## 5. A shared apply path and unambiguous ownership (commit 3)

### 5.1 Registration state

Extend `AimlMirrorRegistered`, remove `has_decoded` as an ownership indicator:

```cpp
uint64_t snapshot_revision = 0; // local number of successfully taken-over DELTAs
int32_t last_decode_error = 0;  // 0=no error, -1=decode, -2=schema/format
uint8_t pending_format = 0;
```

A decode error after a valid snapshot leaves revision and model
unchanged. `last_decode_error` is set; it is cleared on the next success.
The revision is local and not a wire version counter; handle overflow in a controlled way.

### 5.2 Apply helper in mirror_runtime.cpp, before process_packet

```cpp
static int apply_typed_delta(AimlMirrorRegistered& reg,
                             const uint8_t* blob, size_t len) {
    if (!reg.obj_ptr || reg.obj_size <= 0 || !reg.ser_nodes || reg.ser_n <= 0)
        return reg.last_decode_error = -1;
    if (reg.snapshot_revision == UINT64_MAX)
        return reg.last_decode_error = -1;

    const int rc = aiml_ser_decode_delta_tx(
        reinterpret_cast<const char*>(blob), len,
        reg.obj_ptr, size_t(reg.obj_size), reg.ser_nodes, reg.ser_n);
    reg.last_decode_error = rc;
    if (rc != 0) return rc;

    ++reg.snapshot_revision;
    if (reg.shadow)
        memcpy(reg.shadow, reg.obj_ptr, size_t(reg.obj_size));
    return 0;
}
```

In the live DELTA branch, completely replace the previous free/memset/decode:

```cpp
if (reg->ser_nodes) {
    if (reserved != 2) {
        reg->last_decode_error = -2;
        return; // no raw fallback and no forwarding of this error
    }
    const int rc = apply_typed_delta(*reg, data + 6, size_t(data_len - 6));
    if (rc != 0) {
        fprintf(stderr, "[mirror] DELTA rejected net_id=%d rc=%d\n", net_id, rc);
        return;
    }
} else {
    // Only actually untyped objects may reach the raw branch.
}
```

The host relay must not forward a locally discarded snapshot anyway.
For host placeholders without an own schema, provide only bounded forwarding;
the final schema check happens at the typed-bound
receiver. Do not claim that the host has already validated such payloads.

### 5.3 Pending snapshot at binding

After `obj_ptr`, `obj_size`, `ser_nodes`, `ser_n` and the new shadow:

```cpp
// Set s->by_ptr before the apply/status query.
s->by_ptr[obj] = existing;
if (existing->pending_blob) {
    const int rc = existing->pending_format == 2
        ? apply_typed_delta(*existing, existing->pending_blob,
                            size_t(existing->pending_blob_len))
        : (existing->last_decode_error = -2);
    if (rc != 0)
        fprintf(stderr, "[mirror] initial DELTA rejected net_id=%d rc=%d\n", id, rc);
    free(existing->pending_blob);
    existing->pending_blob = nullptr;
    existing->pending_blob_len = 0;
    existing->pending_format = 0;
}
```

Do not set a success flag if the apply fails. Existing defaults stay
readable. On an allocation failure for the pending buffer, keep the old pointer/length
consistent; do not leave an old length standing after a failed `malloc`.

### 5.4 Shared release of a registration

A new helper, called from local unregister, remote UNREGISTER and close:

```cpp
static void erase_registration(AimlMirrorSession& s, AimlMirrorRegistered* reg) {
    if (!reg) return;
    if (reg->obj_ptr) {
        rpc_obj_forget(reg->obj_ptr, &s);
        auto it = s.by_ptr.find(reg->obj_ptr);
        if (it != s.by_ptr.end() && it->second == reg) s.by_ptr.erase(it);
    }
    s.by_net_id.erase(reg->net_id);
    free(reg->shadow);
    free(reg->shadow_blob);
    free(reg->pending_blob);
    // obj_ptr and its fields belong to the AIML caller.
    delete reg;
}

// session_close: no iterator invalidation in the existing range-for.
while (!s->by_net_id.empty())
    erase_registration(*s, s->by_net_id.begin()->second);
```

Send `send_unregister_pkt` before the local removal; do not broadcast a remote UNREGISTER
again as a new local unregister. The previous remote path
must in particular remove the stale `by_ptr` entry and the RPC object lookup.

Tests must prove that model fields are still readable after close/unregister
and are released exactly once at the end of the scope. That distinguishes valid
borrow semantics from merely suppressing a double free.

## 6. Observe readiness and errors from AIML (commit 4)

New runtime APIs in `mirror_runtime.h/.cpp`:

```cpp
uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t net_id);
int32_t aiml_mirror_session_snapshot_error(void* session, int32_t net_id);

uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t id) {
    auto* s = static_cast<AimlMirrorSession*>(session);
    if (!s) return 0;
    const auto it = s->by_net_id.find(id);
    return it == s->by_net_id.end() ? 0 : it->second->snapshot_revision;
}

int32_t aiml_mirror_session_snapshot_error(void* session, int32_t id) {
    auto* s = static_cast<AimlMirrorSession*>(session);
    if (!s) return -3; // invalid session/registration
    const auto it = s->by_net_id.find(id);
    return it == s->by_net_id.end() ? -3 : it->second->last_decode_error;
}
```

Revision 0 means no successfully applied remote snapshot yet, also for
local owners; it is not an error. The combination with `snapshot_error` distinguishes
waiting from rejection. Access only from the session thread; do not promise implicit
thread safety.

In `builtin_registry.cpp`, at `mirror_session` next to `has_event`:

```cpp
ci.builtinMethods["snapshot_revision"] = [i8p, i32, i64](CompilerContext& ctx,
    llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
    // Perform the arity/i32 check via the existing compiler diagnostics.
    if (call->args.size() != 1) {
        LOG_ERROR(ctx.fmtLoc() + "snapshot_revision expects net_id: i32");
        return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
    }
    auto* s = ctx.builder.CreateLoad(i8p, base);
    auto* id = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
        ctx.getFunction("aiml_mirror_session_snapshot_revision", i64, {i8p, i32}),
        {s, id}));
};
// snapshot_error analogously, return type i32; validate the argument type before CreateCall.
```

Carry the AIML return type of `snapshot_revision` as `u64` in type inference/metadata;
LLVM uses `i64` for it. If the existing builtin system requires additional
return metadata, enter it there too. No new event ABI needed.

## 7. Rebuild the WS9 test into a handshake (commit 5)

### 7.1 Host: REGISTER only binds, DELTA makes ready

In `test_mirror_ws_9rooms_imports.aiml`, keep `client_info_nid` and `bound_ci` separately.
In the previous `object_registered` branch, only store the ID and bind:

```yaml
- set: [client_info_nid, cev.net_id]
- call: [sess.register_id, g_ws9_server_client_info, client_info_nid]
- set: [bound_ci, 1]
# No sleep, no got_ci=1 at this point.
```

After every `sess.update` in the wait loop:

```yaml
- if:
    condition: { eq: [bound_ci, 1] }
    then:
      - decl: [snapshot_rc, i32, { call: [sess.snapshot_error, client_info_nid] }]
      - if:
          condition: { neq: [snapshot_rc, 0] }
          then:
            - print: ["FAIL ws9-host: ClientInfo decode rc=${snapshot_rc}"]
            - call: [sys.exit, 1]
      - if:
          condition: { gt: [{ call: [sess.snapshot_revision, client_info_nid] }, 0] }
          then:
            # Check all three ClientInfo fields; error on deviation.
            - set: [got_ci, 1]
```

Only after `got_ci` fill the view with `round=7`, names, HP and hand size. A timeout
ends with an error instead of continuing with an empty name.

### 7.2 Client: check and confirm the complete view

The client waits for a positive view revision AND all expected view fields.
Only then send `client.send_rpc(0, "ws9_view_received", 7)`. The host pumps
until this ACK and checks the method, `arg` and the expected `peer_id` coming from
the connection. The ACK only serves this one-way test handshake,
not as a substitute for the existing request ID correlation of general RPCs.

Then the host does `sess.close`; the client waits for `host_th.join`, keeps checking its own
models and closes its session. Check host fields exclusively in the
host thread. Remove the previous client reads of
`g_ws9_server_client_info` and `ws9server.client_info_seen`. Host errors
end the test or are reported via a synchronised channel; do not introduce an
unprotected global success variable.

An existing monotonic timer or a small shared test helper
provides deadlines for connect, REGISTER, snapshot and ACK. If an AIML builtin
is missing for this, add it before the test rebuild (C++ `steady_clock` in milliseconds).
Sleep stays at most the poll pacing. Every timeout message names the phase,
net ID, revision and decode error.

## 8. Targeted regressions instead of mere repetition (commit 6)

Integrate a new C++ suite into the existing runtime test infrastructure; if no
suitable target exists, create a small target of its own with production objects.
No permanent `#include "mirror_runtime.cpp"` test solution. If needed, move private
apply/parser logic into an internal module.

### 8.1 Transactionality (C++ test sketch)

```cpp
// The test fixture initialises OldState with managed strings/containers.
// encodeWire(model) calls the new aiml_ser_encode_wire API.
const auto before = encodeWire(target);
const auto revision = registration.snapshot_revision;
auto bad = encodeWire(source);
bad.pop_back(); // the inner length prefix no longer matches
EXPECT_NE(applyDelta(registration, bad), 0);
EXPECT_EQ(encodeWire(target), before);
EXPECT_EQ(registration.snapshot_revision, revision);
EXPECT_NE(registration.last_decode_error, 0);

EXPECT_EQ(applyDelta(registration, encodeWire(source)), 0);
EXPECT_EQ(registration.snapshot_revision, revision + 1);
EXPECT_EQ(registration.last_decode_error, 0);
```

`applyDelta` here is a fixture adapter onto the production apply path; the
test names/macros are to be adapted to the actually existing framework.
Additionally test a damaged payload with a formally correct outer length and an error
after the first string allocation: that covers partial heap clean-up.

### 8.2 Mandatory cases

| Case | Expectation |
| --- | --- |
| first DELTA defective | defaults unchanged, revision 0, error visible |
| valid → defective → valid | state/revision only change on success |
| DELTA before the REGISTER binding | pending and live apply have identical semantics |
| same wire schema, different import sets | successful sync |
| `SharedCard` with/without `rarity`, at least 2 elements | rejection in both directions, no partial takeover |
| raw or untagged DELTA to a typed model | rejection without heap pointer mutation |
| close/unregister before/after the first snapshot | object stays readable; ScopeDrop without leak/double free |
| remote UNREGISTER | no dangling `by_ptr`/RPC lookup entries |
| unchanged owner model after the initial sync | no repeated send merely because of a header difference |
| WS9: DELTA 100–250 ms after REGISTER | the test waits correctly and passes |
| WS9: snapshot missing/defective or ACK missing | bounded, phase-exact failure |
| native↔wasm32, identical schema | the same fingerprints despite pointer/offset differences |

For a deterministic delay, use a test proxy or a send hook enabled only in the
test build. It holds back DELTA frames,
lets REGISTER through and only releases DELTA after a test signal. Do not add
production sleeps. Also force the pending case: deliver DELTA completely before the local
binding.

Then run WS9 50–100 times serially, with a clean host shutdown per run;
optionally repeat under CPU load. The fixed test port forbids parallel runs
against the same port. Initial finding: previously 16/16 green, the reported sporadic
failure not yet reproduced dynamically.

## 9. Completion and order

1. Fix the default/lifetime reproduction and the proven initialisation cause.
2. Change the wire envelope, parser and all sender/dirty tracking places together.
3. Transactional apply and borrow ownership including close/unregister.
4. Snapshot revision/error builtins and their compiler typing.
5. Switch the mismatch test to rejection, add a positive context test,
   implement the WS9 handshake.
6. Run sanitizer, native, deterministic delay and WASM tests;
   then update the spec and implementation status.

Every intermediate step must compile; until sender and receiver are switched
together, do not mark the new format path as shippable.
Done only when initialisation without a network is correct, faulty packets leave the
state unchanged, session end does not destroy borrowed objects and the
delayed WS9 test waits for the actual state takeover.

## 10. Implementation status

### Commit 1 – initial state finding (2026-10-03)

Test: `tests/test_mirror_replica_defaults.aiml` (+ `mirror_replica_defaults_lib.aiml`). A model with
a string without/with a default, `list<string>`, `map<string:i32>`, a nested model, `list<Model>`,
a scalar default; variants local, imported global, main file global, `ptr<>`, `shared<>`.

- Defaults correct before any network call, JSON identical across the variants.
- `decode_rpc` (transactional, `release_old = 1`) into every default variant: correct.
- Replica binding of all variants, snapshot taken over.
- Experiment: switched the DELTA live and pending paths to `decode_sized_tx(…, release_old = 1)` →
  the complete mirror test series without a crash (failures only: close readability in the new test, the mismatch test
  rejects correctly); AOT+Valgrind for the new test, WS9 imports, WS9 GameView, list_mutation:
  0 invalid, 0 definitely/indirectly lost.

**Result: not reproduced** (not "fixed"). The previously observed crash occurred before the
`MIRROR_FIND_OBJ_PTR` fix (ptr slot instead of object address, AP6); a connection is plausible,
but not proven. What remains open is the ownership at close (fields empty afterwards) → commit 3.

### Commits 2–6 – implemented (2026-10-03)

- **Spec** 44_5 §4 (objects, borrow ownership, readiness, `snapshot_revision`/`snapshot_error`),
  §10 (DELTA format 2, rules, no mixed operation, optional production test hook
  `AIML_MIRROR_TEST_HOLD_DELTA`), §11.1 (dirty tracking compares the sent blob);
  spec/23 `time.mono_ms`.
- **Serializer**: `aiml_ser_encode_wire`, `aiml_ser_decode_delta_tx` (magic + fingerprint
  mandatory, then `decode_sized_tx(…, release_old = 1)`), `aiml_ser_wire_syncable`;
  `aiml_ser_decode_bounded` (best effort) removed.
- **Mirror**: `has_decoded` → `snapshot_revision`/`last_decode_error`/`pending_format`;
  `build_delta`/`make_delta_packet` for all send paths, dirty tracking on the same blob;
  parser: format 2 with `payload_len`, format 0 only raw, everything else (incl. the old format 1)
  an invalid frame; `apply_typed_delta` for the live and pending path; raw↔typed rejected;
  the host only forwards what was accepted or placeholders; `erase_registration` for unregister,
  remote UNREGISTER and close (no more field release); descriptors with PTR_REF/resources
  are not registered, with a diagnostic.
- **Compiler**: `register`/`register_id` typed for every model with a descriptor (also pure
  number models), otherwise a compile error; temporary value → compile error (neg n27);
  builtins `snapshot_revision` (i64/u64) and `snapshot_error` (i32).
- **Tests**: `test_mirror_replica_defaults`, `test_mirror_delta_reject`,
  `test_mirror_number_model`, WS9 as a handshake (+ `_delayed` with the hold hook),
  `shared_module_context_mismatch` (rejection in both directions, 2 cards) +
  `shared_module_context_same`, the format 1 case in `test_mirror_rpc_malformed`,
  C++ unit `tests/unit/test_mirror_delta.cpp` (binary `out-test/aiml-tests/aiml-tests`).
- **Results**: mirror series 34/34; AOT+Valgrind (8 tests + WS9 in hold mode) 0/0;
  unit test incl. Valgrind 0/0; WS9 100× serially (alternating normal/hold mode) 100/100;
  native↔WASM (`tools/run_wasm_mirror_test.sh`, `…_multifile_test.sh`) PASS.
- **Side finding (regression from cdcba60)**: TCP receive buffer `uint8_t chunk[65536]` on the
  stack → stack overflow in the WASM `session.update` (64 KiB stack). Fixed: directly into `peer->rx`.
  The WASM scripts now only start the server after the client build (previously a timing error);
  the multifile client spec-conformantly uses `ref<Tagged>` instead of `ptr<Tagged>`.
- **Not covered**: the negative WS9 case "snapshot/ACK missing" only via the deadlines in the test,
  no failure test of its own; model version migration deliberately outside.
