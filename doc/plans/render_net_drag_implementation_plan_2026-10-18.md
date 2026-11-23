# Implementation plan: render loop, network, drag & drop (2026-10-18)

Basis: [`9rooms_v2/doc/reports/client_performance_analysis_2026-10-18.md`](../../../9rooms_v2/doc/reports/client_performance_analysis_2026-10-18.md)
(merged with [`doc/analysis/render_network_drag_review_2026-10-18.md`](../analysis/render_network_drag_review_2026-10-18.md)).
Order as specified by the user: **first the simple things and the spec contradictions,
then the engine.** Existing interfaces are **not changed, only
extended.**

---

## Implementation status (2026-10-18, night)

| Item | Status | Evidence |
|---|---|---|
| S0.1 rAF contradiction in `spec/44_1` | **done** | 4 places + price table + "Open: the rAF path", cross-reference `doc/wasm_frame_loop.md` |
| S0.2 Asyncify whitelist §9.4 | **done** | actual state (global `-sASYNCIFY=1`) documented, whitelist/JSPI as an open item |
| S0.3 z order | **done** | `hit_test` code comment corrected + new `spec/44_6` "Hit order" |
| S0.4 `session.update` "once per frame" | **done** | `spec/44_5` §5: blocking calls undermine the rule |
| S2.1 cancel edge | **done** | `spec/44_0` §2.2 normative + §2.4 + §8.2, `spec/44_6` abort pattern; runtime 3 paths; `tests/test_input_touch_cancel.aiml` green, **mutation probe red** |
| S2.2 `cancel_request` clears the queue | **done** | `spec/44_5` §7.2 + warning block; `tests/test_mirror_cancel_request_drains.aiml` green, **mutation probe red** |
| S1.2 `poll_cooldown` | **done** | `poll_forced` beats the cooldown |
| S1.3 `confirm_phase` response | **done** | `consume_phase_ack` + `BoardScreen.show_action_error` (float pool) |
| S1.4 `apply_drag` 1×/frame | **done** | `last_drag_frame` against `engine.frame()` |
| S1.5 z levels of the drop areas | **done** | −8…−1 in creation order → unchanged look, deterministic hits |
| S1.6 request lifecycle | **partial** | board load: `cancel_request` on timeout + `-1` branch; lobby: `-1` branch + `send_request==0` guard. Deck editor **deliberately unchanged** (already handles `-1` via the empty model, see below) |
| **Client guard for the cancel** | **done** | `board_drag_controller.apply_drag` + deck editor drop in `main_menu_app`: without this guard the engine correction would have no effect in 9rooms |
| S1.1 prod build comparison | **config done, measurement blocked** | `exodus-client-o2.json` (new, nothing overwritten) — see the blocker below |
| S2.3 press gesture machine | **done (engine), 9rooms switch-over open** | `spec/44_6` "pick-up at the press point"; additive `engine.update_drag_gesture` + `engine.get_drag_source`, `update_drag` unchanged; `tests/test_prefab_drag_gesture.aiml` green, **mutation probe red** (pick at the cursor → exactly the error message). 9rooms does NOT use it yet — that is a behaviour change of the core interaction and needs a play test |
| S2.4 z dirty flag | **open** | |
| S2.5 separate snapshot/clean-up | **open** | |
| Stage 3 | **open** | |

Three new regression tests, all with a passed mutation probe (first put the bug
back in, saw FAIL, reverted, saw PASS) — without this probe it
would not be proven that the tests hit the respective bug at all:
`tests/test_input_touch_cancel.aiml`, `tests/test_mirror_cancel_request_drains.aiml`,
`tests/test_prefab_drag_gesture.aiml`.

Regression after all changes (`--only input|prefab|mirror`, Xvfb :99):
**10 / 32 / 42 OK, 0 failures of my own.** The only red entry in all three
runs is `tests/closures/run.sh` — an **untracked** directory (`??`) from someone
else's uncommitted session (which also changed `compiler.cpp`/`compiler.h` and ~40 specs).
Not my construction site, which is why **nothing was committed** either.

