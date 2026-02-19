# Concurrency & Channels (Future / Experimental)

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
