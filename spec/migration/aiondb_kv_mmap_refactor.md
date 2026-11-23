# AionDB Kv mmap Refactor — Plan and API Contract

**Date:** 2026-04-29 (plan), 2026-04-30 (done)
**Status:** ✅ **DONE** — all 7 implementation steps deployed, 161/0/19 tests green
**Affects:** all `Kv<...>` stores (typed, string, blob, fixed)
**Compatibility:** old `.kvwal` files are invalidated (format version 1 → 2)
**Final spec:** [`spec/31_db_integration.md`](../31_db_integration.md) §6.2-§6.7

## Implementation summary

| Step | What | Status |
|---|---|---|
| 1 | File header (`AIMLKVWL` magic, version, flags) + format check | ✅ |
| 2 | mmap setup with sparse-extended reserve (16 MB initial, mremap grow) | ✅ |
| 3 | Index-based reads (kv->index instead of kv->map for persistent) | ✅ |
| 4 | Strings with `\0` in the WAL → zero-copy mmap pointer (version bump to 2) | ✅ |
| 5 | `kv.evict` via `madvise(MADV_DONTNEED)`, index intact | ✅ |
| 6 | `kv.checkpoint` with temp file + atomic rename (crash-safe) | ✅ |
| 7 | Decoded cache as a bounded LRU (MAX 1024) | ✅ |

Tests: 161 passed (158 existing + `test_db_kv_evict.aiml` + newly added
`test_db_kv_checkpoint.aiml` + `test_db_kv_lru.aiml`), 0 failed, 19 skipped.

## Motivation

Currently `db.open_kv()` scans the whole WAL and copies all values
into a `std::unordered_map<string, vector<uint8_t>>`. With stores of
hundreds of MB / GB (e.g. tensor trajectories in per-file
training) this leads to RSS pressure and, together with several
stores open at the same time, to OOM.

Goal: read values directly from an `mmap`'d file, RAM footprint = only
a small index `key → (offset, length)`.

## Scope

All four KV storage modes are migrated at the same time:

| Mode | Trigger | Today | After the refactor |
|---|---|---|---|
| Typed | `ser_nodes != nullptr` | blob in `map`, decoded in `decoded_cache` | blob in mmap, decoded LRU-bounded |
| String | `val_size==0`, `!blob_only` | `strdup`'d `char*` in `map` | mmap pointer (with null terminator in the WAL) |
| Blob | `val_size==0`, `blob_only` | raw bytes in `map` | mmap pointer (zero-copy) |
| Fixed | `val_size>0` | raw bytes in `map` | mmap pointer |

## File format (NEW)

### Header (32 bytes, start of file)

```c
struct AimlKvWalHeader {
    char     magic[8];       // "AIMLKVWL"
    uint32_t version;        // 1
    uint32_t flags;          // bit0: typed, bit1: string, bit2: blob, bit3: fixed
    int64_t  val_size;       // fixed-mode element size (0 for string/typed/blob)
    uint64_t wal_used;       // last written byte offset (best-effort)
};
static_assert(sizeof(AimlKvWalHeader) == 32);
```

`wal_used` is:
- updated on `kv_close` / `kv_checkpoint` (msync on the first page)
- NOT written along on normal `set`s (performance)
- reconstructed on open via a crash scan from `header.wal_used`

### Records (immediately after the header)

```
SET:        [op=1:u8][key_len:u32][key:bytes][val_bytes:u32][val:bytes]
DELETE:     [op=2:u8][key_len:u32][key:bytes]
SET_BLOB:   [op=3:u8][key_len:u32][key:bytes][val_bytes:u32][val:bytes]
```

**Change compared with today (string mode):**
- In string mode `kv_wal_write_set` now writes `strlen(str)+1` bytes
  including `\0`. This makes `mmap_base + offset` directly a valid
  null-terminated `char*`.
- All other records keep their format, only prefixed by the new
  header.

### Magic + version check

On open:
1. File does not exist / is empty → write header, `ftruncate` to the reserve size
2. File exists, magic matches → current version: use it
3. File exists, magic does not match → **fatal log + abort**
   (old format version 0 without a header → inform the user, delete manually)
4. File exists, magic matches, version unknown → fatal log

→ old `.kvwal` files cannot be migrated; the user deletes `aiondb/`.

## Memory model

### mmap reserve

