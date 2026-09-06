# AIML: Zeiger in Modell-Wertlisten verhindern — 2026-09-05

## Änderung

`src/core/ast_compiler_call.cpp` prüft bei `list<T>.push()` jetzt die tatsächliche LLVM-Repräsentation des Arguments. Ein Pointerargument für ein Struct-Wertelement führt unabhängig von der AST-Form zum Compile-Fehler. Das umfasst Variablen, Member und verschachtelte CallExpr wie `destination.push(source.get(index))`. Nach der Diagnose wird keine fehlerhafte Push-IR erzeugt.

Die frühere Prüfung untersuchte nur VariableExpr und konnte dadurch den 9rooms-Reveal-Absturz verursachenden CallExpr übersehen. Der vorhandene Interface-Fat-Pointer-Upcast bleibt ausgenommen und unverändert. Listenlayout und get()-Semantik wurden nicht geändert. Der Fix ist auf den Pointer-/Aggregate-Konflikt bei push begrenzt, keine vollständige Prüfung aller möglichen Container-Typkonflikte.

`compiler_change.patch` zeigt ausschließlich diese Änderung gegenüber dem bereits bearbeiteten Nutzer-Arbeitsstand vor Beginn des Fixes. Andere vorhandene Quelländerungen wurden erhalten.

## Regressionstests

- `tests/test_list_model_nested_get_push_repro.aiml`: verschachteltes get/push, jetzt Compile-Fehler.
- `tests/test_ptr_push_into_value_list_error.aiml`: bestehender direkter Zeigerfall bleibt Compile-Fehler.
- `tests/test_ptr_member_push_into_value_list_error.aiml`: Member-Ausdruck wird abgewiesen.
- `tests/test_ptr_call_push_into_value_list_error.aiml`: Funktion mit ptr-Rückgabe wird abgewiesen.
- `tests/test_list_model_push_value_and_pointer.aiml`: gültige Modellrückgaben, Member-Wertkopien, get über lokale Wertvariable, explizites Dereferenzieren, Pointerlisten, Listenwachstum und Copy-/Sharing-Semantik bestehen.

`run_all_tests.sh` erkennt die vier Negativtests und verlangt neben Exit 1 den spezifischen Compile-Diagnosetext. Ein erneuter Runtime-Exit 1 des alten Repros kann dadurch nicht als bestandener Test gelten. Shell-Syntax wurde geprüft. Die vollständige globale Suite wurde nicht ausgeführt; die fokussierte Auswahl steht in results.json.

## Validierung

- Compiler über `exodus build -c exodus.json` erfolgreich neu gebaut.
- 30 native gezielte Prüfungen: **29 bestanden**, ein bestehender heterogener Modelllisten-Repro stürzt ab.
- Isolierter Vorher-/Nachher-Vergleich für `test_model_inherits_hetero_list.aiml`: **beide SIGSEGV (-11), bytegleiche LLVM-IR**. SHA-256 und Exitcodes in baseline_comparison.json. Der isolierte Baseline-Compiler wurde mit der vorher gesicherten ast_compiler_call.cpp und ansonsten denselben Objektdateien separat unter /tmp gebaut; kein temporäres Zurücksetzen des Arbeitsstands.
- 4 wasm32-Kompilierprüfungen: drei neue Pointer-Konflikte korrekt abgelehnt, positiver Modell-/Pointertest erfolgreich kompiliert. Kein WASM-Browserlauf.
- Explizites Dereferenzieren im positiven Test abschließend zusätzlich nativ und für wasm32 geprüft.
- `git diff --check` für die geänderten Bestandsdateien erfolgreich.

Die Negative-Tests werden als PASS gezählt, wenn Kompilierung mit dem erwarteten Pointer-/Wertfehler abbricht, nicht wenn das Programm abstürzt. Die beigefügten Rohlogs dokumentieren den einzelnen verbleibenden Altfehler offen.

## Auswirkung auf 9rooms_v2

Der fehlerhafte Aufruf in `BoardScreen.cp_tick` wird beim nächsten Neubau nun frühzeitig abgewiesen. Für einen wieder baubaren Client ist dort die bereits dokumentierte explizite MockCard-Wertkopie erforderlich. Dieser Auftrag ändert AIML; der 9rooms-Workaround wurde hier nicht angewandt und der Webclient nicht neu ausgeliefert.

## Erneut prüfen

Vom AIML-Projektverzeichnis:

```sh
./out/aiml/aiml tests/test_list_model_nested_get_push_repro.aiml -r
# Erwartet: Exit 1 UND "pointer instead of a value" / "Compilation failed".
./out/aiml/aiml tests/test_list_model_push_value_and_pointer.aiml -r
# Erwartet: Exit 0, PASS.
```
