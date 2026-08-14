# WASM Mirror-RPC Tests — Orchestration & Pair-Run

**Status**: Backlog (2026-06-07)
**Scope**: Test-Infrastruktur (run_wasm_tests.sh, run_all_tests.sh, tools/)
**Failing tests**:
- `tests/wasm/test_mirror_rpc_client.aiml`
- `tests/wasm/test_mirror_rpc_client_multifile.aiml`

## Symptom

```
[mirror] WS connect to localhost:18888 failed
FAIL: missing nids (item=-1 detail=-1)
```

Beide Tests laufen als clean exit-1 wenn man sie via `run_all_tests.sh`
ausführt (native AIML-Interpreter, kein Server-Companion).

## Root Cause

Die `tests/wasm/test_mirror_rpc_client*.aiml` sind **WASM-Clients**, die
einen **nativen Server-Companion** auf `ws://localhost:18888` voraussetzen
(`test_mirror_rpc_server.aiml` bzw. `..._server_multifile.aiml`). Der
Server registriert ItemImpl + Detail-Objekte, der Client connected
und verifiziert sync + send_rpc roundtrip mit nested `list<string>`.

Das ist eine **echte Cross-Runtime-Regression** (native-host wire-codec ↔
wasm-client wire-codec) — genau der Pfad den DataCreatures' WS-RPC-Bridge
benutzt.

**Problem**: Es gibt aktuell keinen Runner, der das Paar orchestriert:

1. `tests/wasm_tests.list` enthält nur pure-compute-Tests
   ("no GL/network/filesystem"). Mirror-RPC ist explizit ausgeschlossen.
2. `run_all_tests.sh` findet die Files via `find tests/` und führt sie
   einzeln durch den nativen Interpreter aus. Client ohne Server → FAIL.
3. Die Server-Counterparts (`*_server.aiml`) **passen** in `run_all_tests.sh`
   weil sie nur stumm `time.sleep_ms` × 800 = 8s lauschen und dann mit
   exit 0 zurückkehren — **false-positive PASS**, sagt nichts über das
   Wire-Protokoll aus.

## Vorschlag

### Option A — Orchestrator-Runner (empfohlen)

Eigener Pair-Runner `tools/run_wasm_pair_test.sh`:

```bash
# tools/run_wasm_pair_test.sh <server.aiml> <client.aiml>
# 1. Compile <client.aiml> nach wasm via tools/run_wasm_test.sh (nicht run, nur build)
# 2. Spawn <server.aiml> nativ im Hintergrund (port 18888)
# 3. Wait-for-port loop (max 5s) bis Server listened
# 4. Run wasm <client> via node, capture exit
# 5. Kill server, return client exit
```

In `tests/wasm_tests.list` eine neue Section:

```
# Paired tests: server runs native, client runs wasm.
# Format: pair <server> <client>
pair tests/wasm/test_mirror_rpc_server.aiml          tests/wasm/test_mirror_rpc_client.aiml
pair tests/wasm/test_mirror_rpc_server_multifile.aiml tests/wasm/test_mirror_rpc_client_multifile.aiml
```

`run_wasm_tests.sh` parsed `pair`-Zeilen und ruft `run_wasm_pair_test.sh`.

### run_all_tests.sh — Skip-Tag für Pair-Tests

Sowohl Server als auch Client bekommen `# Requires: wasm-pair`. Skip-Logik
in `run_all_tests.sh`:

```bash
if grep -q "# Requires: wasm-pair" "$TEST"; then
    echo "[ SKIP ] $TEST (Requires: wasm-pair — run via run_wasm_tests.sh)"
    ((SKIPPED++)); ((TOTAL++)); continue
fi
```

Damit:
- `run_all_tests.sh` zeigt die Tests als saubere SKIPs (statt false-positive
  PASS beim Server + echtes FAIL beim Client).
- `run_wasm_tests.sh` orchestriert den Pair-Run und fängt echte Regressions.

### Option B — Spawn-im-Prozess (Workaround, nicht empfohlen)

Server + Client in einer AIML-Datei mit `spawn` für den Server-Thread.
Vorteile: kein neuer Runner, ein File. Nachteile: wir verlieren genau
das was die Tests testen sollen — den nativen↔wasm Wire-Codec mismatch.
Identische Codepfade auf beiden Seiten würden den Bug-Vektor maskieren.

## Aufwand-Schätzung

| Teil | Größenordnung |
|---|---|
| `run_wasm_pair_test.sh` skript | 1h |
| `wasm_tests.list` Parser-Update | 30min |
| `run_all_tests.sh` Skip-Tag-Handler | 15min |
| Tag-Comments in 4 Test-Files | 5min |
| Verify (server.exit_code; client.exit_code; cleanup-leak-check) | 30min |

→ **< 3h**, eigene Session.

## Verwandte Issues

- Pre-existing `test_aiondb_store_heap_reopen_2_read.aiml` "missing
  write-pair" hatte ähnliche Struktur (read ohne write), gefixt 2026-06-07
  durch Schreiben des fehlenden Counterparts.
- `test_iface_list_return_after_mirror.aiml` Timing-Race (gefixt
  2026-06-07) — gleicher Mirror-RPC-Pfad, aber in-process via `spawn`
  statt cross-runtime.

## Out-of-Scope

- Mirror-RPC selbst funktionsmäßig prüfen — der WIRE-CODEC-Bug aus
  DataCreatures wurde 2026-06-05 gefixt (`wasm_vtable_struct_return`).
  Diese Tests sind Regression-Repros, kein offener Bug.
- run_all_tests.sh komplett umbauen — nur das eine Skip-Pattern ergänzen.
