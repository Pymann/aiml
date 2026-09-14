# Drops-on Suite Hardening — Status & Handoff (2026-09-14)

Autonomous overnight run. Goal: get the full 9rooms + aiml suites green with `AIML_LIFETIME_DROPS=1`
(the deployed config). This documents what's fixed, what remains, each remaining fail's root, and
its relevance to the decided **v2 reference-semantics** memory model
(`doc/aiml_v2_memory_model.md`).

## Fixed this session (committed, aiml sanitizer suite green throughout)

All are drops-on-gated (drops-off path untouched):

1. **Container string ownership** — `map<string:*>` and `list<string>` OWN their strings (dup on
   insert, decref on remove/scope-drop; `aiml_map_free` walks dense `[0,size)`). Fixed the reported
   **mana / event-log** regression. (commit history + volatile report §§8-10)
2. **Value-model view escapes** — deep-copy at genuine escape points only (view-sharing preserved,
   so in-place mutation the game relies on still works):
   - producer returns a view (`return at(..)`/`obj.field`/accessor) → deep-copy;
   - `set [local, view]` → deep-copy;
   - `idx:[list,i,model]` index-store → free-old + deep-copy, **deep-copy-to-temp-FIRST** for the
     self-aliased write-back idiom `idx:[c,i,at(c,i)]` (store_entry_at/remove_entry_at);
   - `cascade_class` instances excluded from the owned-box drop (borrow a shared global cascade).
   → cleared match_play, creature_costs, creature_ability_reaction, target_soulmage_or_creature,
     trigger_per_match, mkp_effect_cascade, mkp_multi_trigger. (report §11)
3. **Model string-field defaults are decref-safe** — unset string fields default to an IMMORTAL
   refcounted `""` (`aiml_str_empty()`), literal defaults (`[artwork_id, string, ""]`) are dup'd.
   → add_life_clamp, buff_expiry, x_mana_cost, ability_text, cost_processor, typed_card_persist.
   (report §12, commit 8acff58)
4. ~~**AIML string literals are immortal** (header-carrying)~~ — **TRIED AND REVERTED** (afdd6cb →
   revert ed30b58). Making ALL literals header-carrying was NET-NEGATIVE: it broke ~20 previously-
   green integration tests (mana/card-type comparisons — "primordial cannot be played from hand", a
   card-type/string comparison misfiring on the header-carrying literal) while only fixing 3 crashes
   (listptr_inplace_kv, on_death_zone_fallback, any_deck_draw). **Lesson: a broad codegen change MUST
   be gated with the FULL 9rooms suite before commit** — the sanitizer suite + a small sample passed
   and hid the regression. The 3 crashes it fixed are the "literal assigned into a model string
   field → decref segv" class; they need a NARROW fix instead (dup the literal only at the
   member-store into a string field), or v2.

Net (after revert): ~12 drops-on crash sites cleared; suite ~125/149 (baseline restored).

## Remaining fails — categorized

### A. Value-model / nested-model memory corruption (→ v2 resolves by design)
These are deep, game-specific corruptions that do NOT reproduce in isolation (a standalone
nested-model list<Model> push+writeback+free probe passes — `_mem_sanitize/nested_model_list_deepcopy`).
They are precisely what v2 reference-semantics removes (no sharing → no double/garbage free).

- **card_states**, **enchantment_picker** (rc=139): `ZoneEntry.buffs` (`list<Buff>`) element's
  `source_id`/node-45 string is GARBAGE at free. Buff = string + `list<CardState>` + `list<AttrMod>`
  + nested `TransformSnap`. Not a stride bug (elem_size 112 matches); the element's string slot holds
  uninitialised/garbage after the build→push(add_buff)→`idx:[zone,i,ent]` writeback chain.
- **trigger_stage3_reaction**, **trigger_stage4_chain** (rc=139): `PlayerState` node-11 (`list<string>`)
  element VALUE is `0x15354524e` = the bytes `{magic 'STRN', refcount 1}` — a heap string's HEADER
  read as the element pointer (8-byte off / UAF: the element buffer overlaps a reused string alloc).
