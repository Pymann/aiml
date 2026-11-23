# AIML Snapshot/Memory Dump Mode

## Problem

The current debug cycle is too expensive:

1. insert logs
2. recompile
3. restart processes
4. run the test manually
5. evaluate the feedback
6. repeat

Especially with mirror/sync/runtime bugs this is too slow. Often one only sees after several iterations which part of the state tips over.

## Idea

A dedicated AIML diagnostic mode that continuously writes readable state images during runtime.

Not:

- raw debug logs at arbitrary places
- constant manual instrumentation

But:

- structured dumps
- derived from existing serialisation and hot reload building blocks
- specifically for models, globals and mirror-relevant runtime objects

## Why this is an obvious step

AIML already has essential foundations for it:

- serialisation already exists
- mirror needs structured state representations
- hot reload already works with compiled code/state boundaries
- the compiler knows types, models, globals and symbol tables

That means: one does not have to start from zero. It is rather about bringing existing runtime information into an explicit diagnostic channel.

## Target picture

A mode such as:

```text
aiml --snapshot-runtime client/main.aiml
```

or at runtime:

```text
AIML_SNAPSHOT_MODE=1
```

Then the runtime writes snapshots at fixed intervals or at defined hooks, for example:

- after every `sess.update`
- after every mirror delta
- after RPC receipt
- after a model mutation
- after every frame or tick

## Form of the output

What matters: not just "more logs", but readable memory images.

Example:

```yaml
tick: 1842
source: client/net.aiml
event: mirror.update
objects:
  g_view:
    round: 2
    event_seq: 13
    event_count: 3
    reaction_open: 0
    reaction_kind: ""
    reactable_count: 3
  g_client_info:
    player_name: "nikolai"
    deckset_id: "..."
  g_selected_deckset_id: "..."
```

Alternatively JSON or NDJSON. For diffing, NDJSON would often be more practical.

## Main benefit

With this one could:

- see the exact moment at which a field tips over
- compare server-side and client-side snapshots directly
- read mirror problems as a state diff instead of a text log
- automate tests: "snapshot N must contain field X = Y"
- instrument considerably less by hand

Especially for bugs like broken `reaction_kind` strings, this would be much more helpful than scattered `print` lines.

## Minimal variant

The simplest usable version would not be a generic memory dump, but a focused runtime snapshot mode:

- only registered mirror objects
- only known globals
- only serialisable AIML types
- output after certain triggers

That would be technically more realistic than a complete dump of all LLVM or heap structures.

## What would probably be easy

Relatively feasible:

- dumping models with `sync:` fields
- dumping AIML globals if they are serialisable
- snapshot hooks at places such as `mirror.update`, `register_id`, `send_rpc`, `next_event`
- separate files per tick or an NDJSON stream

Why:

- these objects are semantically "visible state" anyway
- they fit well with the existing serialize/mirror thinking

## What would be harder

A real generic memory image mode is harder:

- arbitrary pointers
- non-serialisable runtime types
- raw internal containers
- volatile C++ objects outside the AIML model
- memory structures close to LLVM directly

That would quickly become unreadable and fragile.

Therefore the goal should not be "dump all bytes", but:

- semantic state snapshots
- with an optional raw mode only for special cases

## Relation to hot reload

Hot reload is interesting here, because it already has to think about stable boundaries between:

- code
- symbols
- global state
- reinitialisation

A snapshot mode could benefit from that:

- enumerate the same known globals/symbols
- use the same type context
- address the same registered models

I would nevertheless treat it as a separate diagnostic feature and not couple it too tightly to hot reload.

## Concrete proposal

### Phase 1: mirror snapshot mode

A focused mode only for mirror debugging:

- dump client-side replica objects
- dump server-side original objects
- dump relevant globals
- write snapshots before and after `sess.update`

That would speed up current debug cycles immediately.

### Phase 2: runtime snapshot API

A small runtime API such as:

```text
snapshot.dump("after_update")
snapshot.dump_obj("view", g_view)
snapshot.dump_var("selected_deckset", g_selected_deckset_id)
```

