# Analysis: rendering, network and drag-and-drop in AIML / 9rooms_v2

As of: 18.10.2026. Exclusively static code analysis, no implementation and no runtime measurement. The current working files of AIML (HEAD `d216077`, with existing local changes) and 9rooms_v2 (HEAD `ed4f3a7`) were examined. Statements refer to this source state, not to a proven state of the shipped browser binaries. The reported one-off deck editor error was not investigated.

**Result:** the observed pauses fit the existing architecture very well. Game actions wait for RPC responses in the UI frame; afterwards there are often a second state query and a complete board rebuild. Drag-and-drop hangs on the same frames. On top of that come independent weaknesses in gesture detection, hit testing and browser frame timing. A slow network amplifies these problems; it does not have to cause them in the first place.

**Evidence:** "proven" means directly traceable in the code. "Risk" denotes an error path derivable from that without a reproduction. Cost shares and the concrete cause of the occasionally failed grab remain open without measurement. Source references are relative to this report; the lines named refer to the examined working state.

## 1. Flow of a frame and an action

The app puts logic, input evaluation, RPCs, board construction and drawing into `MainMenuApp.frame`. The separate engine render callback stays empty. Canvas and scene are only drawn at the end of `frame`.

- [App loop and callback wiring](../../../9rooms_v2/client/main_menu_app.aiml#L546)
- [Drawing at the end of the frame](../../../9rooms_v2/client/main_menu_app.aiml#L1083)
- [Engine loop](../../src/sys/engine_runtime.cpp#L491)

For an attack, the concrete flow is:

1. `board.render` processes the drag and creates an attack intent.
2. `net_attack_creature` waits synchronously for `attack_creature` (timeout 5 s).
3. On success, `net_get_match_state` waits synchronously for the fresh state (timeout 4 s).
4. The app converts the response, processes death displays, destroys the board and rebuilds it through another `board.render`.
5. Only later does `engine.draw_scene` follow.

Evidence: [attack path](../../../9rooms_v2/client/main_menu_app.aiml#L863), [RPC wrapper](../../../9rooms_v2/client/net/match_rpc.aiml#L213).

**Consequence:** the visible reaction depends on at least two consecutive request/response cycles and the local build time. Example, not a measurement: two cycles of 80 ms each already add up to about 160 ms before additional local work. A fast renderer cannot compensate for this waiting.

The phase check likewise calls `net_confirm_phase` synchronously. Its response is not evaluated at this point; only `board_poll=60` is set. The periodic query can nevertheless still fail on the input cooldown. The comment "poll again immediately" therefore does not match the actual flow in every situation. The check is reset to the old server value until the confirmed state is taken over.

Evidence: [check processing](../../../9rooms_v2/client/main_menu_app.aiml#L792), [cooldown](../../../9rooms_v2/client/main_menu_app.aiml#L676), [check display](../../../9rooms_v2/client/board_screen.aiml#L385).

**UX problem:** "sent", "server waiting", "rejected" and "not clicked yet" are not cleanly separated here. For the attack, rejections end up in the log; for the phase check not even the result is handled explicitly.

## 2. Network: usable foundation, blocking use

### 2.1 Synchronous RPCs stop the game frame — proven, highest priority

`send_and_await` sends a correlated request and pumps the session in a wait loop with 2 ms sleeps. Meanwhile it calls neither the app frame function nor the renderer. Under WASM, the sleep returns to the browser via Asyncify, but the AIML game frame stays suspended. The browser can therefore keep receiving events while the game UI shows its last image.

Evidence: [send_and_await](../../src/sys/mirror_runtime.cpp#L1955), [sleep under WASM](../../src/sys/time_runtime.cpp#L39).

Positive: the request ID and the responding peer are checked; foreign responses stay in the queue. The earlier problem of a purely FIFO-based response assignment should not be blanket-attributed to this current code. `send_request`/`take_response` already exist; the board entry uses them. What is mainly missing is their consistent use for normal game actions and board polls.

Trade-off: synchronous wrappers simplify the control flow and error handling. Asynchronous actions need explicit states for pending, successful, rejected, timeout and connection loss. In return, input and animation stay independent of the network latency.

### 2.2 Polling is bound to frames and interactions — proven

The board queries after 60 frames, sets a cooldown of 30 frames on press/release and does not poll during an active drag. That protects existing prefab handles from destruction, but couples data freshness to FPS and user behaviour.

At 60 FPS, 60 frames are about one second; at 30 FPS about two. Frequent input or long holding can delay the state further. Particularly unfavourable: an outdated `combat_pending=1` keeps blocking dragging, and new click attempts postpone the very query that could correct this state.

Trade-off: polling is simple, robust and can heal missed updates. Server push reduces idle queries and reaction time, but needs revisions, re-registration and a complete resynchronisation after gaps. A sensible intermediate step would be non-blocking polling with time intervals and at most one pending query.

### 2.3 The state response is more expensive than a small status ping — proven; order of magnitude unmeasured

`MatchService.get` loads the match state, checks timeouts, can save changes and continue AI combat, creates a filtered player view and collects visible card data. The client converts this response again into `MockState`. Even unchanged matches therefore go through relevant work.

Evidence: [MatchService.get](../../../9rooms_v2/sources/match/match_service.aiml#L206), [client conversion](../../../9rooms_v2/client/match_to_mock.aiml#L234).

A complete snapshot is easy to follow and heals deviations. Revisions and deltas save bandwidth, serialisation and projection, but require a defined order, a baseline and a snapshot fallback. Static card definitions could also be versioned separately from dynamic match data.

The second state query after an action can **not blindly** be dropped by using the first response: the handlers partly still do AI/resolution work after creating the response object. In addition, `get` creates the filtered view including `visible_cards`. A merged response contract would have to deliver the final state filtered for this player.

Evidence: [ConfirmPhaseHandler](../../../9rooms_v2/sources/game/rpc_handlers/match_play_handlers.aiml#L38), [AttackCreatureHandler](../../../9rooms_v2/sources/game/rpc_handlers/match_combat_handlers.aiml#L41).

### 2.4 The server and the native WebSocket path can hold up other work — proven

The game server processes RPCs one after another in a loop. AI actions and rule resolution run before the response; periodic match ticks also run there. A slow handler therefore delays further clients in this process. The actual duration of this work was not measured.

Evidence: [server loop](../../../9rooms_v2/sources/game/game_server.aiml#L298).

Additionally, `ws_poll(..., 0)` in the native WS/WSS path only means that data or a socket event is present. Afterwards `ws_recv_frame_raw` reads the header and the complete payload with blocking `ws_read_n` calls. A partial frame can therefore block the entire session pump. The write path also writes synchronously and completely.

The current accept path already sets a 10 s read timeout. That bounds individual blocking reads, but does not make receiving non-blocking and is not a hard 10 s total deadline for a frame. The outer RPC deadline is not checked continuously during a read blocking underneath. The 4/5 s RPC timeouts are therefore not guaranteed maximum wall times on this path.

Evidence: [accept/timeout](../../src/sys/ws_runtime.cpp#L615), [low-level I/O](../../src/sys/ws_runtime.cpp#L424), [frame receive](../../src/sys/ws_runtime.cpp#L750), [poll](../../src/sys/ws_runtime.cpp#L1172), [mirror pump](../../src/sys/mirror_runtime.cpp#L882).

Delimitation: browser WebSocket receiving works with messages that have already arrived; this native partial frame weakness must not be attributed to it directly. A browser client can, however, feel its consequences via the native server. The mirror TCP path already buffers partial packets and works non-blockingly. Accepting new WS connections is also already handled step by step.

Trade-off: an incremental parser and buffered writes need more state, limits and backpressure. They do, however, remove the fundamental waiting for a single peer. Additional threads can decouple CPU work, but do not automatically solve this contract; match changes must still stay ordered.

## 3. Rendering and frame timing

### 3.1 Browser timing: working time plus 16 ms — proven

The current WASM engine uses a manual Asyncify loop: `web_loop_tick`, then `emscripten_sleep(16)`. It does **not** use `requestAnimationFrame` here, although some app comments still suggest so. The decision is documented as a workaround for earlier Asyncify/main loop problems.

The waiting time comes on top of the frame work. Arithmetically, 5 ms of work plus 16 ms of sleep give around 48 FPS, 10 ms plus 16 ms around 38 FPS; browser scheduling can deviate from that. The timer is moreover not directly coupled to the screen presentation. Native frames, on the other hand, use swap/poll and a remaining-budget limiter.

Evidence: [actual loop implementation](../../src/sys/engine_runtime.cpp#L503).

Trade-off: the workaround stabilises the described flow, but costs timing quality. A later switch to browser-driven frames should go together with removing the suspending work from frame callbacks and should re-check the documented freeze scenarios. An isolated swap of the loop would be risky.

### 3.2 Complete board rebuilds — proven

`reload` destroys, among other things, the background, phase panel, hand cards, zones, field cards, preview and numerous overlays. The next `render` initialises them again. Successful actions often force this path directly.

Evidence: [BoardScreen.reload](../../../9rooms_v2/client/board_screen.aiml#L460).

Positive: periodic polls already compare a content signature; tap and attacker displays can be updated without a rebuild. Floats are preserved on reload. So not everything is recreated on every frame or every poll.

The granularity nevertheless stays coarse: stat changes can rebuild the whole board. That creates CPU/allocation spikes and destroys visual identity, hover references and potentially interaction handles. The drag protection exists explicitly in the periodic poll path and is not a general contract for every state takeover.

Trade-off: a full rebuild simplifies consistency. Reconciliation via stable card instance IDs keeps objects and gestures stable, but requires explicit updates for position, stats, equipment and visibility. A middle way would be limited updates of individual zones or cards.

### 3.3 Rendering also processes input — proven

`BoardScreen.render` calls `apply_drag`, the phase check, keyboard actions, target selection and the ability UI. After a successful action the same method is called again within the same app frame. That is not pure rendering. Individual paths already have latches against double processing, but this protection logic is scattered.

Evidence: [BoardScreen.render](../../../9rooms_v2/client/board_screen.aiml#L675).

Risk: an input event can be applied again to a new state or to newly created widgets. After a reload, the global scene is moreover not necessarily yet in the z order established by the next draw. A clean order would be: process the input snapshot once, take over the results, update the layout, draw.

### 3.4 CPU/GPU work: plausible additional costs, no proven GPU limit

The prefab runtime sorts the global instance list in every draw, copies it for safe traversal and synchronises container/cascade contents during rendering. Many local batches perform their own buffer uploads, uniform queries, texture bindings and draw calls. Text and complex cards increase this work.

Evidence: [draw_scene](../../src/sys/prefab_runtime.cpp#L5217), [render_instance](../../src/sys/prefab_runtime.cpp#L2104), [flush_batch](../../src/sys/prefab_runtime.cpp#L971).

Positive: textures, fonts and material programs have caches; invisible instances are skipped during the actual rendering. A board rebuild therefore does not automatically mean reloading all textures.

Trade-offs: dirty flags save sorting/layout, but need reliable invalidation. Larger batches reduce API calls, but must not falsify transparent draw order, clipping and materials. These optimisations should be prioritised after a CPU/GPU measurement. For the action-bound pauses, the synchronous control flow is proven much more directly.

### 3.5 Artwork can be loaded synchronously during card construction — proven

Token and CardFull construction call `art_ensure`. On a cache miss, connection setup, header/chunk receipt, image decoding and texture registration follow synchronously. A new card can therefore cause an additional pause. Already cached images avoid the transfer; this is not an explanation for every repeated click.

Evidence: [token construction](../../../9rooms_v2/client/board_widgets.aiml#L42), [artwork fetch](../../../9rooms_v2/client/net/card_art_cache.aiml#L84).

A placeholder with background loading would decouple the construction. For that, errors, retries, load priority and the memory budget must be managed explicitly.

## 4. Drag-and-drop: concrete error classes

### 4.1 A fast drag can lie completely between two game updates

Browser input stores press/release edges, but `engine.update_drag` starts based on `btn_down`. If pressing and releasing happen during a blocked frame, the next snapshot can contain both edges and `button=0` at the same time. Then no prefab drag starts. If processing only begins after a mouse movement, the current position instead of a stored press position is additionally used to select the card.

Evidence: [browser snapshot](../../src/sys/input_runtime.cpp#L1491), [browser mousedown](../../src/sys/input_runtime.cpp#L1051), [prefab drag](../../src/sys/prefab_runtime.cpp#L6763).

**This is a concrete possibility matching the symptom, not a reproduction of your case.** The opposite behaviour is possible too: with the button already held, a drag only starts when later moving over a card. In this prefab path there is no preceding press candidate/movement threshold phase.

### 4.2 Combat reactions lock all drags

With `combat_pending=1`, an existing drag is aborted and the code returns before `engine.update_drag`. This is meant to keep a reaction click from accidentally becoming a drag. That is deliberate game logic, but without a clearly recognisable reason for the lock it looks like an input bug. A state made stale by polling can keep the lock visible for longer.

Evidence: [BoardDragController.apply_drag](../../../9rooms_v2/client/board_drag_controller.aiml#L443).

### 4.3 Pick-up and drop have different rules

The pick-up searches all visible, draggable instances backwards and takes nested transformations into account. Non-draggable foreground is skipped in the process: an overlay is therefore not automatically an input barrier.

The drop, on the other hand, uses `hit_test`: only top-level instances, rectangular bounds, first visible object. A decorative object can therefore cover the actual target. Rotation is not turned into a precisely rotated hit area there. Both paths rely on the last sorted instance list; z changes are only sorted when drawing.

Evidence: [pick-up](../../src/sys/prefab_runtime.cpp#L6763), [hit_test](../../src/sys/prefab_runtime.cpp#L5474), [drop evaluation](../../../9rooms_v2/client/board_drag_controller.aiml#L220).

That does not prove that a particular overlay blocked your drag. It does, however, prove that uniform rules for foreground, click-through and drop targets are missing.

### 4.4 The dragged card is not necessarily the server-side attacker

Field cards are always created with `draggable=1`, including the opponent's. On a creature-on-creature drop, essentially only the target ID is noted for the live attack; `AttackCreatureReq` contains no source ID. The server uses its current attacker. The visual gesture can therefore express something other than the command — or only be rejected on the server side.

Evidence: [field card construction](../../../9rooms_v2/client/board_base.aiml#L244), [drop rules](../../../9rooms_v2/client/board_drag_controller.aiml#L305), [request model](../../../9rooms_v2/sources/models/match_play_reqs.aiml#L43).

Server authority is right. For unambiguous operation, the source ID meant when grabbing should nevertheless be verifiable together with a state revision; allowed gestures and rejection reasons should already become visible locally.

### 4.5 Additional robustness gaps

The prefab drag ends on `btn_released`, not generally on lost contact. Browser mouseup is already registered window-wide, touchcancel too; those are existing improvements. In the examined input runtime, however, a general blur/visibility reset for held buttons and drags is missing. Focus loss therefore remains a check point.

The board logic treats movements under 12 pixels as a possible pin click instead of a drop. This decision is made after the drag has started and is not explicitly restricted to touch. Short mouse gestures can therefore appear as ability pinning. The threshold uses screen coordinates, while parts of the layout use design coordinates.

Trade-off: a shared gesture machine with press origin, pointer ID, capture, threshold and explicit cancel needs more states. In return it makes pick-up, movement, drop, tap and abort traceable and independent of temporary prefab addresses.

## 5. Priorities and architecture decision

| Priority | Measure for a later implementation | Benefit | Effort / trade-off |
|---|---|---|---|
| P0 | action RPCs and board poll non-blocking; UI pending state | frames keep running during network waiting time | explicit request lifecycles and error display |
| P0 | native WS/WSS reads and writes incremental, with budgets | one peer does not stop the session | parser state, queues, limits, backpressure |
| P1 | input once per frame; bind the drag to the press origin and a stable ID | reliable grabbing, no repeated input on rebuild | central gesture/event processing |
| P1 | action response after all follow-up work, filtered and versioned | fewer sequential round trips | uniform response contract |
| P1 | rework browser frame planning after decoupling the RPCs | more even timing | Asyncify regression check |
| P1 | update cards/board incrementally; artwork asynchronous | fewer CPU spikes and handle changes | targeted invalidation and cache lifecycle |
| P2 | push/revisions/deltas plus snapshot fallback | fresh state, less repeated data | reconnect and gap handling |
| P2 | renderer batching, dirty sort and layout caches after profiling | lower base costs | more complex render management |

WebSocket/WSS is fundamentally a suitable foundation for a turn-based game. A switch to UDP, WebRTC or HTTP would not remove the synchronously waiting app and the complete rebuilds. First of all, control flow, state contract and input processing are decisive.

Complete optimistic game simulation is not required for this: local grabbing, hover, a pending indicator and a preview can react immediately while the server decides the move. With complex effects and hidden information, complete prediction would be considerably more laborious and would have to handle visible rollbacks.

When switching to asynchronous operation, a timeout must be treated as "outcome unclear". Transport request IDs do not replace domain-level idempotence. `ConfirmPhaseReq`, for example, contains no expected phase/round; a careless retry can hit a state that has meanwhile advanced. An action ID and an expected revision should therefore be part of the design.

## 6. What should be measured before an implementation

No benchmarks or live games were started. To attribute the actual costs, a shared trace over the following points in time would be sufficient:

- physical press/release → input snapshot → drag candidate → visible drag.
- click/drop → request sent → server received → handler/AI done → response received.
- decode → MockState conversion → board reconciliation/rebuild → draw → next presented frame.
- per action: number of RPCs, response bytes, created/destroyed prefabs, draw calls and artwork cache misses.

More important than average FPS are raw frame times and long outliers. The engine caps `dt` at 100 ms before FPS averaging; this existing FPS value therefore does not fully reflect long hangs ([engine timing](../../src/sys/engine_runtime.cpp#L373)).

Targeted scenarios: phase check and attack at 0/50/150 ms additional latency; a fast complete drag during an RPC wait; a change of `combat_pending`; long holding across a poll deadline; focus loss; a drop under an overlay; cold and warm artwork cache; two clients with a deliberately delayed WS partial frame. The latter belongs in an isolated test environment.

The success criteria should apply separately: immediate local input feedback, frames that keep running during requests, clearly visible pending/error states and no lost or doubly executed gesture. The server-side move confirmation may still need network latency.

## 7. Concrete fix options

The following proposals are drafts for a later implementation. They were neither implemented nor tested. "Small", "medium" and "large" denote the relative intervention in the architecture, not a reliable effort estimate. Each fix names a limited entry solution and, where sensible, a further-reaching expansion.

### F1 — remove action RPCs from the frame waiting path

**Concerns:** `client/main_menu_app.aiml`, `client/net/match_rpc.aiml`; findings 1 and 2.1. **Intervention: medium.**

For `confirm_phase`, `attack_creature` and `attack_soulmage`, first add asynchronous send wrappers following the existing pattern `net_get_match_state_send`. The app stores the request ID, action, match ID, start time and the local context. Per frame it checks with `take_response` whether a result is available, and returns immediately as long as nothing has arrived.

The action flow gets explicit states: ready → pending → state reconciliation → done, or error/outcome unclear. As a first, manageable variant, at most one state-changing action per match stays pending at a time. Hover, preview and independent UI operation remain available. Dependent game actions are visibly locked. A second asynchronous state query may stay for now; F3 removes it later.

For the phase check, "confirmation is being sent" is shown immediately. The real server ack stays separate from that. Rejection and connection errors are shown on the board. A local pending symbol must not appear as an already completed phase change.

**Important:** the request assignment must also be able to expire when leaving the match or on reconnect. For that, also store a local session/view generation and no longer apply responses of old generations to the new view. A timeout must not trigger an automatic repeated game move; until F3 exists, reconcile the state first or show the uncertainty.

**Check:** at 150 ms additional latency and with a missing response, hover and animation keep running. A double click does not send a second identical action. A response for a left match does not change the current board. Errors end the pending state traceably.

### F2 — make board polling time-based and independent of the drag

**Concerns:** `poll_board_state`, `sync_board_poll`; finding 2.2. **Intervention: medium.**

Replace the frame counters with monotonic points in time. Send a poll when the interval has elapsed and none is pending yet; take over responses in later frames. Do not send a retroactive series of "missed" polls. Choose the concrete interval based on the measured load and the desired reaction time.

Separate network receipt and visual update: a response may also arrive during a drag. As long as F6 is not implemented yet, the newest snapshot can be buffered and the destructive board rebuild postponed until the end of the gesture. If the new state removes the dragged card or ends the permitted interaction, abort the gesture explicitly and apply the snapshot. That way long holding does not become a permanent state blockade.

Without server revisions, an old, already running poll should not overwrite the newer state of a successful mutation: use a local request generation and then start a fresh query. With F3, the server revision decides the order.

**Trade-off:** the buffering is a transitional solution and can briefly delay rendering. With stable board objects, updates can be taken over more finely. A shorter poll interval alone, on the other hand, worsens the load and the existing frame pauses.

**Check:** comparable update intervals at 30 and 60 FPS; at most one running poll; the opponent's state is received despite repeated clicks; no access to destroyed drag objects.

### F3 — an unambiguous, versioned action contract

**Concerns:** request/response models, match handlers, match service and client; findings 2.3 and 4.4. **Intervention: medium to large.**

An action gets a domain-level `action_id` and an expected game context. For the phase check, phase and round are particularly important; an attack additionally carries the actually chosen `source_iid`. The server validates this source against its current attacker. On a mismatch it rejects traceably instead of silently applying the gesture to a different creature.

A monotonic match revision value is advanced on relevant state changes, including changes by timers and AI. The response contains the action result and the revision. For a simpler first contract, a change of the expected revision may lead to a rejection with a fresh state. Later, action-specific preconditions can avoid unnecessary rejections on independent changes.

After the follow-up work belonging to the request, the final state is re-read and output via a shared, **purely reading**, player-specific projection. This projection should be extracted from the existing `get` path: calling `get` unchanged could trigger timer/AI work again. The response must have the same visibility filtering and required card data as the previous snapshot. Only then can the client drop the additional `get_match_state` round trip.

For retries, the server stores the result of an `action_id`, bound to the player and match. Mutation and deduplication decision must be consistent together. If the protection is also to survive server restarts, a volatile cache is not enough. Define the retention period, memory limit and behaviour after expiry explicitly.

**Check:** transmit the same action twice, lose a response and request it again, deliver responses swapped. The move is executed once; the view does not fall back to an older revision. A phase check from round N does not confirm round N+1. An attack with a wrong source ID is rejected. Hidden cards stay hidden in all response paths.

### F4 — really make the native WebSocket pump non-blocking

**Concerns:** `src/sys/ws_runtime.cpp`, `src/sys/mirror_runtime.cpp`; finding 2.4. **Intervention: large, delimitable within the engine.**

For every WS connection, keep a receive buffer and parser progress: header, extended length, masking and payload. Use non-blocking sockets; reading ends on `EAGAIN`/`EWOULDBLOCK` and is continued on the next pump call. A message is only handed to mirror after it has been received completely. Ping/pong, close and possibly fragmented messages must use the same incremental lifecycle.

Write outgoing frames into a bounded queue and continue partial transfers with an offset. With TLS, handle `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE` accordingly, including the respectively required readiness interest. Merely switching on `O_NONBLOCK` would not be a complete fix: the previous complete read/write contract could treat partial results as errors.

Provide a byte/time budget per connection and pump pass. Additionally define a maximum message size, queue sizes and a monotonic total deadline for incomplete messages. A full queue must become visible as backpressure or a clear connection error; it must not grow without bound or make the frame wait.

**API option:** introduce a new try-receive/pump interface for mirror and keep the existing blocking WS functions for now as explicitly blocking convenience functions. That limits changes to other AIML applications. Mirror itself must then no longer use the blocking functions.

**Check:** delay header and payload byte by byte; a peer no longer reads; TLS temporarily cannot write; only ping/pong arrives; the remote end disconnects in the middle of a frame. Other clients stay serviceable, memory stays bounded and no message is lost through a partial write.

### F5 — a gesture has an origin, a target context and exactly one end

**Concerns:** `input_runtime.cpp`, `prefab_runtime.cpp`, `board_drag_controller.aiml`; findings 3.3 and 4.1–4.5. **Intervention: medium to large.**

Record input as ordered events with a pointer ID, position and point in time. An immutable input set is consumed at the start of the frame. Position changes may be merged; press, release and cancel must be preserved in order. If a complete short gesture lies between two frames, it is thereby still evaluated correctly.

A press leads to a candidate based on the **press position** and the arrangement visible at that moment. After exceeding a defined movement threshold, it becomes a drag. The gesture holds the stable card ID, the initial game context and an optional render handle. Release leads exactly once to a tap or drop; focus loss, pointer cancel, card removal and scene change lead to cancel. A cancel produces no game move.

Mouse and touch may get different thresholds, but should use the same state machine. For the browser, check a uniform pointer event binding with capture; do not let existing mouse/touch callbacks fire in parallel twice. Measure thresholds in an explicitly defined UI unit and take device/canvas scaling into account.

Split `BoardScreen.render` into input processing, visual update and rendering. An additional rebuild or draw does not process input again. As a transition, a frame/event consumption marker can prevent repeated processing; in the long run it does not replace the separation.

**Board rules:** before the drag starts, clarify which gesture is allowed: attack, movement, equipment or pure preview. Opponent cards can still be looked at, but do not therefore have to be draggable as one's own attackers. With `combat_pending`, show the concrete reason for the lock and keep permitted reaction clicks going separately.

**Check:** a fast drag during a simulated frame pause; a held button only later moves over a card; release outside the canvas; focus loss; scene change; re-rendering in the same frame. Per physical gesture at most one intent arises, and after every abort the next gesture is possible.

### F6 — preserve board objects by their IDs

**Concerns:** `board_screen.aiml`, `board_base.aiml`, `board_helpers.aiml`; finding 3.2. **Intervention: medium to large.**

Keep a mapping of `iid` to card view and associated badges. On a snapshot, first determine the changes: new/removed cards, zone changes and changed properties. Create new cards, tear down removed ones in a controlled way, update existing ones. Phase panel, background and unchanged zones stay in place.

For a limited first implementation, update only stats, tap, attacker and target frame directly; structural changes may still rebuild zone by zone. Later, reposition or re-parent cards on a zone change. Treat an active drag as a separate visual preview so that the authoritative card position can be updated without binding the gesture to a destroyable prefab.

Today's string signature can serve temporarily as a fallback. In the long run, use actual field differences or local dirty flags. A match revision says **that** something changed; it does not replace the decision **which** view is affected.

**Check:** a pure stat change creates no new card prefabs. Hover and preview are preserved for unchanged cards. Death, equipment, owner and zone changes are represented correctly. Existing death/damage displays still run exactly once.

### F7 — shared hit testing with explicit input rules

**Concerns:** `prefab_runtime.cpp` and the board drop evaluation; finding 4.3. **Intervention: medium.**

Use a shared picking foundation for click, drag start and drop: the same world transformation, visibility across the parent chain, clipping and the current z order. For rotated elements, transform the point into local coordinates. The sorting must be correct before the input; a dirty flag on z changes suffices, provided all changes use this path.

Provide independent properties for "blocks input underneath", "receives input itself", "draggable" and "accepts this drop type". Decoration can be explicitly transparent; a modal overlay blocks even when it is not draggable. On drop, exclude the own drag preview from the candidate set instead of temporarily making it invisible.

**Trade-off:** existing prefabs need compatible default values. Activate new rules for the board first, or check existing surfaces for click-through behaviour; a globally changed default can impair other apps.

**Check:** overlapping cards, a decorative frame, a modal overlay, a nested/scrolled element, rotation and a z change in the same frame. The visually expected permitted target is chosen; locked layers are not clicked through.

### F8 — browser timing without an additional full-frame sleep

**Concerns:** `engine_runtime.cpp`; finding 3.1. **Intervention: small as an interim solution, medium for the loop switch.**

As a limited interim solution, the manual loop can sleep only for the remaining frame budget, measured with monotonic time. Under overload, an explicit cooperative yield stays necessary; tightly catching up on several frames would be counterproductive. That reduces the additive 16 ms effect, but does not yet couple presentation to the screen refresh.

The sustainable variant uses browser-driven frames once all suspending calls reachable in them, including artwork loading, have been removed or safely moved out. Network and asset pumps then only deliver finished results. The Asyncify/main loop regression documented in the engine must be secured again with a targeted minimal case.

Keep raw wall time for telemetry and the bounded simulation `dt` separate. A background tab gets a defined pause/resume handling and does not catch up on an unbounded number of old simulation ticks.

**Check:** frame time distribution with an empty and a full scene, repeated WS sends, slow responses, tab switching and resizing. The loop must neither stall nor fall into a catch-up loop after a pause.

### F9 — artwork as a background job with a placeholder

**Concerns:** `card_art_cache.aiml`, `board_widgets.aiml`, `card_full_populator.aiml`; finding 3.5. **Intervention: medium.**

In the future, `art_ensure` in the UI path should check the cache and queue a job if needed; card construction immediately returns a placeholder. Download, decode and GPU upload get separate states and bounded work per frame. Finished textures are bound via the artwork hash to views that still exist.

Merge several requests for the same hash, prioritise visible cards and handle errors with a bounded retry/backoff. Negative cache entries prevent repetition per frame. A GPU memory budget with reference counting or LRU prevents unbounded growth; visible textures must not be evicted in an uncontrolled way in the process.

**Peculiarity of the existing media protocol:** as long as its chunk responses have no request assignment of their own, process at most one transfer at a time on the same connection. Asynchrony does not need parallel transfers. For an image that is no longer needed, end the running transfer cleanly or reset the connection in a controlled way, so that subsequent responses stay assignable.

**Check:** a cold cache with many cards, a slow/failed media server and a scene change during a download. The board stays usable, identical images are loaded once, and finished images only appear on views that still match.

### F10 — limit CPU/GPU costs and server work in a targeted way

**Concerns:** prefab renderer, match handlers and scheduler; findings 2.4 and 3.4. **Intervention: depends on the measurement result.**

In the renderer, first record draw calls, upload bytes, sort time, layout/text time and allocations. Then cache uniform locations per shader, sort only on changes and reuse unchanged text/layout data. Adjacent compatible batches may be merged as long as draw order, clipping and material state are preserved. A global sort by texture would change transparent overlays and is therefore not a safe blanket fix.

If server-side AI/rule work dominates, split it into bounded work steps per match. After an atomic decision, the server can report an explicit "resolution running" state and continue the follow-up work later. The game rules must determine at which boundaries a state may become visible or operable again. A premature success response while the mutation is still inconsistent would be wrong.

Workers are an alternative for actually isolatable, CPU-intensive computations. Take over results only against the still valid match revision. Moving shared KV/match objects into threads without clarified ownership and synchronisation rules would not be a reliable fix.

**Check:** the same game outcome before and after splitting, no concurrent mutations of the same match, bounded waiting time for other clients. Additionally compare renderer changes visually with overlapping transparent cards and clipping.

## 8. Recommended implementation in verifiable stages

1. **Measurement basis and immediate feedback:** provide the trace points from section 6 as well as an explicit pending/error display. Record baseline values for phase check, attack and fast drag.
2. **Decouple the waiting:** implement F1 and F2 first for the named board actions. Handle F4 separately at engine level; otherwise even an asynchronous app stays sensitive to blocking native pump calls.
3. **Stabilise gestures:** F5 and F7; then F6 to preserve the card views. As a result the interaction needs fewer and fewer exceptions for reloads.
4. **Reduce round trips and cold loading pauses:** F3 and F9. Only remove the previous second state query once the new response contract guarantees final, filtered states.
5. **Frame planning and measured remaining costs:** F8 after checking all suspending frame paths; F10 based on remaining bottlenecks. Add push/deltas only when the data volume or update latency justifies it.

For every stage, the scenarios from section 6 and the respective check under F1–F10 apply. A good intermediate goal is already reached when the game confirmation still needs the same network time, but the UI keeps reacting meanwhile and every gesture is unambiguously accepted, rejected or aborted. A complete rewrite of the engine or network stack is not a precondition for that.

## 9. Should AIML connect network and rendering more strongly at engine level?

**Yes: a shared flow control in the engine makes sense.** Particularly valuable would be binding rules for when network progress happens, when finished responses are applied and when input, layout and rendering run. The engine should keep being able to deliver frames while responses are pending. A tighter connection must therefore not create an additional dependency of the image output on the arrival of a message.

The following API and architecture ideas are design proposals, not already existing or verified AIML functions. They build on the examined runtime paths. Their complete language and compiler integration would have to be designed separately.

### 9.1 What today's separation leaves the application to solve itself

The building blocks already exist: the engine owns the frame flow, mirror owns sessions and correlated requests, prefabs own the scene and input interaction. In 9rooms, however, `MainMenuApp.frame` connects these building blocks by hand:

- `net_pump` must be called on time and regularly.
- Every screen organises requests, waiting times and response takeover itself.
- The board postpones polls so that a reload does not destroy a drag.
- `board.render` evaluates input and can be called again after a response.
- Clear and draw were moved to the end of the frame so that suspending network calls do not show an empty intermediate image.

These connections are partly necessary application policy, partly recurring engine infrastructure. Taking over the recurring parts centrally would reduce the number of special cases in new games and screens. The mere existence of `send_request` is not enough for that: an easy-to-use lifecycle for its result is still missing at application level.

### 9.2 A suitable shared frame contract

A possible flow would be:

1. **Take over input:** take events up to the frame boundary into an immutable set. Pointer gestures keep their identity across several frames.
2. **Advance the network:** handle receiving, sending and timeouts within a bounded budget. Only provide complete responses and connection events.
3. **Take over results:** apply matching responses to the game state at a defined point. Check session, match, revision and lifetime in the process; abort gestures that have become invalid.
4. **Run input and the game update:** evaluate every input exactly once. The previously visible card ID is preserved for a gesture; the action is validated against the current state. New requests are only queued.
5. **Reconcile the rendering:** update changed cards, overlays and layouts. Assets may trigger jobs, but not wait for their completion.
6. **Draw and present:** render a consistent state. Outgoing requests can still be handed to the transport beforehand within a budget, without waiting for the response.

The exact order of network takeover and game update is a design decision. What is essential is that it is documented and stable. Network callbacks must not change the scene at arbitrary points in the middle of `render_instance`. Likewise, a new response must not retroactively reinterpret an already read press position as a different card.

**Benefit:** an arriving state has a clear hand-over point. Rendering, input and network access no longer have to be secured against each other via scattered cooldowns and latches. That also helps when reproducing bugs: the trace can show which event was taken over at which frame boundary.

**Limit:** a budget only works if the underlying operations are non-blocking or interruptible. A blocking `recv` does not suddenly become time-bounded by being placed into a "network phase with a 2 ms budget". F4 remains a precondition. Deserialising a single large message can also exceed the budget; message limits and measurement remain necessary.

### 9.3 Which functions belong in the engine

| Proposed capability | Responsibility of the engine | Responsibility of the application |
|---|---|---|
| bind a session to a run context | regular pumping, clear owner, deregistration | which session belongs to which game |
| typed asynchronous request | request ID, timeout, decode, exactly one completion notice | domain meaning of success and rejection |
| result queue | takeover at a defined frame boundary | change of the match/UI state |
| lifetime scope for requests | closed scopes no longer deliver UI callbacks | bind the scope to a screen, match or session |
| pending state | queryable status and notification | display, text and which actions are locked |
| rendering invalidation | update changed views in a targeted way | mapping of a match field to a card view |
| network/frame telemetry | uniform points in time and correlation IDs | domain action names and relevant measurement scenarios |

As a conceivable API, a run context could register a session and return a request handle with states such as pending, successful, rejected at transport level, timeout or closed. The return value of a typed game response stays separate from that: a correctly received response can contain a domain-level rejection. This separation prevents "message arrived" from being confused with "attack allowed".

Callbacks or resumable tasks would have to run at the defined takeover point and stay within the frame budget. A simple poll API for handles would be sufficient as a first step and could build on the existing request ID technique. It avoids, for now, the additional complexity of a new coroutine language feature.

When a screen is closed, its result delivery is deregistered. That does **not** mean that an already sent game move was taken back on the server. Engine cancel and domain-level undo are different operations; these semantics must be stated explicitly in the API contract.

### 9.4 Which connection would be too tight

Direct network RPCs from prefab render functions would spread the current problems further. A card view should receive the data to display and report user intents. Token resolution, AI work or a server-side move do not belong in drawing a widget.

Automatically mirroring arbitrary server objects directly into prefabs would also go too far for 9rooms. Player-specific hidden information, stable card identity, animations and local preview require a deliberate projection. The engine can help with change notification and mapping; the application must decide which data becomes visible and how a change is rendered.

For the first integration, **explicit state hand-overs** should therefore be kept: response → validated game state → view change. A later declarative data binding can shorten these steps, but should keep debugging, costs and invalidation traceable.

### 9.5 Shared scheduler or a dedicated network thread?

**Recommended entry: a shared, budgeted scheduler on the existing game thread.** Non-blocking I/O pumps and result takeover at frame boundaries need no parallel modification of the game state. That fits the existing native and WASM structure and keeps prefab accesses on one thread.

The advantages are simple ownership, a stable order and less synchronisation effort. The disadvantage: CPU-intensive decoding or game updates can still cause long frames. Message sizes, pump budgets and decode times should therefore be visible.

A dedicated network thread would be a later option on native platforms: it receives and processes transport data, hands finished results over via a bounded queue and does not modify any scene itself. On web platforms, workers, memory hand-over and the concrete build configuration would have to be taken into account specifically. A uniform public scheduler contract can remain, even if the transport works differently internally per platform.

Multithreading additionally brings order, shutdown and memory ownership questions. It is only worthwhile once measurements show that budgeted processing on one thread is not enough. It fixes neither a second unnecessary round trip nor the domain-level confusion between the dragged and the actually attacking creature.

### 9.6 Backpressure and fairness belong to the shared contract

As soon as the engine controls network and frames together, it must handle overload explicitly. Otherwise blocking waiting merely turns into a growing message queue.

- Outgoing queues need limits and a visible "not yet accepted" or error state.
- State snapshots can, where appropriate, be reduced to the newest revision. Action responses and one-off game events must not be discarded wholesale.
- Active game input should be prioritised over background lists and artwork, without permanently crowding them out.
- There must be enough fairness per session and frame so that a heavily sending peer does not monopolise the others.
- Decoding, state takeover and rendering each need their own measurements. "Network duration" alone hides local work.

Which messages may be merged is decided by their domain contract. The engine cannot derive that from arbitrary serialised models. An optional marking as a replaceable snapshot would be more suitable for that than an automatic heuristic.

### 9.7 Entry without an engine rewrite

A limited extension could initially consist of four building blocks:

1. **A session pump contract** that can be used for graphical and headless applications and makes bounded progress per call. The game server needs the same transport reliability, but no rendering dependency.
2. **A request manager** based on `send_request`/`take_response`, with a monotonic timeout, an owner scope and a result queue.
3. **Explicit engine phases** for result takeover, input/update and draw; suspending convenience RPCs are diagnosed in frame phases. A warning can be optional at first, later a strict development mode.
4. **A shared trace** that links physical input, request, state revision and frame.

9rooms can use this extension first for the phase check and attack. Existing synchronous APIs stay available for scripts, tools and tests; graphical screens migrate step by step. After every step it should be checked that the framework can also be used headless without a game loop.

**Acceptance criteria for this engine extension:** a simple example can start a request, draw a pending status and stay operable without writing a wait loop itself. Closing its view prevents later accesses to destroyed UI objects. A slow session stops neither other sessions nor the presentation. A fast drag is processed exactly once even if a response arrives at the same time.

### 9.8 Assessment

The tighter integration has a high benefit for AIML, because the errors visible so far affect several layers at the same time. The biggest gain lies in a **shared lifecycle for frame, request and input**. That makes a smooth flow the intended standard of the engine instead of depending on the discipline of every screen.

The costs are a larger public API contract and responsibility for lifetime, overload and cross-platform ordering. This expansion should therefore start small and explicit. It does not replace the fixes F1–F9; it provides the shared infrastructure on which these fixes can afterwards be implemented more easily and consistently.
