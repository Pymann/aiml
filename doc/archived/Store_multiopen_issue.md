# Store Multiopen Issue

## Summary

Yes, `Store_multiopen_issue.md` hits the core.

There are currently several reproducible bugs in the persistent AionDB path. The first visible and most unambiguous bug sits in `Store<T>`, so the file name still fits.

Current state:

- Two `db.open(..., {persist: true, path: ...})` calls with the same `name` and `path`
- in the same process
- apparently do not deliver a consistently shared live state

In addition, new reopen tests show:

- `Store<T>` already fails on the first persistent `insert`
- `Kv` and `TimeSeries` pass the same-process reuse test
- but `Kv` and `TimeSeries` lose their state across separate runs

So the finding is:

- `Store<T>` is currently the most visibly broken persistence path
- `Kv` and `TimeSeries` are better locally within the same process
- but real persistence across several runs is not reliable there either

## Repro

New test:

- [test_aiondb_persist_reuse.aiml](../../tests/test_aiondb_persist_reuse.aiml)

Flow:

1. `players_a = db.open("persist_reuse_players", {persist: true, path: db_path})`
2. `players_b = db.open("persist_reuse_players", {persist: true, path: db_path})`
3. Insert via `players_a`
4. Check `players_b.len()` and `players_b.get(eid)`

Actual result:

```text
FAIL store_reuse_len base=0 got=0
```

Expectation:

- After the insert via `players_a`, `players_b.len()` should see the same new state.

## Cross-check

New test:

- [test_aiondb_persist_reuse_kv_ts.aiml](../../tests/test_aiondb_persist_reuse_kv_ts.aiml)

Result:

```text
AionDB kv/ts persist reuse PASS
```

That narrows the bug down cleanly:

- `Kv` multiopen in the same process: ok
- `TimeSeries` multiopen in the same process: ok
- `Store<T>` multiopen in the same process: broken

## New reopen tests

New tests:

- [test_aiondb_store_reopen_write.aiml](../../tests/test_aiondb_store_reopen_write.aiml)
- [test_aiondb_store_reopen_read.aiml](../../tests/test_aiondb_store_reopen_read.aiml)
- [test_aiondb_kv_reopen_write.aiml](../../tests/test_aiondb_kv_reopen_write.aiml)
- [test_aiondb_kv_reopen_read.aiml](../../tests/test_aiondb_kv_reopen_read.aiml)
- [test_aiondb_ts_reopen_write.aiml](../../tests/test_aiondb_ts_reopen_write.aiml)
- [test_aiondb_ts_reopen_read.aiml](../../tests/test_aiondb_ts_reopen_read.aiml)

Results:

```text
FAIL store_reopen_write_id=-1
AionDB kv reopen write PASS
AionDB ts reopen write PASS
FAIL kv_reopen_read_mode=
FAIL ts_reopen_read_len=0
```

Interpretation:

- `Store<T>` already fails on the first persistent write attempt
- `Kv` and `TimeSeries` write successfully within one run
- but on the second, separate run the state does not come back

## Additional finding: the `path` option from variables is very suspicious

The new reopen and reuse tests use:

- `decl: [db_path, path, "/tmp/..."]`
- then `path: db_path` in `db.open` / `db.open_kv` / `db.open_timeseries`

After the write tests, however, the expected directories in `/tmp/` do not exist at all:

- `/tmp/aiml_test_aiondb_kv_reopen_20260319/` is missing
- `/tmp/aiml_test_aiondb_ts_reopen_20260319/` is missing
- `/tmp/aiml_test_aiondb_store_reopen_20260319/` is missing

At the same time, older tests with literal paths still work and create files in `/tmp/`, e.g.:

- `/tmp/aiml_test_deck_persist_ops/deck_persist_ops.kvwal`
- `/tmp/aiml_test_card_heap_persist/card_heap_persist.kvwal`

