# Review und Beweistests — Container-Leaks, 2026-09-29

[Plan](../../spec61_container_leak_fix_plan_2026-09-29.md) · [Tests/Runner](../../../tests/lifetime/container_leaks/README.md)

## Ergebnis

Compiler und native Runtime wurden mit `exodus build -c exodus.json -j 4` bzw. `exodus build -c exodus2.json -j 4` frisch gebaut; beide Builds erfolgreich. Anschließend native AOT-Programme mit Größen-/Inhaltsassertions und Valgrind geprüft. Flags: `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`, `AIML_MODEL_REFCOUNT=0`. Binary-Hashes vor/nach jedem Lauf identisch. Vorhandene Compileränderungen wurden nicht verändert; Quellstand und Buildlogs sind hier festgehalten.

**15 dauerhafte Probes × N=200/400 = 30 endgültige Messungen:** 10 PASS (fünf Kontrollfälle), 12 LEAK (sechs Varianten der vier bekannten Leak-Klassen), 8 SAFETY_FAIL (vier Varianten von drei Safety-Ursachen einschließlich des neuen Modell-Get-Crashs). Alle als LEAK klassifizierten Varianten: nativer Exit 0, vollständiger Valgrind-Bericht, definite/indirect lost > 0. Sämtliche bestätigten Leaks verdoppeln die verlorenen Bytes bei doppelter Iterationszahl. Strings verwenden dafür konstante Payloads.

Der strikte Runner liefert für die offenen Fehler Exit 1; das ist ein nachgewiesener roter Zielzustand, kein erfolgreicher Fix. Historische Erwartungen lockern das Gate nicht. Das allgemeine Leak-Gate registriert die zehn fehlerhaften Probes als RED und die fünf Kontrollen als GREEN; RED beeinflusst dort weiterhin nicht den Exitcode. Deshalb zusätzlich den strikten Runner benutzen.

## Review-Korrekturen am Plan

- P1-Safety/Layout von der erst in P4 abgeschlossenen Self-put-Referenzbilanz getrennt.
- Strikte Abnahme ergänzt: das bestehende RED-reporting-Gate allein genügt nicht.
- Bei neuem SHARE-Vertrag müssen Map-Append und Update sowie überzählige Key-Temporaries gemeinsam umgestellt werden.
- Bereits fremd bearbeitete Tests 86/87 (nun GREEN) und 88 erhalten.
- B7 als zusätzliche Voraussetzung für Modell-Get-Inhaltsprüfungen aufgenommen: get-Borrow und Value-Materialisierung dürfen nicht denselben falsch interpretierten Structwert verwenden.

## Zusätzlich entdeckt: B7

Die neue Assertion `decl [view, ref<Item>, {call: [index.get, "key"]}]` gefolgt von `view.text` verursacht bereits ohne Remove/Replace einen Invalid read in `strcmp`, danach SIGSEGV. Die Adresse enthält Bytes des Stringinhalts, z.B. `0x3d64616f6c796170` ("payload="). Exakte erste fehlerhafte IR-Operation noch nicht abschließend lokalisiert. Kein Leakbefund: der Zugriff auf die Borrow-View ist kaputt.

Zunächst löste diese Leseassertion in `map_model_control` und `map_model_replace` den früheren Crash aus und verdeckte deren Scope-Abbau. Beide ersten Messungen bleiben unter `initial/` erhalten. Die Assertion steht jetzt isoliert in `map_model_get_borrow`; die beiden Leak-/Kontrollprobes prüfen gezielt Map-Größe und vollständigen Abbau. Sie wurden als einzige geänderte Probes zusammen mit B7 erneut ausgeführt (`targeted/`). `results.json` und die Tabelle unten verwenden pro Case/Iteration ausschließlich den endgültigen Quellstand; kein Crash wurde als 0-Byte-Erfolg gewertet.

## Gemessene endgültige Varianten

Alle Zahlen sind Bytes. Safety-Fälle können 0 verlorene Bytes melden, sind aber wegen Invalid-read/Signal trotzdem fehlgeschlagen.