**Correction to the plan (found while implementing):** `spec/44_0` §2.4 **already**
specifies a complete gesture machine — `input.drag_active/started/ended`,
`drag_origin_x/y`, `drag_delta_*` and a configurable threshold
(`drag_set_threshold`, default 5 px), fed from the **press origin**.
`engine.update_drag` uses **none** of it and instead picks level-triggered
with the current position. S2.3 is therefore much smaller than planned: not "build a
gesture machine", but "connect the existing one to the prefab drag"
(additively as a second call next to `update_drag`). The cancel edge is already
connected there (`input.drag_cancelled`).

**Blocker for S1.1 (one command, then the measurement runs):** `9rooms_v2/__exodus_cache`
is empty — the project-local emsdk installation is missing. For an `output_type: wasm`,
exodus insists on **exactly this** project-local path; an absolute reference to
the existing emsdk under `aiml/__exodus_cache/git/emsdk` is ignored when linking
(reproduced twice; the compile step went through each time). So it
needs the emsdk setup from the `git_packages` once — a
toolchain download in the GB range. I did **not** start it unasked
at night; `exodus-client-o2.json` is derived 1:1 from `exodus-client.json`
(same emsdk setup) and differs only in
`optimization: 2`, `-sASSERTIONS=0`, `-g1` and the absence of all
`AIML_MEM_DIFF*` variables. As soon as emsdk is installed once:

```bash
exodus build -c exodus-client-o2.json     # writes to out/client_o2/, does not touch out/client/
```

Then compare the fps log of the same scene (lobby + board entry) against today's build
— that is the measurement for ranks #1 and #2.

**Second correction:** S1.6 was too broad for the deck editor. Its three
`take_response` places deliberately check only `== 0`, because a `-1` leaves the target model
empty → `success != 1` → warning path; the comment there says so explicitly.
That is right; not touched.

---

## Guard rails

Binding for every item below:

1. **Additive.** New calls, new fields, new files. No changed
   signature, no removed call, no changed return format. Where a
   behaviour change is unavoidable, it is listed below **explicitly** as
   such (section "Behaviour changes" — exactly four, all bug fixes).
2. **Spec → tests → code** (working principle 3). No code before spec text and
   test case, unless the user approves it in the individual case.
3. **The spec must be true** (principle 2). Stage 0 is therefore mandatory and comes
   before everything else.
4. **Build** only with `exodus` from the `exvenv`; for aiml itself **always**
   `exodus build --all`. Never the compiler binary directly on individual files.
5. **One** `run_all_tests.sh` at a time. Never sanitizer and suite in parallel.
   Logs persistently into the session dir, not `/tmp`. `cleanup.sh` before every run.
6. No routine full acceptance runs on my own, only the affected tests.
7. No interactive demos in test loops (they block the suite).
8. Terse comments; for `.aiml` annotations mostly none.

---

## Stage 0 — spec contradictions (spec only, no code)

Four places where the spec says something different from the implementation.
No test case needed: acceptance is "the spec text is verifiably true against the code".

### S0.1 — `spec/44_1`: the browser loop is not rAF

**Finding:** four places claim `emscripten_set_main_loop_arg` +
`requestAnimationFrame` (lines 19, 95, 212/218, 314); the implementation
runs `while (!quit) { web_loop_tick(); emscripten_sleep(16); }`.

**The direction has already been decided** — not by me: `doc/wasm_frame_loop.md`
declares the sleep loop **"load-bearing — do not simplify this back to
`emscripten_set_main_loop`"** and proves via an isolation table that a
WS send that returns to the rAF scheduler silently kills the loop. So:
**adapt the spec to the implementation**, not the other way round.

**Change:**
- switch the platform mapping line and §4.1 to the manual Asyncify loop.
- §7.1: replace "VSync via `requestAnimationFrame` (always active)" with the
  true statement including its price: **frame period = work + 16 ms**, so never 60 fps,
  no compositor synchronisation, `setTimeout` throttling in a background tab.
- §9.3 (frame limiter browser) accordingly.
- cross-reference to `doc/wasm_frame_loop.md` as the rationale.
- a new sub-item "Open: the rAF path" with the preconditions (all
  suspending calls out of the frame) — so that the target state stays
  documented instead of standing there as a false present.

### S0.2 — `spec/44_1` §9.4: Asyncify whitelist

