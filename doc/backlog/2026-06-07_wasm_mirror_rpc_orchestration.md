# WASM mirror RPC tests — orchestration & pair run

**Status**: backlog (2026-06-07)
**Scope**: test infrastructure (run_wasm_tests.sh, run_all_tests.sh, tools/)
**Failing tests**:
- `tests/wasm/test_mirror_rpc_client.aiml`
- `tests/wasm/test_mirror_rpc_client_multifile.aiml`

## Symptom

```
[mirror] WS connect to localhost:18888 failed
FAIL: missing nids (item=-1 detail=-1)
```

Both tests end as a clean exit 1 when run via `run_all_tests.sh`
(native AIML interpreter, no server companion).

## Root cause

The `tests/wasm/test_mirror_rpc_client*.aiml` are **WASM clients** that
require a **native server companion** on `ws://localhost:18888`
(`test_mirror_rpc_server.aiml` or `..._server_multifile.aiml`). The
server registers ItemImpl + detail objects, the client connects
and verifies sync + a send_rpc round trip with a nested `list<string>`.

That is a **real cross-runtime regression** (native host wire codec ↔
wasm client wire codec) — exactly the path DataCreatures' WS RPC bridge
uses.

**Problem**: there is currently no runner that orchestrates the pair:

1. `tests/wasm_tests.list` only contains pure-compute tests
   ("no GL/network/filesystem"). Mirror RPC is explicitly excluded.
2. `run_all_tests.sh` finds the files via `find tests/` and runs them
   one by one through the native interpreter. Client without server → FAIL.
3. The server counterparts (`*_server.aiml`) **pass** in `run_all_tests.sh`
   because they only listen silently for `time.sleep_ms` × 800 = 8s and then
   return with exit 0 — a **false-positive PASS** that says nothing about the
   wire protocol.

## Proposal

### Option A — orchestrator runner (recommended)

A dedicated pair runner `tools/run_wasm_pair_test.sh`:

```bash
# tools/run_wasm_pair_test.sh <server.aiml> <client.aiml>
# 1. Compile <client.aiml> to wasm via tools/run_wasm_test.sh (build only, no run)
# 2. Spawn <server.aiml> natively in the background (port 18888)
# 3. Wait-for-port loop (max 5s) until the server listens
# 4. Run the wasm <client> via node, capture the exit
# 5. Kill the server, return the client's exit
```

A new section in `tests/wasm_tests.list`:

```
# Paired tests: server runs native, client runs wasm.
# Format: pair <server> <client>
pair tests/wasm/test_mirror_rpc_server.aiml          tests/wasm/test_mirror_rpc_client.aiml
pair tests/wasm/test_mirror_rpc_server_multifile.aiml tests/wasm/test_mirror_rpc_client_multifile.aiml
```

`run_wasm_tests.sh` parses `pair` lines and calls `run_wasm_pair_test.sh`.

### run_all_tests.sh — skip tag for pair tests

Both server and client get `# Requires: wasm-pair`. Skip logic
in `run_all_tests.sh`:

```bash
if grep -q "# Requires: wasm-pair" "$TEST"; then
    echo "[ SKIP ] $TEST (Requires: wasm-pair — run via run_wasm_tests.sh)"
    ((SKIPPED++)); ((TOTAL++)); continue
fi
```

With that:
- `run_all_tests.sh` shows the tests as clean SKIPs (instead of a false-positive
  PASS for the server + a real FAIL for the client).
- `run_wasm_tests.sh` orchestrates the pair run and catches real regressions.

### Option B — spawn in-process (workaround, not recommended)

Server + client in one AIML file with `spawn` for the server thread.
Advantages: no new runner, one file. Disadvantages: we lose exactly
what the tests are meant to test — the native↔wasm wire codec mismatch.
Identical code paths on both sides would mask the bug vector.

## Effort estimate

| Part | Order of magnitude |
|---|---|
| `run_wasm_pair_test.sh` script | 1h |
| `wasm_tests.list` parser update | 30min |
| `run_all_tests.sh` skip tag handler | 15min |
| Tag comments in 4 test files | 5min |
| Verify (server.exit_code; client.exit_code; cleanup leak check) | 30min |

→ **< 3h**, a session of its own.

## Related issues

- The pre-existing `test_aiondb_store_heap_reopen_2_read.aiml` "missing
  write pair" had a similar structure (read without write), fixed 2026-06-07
  by writing the missing counterpart.
- `test_iface_list_return_after_mirror.aiml` timing race (fixed
  2026-06-07) — the same mirror RPC path, but in-process via `spawn`
  instead of cross-runtime.

## Out of scope

- Checking mirror RPC itself functionally — the WIRE CODEC bug from
  DataCreatures was fixed 2026-06-05 (`wasm_vtable_struct_return`).
  These tests are regression repros, not an open bug.
- Rebuilding run_all_tests.sh completely — only add the one skip pattern.