On open the file is stretched via `ftruncate` to a reserve size
(default `KV_MMAP_RESERVE = 16 GB`, sparse — no disk usage). The
whole reserve window is mapped with `MAP_SHARED|MAP_NORESERVE`:

```c
ftruncate(fd, KV_MMAP_RESERVE);
void* base = mmap(nullptr, KV_MMAP_RESERVE,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_NORESERVE,
                  fd, 0);
```

- Pages are allocated lazily on first touch
- RSS only grows with the data volume actually used
- No `mremap` case distinction in the hot path (reserve large enough)
- 16 GB virtual per store is acceptable (even 1000 stores = 16 TB virtual, no problem)

### Append = memcpy

Writing no longer uses **any** `write()` syscalls:

```c
char* dst = (char*)kv->mmap_base + kv->wal_used;
memcpy(dst, &record, record_size);
kv->wal_used += record_size;
```

Entries land directly in the page cache region. The kernel syncs lazily.

If `wal_used + record_size > KV_MMAP_RESERVE`, either:
- the reserve is doubled (`ftruncate` + `mremap`) — phase 2
- or `log.fatal` (phase 1)

### Index

```c
struct ValueLoc {
    uint64_t offset;     // offset in mmap_base
    uint32_t length;     // bytes of the value record
};
std::unordered_map<std::string, ValueLoc> index;
```

`ValueLoc::offset` points to the **value block** (not the start of the record).
This makes `mmap_base + offset` directly a valid pointer to the data.

### Open path

1. `open(wal_path, O_RDWR|O_CREAT)`
2. If empty: write the header, `ftruncate(fd, RESERVE)`
3. If present: read the header, check magic/version
4. `mmap(RESERVE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_NORESERVE)`
5. Build the index: linear scan from `sizeof(header)` up to the first
   invalid op byte (crash recovery) or up to `header.wal_used`
6. Done — no data copied, only the index filled

## API contract

### What stays unchanged (all AIML calls)

```yaml
- decl: [kv,  Kv<string:string>, { call: [db.open_kv, "name", {persist: true}] }]
- call: [kv.set, "k", "v"]
- decl: [v,   string,            { call: [kv.get, "k"] }]
- decl: [n,   i32,                { call: [kv.len] }]
- decl: [has, i32,                { call: [kv.has, "k"] }]
- call: [kv.delete, "k"]
- call: [kv.clear]
- call: [kv.set_blob, "k", buf]
- decl: [bl,  buffer<u8>,         { call: [kv.get_blob, "k"] }]
- call: [kv.iter]
- call: [kv.checkpoint]
- call: [kv.evict]
- call: [kv.reload]
```

All the calls above have **identical signatures and semantics** to before.

### What changes: pointer lifetime

| Operation | Valid until (today) | Valid until (new) |
|---|---|---|
| `kv.get(k)` string mode | next `kv.set(k,...)` or `kv.delete(k)` | `kv.checkpoint()` or `kv.clear()` |
| `kv.get_blob(k)` blob mode | next `kv.set_blob(k,...)` | `kv.checkpoint()` or `kv.clear()` |
| `kv.get(k)` typed mode | next `kv.set(k,...)` or `kv.delete(k)` | unchanged (decoded cache) |
| `kv.iter()` keys | until `kv.set/delete/clear` | until `kv.set/delete/clear` (unchanged) |

**Important relaxation:** pointers from `kv.get(k)` are NO longer invalidated by
`kv.set(k, other_value)`. Background: old records
stay in the WAL, the new value is appended, the index points to the
new one — but the old pointer still points into a valid
mmap region.

→ **All code that is correct today stays correct.** The new
guarantee is stronger, not weaker.

### What changes: performance profile

| Operation | Today | New |
|---|---|---|
| `db.open_kv()` (large) | a lot of RAM, slow | little RAM, about as fast |
| `kv.get()` | hash + memcpy/strdup | hash + pointer return (faster) |
| `kv.set_blob(large value)` | malloc + memcpy + write | memcpy into mmap (faster) |
| `kv.evict()` | clear() of the map (free) | `madvise(MADV_DONTNEED)` (instant, RSS drops) |
| `kv.reload()` | full WAL replay | index rebuild (equally fast) |
| `kv.checkpoint()` | rewrite + fsync | unmap + truncate + rewrite + remap (slower, but rare) |

### What changes: crash behaviour

- Today: crash → the last incomplete record stays in the WAL; on the
  next open it is detected during replay (read fails) and
  aborted. Data loss = that one incomplete entry.
