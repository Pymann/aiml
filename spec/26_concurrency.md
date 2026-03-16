# Concurrency: Parallel Execution

> **Thread-Safety der Runtime-Primitives:** Welche Namespaces implizit thread-safe
> sind und welche Handles der AIML-Autor selbst absichern muss, ist in
> **Kapitel 47 (Thread-Safety-Modell)** definiert.

```yaml
# AIML v1 uses Native OS Threads (1:1 mapping to pthreads/Windows Threads).
# They are heavyweight (stack per thread) but offer true parallelism on multi-core CPUs.

# 1. Spawning a Thread
# Syntax: spawn: [function_name, args...]
# Returns: thread_handle
- decl: [t1, thread_handle]
- set: [t1, { spawn: [worker_function, 10] }]

# 2. Thread API
# join(): Blocks until thread completes.
# Calling join() on an already-detached thread raises InvalidThreadStateException.
- call: [t1.join]

# detach(): Detaches thread (runs in background). Handle becomes invalid.
# After the thread exits, the OS reclaims its stack and thread-local storage.
- call: [t1.detach]

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
