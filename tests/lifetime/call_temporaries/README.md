# Spec-61-Aufruf-Temporaries: Regressionen

Status (2026-10-02): **alle 44 Programme kompiliert und ausgeführt — Exit 0, 0 definitiv/indirekt
verlorene Bytes, keine Valgrind-Fehler.** Vor dem ersten Lauf wurden die Programme in gültiges AIML
gebracht (fehlende String-Anführungszeichen, abgeschaffte Bindestrich-Schreibweise, sieben `main`
über 300 Zeilen in `main_part_1/2` aufgeteilt, `buffer.len`-Erwartung nach spec/11 mit `zero`).

Alle Programme erwarten Exitcode 0. Das AOT-Leak-Gate verlangt zusätzlich
0 definitiv und 0 indirekt verlorene Bytes sowie keine Valgrind-Fehler.
Throw-Proben prüfen Catch-Zahl und Exception-Nachricht; ein unerwarteter normaler
Return aus einem als werfend erwarteten Aufruf führt zu Exitcode 90.

| Dateien | Abdeckung |
|---|---|
| `forward*`, `chain*`, `fnptr*`, `throw*`, `iface_consumer*` | Sechs ursprüngliche Muster, jeweils direkte Temporary-Variante und `_local`-Kontrolle mit 10/1000 Iterationen; zusätzlich zwei Forward-Reihenfolgekontrollen (26 Programme). |
| `ptr_dispatch_{free,method,interface,fn}` | Benannte und frisch erzeugte `ptr<Card>` bei normalem Return und Consumer-Throw. Später werfendes Argument vor dem Move; benannter Caller muss anschließend noch lesbar sein (4 Programme). |
| `unwind_{string,list,map,buffer,model}` | Alle vier Dispatch-Wege mit Normalpfad, Producer-Throw, Consumer-Throw und später werfendem Argument. Verschachtelte Handler und überlebender äußerer Owner; String zusätzlich direkt interpoliert (5 Programme). |
| `consumed_rc_{string,list,map,buffer,shared,cascade}` | Benannter Caller bleibt nach Freigabe/Ersetzung der Callee-Referenz gültig; gespeicherter Wert überlebt den Caller-Scope. Temporary-Übergabe und Throw nach Retain, jeweils über vier Dispatch-Wege (6 Programme). |
| `condition_resets_once` | Wiederholte While-Bedingung, früher Return, exakte Producer-Aufrufzahl über verschachtelten Receiver. |
| `void_sret_alias_returns` | Interface-void/sret, indirekter void-Aufruf, Containerreturn teilt Argumentreferenz und bleibt nach Caller-Temp-Cleanup gültig. |
| `imported_inherited_signature` | Importierte/geerbte Methode über Feldkette und gleichnamige Methode mit skalarem Rückgabewert. |

`support/imported_factory.aiml` ist nur eine Import-Fixture und kein ausführbares Testziel.
Die fünf weiteren ursprünglichen Muster stammen aus
`9rooms_v2/leak_probes/review_20261002_more`; die Forward-Probe ist unabhängig vom Spiel reduziert.
Die Tests benötigen keine 9rooms-Imports oder Produktionsdatenbanken.

## Einbindung

Das explizite Manifest `cases.sh` wird von allen drei vorhandenen Runnern verwendet:

- `tests/lifetime/run_lifetime.sh`: funktionale Abnahme im Modus `spec61`; `ONLY=...` gilt weiterhin.
- `tests/run_lifetime_leak_gates.sh`: verpflichtender Abschnitt `REGRESSION`, getrennt von bereits als `GREEN` geführten Fällen. Ein Fehler lässt das Gate scheitern; ungetestet bedeutet nicht optional.
- `tests/run_memory_sanitizers.sh`: normale Ausführung plus Valgrind-Memory-Safety mit aktivem memV3 und Lifetime-Drops.

Gezielte Ausführung:

```sh
ONLY=ptr_dispatch bash tests/lifetime/run_lifetime.sh
```

Offen: vollständige Gate-Abnahme im Gesamtlauf, 9rooms-End-to-End-Prüfung und die geplanten
Compilezeit-/Binärgrößenmessungen.