Then tests and the runtime can use the same mechanism.

### Phase 3: compiler-supported auto snapshots

A compiler flag that automatically instruments certain hooks:

- after `set` on `sync:` models
- after `mirror.update`
- after `register`
- after `register_id`

Then one no longer has to insert logs manually.

## Format recommendation

I would start with NDJSON:

- easy to write
- easy to diff
- easy to evaluate with `rg`, `jq` or Python
- good for long runs

Example:

```json
{"tick":1842,"side":"client","event":"mirror.update.after","view":{"event_seq":13,"reaction_kind":"","reactable_count":3}}
```

## Why this is better than the current process

The current process is:

- slow
- invasive
- heavily manual
- hard to reproduce

A snapshot mode would be:

- faster
- more comparable
- more testable
- closer to the actual state machine of the system

## Recommendation

Yes, such a mode makes sense and is probably considerably more valuable than even more ad-hoc logs.

What matters, however, is the sharpness of the goal:

- not "arbitrary memory images"
- but "readable semantic state snapshots"

For AIML I would start with a mirror/runtime snapshot mode, because that is where the current pain is greatest and most of the structured data already exists.

## Technical design

### Affected C++ files

The most sensible entry points in the current AIML code are:

- [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp)
  That is where the mirror lifecycle hooks such as `aiml_mirror_session_update`, `register`, `register_id`, `next_event` and `send_rpc` sit.
- [mirror_runtime.h](../../src/sys/mirror_runtime.h)
  For the runtime API of the snapshot mode.
- [serialize_impl.cpp](../../src/core/serialize_impl.cpp)
  The existing structure serialisation can be reused for readable snapshot payloads.
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
  If an AIML-side API such as `snapshot.dump(...)` is to be offered.
- [context.h](../../src/core/context.h)
  That is where type info, class info and mirror metadata such as `sync`/`rpc` fields already live.

### Why exactly there

In the mirror path, the most important hooks are already centralised:

- `aiml_mirror_session_update(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L794)
- `aiml_mirror_session_register(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L828)
- `aiml_mirror_session_register_id(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L837)
- `aiml_mirror_session_register_id_typed(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L915)
- `aiml_mirror_session_next_event(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L1005)
- `aiml_mirror_session_send_rpc(...)` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L1084)

That is exactly the path in which the current debugging need is greatest.

### Proposal for a new runtime component

New file:

- `src/sys/snapshot_runtime.{h,cpp}`

Tasks:

- activate the mode via env or API
- manage the snapshot output file
- write NDJSON
- offer helper functions for strings, events and structured payloads

Minimal API:

```cpp
void aiml_snapshot_init_from_env(void);
void aiml_snapshot_emit_json_line(const char* json_line);
void aiml_snapshot_emit_kv(const char* phase, const char* side, const char* event, const char* payload_json);
int  aiml_snapshot_enabled(void);
```

Optionally later:

```cpp
void aiml_snapshot_emit_model(const char* phase, const char* side,
                              const char* event, int32_t net_id,
                              const char* type_name, const char* json_payload);
