# AIML RPC: implementation plan after review

As of: 2026-10-02. Status: planned, not yet implemented.

The six review findings are fixed in seven work packages that build on one another.
Line numbers refer to the state read during planning;
function names are the durable anchors. Changes to `take_rpc` and the structural RPC fingerprint
that exist in parallel are taken into account and must
not be overwritten.

## 1. Binding target semantics

- Network data is decoded exclusively with its actually received length.
  Invalid frames lead to a controlled error, not to
  partial mutation, out-of-bounds accesses or unbounded allocations.
- Low-level client RPCs go exclusively to the server. A server handler
  decides explicitly about responses and broadcasts.
- Low-level host sends keep `target == 0` as an explicit broadcast and
  `target > 0` as a peer ID. An unknown positive peer never becomes a
  broadcast; negative targets are rejected.
- Request/response uses an explicit request ID and message kind.
  Method name and schema are additional checks, not correlation.
- Uninvolved messages keep their order and their content.
- Automatic object RPCs form a separate, registered dispatch path.
  Object ID, peer ID and request ID are separate concepts and fields.
- Supported automatic RPC methods are initially `void` and have
  exclusively serialisable value parameters. Inadmissible signatures get
  compiler errors. Return values stay with the explicit request/response API.

## 2. Work package 0: consolidate build and ABI

**C++ places**

- `src/sys/ser_runtime.h:67`: `aiml_ser_rpc_blob_matches` currently stands before the
  anonymous `typedef ... AimlTypeNode` from line 73. Move the declaration behind the
  type definition. Do not introduce an unsuitable forward declaration of an anonymous
  struct.
- `src/sys/mirror_runtime.h`: add the public declaration for the meanwhile
  implemented `aiml_mirror_session_take_rpc`; update the comments on
  event lifetime, routing and `send_and_await`.
- `src/core/context.h:1243`: the fallback for `rpc_event` creates only three fields;
  `src/core/builtin_registry.cpp:9708` creates seven. Use a shared
  LLVM type creation function so that both paths are identical.
- `src/sys/mirror_runtime.cpp:185`, `AimlRpcEvent`: manage/test the C++ layout and LLVM fields
  together; add the upcoming request ID fields everywhere.

**Acceptance**

A clean native build and compile smoke test for both orders of the
`rpc_event` type initialisation. Check the LLVM DataLayout against C++ `sizeof`/`offsetof`;
check wasm32 separately, do not adopt x86-64 padding constants.

## 3. Work package 1: secure packet and decoder boundaries [P1]

**C++ places**

- `src/sys/mirror_runtime.cpp:413`, `process_packet`, RPC branch from line 524.
- `src/sys/mirror_runtime.cpp:1384`, `aiml_rpc_event_decode_into`.
- `src/sys/mirror_runtime.cpp:1400`, `aiml_mirror_session_take_rpc`.
- `src/sys/ser_runtime.cpp:1443`, `aiml_ser_rpc_blob_matches`, and
  `aiml_ser_decode` directly after it.
- `src/sys/ser_runtime.cpp:493`, the recursive `dec` walker and its read helpers.
- Declarations in `src/sys/ser_runtime.h`.

**Changes**

1. Introduce a shared RPC parser with the results `NeedMore`, `Complete`, `Invalid`.
   It receives `(const uint8_t*, size_t available)` and returns a
   validated packet view as well as the bytes actually consumed.
2. Check lengths with subtraction: first `offset <= available`, then
   `blob_len <= available - offset`. Remove the cast `(int)blob_len` from the previous
   check. In WebSocket messages, require exactly one complete packet;
   unexpected remaining bytes are an error.
3. Introduce a configurable upper limit for RPC frames; initial proposal:
   8 MiB per frame. Check the limit before buffer growth/allocation and document it
   in the spec. Method names must be 1–255 bytes long and free of NUL;
   replace `strlen(method) & 0xFF` in the sender with an explicit check.
4. `arg_kind` exclusively 0 or 1; for 0 the blob length must be 0. For 1,
   require at least the four-byte prefix, whose value must exactly equal the
   remaining blob length. For new typed RPCs, additionally require
   magic and fingerprint. Limit legacy compatibility explicitly.
5. Add new size-aware APIs, for example
   `aiml_ser_decode_sized(blob, blob_size, out, nodes, n_nodes)` and
   `aiml_ser_rpc_blob_matches_sized(...)`. All RPC callers including
   `take_rpc` use these APIs. Do not change existing persistent data formats
   along the way; old size-less APIs may only process internally trusted
   buffers.
