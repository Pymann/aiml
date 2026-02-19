# Concurrency: Parallel Execution

```yaml
# To run things in parallel, we need to create execution units.
# AIML v1 uses Native OS Threads (1:1 Mapping to pthreads/Windows Threads).
# This means they are "heavyweight" (Stack per thread) but offer true parallelism on multi-core CPUs.
# Future versions might introduce "Green Threads" or Coroutines (M:N scheduling).

# 1. Spawning a Thread
# Syntax: spawn: [function_name, args...]
# Returns: Thread Handle (i32 or specialized type)
- decl: [t1, thread_handle]
- set: [t1, { spawn: [worker_function, 10] }]

# 2. Waiting for a Thread (Join)
# Blocks current thread until t1 finishes.
# Essential to ensure work is done before exiting main.
- call: [t1.join]

# 3. Standard Thread Methods (API)
# -----------------------------
# join(): Blocks until thread completes.
- call: [t1.join]

# detach(): Detaches thread (runs in background). Resources cleaned up automatically on exit.
# Handle becomes invalid.
- call: [t1.detach]

# get_id(): Returns system thread ID (i32/u64).
- decl: [id, i32]
- set: [id, t1.get_id]

# cancel() (Future/Unsafe): Requests cancellation.
# - call: [t1.cancel]

# -----------------------------

# 3. Shared Memory & Locking (Mutex)
# If threads share data (not recommended, use channels!), we need locks.
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
       # ... do heavy work ...
       - print: ["Worker done"]

- def:
    name: main
    args: []
    return: i32
    body:
       - decl: [t1, thread_handle]
       - decl: [t2, thread_handle]
       
       # Start both in parallel
       - set: [t1, { spawn: [worker, 1] }]
       - set: [t2, { spawn: [worker, 2] }]
       
       - print: ["Main execution continues..."]
       
       # Wait for both
       - call: [t1.join]
       - call: [t2.join]
       
       - return: [0]
```