```

### Recommended activation

At first only via the environment:

```text
AIML_SNAPSHOT_MODE=1
AIML_SNAPSHOT_FILE=/tmp/aiml_snapshot.ndjson
AIML_SNAPSHOT_SIDE=client
```

Optionally:

```text
AIML_SNAPSHOT_FILTER=mirror.update,register_id,next_event
```

That is simpler than pulling new CLI syntax through the whole compiler right away.

## Hook points

### Phase 1: mirror runtime hooks

I would first instrument only these hooks:

- before `aiml_mirror_session_update(...)`
- after `aiml_mirror_session_update(...)`
- after `aiml_mirror_session_register_id(...)`
- after `aiml_mirror_session_register_id_typed(...)`
- directly in `aiml_mirror_session_next_event(...)`
- directly in `aiml_mirror_session_send_rpc(...)`

Benefit:

- one sees the state exactly at the transitions at which the replica state changes
- no broad instrumentation needed in the rest of the compiler

### Concrete snapshots at these hooks

`update.before`

- session state
- queue lengths
- known registered net IDs

`update.after`

- the same again
- plus snapshots of the registered typed objects

`register_id.after`

- net_id
- obj_size
- whether typed or untyped
- initial object snapshot

`next_event`

- event type
- net_id
- peer ID
- queue status before/after the pop

`send_rpc`

- net_id
- method
- arg_kind
- arg or model blob length

## What exactly should be snapshotted

### Session metadata

From `AimlMirrorSession` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L121):

- `is_host`
- `next_net_id`
- number of `peers`
- number of `by_net_id`
- number of `event_queue`
- number of `rpc_queue`

### Registered mirror objects

From `AimlMirrorRegistered` in [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp#L86):

- `net_id`
- `obj_size`
- `is_owner`
- `manual_sync`
- `has_decoded`
- typed/untyped

And additionally for typed objects:

- the serialised JSON representation of the current object

### Mirror events

From `AimlMirrorEvent`:

- `type`
- `net_id`
- `peer_id`

### RPC events

From `AimlRpcEvent`:

- `method`
- `net_id`
- `arg_kind`
- `arg`
- `arg_blob_len`

## How the object snapshot can be produced

### Typed mirror objects

This is the most important case and also the most realistic one.

If `AimlMirrorRegistered.ser_nodes != nullptr`, the object already has typed serialisation metadata. That is exactly where the snapshot mode should start.

Pragmatic way:

1. a new runtime helper in `ser_runtime.cpp` or `snapshot_runtime.cpp`
2. convert the typed object to JSON via the existing ser information
3. write the result as a string into NDJSON

That is better than raw bytes.

### Untyped objects

For untyped registrations, at first only dump metadata:

- `net_id`
- `obj_size`
- a hex preview of the first N bytes

That is enough for phase 1.

## NDJSON schema

I would deliberately start small.

Example:

```json
{
  "ts_ms": 1712460734123,
  "side": "client",
  "phase": "update.after",
  "event": "mirror_session_update",
  "session": {
    "is_host": 0,
    "peer_count": 1,
    "registered_count": 2,
    "event_queue_count": 0,
    "rpc_queue_count": 0
  },
  "objects": [
    {
      "net_id": 1,
      "typed": true,
      "owner": false,
      "type_name": "GameView",
      "json": {
        "event_seq": 13,
        "event_count": 3,
        "reaction_open": 0,
        "reaction_kind": "",
        "reactable_count": 3
      }
    }
  ]
}
```

Minimal mandatory set:

- `ts_ms`
- `side`
- `phase`
- `event`

The rest can vary depending on the hook.

## Compiler-side extension later

Once phase 1 works, an AIML API can be added.

Example:

```yaml
- call: [snapshot.dump, "after_update"]
- call: [snapshot.dump_obj, "view", g_view]
```

That would require adjustments in [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

At first, however, I would avoid that and only build the runtime hooks. Otherwise one ends up with a bigger compiler topic again before the first benefit is there.

## Rough implementation order

1. create `snapshot_runtime.{h,cpp}`
2. build env-based activation and the NDJSON writer
3. instrument `mirror_runtime.cpp` at the six hook points
4. output session metadata plus event/RPC dumps
5. output typed mirror objects as JSON snapshots
6. only afterwards discuss an AIML API or auto-instrumentation in the compiler

## Risk assessment

Low risk:

- NDJSON writer
- mirror hooks
- session/event metadata

Medium risk:

- producing typed object JSON stably and cheaply at runtime

Higher risk:

- generic snapshots for arbitrary non-typed objects
- automatic compiler instrumentation at the `set`/`sync:` mutation level

## My recommendation

Do not build an all-encompassing snapshot mode right away.

Instead:

- mirror runtime first
- typed registrations first
- NDJSON first
- no new AIML syntax in phase 1

That quickly gives a tool that is considerably more useful for the current `reaction_kind`/mirror state bug than the current log-and-restart cycle.
