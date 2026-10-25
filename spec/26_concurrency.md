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

> ⚠️ **`spawn`-Argumente: keine Container direkt übergeben.** Der Trampolin
> (`__aiml_spawn_<fn>`) baut seine Argument-Struktur aus dem Typ des
> AUSGEWERTETEN Ausdrucks, nicht aus der Zielsignatur
> (`ast_compiler_expr.cpp`, `visitSpawn`). Für `list<T>`/`map<K:V>` packt er
> deshalb das Struct **by value**, während die Funktion unter memV3 den
> Handle-Zeiger erwartet → `Function '__aiml_spawn_<fn>' verification FAILED:
> Call parameter type does not match function signature`. Das bricht den Build
> (also keine stille Falle), kostet aber Zeit.
>
> Tragfähige Form: den geteilten Zustand in eine Klasse legen und `ref<T>`
> übergeben — belegt durch `tests/test_chan_class_member.aiml` und
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
> Skalare (`i32`/`i64`/`f32`) und `chan<T>` funktionieren direkt; ein
> `ptr<T>`-Argument wird vom Thread ÜBERNOMMEN (spec/61 §8, `movePtrArg`) —
> wer das Objekt weiterbenutzt, übergibt `ref<T>`.

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
