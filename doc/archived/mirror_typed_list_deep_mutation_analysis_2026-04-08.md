# Mirror Typed List Deep-Mutation Analysis

Date: 2026-04-08

## Question

Do the existing AIML repro tests already cover the suspected mirror bug for `list<Effect>`-like fields?

Short answer: no, not the most likely failure case.

## Core finding

The currently most likely bug in the C++ mirror path is not a general `list<struct>` encode/decode defect, but the dirty detection for typed mirror objects with heap fields.

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L819), dirty state is detected only via a flat `memcmp(reg->obj_ptr, reg->shadow, reg->obj_size)`.

That is structurally insufficient for the following fields:
- `string`
- `list<T>`
- `map<K:V>`
- in general all heap fields in typed models

If only the content behind a stable pointer changes, but the outer inline structure stays the same, the mirror sees no dirty delta.

Typical problem case for `list<Effect>`:
- the list already exists
- `len` stays the same
- `capacity` stays the same
- `data*` stays the same
- but an existing element is changed in place, e.g. `effects-0.duration = 2`

Then the top-level `memcmp` very probably stays unchanged, although the semantic data is different.

## What the existing tests really cover

### 1. `tests/test_mirror_ws_list_mutation.aiml`

Covers:
- mutation of scalar fields via pointers to list elements, e.g. `p1.hp`, `p1.is_active`
- `remove` and `push` on a list
- `push` on a top-level list
- replacing a nested struct field (`root.match`)

Does not cover:
- in-place mutation of an already existing list element with an unchanged list structure
- example: `z0.cards-1.order_idx = 99` without `push/remove`
- example: `participant.flags-0.enabled = 1` without changing `flags.len`

Finding:
- the test proves that structural mutations are detected
- it does not prove that deep mutations behind a stable `data*` are detected

Reference:
- [tests/test_mirror_ws_list_mutation.aiml](../../tests/test_mirror_ws_list_mutation.aiml)

### 2. `tests/test_mirror_ws_list_mutation_poll.aiml`

Covers the same case as above, just more robust against timing/frame drain.

Does not cover:
- the same critical deep mutation case either

Reference:
- [tests/test_mirror_ws_list_mutation_poll.aiml](../../tests/test_mirror_ws_list_mutation_poll.aiml)

### 3. `tests/test_mirror_ws_events_tail_fields.aiml`

Covers:
- `list<model>` before further sync fields
- subsequent tail fields after lists
- `push` on `events` and `active_triggers`
- nested struct `reaction_state`

That is a good test against an old "tail fields after list" layout/decode bug.

Does not cover:
- in-place change of an existing `events` or `active_triggers` element
- example: `root.events-0.text = "changed"`
- example: `root.active_triggers-0.code = 99`

Reference:
- [tests/test_mirror_ws_events_tail_fields.aiml](../../tests/test_mirror_ws_events_tail_fields.aiml)

### 4. `tests/test_mirror_ws_9rooms_gameview_events.aiml`

Covers:
- real 9rooms models
- `GameView.events`, `active_triggers`, `reactable_triggers`
- `reaction_state`
- several `push` operations on lists of real view models

That is the closest existing test to your real GameView path.

Does not cover:
- in-place mutation of an existing entry in `events`
- in-place mutation of an existing entry in `active_triggers`
- in-place mutation of an existing entry in `reactable_triggers`

Examples that are currently not tested:
- `v.events-0.text = "changed"`
- `v.active_triggers-0.resolved = 1`
- `v.reactable_triggers-0.expired = 1`

Reference:
- [tests/test_mirror_ws_9rooms_gameview_events.aiml](../../tests/test_mirror_ws_9rooms_gameview_events.aiml)

### 5. `tests/test_mirror_rpc_model_ws.aiml`

Covers:
- typed RPC payload decode of a model with `list<string>`

Does not cover:
- mirror dirty tracking of an already registered typed sync object
- i.e. not the actual problem of periodic delta detection

Reference:
- [tests/test_mirror_rpc_model_ws.aiml](../../tests/test_mirror_rpc_model_ws.aiml)

## Analysis of the likely cause in the C++ code

### Dirty detection is shallow

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L819):

- dirty if `memcmp(obj_ptr, shadow, obj_size) != 0`

In [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L321) the shadow is afterwards likewise updated only via `memcpy`.

That means:
- only the inline structure of the root model is compared
- not the recursively serialised meaning
- not the contents behind heap pointers

### Typed encode/decode looks consistent for `list<struct>` itself

The actual runtime codec in [src/sys/ser_runtime.cpp](../../src/sys/ser_runtime.cpp) looks normal for `AIML_TD_LIST`:
- encode iterates over `count` and `elem_size`
- decode allocates the list anew and decodes recursively per element
- `list<struct>` and nested lists are explicitly provided for in the code