That makes an additional compiler/builtin suspicion very plausible:

- The `path` option from variables (`path: db_path`) is not passed through to the runtime correctly.

Then the symptoms are explained cleanly:

- `Store<T>` fails visibly, because `aiml_db_store_open_persist(...)` can return `nullptr` on open/mmap problems.
- `Kv` and `TimeSeries` initially look successful, because they return a handle even when `open()` fails and silently mask errors.

## Expected semantics according to the runtime

[db_runtime.cpp](../../src/sys/db_runtime.cpp#L279) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L282) explicitly state:

- same `name + path`
- same process
- reuse the existing persistent handle

Relevant:

- `g_persistent_stores_by_name`
- `registry_key = bp + nm`
- `if (it != g_persistent_stores_by_name.end()) return it->second;`

The test shows, however, that this expected reuse semantics does not reach the AIML code for `Store<T>`.

## Most likely interpretation

The bug is not only in the WAL/checkpoint behaviour, but more generally in the persistent open/handle path.

Because:

- The repro already fails in the same process.
- It fails right after `insert`, i.e. before any real reopen in a new process.
- `db.checkpoint` is not needed for it.

## Possible C++ causes

### 1. The `Store<T>` open fails and becomes visible in the AIML code only indirectly

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L289) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L350), `aiml_db_store_open_persist(...)` can return `nullptr` on errors:

- `open(db_path, ...) < 0`
- `mmap(...) == MAP_FAILED`
- the initial `mmap` after `ftruncate` fails

That fits the observed symptoms well:

- `players.len()` on `nullptr` simply yields `0` in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L497) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L500)
- `players.insert(...)` on `nullptr` yields `-1` in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L381)

Exactly this pattern is what the new tests see:

- `len == 0`
- `insert == -1`

### 2. `Store<T>` could additionally have an `elem_size` mismatch

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L382), `aiml_db_store_insert(...)` also returns `-1` if `elem_size != s->elem_size`.

That is the second plausible explanation for `FAIL store_reopen_write_id=-1`.

Relevant here:

- `db.open` computes `elemSize` in [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1773) to [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1843)
- `Store.insert` computes `elemSize` separately in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L803) to [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L823)

If these two paths do not emit exactly the same size, `insert == -1` is the direct consequence.

### 3. `Kv` and `TimeSeries` silently mask `open()` errors

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L618) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L643) and [db_runtime.cpp](../../src/sys/db_runtime.cpp#L908) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L933):

- `open(...)` can fail
- an `AimlKv*` or `AimlTimeSeries*` is returned anyway
- the handle is even entered into the global persistent registries

That is dangerous, because the error then looks like an empty store instead of a real open error.

That fits the new reopen results very well:

- the write test within one run can work against the in-memory state
- `db.checkpoint` is a silent `0` when `wal_fd < 0` or `log_fd < 0` in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L645) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L663) and [db_runtime.cpp](../../src/sys/db_runtime.cpp#L937) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L944)
- the second process then simply sees nothing any more

### 4. The `path` option from variables could get lost in the compiler/builtin

The relevant hand-over path is:

- preprocessing of the option bags in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L95) to [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L136)
- use of `ctx.dbOpenBasePathVal` in [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1829) to [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1953)

Suspicion:

- `path: <VariableExpr>` is formally recognised,
- but the value passed at runtime is apparently not the expected `/tmp/...` string.

Indications:

- new tests with a `path` variable create no files at the target location
- older tests with a literal path work
- `path` is a type of its own with interning in [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L113) to [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L121) and [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L296) to [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L304), so exactly there lies a plausible integration bug

### 5. `Kv`/`TimeSeries` replay has no hard error diagnostics at all

Even when the file is opened, reliable error checks are missing at critical places:

- `write(...)` return values are ignored
- `read(...)` aborts only lead to a silent `break`
- `fsync(...)` return values are not evaluated
- `mkdir(...)` errors are ignored

