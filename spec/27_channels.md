# Concurrency & Channels

> **Status (2026-10-25): implemented and covered by tests.**  Until that date this
> chapter was headed "(Future / Experimental)" — that was wrong and skewed
> architecture decisions (a server design without channels, because the spec
> presented them as unfinished).  Covered by:
>
> | Test | covers |
> |------|--------|
> | `tests/test_concurrency.aiml` | `chan<i32>` as a function argument into a `spawn` thread, `spawn`/`join` |
> | `tests/test_chan_class_member.aiml` | `chan<T>` as a class member, shared with a `spawn` thread |
> | `tests/test_chan_ptr_model_repro.aiml` | `chan<ptr<Model>>` carries the POINTER, not inline model bytes (job/result pair) |
> | `tests/test_sys_cpu_topology.aiml` | pool size from `sys.core_count`/`logical_thread_count`/`quota_thread_count` (spec/23 §4.1) |
>
> **Not** available (and not planned as long as there is no need):
> `close`, `select`, `try_recv`, `recv` with timeout, `len`/`cap` queries.  A
> channel is therefore never closed — a worker pool is shut down with a
> **sentinel job** (field `stop: 1` in the job model, see
> `test_chan_ptr_model_repro.aiml`), not by closing the channel.  Visibility of
> shared state is governed by chapter 47 (`db`/`Kv` are category 1,
> `mirror_session` is category 3).

```yaml
# Channels are typed pipes for communication between threads/coroutines.
# Syntax: chan<Type>

- decl: [ch, chan<i32>]

# Sending
# Syntax: call: [ch.send, value]
# Blocks if channel is full (unbuffered)
- call: [ch.send, 42]

# Receiving
# Syntax: set: [val, { call: [ch.recv] }]
# Blocks if channel is empty
- set: [val, { call: [ch.recv] }]

# Buffered Channels
# - decl: [buffered_ch, chan<i32>]
# - call: [buffered_ch.init, 10] # Buffer size 10

# Spawning Coroutines/Threads (Required for channels to make sense)
# - spawn: [worker_function, args...]
- call: [spawn, worker, ch]

# Select Statement (Multiplexing)
# - select:
#     - case: { call: [ch1.recv] } -> [var1]
#       body: ...
#     - case: { call: [ch2.send, 1] }
#       body: ...
```