That argues against the blanket thesis "the mirror cannot serialise list<Effect>".

More precisely:
- the mirror probably serialises `list<Effect>` correctly whenever a delta is triggered at all
- the bug is probably that certain deep mutations do not trigger a delta at all

## Assessment of the existing test coverage

Current state:
- `list<struct>` roundtrip: partly well covered
- lists before/after tail fields: covered
- real 9rooms event lists in the mirror: covered
- deep mutation of an existing list element: not covered

So exactly the test is missing that either confirms or cleanly refutes the current main suspicion.

## Concrete gap for `list<Effect>`

If your real problem concerns `list<Effect>` or `list<PendingEffect>`, then at least one test of this pattern is missing:

1. The host registers a typed model with `list<Effect>` and at least 1 entry
2. The client registers a replica and confirms the initial state
3. The host does not mutate via `push/remove`, but a field of the existing element
4. The host only calls normal `sess.update` ticks
5. The client checks whether exactly this deep mutation arrived

Exemplary critical changes:
- `effects-0.amount = 7`
- `effects-0.kind = "poison"`
- `effects-0.remaining_turns = 2`
- `pending_effects-0.resolved = 1`

## Conclusion

The existing repro tests are not useless, but they do not yet cover the most likely failure case.

They show:
- typed `list<model>` decode works in several cases
- several real 9rooms list paths work for `push/remove` and field replacement

They do not show:
- that deep mutations in existing list elements with an unchanged list structure become dirty in the mirror

That is exactly where the current test gap is.

## Recommended next step

The next sensible test is a minimal WS mirror repro with:
- `model EffectLite`
- `model Root { effects: list<EffectLite> }`
- an initial state with 1-2 effects
- afterwards only `effects-0.some_field = new_value`
- no `push/remove`

If this test fails, the cause is practically confirmed:
- shallow dirty tracking in `mirror_runtime.cpp`
- not primarily `aiml_ser_encode/decode`

## Addendum from 9rooms runtime logs

The fresh 9rooms logs sharpen the finding considerably: there, not only a deep mutation fails, but a real mirror delta is discarded on the client during decode.

Concrete indications from the repro match:
- The attack of `Canopy Viper` goes through on the server side and fires `ON_HIT`.
- `execute_decoded_effect(...)` is executed for the hit path.
- `view_sync_creatures(...)` checks status `200` for the target `iid=36` directly afterwards.
- The host then sends a typed delta with `blob=2498`.
- The client discards exactly this packet with `ser_decode failed (schema mismatch?)`.

See:
- `test/9rooms/frontend_logs.txt` around the attack at `20:13:19`
- `test/9rooms/logs.txt` with
  - `absurd LIST count=2079857120 idx=14 off=336 esz=56`
  - `ser_decode failed (schema mismatch?) — discarding packet`

Important classification:
- The previous report focus on shallow dirty tracking remains plausible, but is no longer complete for the 9rooms case.
- In the 9rooms case there is hard runtime evidence for a decode/schema error on the replica client.
- This error already occurs several times before the `Canopy Viper` hit; the status effect only makes it visible.

So there are currently two realistic error classes that must be tested separately:
1. Deep mutations do not become dirty and are therefore never sent.
2. Sent typed deltas with several list/tail fields are decoded wrongly on the client under certain real schemas.

## Refinement of the target picture for new tests

The previous test gap is therefore split in two:

### A. Dirty tracking repro

A minimal test must still check:
- an existing `list<model>`
- no `push/remove`
- only a field mutation of an existing element
- expectation: the delta still arrives

This test answers the question:
- does the mirror recognise deep mutations as dirty at all?

### B. Decode/schema repro with a 9rooms-like structure

A second test must specifically recreate the 9rooms finding:
- several lists of the same element family or of similar layout complexity
- lists followed by further lists and tail fields
- nested struct fields between or after lists
- several updates in a row, not only the initial state
- the client must be able to keep decoding after every update

This test answers the question:
- does the replica decode tip over at a certain field layout or update pattern?

## Concrete test candidates

### 1. `test_mirror_ws_list_deep_element_mutation.aiml`

Purpose:
- an isolated repro for the original main suspicion `list<Effect>` / deep mutation

Minimal model:
- `EffectLite { code, amount, remaining_turns, key }`
- `Root { tick, effects: list<EffectLite> }`

Flow:
1. The host registers `Root` with 2 effects.
2. The client connects and confirms the initial state.
3. The host changes only `effects-0.remaining_turns` and `effects-1.amount`.
4. No list length changes.
5. The client must see the new field values.

A failure means:
- dirty tracking is shallow and misses deep mutations.

### 2. `test_mirror_ws_gameview_tail_decode_repro.aiml`

Purpose:
- an isolated repro for `absurd LIST count ... off=336 esz=56` / client decode discard