- New: the same. Open scans up to the first invalid op byte. Sparse
  pages after the last valid record are 0 (through ftruncate),
  which is a natural sentinel.

## API for the 9rooms project — migration notes

### What 9rooms will notice

1. **Old `.kvwal` files are unusable** — on open, the magic check
   aborts. The user must delete the `aiondb/` directory
   or the specific `.kvwal` files.

2. **Database reinit no longer hangs** — `kv.evict()` is now
   instant instead of expensive.

3. **Hot reload from another process** — `kv.reload()` behaviour
   stays: the index is rebuilt. But MAP_SHARED mode sees
   new bytes from other writers automatically. `reload()` is only still
   needed when the writer has created **other stores** or
   existing ones have been **shortened** (compaction).

4. **`get()` pointer lifetime is relaxed** — existing code
   stays correct. New code may hold pointers longer.

### What 9rooms does not need to change

- No AIML source change
- No recompile needed (libaiml ABI stays)
- No build config change

### Recommended migration step for 9rooms

```bash
# Before the first run with the new libaiml:
rm -rf aiondb/*.kvwal
# Optional: repopulate all stores
```

For stores with non-reconstructible data (user state,
game progress) — **export manually** before the update and rewrite them after
the update.

## Implementation — sub-steps

### Step 1: file header and format migration
- `AimlKvWalHeader` struct + constants
- `aiml_db_kv_open_persist`: write/read the header, magic check
- Reject old files without a header

### Step 2: mmap setup
- Reserve via `ftruncate` + `mmap(MAP_SHARED|MAP_NORESERVE)`
- `wal_used` cursor in the `AimlKv` struct
- Records via memcpy into the mmap, no `write()` any more

### Step 3: index-based reads
- `ValueLoc` struct + `index` map replaces `kv->map`
- `kv_wal_replay()` only builds the index, copies no data
- `aiml_db_kv_get`: lookup → return `mmap_base + offset`

### Step 4: string mode with null terminator
- `kv_wal_write_set` (string mode): `val_bytes = strlen(str) + 1`
- The WAL contains `\0`-terminated strings → direct `char*` return

### Step 5: blob mode zero-copy
- `kv_set_blob`: memcpy directly into the mmap region
- `kv_get_blob`: pointer + length from the index

### Step 6: typed mode with decoded cache
- Serialised blob via mmap
- `decoded_cache` as a bounded LRU (e.g. 100 entries)
- On a cache miss: decode from the mmap

### Step 7: kv_evict with madvise
- Clear the decoded cache
- `madvise(mmap_base, mmap_size, MADV_DONTNEED)`
- Do NOT clear the index (small, faults back in on the next touch anyway)
- The `evicted` flag and `lazy_reload` are dropped

### Step 8: compaction (kv_checkpoint)
- Acquire the lock
- unmap the old mmap
- write all active records into a temp file
- temp → real (rename)
- set up the new mmap
- rebuild the index (offsets have changed)

### Step 9: tests + documentation
- Run the existing tests (should all be green)
- New tests: large store + evict → verify the RSS drop
- update spec/31_db_integration.md

## Risks and mitigation

| Risk | Likelihood | Mitigation |
|---|---|---|
| 16 GB reserve impossible on 32-bit systems | irrelevant (64-bit only) | – |
| Reserve overflow (file grows >16 GB) | low | log.fatal in phase 1, mremap in phase 2 |
| Crash in the middle of a header update | very low | crash scan from `sizeof(header)` ignores header fields |
| `kv.checkpoint()` while other threads read | medium | per-KV mutex holds; compaction blocks reads |
| Page cache pressure from many stores | low | madvise(DONTNEED) on evict frees pages |
| msync latency with very large write volumes | low | the Linux flush daemon syncs automatically every 30 s |

## Success criteria

1. **Per-file training runs through without OOM** (10 files × 4 workers)
2. **RSS drops after worker join** (kv.evict + tensor.arena_free take effect)
3. **Existing AIML tests all run green** (159+ tests)
4. **Test run with a large store**: open of a 1 GB store without an RSS spike
5. **9rooms smoke test**: after a directory reset the game works

## Open questions

- Should the reserve size be configurable? (env var `AIMLDB_KV_RESERVE_MB`?)
- Do we need a bounded LRU explicitly, or heap pressure as implicit eviction?
- Trigger compaction automatically (e.g. when the DELETE share is > 30 %)?

These can be decided in phase 2.