- **ceremony_cost** (rc=139): `SpellCard` map-element string garbage (map<*:Model> variant).
- **match_e2e_mirror** (rc=139): drops-on crash (was masked by the 60s timeout; ~3-4 min runtime).
- **effect_codes_real**, **view_filter** (rc=134): double-free (tcache) — value-model sharing class.

Recommendation: do NOT chase these individually via retrofit — they're the shared-heap class v2
eliminates. If a v1 stopgap is wanted, a defensive `aiml_str_decref` that validates the pointer is a
plausible heap address before reading `-8` would turn these SEGVs into safe no-ops, but it MASKS the
underlying corruption (a workaround, against the root-cause-first preference).

### B. Effect-path exception (not a memory crash)
- **all_in_zone_buff** (rc=139 via `__cxa_throw`): a C++/AIML exception thrown inside `do_apply_buff`
  → abort. Not a free/decref crash; a runtime error in the buff apply path. Needs its own trace
  (independent of the memory model).

### C. Dice-nondeterminism (game-test issue, independent of memory model) — RESOLVED
`roll_soulmage_dmg` draws from `mat.dice_queue` when seeded, else `math.random_int` → flaky.
Fix = seed `mat.dice_queue` with values whose `die % span` yields the asserted (usually max) result.
For service-layer e2e tests (match lives in KV, no direct `mat` handle) seed via the
`matches.inject_dice(match_id, dice)` test-hook (match_service_base.aiml:382).

- **creature_damage**: SEEDED (`mat.dice_queue.push, 2`) — green.
- **activate_ability**: SEEDED via `matches.inject_dice` (value 3 → dlo=1, span=3; die 2 → 3 dmg) +
  the battle-size life fix below. GREEN (commit on 9rooms main, 2026-09-14).

### D. Real logic / setup fails — RESOLVED
- **activate_ability**, **trigger_on_phase_start**, **match_service**: hard-coded soulmage start
  life 20; correct value is `soulmage_life_for(skirmish)=30` (battle-size based). Fixed the stale
  expectations. activate_ability + trigger_on_phase_start GREEN. **match_service** still hits a
  category-A double-free (`aiml_ser_free_decoded`) under drops-on AFTER the life check passes — the
  D-part is fixed, the A-crash remains (see below).
- **mana_pool**: `build_mana_loadout` pushed the SAME mana `card_id` twice into the mana deck, which
  `deck_service` now rejects ("each card only once", deck_service.aiml:116) → `mana_deck_id` empty →
  no mana in the initial draw → "trap only in spell or end phase". Fixed: two distinct mana cards +
  find-mana-in-hand-by-type instead of a fixed index. GREEN.
- **auth_e2e_mirror**: NOT a logic fail — the test PASSES (register+login+validate), it just takes
  ~3-4 min (real websocket mirror round-trips) and the 120s per-test timeout scored it a false
  TIMEOUT (rc=124). Fixed: `run_all_tests.sh` raises the per-test floor to 420s for `*_e2e_mirror`.

**Reclassified to A** (turned out to be memory corruption, not C/D):
- **reaction_state** (rc=1): "recent_target iid=<garbage> expected v1" — the iid string is garbage
  bytes (the `list<string>` element-holds-header-bytes corruption class, same as trigger_stage3/4).
- **mana_pick**: flaky between rc=4 ("drain-submit rc=0 exp 1", a corrupted-value read) and rc=134
  (abort) — value-model corruption, not a stable logic assertion.

## Notes for next session
- Suite runs drops-on by default now (`run_all_tests.sh` exports `AIML_LIFETIME_DROPS=1`).
- The fail LIST varies run-to-run (dice flakiness) — categorize by rc, not by presence.
- Per-test `aiml -r` recompiles the game each time (slow). Bundling many assertions into one
  compilation (the test-overhaul TODO) would speed the suite a lot; safe only once tests don't share
  mutable global/DB state — deferred until the crash class is closed.
- Debug recipe: `MALLOC_CHECK_=3` + gdb aborts at the true double-free and symbolizes JIT frames;
  `ns=__aiml_sernodes_X` names the model. Beware malloc address reuse in plain free-logs (only
  MALLOC_CHECK/valgrind are reliable).
