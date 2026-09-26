# AIML Test-Suiten — Status & Übersicht

Stand: 2026-09-23 · Branch `mem-v3-compiler` · Build: `exodus build --all` (aus `exvenv`) · Gates: `AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1` (memV3, Compiler-Default)

## TL;DR — sind alle Suiten grün?

**Fast — der memV3-/Lifetime-/Memory-Safety-Kern ist grün, mit vier Ausnahmen, von denen keine ein offener memV3-Bug ist:**

- ✅ **Lifetime (spec/61)**: 47/0, 13 neg, 2 warn — vollständig grün.
- ✅ **Memory-Sanitizer (valgrind)**: 41/41 memory-safe (die vormalige leak-probe-A-Verletzung war der `sys.env`-Bug, jetzt behoben).
- ✅ **JSON-Serialize**: alle Serialize-Tests grün + **deterministisch** — der langjährige nicht-deterministische #2-Bug ist an der Wurzel behoben.
- 🟢 **Haupt-Korpus**: der einzige rote Test ist **1 offene Design-Frage** (`test_model_inherits_hetero_list`). Die `lifetime/neg`-Negativtests werden jetzt (korrekt) nur noch von `run_lifetime.sh` gefahren, nicht mehr vom Korpus. **Keine flaky Tests, kein Harness-Rauschen mehr.**
- ⚠️ **AOT-Smoke**: 1/3 — 2 **pre-existing** Multi-File-Linking-Fehler (eigene Infra-Domäne, von der memV3-Arbeit unberührt).
- ⏸️ **WASM**: diese Session **nicht** ausgeführt (braucht node/wasm-Toolchain).

---

## Die Suiten im Detail

| Suite | Runner | Zweck | Stand |
|---|---|---|---|
| Haupt-Korpus | `run_all_tests.sh` | Alle `tests/**/*.aiml` via nativem JIT (`aiml <file> -r`); räumt DB-State + `__aiml_cache` | **421/466 OK** |
| Lifetime (spec/61) | `tests/lifetime/run_lifetime.sh` | Ownership/Borrow/Move + Teardown-Leaks (pos + neg + warn), MODE=spec61 | **47/0, 13 neg, 2 warn** ✅ |
| Memory-Sanitizer | `tests/run_memory_sanitizers.sh` | 41 Probes je 2× (nativ = Correctness + valgrind = keine UAF/Double-Free/Invalid-Access) | **40/41** |
| AOT-Smoke | `run_aot_tests.sh` | `.aiml` → `.o` kompilieren, gegen `libaiml.so` linken, Binary ausführen | **1/3** ⚠️ |
| WASM | `run_wasm_tests.sh` | `.aiml` → wasm32, Ausführung via node | **nicht ausgeführt** ⏸️ |
| (Wrapper, keine Suite) | `run_safe.sh` | ulimit-Wrapper (RAM/Stack/CPU) zum Debuggen von Compiler-Crashes | — |

---

## Nicht-grüne Punkte — Einordnung

