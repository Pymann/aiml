# Spec 61: Container-Leaks und Speicherfehler

Gezielte Regressionen zum [Implementierungsplan](../../../doc/spec61_container_leak_fix_plan_2026-09-29.md).
Sie ergänzen die bestehende Lifetime-Suite: deren Tests für List-Remove, Shared-Listen und
verworfene Pops decken die hier gezeigten Map-/String-Pop-Fehler nicht ab.

## Ausführen

Vom AIML-Projektroot, mit zueinander passendem gebautem Compiler und `libaiml`:

```bash
python3 tests/lifetime/container_leaks/run.py
python3 tests/lifetime/container_leaks/run.py --only map_shared_control map_shared_string_key_control
python3 tests/lifetime/container_leaks/run.py --iterations 200 --output /tmp/new-container-audit
```

`--output` muss ein neues Verzeichnis sein. Ohne Angabe wird ein neues Verzeichnis unter `/tmp`
erstellt und ausgegeben. `--root` überschreibt den automatisch ermittelten AIML-Projektpfad.
Voraussetzungen: Python 3.11+, g++, Valgrind und native AIML-Buildartefakte. Der Runner baut nicht
den Compiler, sondern jeden Repro AOT und linkt gegen `out/libaiml/libaiml.so`.

Der Runner setzt `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`, `AIML_MODEL_REFCOUNT=0` und führt jede
Probe nativ sowie unter Valgrind aus. Default: 200 und 400 Iterationen. Die Quellen enthalten
genau einen markierten Schleifengrenzwert; nur diese Grenze wird für den Testlauf variiert.
String-Payloads bleiben konstant, damit Skalierung nicht von ihrer Länge abhängt.

**Ziel jedes Tests: richtige Inhalte, Exit 0, 0 definite/indirect lost und keine Speicherfehler.**
Historische Fehler in `cases.json` sind Diagnosemetadaten, keine erwarteten erfolgreichen XFAILs.
Solange ein Fehler besteht, liefert der Runner Exit 1. Fehlendes Werkzeug ist ebenfalls ein Fehler.
Crash, Compilerfehler oder fehlendes vollständiges Valgrind-Log gelten nicht als Leakbeweis.
Der Runner unterscheidet LEAK, SAFETY_FAIL, ASSERTION_OR_RUN_FAIL und Infrastrukturfehler.
Binärdatei-Hashes vor/nach dem Lauf müssen identisch sein, sonst ist die Abnahme ungültig.

Artefakte werden erhalten: pro Variante source/build/link/native/Valgrind-Logs, erzeugtes Binary,
`results.json` und `provenance.json`. Native Runs erzeugen keine Core-Dumps. Definite/indirect
werden nur aus vollständigen Valgrind-Berichten gelesen; RSS ist hier kein Gate.

## Fälle

| Probe | Befund | Isolierter Unterschied / Prüfung |
|---|---|---|
| `map_nested_control` | B1 Safety | Map mit Listenwert, gewöhnlicher Scope-Abbau; trotz Name KEINE grüne Kontrolle |
| `map_nested_self_put` | B1 Safety / P4 | Zusätzlich denselben Wert via `get` erneut speichern |
| `list_string_insert` | B2 Safety | Lokalen String einfügen; Inhalt/Größe korrekt, Besitz beim Drop prüfen |
| `map_shared_control` | B3 Leak | Numerische Keys und Shared-Modell; trotz Name ein fehlerhafter Fall |
| `map_shared_string_key_control` | Kontrolle zu B3 | Derselbe Besitzpfad mit String-Key |
| `map_nested_remove` | B4 Leak | Inneren Listenwert aus der Map entfernen; Map danach leer |
| `map_model_remove` | B4 Leak | Inline-Modell mit String-Feld entfernen; Map danach leer |
| `map_model_replace` | B5 Leak | Modell unter gleichem Key ersetzen; Map bleibt bei einem Eintrag |
| `map_model_get_borrow` | B7 Safety (Review) | Separater Modell-Get als `ref<Item>` mit Feld-Leseassertion |
| `map_model_control` | Kontrolle zu B4/B5 | Einmal speichern, Größe prüfen, normaler Scope-Abbau |
| `map_string_control` | Kontrolle zu B4 | Direkten Stringwert entfernen; Map danach leer |
| `list_pop_bound` | B6 Leak | Pop an lokalen String binden; Text und leere Liste prüfen |
| `list_pop_arg` | B6 Leak | Pop an Funktion übergeben; Callee prüft Text, Caller leere Liste |
| `list_pop_discard` | Kontrolle zu B6 | Pop direkt verwerfen; Liste leer |
| `list_nested_control` | Layout-Kontrolle | Verschachtelte Liste anstelle verschachtelter Map |

Die Namen behalten die Zuordnung zur ursprünglichen Untersuchung. Die Zuordnung `finding`
in `cases.json`, nicht das Wort `control` im Dateinamen, kennzeichnet grüne Gegenproben.

Die Unterordnerlage verhindert, dass das normale `tests/lifetime/[0-9]*.aiml`-Glob offene Fälle
als grüne Tests einsammelt. Das bestehende Leak-Gate registriert bestätigte Fehler als RED und
saubere Gegenproben als GREEN. **Sein Exit 0 alleine genügt nicht:** es lässt RED absichtlich
nicht fehlschlagen. Für diese Arbeit immer den strikten Runner oben verwenden. Nach Reparatur
jeweilige Fälle im allgemeinen Gate nach GREEN verschieben; dieser Runner braucht dafür keine
Lockerung seiner Erwartungen.

Die größere Alias-/Move-/WASM-Testmatrix im Implementierungsplan ist zusätzliche spätere
Fix-Abnahme. Diese Minimaltests behaupten weder vollständige Spec-61-Abdeckung noch eine
WASM-/Browser-Abnahme.

Die Modell-Get-Leseassertion zeigte einen zusätzlichen, unabhängig von Replace auftretenden Crash. Sie steht deshalb in `map_model_get_borrow`, statt den B5-Leak mit einem früheren Crash zu verdecken. B5 und seine Kontrolle prüfen isoliert Map-Größe und vollständigen Scope-Abbau; die gespeicherten Feldinhalte werden über B7 separat abgesichert.