Minimal model, closely oriented on 9rooms:
- `EventLite`
- `TriggerLite`
- `ReactionLite`
- `PlayerLite` with at least one `list<CreatureLite>`
- `GameViewTailRepro` in this order:
  - scalar meta fields
  - `me`, `opp`
  - `event_seq`, `event_count`, `events`
  - `trigger_seq_counter`, `active_trigger_count`, `active_triggers`
  - `reaction_state`, `reactable_triggers`
  - 2-3 tail fields afterwards

Important:
- `EventLite` and `TriggerLite` should contain strings plus several `i32`, so that the element layout footprint is not too trivial.
- `CreatureLite` should carry at least one `map<string:i32>` or another heap structure, so that `PlayerLite` becomes more 9rooms-like.

Flow:
1. The host registers the initial state with 1 event/trigger/reactable each.
2. The client registers the replica.
3. The host performs several consecutive updates:
   - `events.push(...)`
   - `active_triggers.push(...)`
   - `reactable_triggers.push(...)`
   - `reaction_state.window_prompt = ...`
   - change a tail field
4. After every step the client must pass `update` + the field check successfully.

A failure means:
- not only dirty tracking, but real typed decode/schema damage in the replica path.

### 3. `test_mirror_ws_9rooms_gameview_events_mutate_existing.aiml`

Purpose:
- extend the existing 9rooms-like test specifically instead of only putting new minimal tests next to it

Changes compared to the current test:
- after a successful initial sync, no new `push`es first
- instead in-place mutations:
  - `v.events-0.text = "Round start updated"`
  - `v.active_triggers-0.resolved = 1`
  - `v.reactable_triggers-0.expired = 1`
  - `v.reaction_state.window_prompt = "Updated"`
- only afterwards additionally a `push` on all three lists

Why this test is important:
- it checks the same model family as the real 9rooms case
- it cleanly separates `in-place mutate` from `push`
- it shows whether the existing test is green only because it checks only structural growth

## Recommended order for the implementation

1. `test_mirror_ws_list_deep_element_mutation.aiml`
- the cheapest and sharpest test for the dirty tracking hypothesis

2. Extension of `test_mirror_ws_9rooms_gameview_events.aiml`
- the smallest additional effort on an existing real model base

3. `test_mirror_ws_gameview_tail_decode_repro.aiml`
- a targeted layout/decode repro, in case 1 and 2 do not make the packet discard visible on their own

## Updated conclusion

The existing report remains right at its core, but after the 9rooms logs it is too narrowly framed.

Current overall finding:
- A hard repro for deep mutation without a list structure change is still missing.
- In addition, there is now real evidence of a replica decode error for a 9rooms-like typed model with several list fields.
- For the bug hunt we therefore need at least two repros: one for dirty tracking and one for the decode/schema path.


## Test results after implementing the new repros

Executed tests:
- `tests/test_mirror_ws_list_deep_element_mutation.aiml`
- `tests/test_mirror_ws_9rooms_gameview_events.aiml`
- `tests/test_mirror_ws_gameview_tail_decode_repro.aiml`

Result:
- `test_mirror_ws_list_deep_element_mutation.aiml`: PASS
- `test_mirror_ws_9rooms_gameview_events.aiml`: PASS
- `test_mirror_ws_gameview_tail_decode_repro.aiml`: FAIL

Important interpretation:
- The pure hypothesis "in-place mutation of a list<model> element generally does not become dirty" is too coarse in its minimalist form. The new deep mutation test goes green.
- The extended 9rooms event/trigger list goes green as well. A pure `events/active_triggers/reactable_triggers` path is therefore not enough to reproduce the real bug.
- The new 9rooms-like `GameView` tail repro, however, fails reproducibly. The client sees meta fields, event lists and tail fields correctly, but `CreatureView.passives` is missing on the replica.

Concrete failure finding from the red test:
- `round=1`
- `me.creatures.len=2`
- `opp.creatures.len=2`
- `event_seq=2`
- `events.len=2`
- `active_triggers.len=2`
- `reactable_triggers.len=2`
- `reaction_state.window_prompt='Fenster geschlossen'`
- `interaction_mode=''`
- FAIL: `opp1 missing frozen passive`

That is very close to the 9rooms symptom in substance:
- The game state as such arrives.
- Event/trigger/tail parts arrive as well.
- But the `passives` map in `CreatureView` does not arrive reliably on the replica.

Updated priority for the bug hunt:
1. Investigate the `map<string:i32>` in the nested `list<CreatureView>` / `PlayerView.creatures` mirror path.
2. Focus on the real `GameView -> PlayerView -> list<CreatureView> -> passives` chain, no longer primarily on generic `list<Effect>` deep mutation.
3. Additionally check whether the bug is a replica decode problem or a host encode/layout problem for `map<string:i32>` in this concrete nesting.
