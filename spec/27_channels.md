# Concurrency & Channels

> **Status (2026-10-25): implementiert und durch Tests belegt.**  Dieses Kapitel
> war bis heute mit „(Future / Experimental)" ueberschrieben — das war falsch und
> hat die Architekturwahl verzerrt (Server-Entwurf ohne Channels, weil die Spec
> sie als unfertig auswies).  Belegt durch:
>
> | Test | deckt ab |
> |------|----------|
> | `tests/test_concurrency.aiml` | `chan<i32>` als Funktionsargument in einen `spawn`-Thread, `spawn`/`join` |
> | `tests/test_chan_class_member.aiml` | `chan<T>` als Klassen-Member, geteilt mit einem `spawn`-Thread |
> | `tests/test_chan_ptr_model_repro.aiml` | `chan<ptr<Model>>` transportiert den ZEIGER, nicht inline Modell-Bytes (Job/Result-Paar) |
> | `tests/test_sys_cpu_topology.aiml` | Pool-Groesse aus `sys.core_count`/`logical_thread_count`/`quota_thread_count` (spec/23 §4.1) |
>
> **Nicht** vorhanden (und auch nicht geplant, solange es keinen Bedarf gibt):
> `close`, `select`, `try_recv`, Timeout-`recv`, `len`/`cap`-Abfrage.  Ein
> Channel wird also nie geschlossen — ein Worker-Pool wird ueber einen
> **Sentinel-Job** beendet (Feld `stop: 1` im Job-Modell, siehe
> `test_chan_ptr_model_repro.aiml`), nicht ueber ein Channel-Close.  Fuer die
> Sichtbarkeit von geteiltem Zustand gilt Kapitel 47 (`db`/`Kv` sind
> Kategorie 1, `mirror_session` ist Kategorie 3).

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