6. Switch the decoder read helpers to overflow-safe remaining length checks.
   Bound container counts, `count * element_size`, nesting depth and the total
   decode allocation. A frame limit alone does not prevent a huge
   allocation from a small, manipulated element counter.
7. Decode into a temporary, zero-initialised object. On error, free its
   already created heap fields; leave the existing target unchanged.
   Only after a completely successful check free the old target fields and
   transfer the result. Pass the target size as checked compiler metadata,
   do not derive it unseen from an arbitrary root node.
8. `builtin_registry.cpp`, builtins `decode_rpc`, `take_rpc`, `send_and_await`:
   remove the preceding `CreateMemSet` calls and use the transactional runtime.
   Use a shared pointer extraction for value models, `ptr<>`, `ref<>` and
   enabled refcount models; do not clear a pointer slot as an object.

**Regressions**

Blob lengths 0/1/2/3, a contradictory inner prefix, truncated headers,
`UINT32_MAX`, unknown `arg_kind`, invalid method length, wrong fingerprint,
overflowing container count and deep nesting. A target object with existing
strings/lists must be preserved on error. ASan/UBSan and a leak check of the
partially built temporary object.

## 4. Work package 2: bring TCP framing to the same format [P1]

**C++ places**

- `src/sys/mirror_runtime.cpp:633`, `mirror_recv_from_peers`, especially the RPC branch
  from line 672: currently reads only `method_len + method + i32`.
- `AimlMirrorPeer` from line 149; `tcp_recv_all`/`tcp_send_all` from line 260.
- `mirror_send` from line 299 and `mirror_send_rpc_v2` from line 1307.

**Changes**

1. Add a receive buffer and parse position per peer. Read available bytes
   non-blockingly; consume complete packets via the parser from package 1,
   keep incomplete packets until the next `update`.
2. For v2, consume exactly `6 + 1 + method_len + 1 + 4 + 4 + blob_len` bytes.
   Support several packets in one TCP read and packets fragmented across arbitrary reads.
   Integrate all previous packet kinds into the buffer management, so that mixed
   DELTA/REGISTER/RPC traffic does not desynchronise.
3. Also handle partial sends: a send buffer and offset per peer, continuing
   after `EAGAIN` on the next update. No silent loss after a short `send`.
4. Faulty/overlong frames close only the affected peer
   and produce a diagnosable disconnect. Free the buffers on close.

**Regressions**

Legacy i32 and typed RPCs over TCP; send every byte individually; several RPCs
in one write; an RPC between REGISTER and DELTA; disconnect in the middle of a frame;
a slow peer must not block the host update for other peers. Existing
WebSocket tests must cover the same parser path.

## 5. Work package 3: remove unrequested forwarding [P1]

**C++ places**

- `src/sys/mirror_runtime.cpp:558–560`, RPC enqueue and the subsequent broadcast.
- `mirror_send_rpc_v2` and `aiml_mirror_session_send_rpc[_model]`.
- `spec/44_5_mirror.md`, routing/targeted send contract.

**Changes**

1. Remove the blanket `broadcast_pkt` after incoming low-level RPCs.
   A client request is only queued locally at the host.
2. Keep addressing explicit server responses by the `peer_id` determined from the socket.
   Never use `net_id` as a substitute for the sender identity.
   Correct the spec and examples that still describe unknown targets as a
   broadcast or responses via `rpc.net_id`.
3. Only allow automatic routing via the separate, registered object path in
   package 6. No client-selectable relay option for low-level RPCs.

**Regressions**

Host with two clients: A sends a private payload, B receives nothing. Even a
`set_result` sent by A does not reach B. A targeted host response reaches only
A; a host broadcast with target 0 reaches both. An unknown/negative target
sends nothing. After a disconnect, an old peer ID must not address a new peer;
do not reuse IDs within a session.

## 6. Work package 4: stable queue removal [P2]

**C++ places**

- `AimlMirrorSession::rpc_queue` from `mirror_runtime.cpp:205`.
- `aiml_mirror_session_send_and_await`, requeue loop currently around line 1523.
- `aiml_mirror_session_take_rpc`, in particular `kMaxQueued = 64`.
- `next_rpc`, `poll_rpc`, `has_rpc`, session close.

