# spec/61 ownership — state 2026-09-11 (feature OFF by default, `AIML_LIFETIME_DROPS=1` opt-in)

## Fixed & validated (feature ON)
All secured with probes natively + valgrind + WASM; aiml suite baseline 363/1; memsan gate green.

1. **Crash class `list<Model>`** (WASM client crash): the same model in two containers / `at`→push
   shared inner heap → double free. Fix: `list<Model>.push` deep-copies the element, unless the
   source is a provably dead owned local (then move). `ListImpl::pushDeepCopyModel` +
   push handler routing. Probe `model_into_two_containers`.
2. **Container field from a borrow** (`dat.produces = req.produces`, `persist_from_req` crash): shallow
   memcpy → shared buffer. Fix: `emitContainerFieldDeepCopy` (zero leak). Probe
   `member_container_deepcopy`.
3. **`map.put` with a declared string key** (`mana_choice` regression): a declared interp key was
   not consumed → decref freed the map key. Fix: `consumeLocalDrop(key/val)`. Probe
   `map_put_declared_key`.

## Design clarification (your question)
9rooms treats value containers as values (`get_copy→push`, `at`→push, get-modify-`set`); shared
identity via `list<ptr<Model>>`; **0** "mutate-after-push". ⇒ deep-copy-on-push is semantically
correct, **no `push_at`/`push_from` needed**.

## New tool: `AIML_TRACE_SYMBOL`
Compile-time env (`=name1,name2` or `1`/`all`), zero overhead when unset. Emits a runtime
`printf` at **DECL** (initial value) and **DROP** (value before the free): string→content, list→len+addr,
model→addr. Shows exactly when a symbol is moved (no DROP) or freed. In WASM
the `[aiml-trace]` lines end up in the browser console.

## OPEN: `artwork_id` UAF in the client (feature ON)
Symptom: `@art:XX��` / `Invalid UTF-8 0x95` — `card.artwork_id` reads garbage in the render loop. **OFF
works.** NOT reproducible headless: ~12 repros (data path, push/deep-copy, at views,
member store, complex nested `list<Model>`, a real `rebuild_library` replica — native AND WASM) all
green. ⇒ client-runtime specific. Strongest suspicion: **Asyncify × drop in wire receive**
(`net_list_cards`: no-init `res` with a drop, filled via `send_and_await`, which SUSPENDS; `req` dropped by P2a
at `return res`).

### How we pin it live (tracer)
I added at the read point in `client/card_full_populator.aiml`:
`- decl: [artdbg, string, card.artwork_id]` (marked DEBUG, easy to remove).

1. Build the client with both envs (with sudo, behind `sudo`):
   `sudo AIML_LIFETIME_DROPS=1 AIML_TRACE_SYMBOL=artdbg ./run_web.sh --build`
2. Run it and watch the browser console: `[aiml-trace] DECL artdbg (string) = "…"` per
   rendered card. The **first frame with garbage** + what happened before it (reload? RPC?) shows the
   cause. Send the console log here.

## Environment
`sudo …run_web.sh --build` had set 1611 files in `__exodus_cache`/9rooms to **root** →
build/emcc permission errors. Fixed via `chown`. In future build WASM **without** sudo if possible, otherwise
repeat the chown.

## Deferred
`map<K:Model>` **value** insert does not deep-copy yet (MapImpl put: 2 store sites). 9rooms uses
no `map<*:Model>` → not client-relevant; a small follow-up package analogous to `pushDeepCopyModel`.