| Probe | N | Status | Definite | Indirect | Native Exit | Belege |
|---|---:|---|---:|---:|---:|---|
| map_nested_control | 200 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](initial/map_nested_control-200/main.aiml), [Valgrind](initial/map_nested_control-200/valgrind.log) |
| map_nested_control | 400 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](initial/map_nested_control-400/main.aiml), [Valgrind](initial/map_nested_control-400/valgrind.log) |
| map_nested_self_put | 200 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](initial/map_nested_self_put-200/main.aiml), [Valgrind](initial/map_nested_self_put-200/valgrind.log) |
| map_nested_self_put | 400 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](initial/map_nested_self_put-400/main.aiml), [Valgrind](initial/map_nested_self_put-400/valgrind.log) |
| list_string_insert | 200 | SAFETY_FAIL | 0 | 0 | 0 | [Quelle](initial/list_string_insert-200/main.aiml), [Valgrind](initial/list_string_insert-200/valgrind.log) |
| list_string_insert | 400 | SAFETY_FAIL | 0 | 0 | 0 | [Quelle](initial/list_string_insert-400/main.aiml), [Valgrind](initial/list_string_insert-400/valgrind.log) |
| map_shared_control | 200 | LEAK | 3200 | 3800 | 0 | [Quelle](initial/map_shared_control-200/main.aiml), [Valgrind](initial/map_shared_control-200/valgrind.log) |
| map_shared_control | 400 | LEAK | 6400 | 7600 | 0 | [Quelle](initial/map_shared_control-400/main.aiml), [Valgrind](initial/map_shared_control-400/valgrind.log) |
| map_nested_remove | 200 | LEAK | 4800 | 3200 | 0 | [Quelle](initial/map_nested_remove-200/main.aiml), [Valgrind](initial/map_nested_remove-200/valgrind.log) |
| map_nested_remove | 400 | LEAK | 9600 | 6400 | 0 | [Quelle](initial/map_nested_remove-400/main.aiml), [Valgrind](initial/map_nested_remove-400/valgrind.log) |
| map_model_remove | 200 | LEAK | 3800 | 0 | 0 | [Quelle](initial/map_model_remove-200/main.aiml), [Valgrind](initial/map_model_remove-200/valgrind.log) |
| map_model_remove | 400 | LEAK | 7600 | 0 | 0 | [Quelle](initial/map_model_remove-400/main.aiml), [Valgrind](initial/map_model_remove-400/valgrind.log) |
| map_model_replace | 200 | LEAK | 3400 | 0 | 0 | [Quelle](targeted/map_model_replace-200/main.aiml), [Valgrind](targeted/map_model_replace-200/valgrind.log) |
| map_model_replace | 400 | LEAK | 6800 | 0 | 0 | [Quelle](targeted/map_model_replace-400/main.aiml), [Valgrind](targeted/map_model_replace-400/valgrind.log) |
| list_pop_bound | 200 | LEAK | 3400 | 0 | 0 | [Quelle](initial/list_pop_bound-200/main.aiml), [Valgrind](initial/list_pop_bound-200/valgrind.log) |
| list_pop_bound | 400 | LEAK | 6800 | 0 | 0 | [Quelle](initial/list_pop_bound-400/main.aiml), [Valgrind](initial/list_pop_bound-400/valgrind.log) |
| list_pop_arg | 200 | LEAK | 3400 | 0 | 0 | [Quelle](initial/list_pop_arg-200/main.aiml), [Valgrind](initial/list_pop_arg-200/valgrind.log) |
| list_pop_arg | 400 | LEAK | 6800 | 0 | 0 | [Quelle](initial/list_pop_arg-400/main.aiml), [Valgrind](initial/list_pop_arg-400/valgrind.log) |
| map_model_control | 200 | PASS | 0 | 0 | 0 | [Quelle](targeted/map_model_control-200/main.aiml), [Valgrind](targeted/map_model_control-200/valgrind.log) |
| map_model_control | 400 | PASS | 0 | 0 | 0 | [Quelle](targeted/map_model_control-400/main.aiml), [Valgrind](targeted/map_model_control-400/valgrind.log) |
| map_string_control | 200 | PASS | 0 | 0 | 0 | [Quelle](initial/map_string_control-200/main.aiml), [Valgrind](initial/map_string_control-200/valgrind.log) |
| map_string_control | 400 | PASS | 0 | 0 | 0 | [Quelle](initial/map_string_control-400/main.aiml), [Valgrind](initial/map_string_control-400/valgrind.log) |
| map_shared_string_key_control | 200 | PASS | 0 | 0 | 0 | [Quelle](initial/map_shared_string_key_control-200/main.aiml), [Valgrind](initial/map_shared_string_key_control-200/valgrind.log) |
| map_shared_string_key_control | 400 | PASS | 0 | 0 | 0 | [Quelle](initial/map_shared_string_key_control-400/main.aiml), [Valgrind](initial/map_shared_string_key_control-400/valgrind.log) |
| list_nested_control | 200 | PASS | 0 | 0 | 0 | [Quelle](initial/list_nested_control-200/main.aiml), [Valgrind](initial/list_nested_control-200/valgrind.log) |
| list_nested_control | 400 | PASS | 0 | 0 | 0 | [Quelle](initial/list_nested_control-400/main.aiml), [Valgrind](initial/list_nested_control-400/valgrind.log) |
| list_pop_discard | 200 | PASS | 0 | 0 | 0 | [Quelle](initial/list_pop_discard-200/main.aiml), [Valgrind](initial/list_pop_discard-200/valgrind.log) |
| list_pop_discard | 400 | PASS | 0 | 0 | 0 | [Quelle](initial/list_pop_discard-400/main.aiml), [Valgrind](initial/list_pop_discard-400/valgrind.log) |
| map_model_get_borrow | 200 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](targeted/map_model_get_borrow-200/main.aiml), [Valgrind](targeted/map_model_get_borrow-200/valgrind.log) |
| map_model_get_borrow | 400 | SAFETY_FAIL | 0 | 0 | -11 | [Quelle](targeted/map_model_get_borrow-400/main.aiml), [Valgrind](targeted/map_model_get_borrow-400/valgrind.log) |

## Wiederholen

```bash
python3 tests/lifetime/container_leaks/run.py
```

Weitere Optionen und Testzuordnung im Test-README. Die Messartefakte hier enthalten Quellen und Logs, keine Binärdateien. `source_state.json`, `preexisting_source_changes.patch` und die beiden Run-`provenance.json` dokumentieren den untersuchten Stand. Die großen bestehenden Suites sowie WASM/Browser wurden bei diesem Test-/Planreview nicht neu ausgeführt; deren Abnahme bleibt Implementierungsarbeit. Dieser Bericht behauptet weder Leakfreiheit der Sprache noch vollständige Spec-61-Konformität.