**Changes**

1. Replace `std::queue<AimlRpcEvent*>` with `std::deque` and introduce a shared
   helper `take_matching_rpc(predicate)`. Remove only the hit;
   all other elements stay at the same position.
2. Implement `send_and_await` and `take_rpc` via this helper. No
   repeated pop/push and no appending of skipped events behind the rest.
3. Remove the new blanket deletion of all events beyond position 64 in `take_rpc`:
   it also deletes valid pushes of other consumers. Enforce queue limits on
   receipt per peer by count AND bytes; report overload in a controlled way/close the connection,
   never silently delete foreign events.
4. Unify event ownership: every entry is freed exactly once.
   A borrow from `next_rpc`/`poll_rpc` stays valid until the next removal via the same
   borrow path or close; internal search removal must not free it.

**Regressions**

`A, Result, B` yields `A, B` after removal; hit at the front/back; no hit;
timeout; wrong schema; several identical methods; at least 65 uninvolved
pushes. Check repeated removal and session close under ASan.

## 7. Work package 5: real request/response correlation [P1]

**C++ places**

- `AimlMirrorSession`, `AimlRpcEvent`, `mirror_send_rpc_v2`, `process_packet`.
- `aiml_mirror_session_send_and_await` and a new reply function in
  `src/sys/mirror_runtime.h/.cpp`.
- `builtin_registry.cpp`: `rpc_event`, `send_and_await` from line 10184 and
  a new builtin `reply_rpc`.
- `context.h`: the shared `rpc_event` definition from package 0.

**Protocol decision**

Introduce a new, unambiguously distinguishable `PKT_RPC_V3`; do not reinterpret v2
heuristically. V3 gets a fixed header with version, total length, message kind
(`push`, `request`, `response`, `object_call`), `request_id:u64`, a separate
object ID and routing metadata for object calls. Encode all integers explicitly LE;
never copy C++ structs as a wire format. Fully pin down the method/payload format
and field widths in the spec before implementation.

Add a capability exchange at connection setup. While the capability status
is unknown, do not send new request/response packets. With old peers, only allow
explicitly supported v2 one-way communication; `send_and_await` reports
missing support instead of falling back unsafely to method matching.
This protocol change needs a coordinated update of client/server.

**Changes**

1. Assign monotonic request IDs per connection, reserve 0 for push.
   Store the connection generation and the expected response peer in the local pending entry;
   end pending requests on disconnect. Handle overflow in a controlled way,
   never reassign a still relevant ID.
2. Add `session.reply_rpc(ev, method, arg[, model])`. Take the response target from the
   trusted `ev.peer_id`, the request ID from `ev.request_id`;
   set the response message kind. Migrate server handlers and tests to this API.
   Without the server echoing the ID, the fix is incomplete.
3. `send_and_await` waits for `response + request_id + expected connection`;
   only then check method/schema/payload. A correlated but broken response
   ends the request as a decode/protocol error and is not scanned endlessly.
4. Determine the deadline with a monotonic clock. A count of 2 ms sleeps is not a
   time measurement; update/decode duration must count towards the timeout.
5. Late, duplicate and unknown response IDs are discarded/diagnosed specifically as such,
   without removing pushes. Responses of the same kind already waiting before the send
   must never fulfil the new request.
6. Also provide the request ID API for asynchronous consumers, e.g.
   `send_request(...) -> u64` and `take_response(id, target)`. `take_rpc(method,
   target)` explicitly remains a push/legacy queue filter and does not claim
   safe request correlation.

**Regressions**

Request A times out, request B has the same response schema, response A arrives before B:
only B fulfils B. Two pending requests with swapped response order;
a response from the wrong peer; a duplicate response; a new connection with old events;
timeout 0; a type-identical push message `set_result`; v2/v3 incompatibility with a
clear error. Native↔WASM must use the same wire IDs and header sizes.

## 8. Work package 6: really implement automatic object RPCs [P2]

**C++ places**

- `src/core/ast/model_builder.cpp`, reading in `rpc`.
- `src/core/ast_compiler.cpp:4355`, taking over `rpcMethods` and model codegen.
- `src/core/context.h:206`, class metadata for `rpcMethods`.
- `src/core/ast_compiler_call.cpp:891`, `visitObjectMethodCall`, including
  interface dispatch from line 944 and direct method resolution around line 3458.