Relevant:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L538) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L574)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L644) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L663)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L920) to [db_runtime.cpp](../../src/sys/db_runtime.cpp#L944)

## Not yet covered

Still open:

- `Store<T>` cross-run reopen after a working first write
- `Store<T>` `update`/`delete` after a successful persistent open
- persistent column layout store
- typed `Kv` cross-run reopen
- diagnosis whether `Kv`/`TimeSeries` really do not write or just run silently with an invalid FD

These follow-up cases are secondary as long as:

- `Store<T>` already fails on the first persistent insert
- and `Kv`/`TimeSeries` reopen across separate runs does not find the written state

## Concrete fix plan

### 1. Verify and harden the `path` hand-over first

Affected places:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L95)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1829)

Approach:

- instrument `path: <VariableExpr>` in the `db.*open` option bag specifically
- temporarily log the actually received `base_path` in the runtime open
- check whether `/tmp/...` really arrives for `path: db_path`, or empty/default

Probably necessary change:

- make option bag parsing for `path` more robust
- make sure that `visitExpression(u->operand)` reliably yields an `i8*` for `path` variables
- if needed, explicitly normalise the `path` value as a `char*`/interned path before passing it

### 2. Stop silently masking persistent `open()` errors in the runtime

Affected places:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L618)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L908)

Approach:

- If `open()` fails, do not return a seemingly valid `Kv`/`TimeSeries` handle anyway
- Make errors visible:
  - return `nullptr`, or
  - at least log a clear `stderr` error and do not register the handle

Benefit:

- The difference between "empty" and "persistence open broken" becomes visible immediately
- Follow-up errors such as `checkpoint == 0` despite an invalid FD disappear

### 3. Check the `Store<T>` open against the `insert` path for `elem_size` consistency

Affected places:

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1773)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L803)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378)

Approach:

- for the same model type, log the computed `elemSize` in `db.open` and `Store.insert` against each other
- check whether `packed_size` vs. `DataLayout` diverges anywhere
- on a mismatch, either:
  - force both paths onto the same size source, or
  - emit an explicit diagnostic in the error case instead of just `-1`

### 4. Diagnose the `Store<T>` persistence path cleanly on open/mmap

Affected place:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L289)

Approach:

- temporarily add logging on every early error path:
  - `open(db_fd)` failed
  - `mmap(existing)` failed
  - `mmap(initial)` failed
  - header invalid / `elem_size` does not match

Benefit:

- immediately separates a `nullptr` open from an `elem_size` mismatch
- explains the current `len == 0` / `insert == -1` finding unambiguously

### 5. Give `Kv`/`TimeSeries` replay and checkpoint real error codes

Affected places:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L538)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L644)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L920)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L937)

Approach:

- check `write`, `read`, `fsync`, `mkdir`, `ftruncate`, `lseek` for return values
- on errors:
  - log clearly
  - stop having `checkpoint` blindly return `0`

### 6. Then rerun the tests in this order

1. [test_aiondb_store_reopen_write.aiml](../../tests/test_aiondb_store_reopen_write.aiml)
2. [test_aiondb_persist_reuse.aiml](../../tests/test_aiondb_persist_reuse.aiml)
3. [test_aiondb_kv_reopen_write.aiml](../../tests/test_aiondb_kv_reopen_write.aiml)
4. [test_aiondb_kv_reopen_read.aiml](../../tests/test_aiondb_kv_reopen_read.aiml)
5. [test_aiondb_ts_reopen_write.aiml](../../tests/test_aiondb_ts_reopen_write.aiml)
6. [test_aiondb_ts_reopen_read.aiml](../../tests/test_aiondb_ts_reopen_read.aiml)

Recommended priority:

- first diagnose the `path` hand-over and the `Store<T>` open/insert
- then remove the `Kv`/`TimeSeries` open error masking
- only then follow up on replay/checkpoint details