**Finding:** the spec names `-sASYNCIFY -sASYNCIFY_IMPORTS=[engine_swap_internal]`;
real builds use a global `-sASYNCIFY=1` without `ASYNCIFY_ONLY`.
**Change:** document the actual state, mark the whitelist as an open
optimisation item (it is part of S1.1/rank #2).

### S0.3 — z order only takes effect after drawing

**Finding:** `aiml_prefab_hit_test` claims in its comment "Iterate in reverse
z_order (topmost first)", but iterates the instance list; that is only true because
`prefab_draw_scene` sorts the list as a side effect. `set_z` does not sort.
**Change:** (a) correct the comment in the code; (b) `spec/44_6` states
explicitly that `set_z` only takes effect for **hit and drag checks** after the
next `draw_scene`, and that newly created instances stand at the end of the list until then.
That is unpleasant, but true — and S2.4 changes it afterwards
deliberately and visibly.

### S0.4 — `session.update` "once per frame" cannot be kept

**Finding:** `spec/44_5` §5 says "called once per frame"; `send_and_await` pumps
internally per 2 ms tick, i.e. dozens of times per frame, in the middle of the render pass.
**Change:** add a note to §5 that the blocking
convenience calls (`send_and_await`, `connect`) pump the session themselves and
thereby undermine this rule; point frame applications to
`send_request`/`take_response`. Preparation for K1 (§4.5 of the report).

---

## Stage 1 — simple and local (9rooms + build configuration)

No engine code. Each item can be accepted on its own.

### S1.1 — make the production build measurable *(additive: new file)*

Do **not** rewrite `exodus-client.json` — that overwrites the
shipped artefacts in `out/client/`. Instead, **new**:
`9rooms_v2/exodus-client-prod.json` with `"name": "client_prod"` (its own
output directory `out/client_prod/`), containing:

- `"optimization": "2"`, `ASSERTIONS=0`, `-g1`
- **without** `AIML_MEM_DIFF*`
- an `ASYNCIFY_ONLY` list, if it can be determined; otherwise without it for now

**Acceptance:** both builds side by side, the same scene (lobby + board entry),
compare the fps log and the `client.wasm` size. Only once the number stands is the
decision about the prod config a decision and not a guess.

**Risk that must be named:** `-O0 → -O2` can make latent UB in the generated
code visible. Hence a separate build, a separate directory, nothing
overwritten. Do not deploy anything beforehand.

### S1.2 — do not set `poll_cooldown` through the triggering click

`poll_board_state` sets `poll_cooldown = 30` on press/release — also for the
click that itself requested `board_poll = 60` (§2.4 of the report). Fix:
a "refresh was requested" flag beats the cooldown. Additionally
frame counter → monotonic timestamps.
**Acceptance:** stopwatch on the check button; the ~1 s dead time disappears.

### S1.3 — evaluate the `confirm_phase` response + local pending

Today the response is discarded and only `board_poll = 60` is set. Fix:
evaluate the result, show a rejection on the board, an immediate local
"being sent" — clearly separated from the confirmed phase change.
**Acceptance:** blind test without a console: sent / waiting / rejected / not
clicked are distinguishable.

### S1.4 — `apply_drag` once per frame

After an action, `main_menu_app.frame` calls `board.render` a second time,
i.e. `apply_drag` twice with the same edges (§3.3). Fix: a frame latch as
already with `deck_click_latch`, only for the drag entry.
**Acceptance:** trigger an action while the mouse stays pressed → no
second grab.

### S1.5 — explicit z levels for the drop areas *(pure AIML)*

`spell_stack`, `trap_band_ali`, `perm_spell_band`, `mana_band_ali`, the five
`combat_zones`, `decks_ali`, `soul_self/others` all have z = 0 and are separated
only by the creation order (§3.5). Fix: a documented
level assignment (e.g. bands 1, zones 2, decks 3, soulmages 4). No
engine change, takes effect immediately.
**Acceptance:** dropping on an overlap reproducibly hits the same
area, independent of `init`.

### S1.6 — close the request lifecycle at the call sites

`send_request == 0` and `take_response == -1` are partly not handled
(`pump_board_load` step 2 only checks `== 0`; `poll_consume` lets `-1` past the
gate and keeps rotating with silent data loss). In addition,
`cancel_request` is missing on the board load timeout and on view changes.
**Acceptance:** a protocol error ends the pending state visibly instead of running into the
300-frame timeout.
**Caution:** this only fully takes effect with S2.2 — `cancel_request` today does not clear
the already queued response.

---

## Stage 2 — engine, additive

### S2.1 — a cancel edge that is not a release *(most important item)*

**Bug:** `touchcancel` hangs on the same handler as `touchend` and produces
`pend_release[0] = 1`; `bin_touchstart` does the same with two fingers. An
aborted touch therefore runs into `handle_drop` and **triggers a game move**
(§3.11). Affects every AIML app with touch drag.

**Spec first** (`spec/44_0`):
- extend §7 with the cancel edge: a new query `input.mouse_cancelled(btn)`
  next to `mouse_pressed`/`mouse_released` — same frame semantics.
- fix the contract: **a cancel ends the gesture and never produces a
  tap or drop.** `touchcancel` and the two-finger transition deliver cancel,
  **not** release.
- §8.2 additively: `input.post_touch_cancel()` as an injection call, frame contract
  from §8.3 unchanged.
- `spec/44_6`: `engine.update_drag` aborts on cancel (snap-back like
  `cancel_drag`), without a drop transition.

**Test** (new, `tests/test_input_touch_cancel.aiml`): grab a card, inject a
cancel, check: `get_dragged() == null`, position = origin, **no**
drop transition observed. Desktop + WASM path (emsdk node, see
`wasm-headless-testing`).

**Code:** its own `bin_touchcancel`, its own `pend_cancel` latch,
`aiml_prefab_update_drag` evaluates it. The two-finger path in
`bin_touchstart` sends cancel instead of release.

**Addition:** the blur/visibility reset (`visibilitychange`, `blur`) goes through
the same cancel path — otherwise one swaps a hanging card for an
unintended move.

### S2.2 — `cancel_request` clears the queue too

`cancel_request` only deletes the ID from `outstanding`; an already queued
response stays there and is not reachable via `take_response` (ID check
first, then `-1` without removal). The client drains `poll_rpc` nowhere,
but already uses `cancel_request`. Orphaned events run against the
per-peer limit from `spec/44_5` §7.2 (4096 / 64 MiB → **the peer is disconnected**).

**Spec:** add to `spec/44_5` §7.2 at `cancel_request`: "removes the
registration **and** an already queued response of this ID".
**Test:** request → let the response arrive (`update`) → `cancel_request` →
`has_rpc()` is back at its initial value.
**Code:** `rpc_take_matching` + `aiml_free_rpc_event` in the cancel — both
exist, they are just not called. Signature unchanged.

### S2.3 — press gesture machine *(additive next to `update_drag`)*

`update_drag` is level-triggered, picks with the *current* position and has
no threshold phase (§3.7, §3.10, §3.12). Instead of changing it:

**New** `engine.update_gesture(mx, my, btn, rel, cancel, threshold)` →
state (`none`/`candidate`/`dragging`/`dropped`/`cancelled`), which internally
- resolves and holds the press target at the **press position** and the last drawn
  scene,
- only becomes a drag after `threshold`,
- delivers exactly one end: tap **or** drop **or** cancel.

`engine.update_drag` stays valid unchanged; 9rooms switches, other apps
do not. The board's 12 px logic (today after the drag start, in
screen coordinates) is thereby replaced, not repaired — the threshold
belongs in design coordinates and before the start.