- `builtin_registry.cpp`: `register`/`register_id` around lines 9890/9925 as well as
  `set_rpc_target` around line 10371.
- `mirror_runtime.cpp`: `AimlMirrorRegistered`, register/unregister from line
  1002, `set_rpc_target` from line 1258 and session update.

**Compiler changes**

1. Validate `rpc:` against the actually resolved methods: names,
   inheritance, signatures, `void`, serialisable parameters. Generate a
   synthetic argument struct plus a serializer description for the parameters; do not write a
   native pointer address onto the network.
2. Generate a public wrapper and a local body/dispatch thunk per RPC.
   The wrapper makes the routing decision once; the thunk
   decodes arguments and calls the local body directly. Receiving must not
   run through the network wrapper again and create relay loops.
3. Route direct and interface/vtable calls to the same wrapper. Evaluate the receiver
   and argument expressions only once. Unregistered objects keep
   normal local method calls; registered objects use RPC routing.
4. Registration passes an RPC descriptor table with method names,
   argument schema and thunk. Unknown methods or wrong schemas must
   never dispatch into arbitrary local methods.

**Runtime changes and ownership**

1. Registered objects get a session assignment, a stable net ID and
   `owner_peer_id`. Object addresses are only local lookup keys. Reject multiple
   registration of the same object in different sessions as ambiguous;
   unregister/close removes the assignment and dispatch metadata.
2. The host assigns/controls global object IDs and confirms them to the
   registering client. Client-side counters must not produce colliding global
   IDs. Register messages transmit authorised ownership;
   owner disconnect invalidates/removes the affected registrations.
3. `set_rpc_target` stores a validated enum per object/method.
   Unknown methods/targets or unregistered objects report an error.
4. Implement the targets exactly:
   - `owner`: only the owner; local if the caller is the owner.
   - `server`: only the host; local on the host.
   - `all`: every participating peer including the sender, exactly once.
   - `others`: all participating peers except the sender, including the host, unless
     it is the sender.
5. Clients relay remote targets via the host. Only an `object_call` with a valid
   object/method registration may be routed. The host checks the released
   target policy; mere client statements are no relay permission. The sender ID is
   obtained exclusively from the connection and only passed on by the host.
   Clients cannot extend method/target releases set by the server through
   their local `set_rpc_target`.
6. On receipt, only the separate object path allows automatic execution;
   low-level RPCs stay in the queue for manual handlers. A packet is never
   executed automatically AND then delivered again manually.
7. Plan local execution and remote forwarding for `all`/`others` together,
   so that the host does not deliver to the sender again. Provide defined protection against
   re-executing the same forwarded object call.

**Regressions**

Host plus two clients; call all four targets from host and client respectively;
check the exact execution count and sender per target. Unregistered object,
unknown method, invalid target, missing release, colliding registration,
owner disconnect, unregister, inherited RPC method, interface call and
a side effect in an argument. Negative compiler cases for non-serialisable
parameters and unsupported return values. Native↔WASM with string/list
arguments. The test proves in particular: no re-sending in the dispatch thunk.

## 9. Planned test files and integration order

A new targeted runtime suite, e.g. `tests/runtime/test_mirror_rpc_protocol.cpp`,
with a private testable parser/queue module; do not include productive `.cpp` files
in tests as a permanent solution. Additionally:

| Test file (new) | Checks |
| --- | --- |
| `tests/test_mirror_rpc_tcp.aiml` | i32 and typed TCP RPCs |
| `tests/test_mirror_rpc_no_relay.aiml` | isolation of two clients, explicit targets |
| `tests/test_mirror_rpc_queue_order.aiml` | stable removal, more than 64 pushes |
| `tests/test_mirror_rpc_request_id.aiml` | timeout stragglers, ID/peer correlation |
| `tests/test_mirror_rpc_auto_dispatch.aiml` | normal method calls and the target matrix |
| `tests/test_mirror_rpc_decode_failure.aiml` | target is preserved, memory is freed |
| `tests/wasm/test_mirror_rpc_v3_client.aiml` | wire/ABI parity with the native server |

Keep running the existing tests: `test_mirror_rpc_int_compat`,
`test_mirror_rpc_model_ws`, `test_mirror_rpc_model_map`, `test_mirror_rpc_peer_id`,
`test_mirror_rpc_dispatcher`, `test_mirror_rpc_handler_builtin`,
`test_rpc_typed_lists_inherited`, `test_iface_list_ptr_rpc_binary` and the
existing single/multi-file WASM RPC tests. Run TCP/WS tests with isolated ports
or dynamic port assignment.

