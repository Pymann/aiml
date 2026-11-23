# Concurrency: Parallel Execution

> **Thread-Safety of Runtime Primitives:** Which namespaces are implicitly thread-safe
> and which handles the AIML author must protect themselves is defined in
> **Chapter 47 (Thread-Safety Model)**.

```yaml
# AIML v1 uses Native OS Threads (1:1 mapping to pthreads/Windows Threads).
# They are heavyweight (stack per thread) but offer true parallelism on multi-core CPUs.

# 1. Spawning a Thread
# Syntax: spawn: [function_name, args...]
# Returns: thread_handle
- decl: [t1, thread_handle]
- set: [t1, { spawn: [worker_function, 10] }]

> ⚠️ **`spawn` arguments: do not pass containers directly.** The trampoline
> (`__aiml_spawn_<fn>`) builds its argument structure from the type of the
> EVALUATED expression, not from the target signature
> (`ast_compiler_expr.cpp`, `visitSpawn`). For `list<T>`/`map<K:V>` it therefore packs
> the struct **by value**, while under memV3 the function expects the
> handle pointer → `Function '__aiml_spawn_<fn>' verification FAILED:
> Call parameter type does not match function signature`. That breaks the build
> (so it is not a silent trap), but it costs time.
>
> Workable form: put the shared state into a class and pass `ref<T>`
> — demonstrated by `tests/test_chan_class_member.aiml` and
> `tests/test_lock_class_member.aiml`:
>
> ```yaml
> - class: { name: Snapshot, members: [[ids, list<string>]] }
> - def:
>     name: worker
>     args: [[snap, ref<Snapshot>], [shard, i32]]
>     body: [{ decl: [cnt, i32, snap.ids.len] }]
> - decl: [job, ptr<Snapshot>, { new: Snapshot }]
> - decl: [handle, thread_handle, { spawn: [worker, job, 0] }]
> ```
>
> Scalars (`i32`/`i64`/`f32`) and `chan<T>` work directly; a
> `ptr<T>` argument is TAKEN OVER by the thread (spec/61 §8, `movePtrArg`) —
> whoever keeps using the object passes `ref<T>`.

# 2. Thread API
# join(): Blocks until thread completes.
# Calling join() on an already-detached thread raises InvalidThreadStateException.
- call: [t1.join]

# detach(): Detaches thread (runs in background). Handle becomes invalid.
# After the thread exits, the OS reclaims its stack and thread-local storage.
- call: [t1.detach]

# Owner drop (spec/61 Class 4): a thread_handle neither joined nor detached is detached
# when its owner goes out of scope (never an implicit join); the handle block is freed.

# id: System thread ID. Property access — no call: required.
- decl: [id, i32]
- set: [id, t1.id]

# 3. Shared Memory & Locking (Mutex)
- decl: [mutex, lock]
- call: [mutex.init]

- call: [mutex.acquire] # Blocks if locked
# ... critical section ...
- call: [mutex.release]


# Example: Parallel Calculation
- def:
    name: worker
    args: [[id, i32]]
    return: void
    body:
       - print: ["Worker started"]
       - print: ["Worker done"]

- def:
    name: main
    args: []
    return: i32
    body:
       - decl: [t1, thread_handle]
       - decl: [t2, thread_handle]

       - set: [t1, { spawn: [worker, 1] }]
       - set: [t2, { spawn: [worker, 2] }]

       - print: ["Main execution continues..."]

       - call: [t1.join]
       - call: [t2.join]

       - return: [0]
```