**Spec:** `spec/44_6` new section; extend `spec/44_0` §8 with the
`post_touch_cancel` interplay.
**Test:** a fast drag across a simulated frame pause; a held button
only later moves over a card; a click with a moving mouse hits the pressed
card.

### S2.4 — z sorting via a dirty flag

`set_z` marks the scene as unsorted; hit and drag checks sort
on demand instead of waiting for the next `draw_scene`. Fixes the
one-frame lag and the "new instances are on top" after a reload (§3.4).
Signatures unchanged.
**Test:** `set_z` + an immediate `hit_test` in the same frame hits the new
order.

### S2.5 — separate snapshot and clean-up

`web_loop_tick` takes the input snapshot **after** `update` → edges lag one frame
behind the live-written mouse position (§3.12). Moving `input_update`
forward is wrong: the native path clears the latches there first thing.

So two internal steps (take over the snapshot / clean up events), called by the
frame driver at the right places. **Public interface
unchanged** — `input.update()` stays what it is.

**Danger:** `spec/44_0` §8.3 fixes the injection frame contract ("an
injected edge fires exactly once, same contract on every platform").
This item therefore comes **last** in stage 2 and only with the three
existing `test_input_injection_*` green.

---

## Stage 3 — big, not in one night

Only the order; details in the report (§4.5, §5):

1. Board RPCs asynchronous, based on a typed request handle (K2) with
   an owner scope and a generation.
2. Native WS/WSS receive incremental + backpressure (precondition for any
   frame budget, K3).
3. Artwork asynchronous with a placeholder (K4).
4. Keep board views via `iid` instead of a full reload.
5. A purely reading player projection from `MatchService.get`, then drop the second
   round trip; `source_iid` + expected revision into the requests.
6. Session pump contract + frame contract in `engine.run` (K1, together with the
   `ctx` parameter from `spec/44_1` "Open" — additive four-argument version).
7. rAF path, only once 1–3 have the frame free of suspending calls.

---

## Behaviour changes (the four exceptions to "additive only")

The interface stays the same everywhere; for these four the *behaviour* of
existing calls changes. All four are bug fixes, none was
documented as guaranteed:

| Item | What changes | Who could rely on it |
|---|---|---|
| S2.1 | `touchcancel` / two fingers deliver cancel instead of release | only code that uses the bug (none — the move is unwanted) |
| S2.2 | `cancel_request` additionally removes the queued response | code that picks it up afterwards via `poll_rpc` — not present in the client |
| S2.4 | `set_z` takes effect immediately for hit/drag instead of from the next draw | code that plans for the lag — nowhere evidenced |
| S2.5 | edges and mouse position come from the same frame | the injection contract `spec/44_0` §8.3 — **must** be preserved, hence last |

The decision on this rests with the user; my proposal is to make all four ungated,
because a gate here only keeps two behaviours alive at the same time (as
with the two memV3 gates that silently turned the `free:` operator into a no-op).

---

## What I do tonight — in this order

Without asking back, because it falls under principle 3 (spec+tests first) and overwrites
nothing:

1. **Stage 0 completely** (S0.1–S0.4). Pure spec and comment work, makes the
   spec true again. Afterwards `exodus build --all`, to be sure that the
   doc changes touched nothing.
2. **S2.1 spec + test** — write the cancel edge into `spec/44_0`/`44_6`,
   create `tests/test_input_touch_cancel.aiml`. The test is **red** at first;
   that is the proof that it hits the bug.
3. **S2.2 spec + test** (small, sharply delimitable).
4. **S2.1 + S2.2 code**, then the affected tests plus the three
   `test_input_injection_*` and the `test_mirror_*`. No full suite.
5. **S1.4, S1.5, S1.2** in 9rooms — local, one small intervention each.
6. **S1.1**: create and build `exodus-client-prod.json` (takes a while), fps log
   against today's build.

That is enough for the night; S2.3 and S2.5 come afterwards, because they both touch the
gesture machine and I do not want to leave them half open.

## What I do *not* do

- No deploy, no touching of `exodus-client.json` or `out/client/`.
- No full suite and no acceptance runs on my own (principle from
  `no-long-acceptance-runs`); only the affected tests.
- Do not start any stage 3 items — they first need K2 or a
  server decision.
- No `rm -rf __aiml_cache` (exodus invalidates by itself).

## What needs a decision (default if none comes)

1. **Switch the prod config?** I only build the comparison config and measure.
   Switching `exodus-client.json` and the deploy stay with you.
2. **The four behaviour changes ungated?** Default: yes, ungated (rationale
   above). Say stop, and I will build S2.1 behind a spec flag — but I would
   not recommend it.
3. **S2.3 `update_gesture` in the engine or first in the board?** Default: engine,
   additive — because `cosmos` and every future touch app would otherwise inherit the same bug.
   Board-local would be faster, but the third copy of the same logic.