Recommended commit order:

1. Build/ABI and the length-aware parser/decoder with sanitizer regressions.
2. TCP buffering and correct v2 framing.
3. Remove the relay, test targeted delivery and correct the spec.
4. Unify queue removal and remove the silent deletion in `take_rpc`.
5. V3 header/capabilities, request IDs, reply API and handler migration together.
6. RPC wrappers/thunks, registration/ownership and target routing together.
7. Native/WASM integration, documentation and the complete relevant test suite.

Every commit must compile and pass its targeted regressions.
Automatic routing must not be accepted as a mere implementation of the previous
`set_rpc_target` stub: compiler wrappers, object identity,
receive dispatch and authorisation belong together.

## 10. Done criteria

- All six review findings have a previously failing regression test.
- The malformed packet suite under ASan/UBSan without memory errors; decode error paths
  without leaks and without mutating the existing target object.
- TCP and WebSocket deliver the same RPC semantics; native/WASM interop tested.
- No private client payload is forwarded implicitly.
- Old/foreign responses cannot fulfil a new request.
- Push order is preserved, queue overload is visible and bounded.
- `rpc:` and all documented targets take effect on normal method calls.
- `spec/44_5_mirror.md`, runtime headers and compiler builtins agree;
  v2/v3 migration and limits are documented explicitly.

## 11. Decision of 2026-10-03: DELTA, replica initial state and the WS9 test

Status: binding refinement of the plan; the runtime/test
corrections below are not yet implemented by this. The implementation has
evolved since this plan was written; this section refers
to the state checked on 2026-10-03.

### 11.1 DELTA also becomes strict and transactional

**Decision: no implicit best-effort exception for DELTA.** Received
snapshots are validated completely and either taken over completely or
discarded completely. On an error, the last valid state or
the valid local default state stays unchanged. Schema deviations must become
visible; a disconnect is not mandatory on a schema error, but a
success/ready signal is forbidden.

Rationale based on the actual fixtures:

- `tests/ws_context_card_a.aiml`: `SharedCard = {name:string, cost:i32}`.
- `tests/ws_context_card_b.aiml`: additionally `rarity:i32`.
- `tests/ws_context_shared_view.aiml`: contains `list<SharedCard>`.
- `test_mirror_ws_shared_module_context_mismatch` therefore really tests different
  nested schemas, not merely different local compiler type IDs.
  It uses only one card element and checks the prefix fields already read.

The position-based wire format contains no field IDs or length boundary per
struct element. With just two cards, the receiver can read the missing `rarity`
from the bytes of the next element. Bounded buffer access does not make
such data compatible or semantically right. The existing green test
is therefore no foundation for a version compatibility contract.

**Concrete implementation:**

1. `src/sys/mirror_runtime.cpp`, `process_packet`, DELTA branch around line 590:
   replace `aiml_ser_decode_bounded` and the preceding free/memset with transactional
   decoding. Update `has_decoded`, the shadow and any ready notification only
   after a successful commit.
2. `aiml_mirror_session_register_id_typed`, pending blob branch around line 1287:
   use the same central apply helper. Check the return value, do not mark errors
   as a successful initial snapshot. Free the pending blob in an orderly way
   in every case, diagnose errors.
3. Also check the schema identity: merely consuming the bytes completely does not
   detect e.g. equally sized, differently interpreted fields.
   For DELTA, transmit/compare a structural wire schema fingerprint
   that is independent of native offsets, pointer width and local registry IDs.
   Identical wire schemas from different import contexts must match.
   Version or negotiate the DELTA format change explicitly; never wrongly call existing
   untagged frames schema-verified.
4. Turn the previous mismatch success test into a negative test:
   a different schema is rejected, the default/previous state stays
   unchanged, and an observable decode/schema error result arises.
   Check both directions and at least two nested list elements.
5. Keep a separate positive import context test with an identical wire schema, but
   different import sets/compiler contexts.

If schema evolution is to be supported later, it needs a separate,
versioned format with unambiguous field boundaries/IDs or explicit migrations.
Decoding stays strict in the process: first validate the source schema, then
migrate in a defined way. A silent best-effort fallback is not introduced.

### 11.2 An invalid replica initial state remains an error of its own