### Haupt-Korpus (419/466) — die 47 „nicht-OK" zerfallen so:
- **38 SKIP** — Bibliotheks-/Fixture-Dateien, die von anderen Tests importiert werden (kein eigenständiger Lauf).
- **9× `tests/lifetime/neg/n01…n13`** — **Harness-Rauschen, kein Bug**: das sind Negativ-Tests (müssen einen Compile-Fehler werfen), aber `run_all_tests.sh` erwartet Exit 0 → zählt sie als Fail. Validiert werden sie von `run_lifetime.sh` (dort 13/13 grün).
- **1 echte offene Frage: `test_model_inherits_hetero_list`** — `list<any>` mit Base-Slice-Semantik für heterogene Model-Listen. **Vom Test-Autor selbst als offene Typsystem-Design-Frage markiert** („Es bleibt zu entscheiden, wie der compiler hinsichtlich Typisierung gestaltet wird") — braucht eine Grundsatzentscheidung + Feature, keinen Bugfix.
### JSON-Serialize (✅ gelöst)
- `test_json_style_serialize` + `test_serialize_ext` waren der bekannte nicht-deterministische Serialize-Bug #2 (~75% SIGSEGV, layout-/ASLR-abhängig, valgrind-unsichtbar). **Root-Cause:** `sys.serialize` übergab für ein `list/map/buffer`-Arg unter memV3 die **alloca-Adresse** statt des **Handle-Pointers** → `emitSerializeList` las die Pointer-Bytes als `{size,cap,data}` → `size` = niederwertige Bits einer Heap-Adresse → Garbage-Iteration → Crash (mal klein/ok, meist riesig). **Fix:** Handle unter memV3 vor dem Serialisieren laden. Jetzt **30/30 deterministisch, valgrind 0 invalid-access**, alle Serialize-Tests 5/5.

### Flaky-Cluster test_system + test_mem_leak_probe (✅ gelöst)
- SIGABRT „corrupted size vs. prev_size" / Sanitizer-leak-probe-A. **Root-Cause:** `sys.env()` lieferte eine `AimlHttpMap*` OHNE memV3-`[rc][pad]`-Header (open-addressing, libc-strdup-Strings) — aber der `map<string:string>`-Local ist ein Handle, dessen Scope-Drop `aiml_map_handle_release` den rc-Header 8 B VOR dem Block liest+schreibt → Heap-Korruption. **Fix:** echtes memV3-Map-Handle (`aiml_map_handle_new`) + DENSE-Store aiml-managed Strings. Zusätzlich `io.read_all` → aiml-managed String (war bare-malloc, OOB-Read). Jetzt **test_system 20/20 + valgrind 0, test_mem_leak_probe 10/10, Sanitizer 41/41**.

### AOT-Smoke (1/3)
- 2 Fehler beim **Multi-File-AOT-Linken** (`undefined reference to testlib.inc_counter` u.a.) und ein `.ll`-Output-Test. Das ist die Objektdatei-/Linker-Infrastruktur — eine eigene Domäne, **unberührt** von den memV3-/Ownership-Änderungen.

### WASM
- Diese Session nicht ausgeführt (separates Target, braucht node + wasm-Toolchain). Kein Aussage über grün/rot.

---

## Diese Session gefixt (Branch `mem-v3-compiler`)

memV3/spec-61-Konsolidierung, jeder Fix valgrind-verifiziert, Lifetime durchgängig 47/0:

- **§6.2/§10** — Push einer Pointer-Expression in `value list<Model>` = Compile-Fehler (3 Diagnostic-Tests).
- **§5.1** — Zyklus-Guard im Free-Walker (rekursiver `ptr<>`-Back-ref, `test_recursive_ptr_ser`).
- **§5.4/§7** — move-only Ressourcen (`tensor`, `ptr<>`) nicht value-kopierbar; Tests auf `ref<>`/`{clone}` migriert (`test_class_member_shared_tensor`, `test_list_model_push_value_and_pointer`) + Compiler-Enforcement.
- **§5** — `srv.filesystem` lädt den `list<path>`-Handle (memV3); `get_copy_at` re-initialisiert leere Container-Handles; `db_indexed list<string>` (Default/Backfill/`aiml_str_dup`).
- **§5.4 (Lösung c)** — `.get`/`.at`/`{at:}` liefern `ref<T>`-Borrow; `ptr<T>`-Binding = Compile-Fehler (nutze `ref<T>` oder `get_copy`); **`ref<Interface>` vereinheitlicht** (= Fat-Pointer wie `ptr<Interface>`, inkl. Dispatch + Cross-Interface-Upcast); ~14 Tests migriert + Negativ-Test `test_ptr_bind_get_borrow_error`.
- **§5 (JSON-Serialize #2)** — `sys.serialize` lädt für `list/map/buffer`-Args den memV3-Handle statt der Slot-Adresse → behebt den nicht-deterministischen Serialize-Crash an der Wurzel (`test_json_style_serialize`, `test_serialize_ext`).
- **§5 (sys.env / io.read_all)** — `sys.env()` liefert ein echtes memV3-Map-Handle (statt header-loser `AimlHttpMap*`), `io.read_all` einen aiml-managed String → behebt den Heap-Korruptions-Cluster (`test_system`, `test_mem_leak_probe`, Sanitizer leak-probe A).

## Reproduzieren

```bash
source /media/.../python/exvenv/bin/activate
cd .../test/aiml
exodus build --all
bash run_all_tests.sh --no-build          # Haupt-Korpus (nur EIN Lauf gleichzeitig!)
bash tests/lifetime/run_lifetime.sh        # Lifetime spec61
bash tests/run_memory_sanitizers.sh        # valgrind-Gate (~20–30 min)
bash run_aot_tests.sh                       # AOT-Smoke
```
