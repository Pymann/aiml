# Performance: die A-Grundlinie vervollständigen und den JIT/AOT-Faktor 2 klären

Date: 2026-11-16
Status: backlog (beides hat Zeit — vom User so eingeordnet)
Spec: `spec/70_performance.md`
Erster Messpunkt: Commit `929c80ff`, `performance/cases/fib100.aiml`

## Warum beides zusammengehört

Die Reihenfolge aus spec/70 §10 ist: Suite, Grundlinien auf der heutigen
Pipeline, **dann** die IR-Mittelschicht, dann dieselbe Suite. Beide Punkte hier
sind das „Grundlinien"-Stück. Wer die `PassBuilder`-Pipeline vorher baut, hat
hinterher kein Vorher.

## Punkt 1 — der JIT ist 1,9× langsamer als das AOT-Binary

Gemessen an `fib100` (Ryzen 7 5700U, Governor powersave, LLVM 18.1.3,
`ir_opt=none`, Median aus 7 Proben, Kalibrierung ≥500 ms):

| Batterie | ns je Runde (100 Schritte) | reps | Median |
| --- | --- | --- | --- |
| JIT | 455 | 2.048.000 | 932 ms |
| AOT | 245 | 4.096.000 | 1.005 ms |
| C (gcc 13.3 -O2) | 22 | 32.768.000 | 748 ms |

Beide Batterien übersetzen **dieselbe IR** und setzen **keinen**
Optimierungsgrad explizit:

* JIT: `llvm::EngineBuilder` ohne `setOptLevel` (`compiler.cpp:1559`, `:1705`)
* AOT: `createTargetMachine(triple, cpu, features, TargetOptions, PIC)` ohne
  `CodeGenOptLevel`, danach `addPassesToEmitFile` über einen
  `legacy::PassManager` (`compiler.cpp:2106`, `:2118`)

Die Ausgaben sind identisch — es ist **kein** Korrektheitsbefund (spec/70 C5),
aber ein Faktor 2 ohne genannten Grund. Zu prüfen:

1. Welchen `CodeGenOptLevel` nimmt `EngineBuilder` in LLVM 18 tatsächlich an,
   und welchen die Standard-`TargetMachine`? Wenn sie sich unterscheiden, ist das
   die Erklärung — und dann gehört der Grad an beiden Stellen **explizit**
   hingeschrieben, statt sich auf Vorgaben zu verlassen (eine Vorgabe, die man
   nicht sieht, ändert sich beim LLVM-Sprung still).
2. Ist es stattdessen die Relocation (PIC im AOT) oder die Symbolauflösung über
   `DynamicLibrary` im JIT?
3. Gilt der Faktor nur für diese Schleife oder über die ganze A-Gruppe? Das
   entscheidet, ob es ein Codegen-Thema oder ein Aufruf-Overhead ist.

## Punkt 2 — die restlichen A-Fälle (spec/70 §6.1)

`fib100` deckt A1 (`loop_sum`) ab. Offen sind A2–A12: `float_math`,
`call_overhead`, `iface_dispatch`, `string_build`, `list_ops`, `map_put_get`
(trägt den unerklärten 4,87-s-Ausreißer der alten Messung), `sort`,
`struct_array_walk`, `alloc_churn` (mit/ohne `scope`), `exceptions`,
`serialize`.

Je Fall gilt, was `fib100` schon zeigt: eigene Kalibrierung, sieben Proben,
Korrektheitsanker, C-Grundlinie im selben Lauf, beide Batterien aus derselben
Quelle (F70-30).

## Was dafür vorher fehlt

* **`time.mono_us`** (F70-3, spec/24). Mit Millisekunden braucht jede Probe
  ≥500 ms; bei zwölf Fällen × zwei Batterien × acht Proben sind das allein
  ~2,5 Minuten reine Messzeit, und Latenz-Perzentile (Gruppe D/E) sind
  unmöglich.
* **JSON-Ausgabe im Runner** (F70-70). `performance/run_case.sh` misst richtig,
  schreibt aber noch Text; ohne `results/<case>/<run-id>.json` gibt es kein
  Regressions-Gate (F70-71).
* **`performance/run.sh` ist für AOT kaputt** — er übergibt `-o <case>.bin`,
  bekommt ein Objektfile und versucht es auszuführen; mit `set -e` endet er dort.
  Entweder auf `run_case.sh` umstellen oder ersetzen.

## Nebenbefund, der bleibt

Eine Probe je JIT-Serie fällt um ~27 % nach unten aus (678 ms gegen Median 932,
in beiden Läufen reproduziert). Unter `powersave` vermutlich Boost-Verhalten;
`performance` als Governor braucht root. Solange das nicht erklärt ist, ist die
JIT-Zahl nach F70-13 grenzwertig (der MAD bleibt zwar bei 0,6 %, weil der
Ausreißer einzeln bleibt).