**Decision: valid local initialisation is a precondition for replication;
`release_old = 0` and error tolerance are not a permanent repair.**

Confirmed findings in the current runtime code:

- `process_packet` sets `has_decoded = true` even when the decoder fails.
- `register_id_typed` ignores the decode return value for the pending blob.
- On the first decode, data is written directly into an already existing model;
  the previous default/heap content has no uniform replacement contract.
- `session.close` and `PKT_UNREGISTER` partly free model fields,
  while the compiler scope clean-up also holds ownership. This responsibility
  must be clarified before a blanket change to `release_old = 1`.

The concrete cause of the reported invalid state before the first decode
has not yet been isolated by this analysis. Do not declare it fixed.

**Implementation and acceptance:**

1. A minimal reproduction without a network: declare the same replica model,
   read string/list/map defaults, serialise and leave. Test locally,
   imported-global and with the refcount representation enabled.
2. C++ anchors: `Compiler::initClassContainerMembers` in
   `src/core/ast_compiler.cpp:131`, global initialisation in the same file;
   `src/core/ast_compiler_stmt.cpp` around line 1711 and the ScopeDrop registration;
   `builtin_registry.cpp`, `MIRROR_FIND_OBJ_PTR` and `register_id`.
   Check initialisation, the correct object address and the generated serializer descriptors
   against each other. No blanket zero-initialisation of live containers.
3. Define target ownership: mirror registers an object owned by the AIML caller
   as a borrow. A successful apply replaces its fields correctly; close
   and unregister release the registration and runtime buffers, but do
   not secretly destroy the still living AIML model. Align the compiler scope drop
   and the existing runtime free path with this.
4. Treat readiness separately from valid initialisation:
   `WaitingForSnapshot` is a valid local object; `Ready` means a
   successfully taken-over snapshot. A faulty snapshot must produce neither
   readiness nor a successful decode history.
5. Regressions for DELTA before/after `register_id`, an invalid first DELTA, valid →
   invalid → valid, close before the first DELTA and model access after close.
   ASan/UBSan plus leak/double free checks; existing defaults must be preserved on a
   rejected snapshot.

### 11.3 Remove the timing dependency in the WS9 test

Check on 2026-10-03: `test_mirror_ws_9rooms_imports.aiml` run serially 16 times with the existing
binary, 16/16 successful. Logs are located locally under
`/tmp/aiml-ws9-review/run-*.log`. This neither reproduces the reported failure rate
nor refutes the race location.

**Concrete race location:** in the host branch of the test, after
`object_registered` it merely waits 16 ms and calls `sess.update` twice.
Afterwards the test sets `got_ci = 1` and copies `player_name` into the view.
REGISTER, however, confirms neither the receipt nor the successful application of the
following DELTA. If that comes later, the view is filled with the old name
and not corrected afterwards.

**Decision: state/acknowledgement synchronisation instead of longer sleeps.**

1. After REGISTER, only bind the replica. In the update loop, wait until the deadline
   for successfully applied ClientInfo; in the concrete test additionally check
   `player_name == "Nikolai"`, `deckset_id == "deckset-alpha"` and
   `player_id == "token-123"`. Only then fill the view and set `got_ci`.
2. Generically, a ready/snapshot counter from a successful apply is better
   than a field value as a signal. An empty name can be legitimate in a real program;
   the values above are exclusively test predicates.
3. Keep pumping the host until the client confirms the completely checked view.
   The client confirms via an explicit RPC; the host validates its own
   ClientInfo fields before the confirmation. The current 40 fixed
   closing updates do not replace a receipt confirmation.
4. No unsynchronised reads of host globals by the client thread.
   The host checks server fields itself; evaluate its result after the thread join
   or transmit it via a synchronised channel. Object lifetime on
   session close must not race with the assertions.
5. Timeouts in every phase must name the state actually being waited for
   and end as an error. Sleep serves at most as poll pacing, not as
   proof of readiness.
6. In addition to the repeated run, deliberately delay DELTA after REGISTER,
   e.g. via a test transport/proxy. The test must also pass with considerably more
   than 16 ms delay. Then 50–100 serial runs; a randomly
   green run alone is no acceptance. A fixed port requires isolated execution.

Concrete C++ changes, API signatures and code snippets for section 11:
[DELTA/replica/WS9 implementation plan](delta_replica_ws9_implementation_plan_2026-10-03.md).
