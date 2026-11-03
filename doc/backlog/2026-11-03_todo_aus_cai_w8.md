# AIML-To-do-Liste aus der cai-Arbeit an W5–W8

Datum: 2026-11-03
Status: **abgearbeitet** (siehe „Ergebnis" je Punkt; Stand 2026-11-03 abends)
Quelle: cai-Branch `feature/weighted-symbolic-reasoning` (Regelgewichte, Kausal-Export, TransE mit der
Tensor-Laufzeit). Jeder Punkt ist mit einem kleinen Programm gegen den Compiler von `43144207` geprüft
(`aiml DATEI -r`); die Programme stehen unten. Was nur beobachtet und nicht klein nachgestellt ist, steht
unter „Beobachtet“.

## Ergebnis im Überblick

| # | Punkt | Ergebnis |
|---|-------|----------|
| 1 | f32-Literale im f64-Zusammenhang | **behoben** — Literal wird im Kontexttyp gebildet (spec/04_1 §7a-Tabelle), `tests/test_f64_literals.aiml` |
| 2 | Liste von Klassen: get→remove→insert | **nicht reproduzierbar** — als Regressionstest festgehalten (`tests/test_list_model_mutate.aiml`, Modell- UND Klassenvariante grün) |
| 3 | Methodenaufruf als Wert-Argument vom Klassentyp | **behoben** — Elementzeiger wird geladen wie im Freie-Funktions-Pfad (spec/10), Test im selben File |
| 4 | `math.exp`/`log` nur f32 | **behoben** — Breite nach Argument ODER Ergebnis-Kontext (spec/43_0 §7.1) |
| 5 | `string.parse_f64` | **ergänzt** (spec/09) |
| 6 | Feste Nachkommastellen | **ergänzt** als `sys.format_fixed` (spec/23 F23-78) |
| 7 | Tensor-Zufall nicht seedbar | **ergänzt** als `tensor.seed` + eigener splitmix64 (spec/50 §17.1) |
| 8 | `begin_graph` gibt eigene Tensoren frei | **dokumentiert + `t.keep`** (spec/50 §14.1) |
| 9 | Anführungszeichen in `${ { … } }` | **dokumentiert** (spec/09_1 §3.4) + Compiler nennt die betroffenen Zeilen |
| 10 | Elementschreiben schwer zu finden | **behoben** — `idx:` steht jetzt im Listen-Abschnitt, und `Unknown list method: set` nennt es |
| 11 | `try` in verschachteltem `if` | **nicht reproduzierbar** — Reduktion grün (`tests/test_try_in_nested_if.aiml`); die cai-Ursache lag anders |

Nebenbefunde am `sys.arg_dump`-Fix desselben Branches: zwei Felder, die auf dasselbe Flag
antworten, sind jetzt ein Compile-Fehler, und ein Alias `given` ist wie `positional` reserviert
(spec/23 F23-63, Fälle in `tests/test_arg_dump_strict.aiml`).

**Richtigstellung zu meinen früheren Anmerkungen in cai:**
- „Es gibt kein `parse_f64`“ greift zu kurz. Mit `cast` geht es: `{ cast: [{ call: [s.parse_f32] }, f64] }`.
  Offen ist nur die Genauigkeit, siehe Punkt 5.
- „`list.set` fehlt“ ist falsch. Ein Element schreibt man mit `idx: [liste, i, wert]` (spec/10 §2). Das
  funktioniert auch für Listen von Klassen (p2). Der Fehler, den ich in cai umgangen habe, ist ein anderer:
  Punkt 2.

## Fehler (nachgestellt)

### 1. Gleitkomma-Literale sind f32, auch in f64-Zusammenhang — hoch

```yaml
- decl: [val_a, f64, 0.1234567891]          # ergibt 0.123456791043282
- decl: [val_b, f64, { "/": [1.0, 3.0] }]   # ergibt 0.333333343267441
- decl: [one, f64, 1.0]
- decl: [three, f64, 3.0]
- decl: [val_d, f64, { "/": [one, three] }] # ergibt 0.333333333333333 (richtig)
```

Das Literal wird offenbar als f32 erzeugt und erst danach auf f64 erweitert. Rechnen mit f64-Variablen ist
exakt, Literale sind es nicht.

- **Folge:** Jede Konstante in einer f64-Rechnung verliert ab der 8. Stelle. In cai sind davon zum Beispiel
  Lernraten und Schwellen betroffen.
- **Erwartet:** Ein Literal im f64-Zusammenhang (`decl … f64`, Operand einer f64-Rechnung) wird als f64
  erzeugt, wie in spec/04_1 §7 („`f32` → `f64` is always exact“ setzt einen f64-Ausgangswert voraus).

### 2. Liste von Klassen: get → ändern → remove → insert zerstört Zeichenketten — hoch

```yaml
- decl: [cur, Item, { call: [items.get, row] }]
- set: [cur.part, "test${row}"]
- call: [items.remove, row]
- call: [items.insert, row, cur]
# danach: items[0].name und items[1].name sind Speichermüll ("���"), items[2] stimmt
```

Vermutlich gibt `remove` die Zeichenketten des Elements frei, die auch `cur` noch hält (`get` liefert eine
Kopie ohne incref?). Ein Neuaufbau der Liste mit `push` geht; `idx` habe ich hier nicht geprüft.

- **Erwartet:** Nach spec/61 hält `cur` eigene Referenzen, und `remove` darf sie nicht freigeben.

### 3. Methodenaufruf als Wert-Argument vom Klassentyp: falsches IR — mittel

```yaml
- print: [{ call: [<.line, { call: [<.scores.get, 0] }] }]   # line(score: Score)
# Function 'Holder_show' verification FAILED: Call parameter type does not match function signature!
#   %call_method = call ptr @Holder_line(ptr %this_ptr, ptr %12)
```

Mit einer Zwischenvariable (`decl: [score, Score, { call: [<.scores.get, 0] }]`) geht es.

- **Erwartet:** Gleiches Verhalten wie mit der Zwischenvariable, oder ein Compilerfehler mit Ort statt
  „LLVM verification failed“.

### 4. `math.exp`, `math.log` usw. rechnen nur in f32 — mittel

```yaml
- decl: [val_exp, f64, { call: [math.exp, 1.0] }]   # 2.71828174591064 (e = 2.718281828459045)
```

spec/43_0 §7 dokumentiert nur f32. Für Statistik und Lernen (logistische Regression in cai W3) fehlen
f64-Fassungen.

- **Vorschlag:** `math.exp`/`log`/`sqrt`/`pow` mit f64-Argument rechnen in f64 (Überladung), oder eigene
  Namen (`math.exp64` …).

## Fehlende Bausteine

### 5. `string.parse_f64`

`parse_f32` + `cast` geht, verliert aber ab der 8. Stelle: `"0.123456789"` → `0.123456791043282`. Gebraucht
wird ein `strtod`-Gegenstück, wie `parse_f32` (spec/09 „`string.parse_f32()`“).

### 6. Zahlen mit fester Nachkommastellenzahl formatieren

Die Interpolation schreibt f64 mit bis zu 15 Stellen (`0.333333333333333`), f32 mit 6. Für Berichte fehlt
etwas wie `{ call: [sys.format_fixed, wert, 3] }` → `"0.333"`. cai rechnet dafür heute über ganze
Tausendstel.

### 7. Zufall der Tensor-Laufzeit lässt sich nicht seeden

`tensor.randn`/`randn_ex` nutzen `rand()` (Box-Muller in `aiml_tensor_randn`). Es gibt keinen Seed, also
keine reproduzierbaren Startwerte über Prozesse und Plattformen. cai baut sich deshalb eigene Startwerte mit
einem LCG.

- **Vorschlag:** `tensor.seed(n)` mit eigenem, plattformunabhängigem Generator.

### 8. Doku: `begin_graph` gibt auch selbst angelegte Tensoren frei

`aiml_tensor_begin_graph` sammelt Tensoren mit `is_param=1`, ohne Gradient, ohne Graphknoten und mit
`alloc_epoch < epoch-1` ein. Das trifft auch vom Benutzer vor der Schleife angelegte Index- oder
Konstantentensoren, zum Beispiel `tensor.zeros(n)` für Kennungen. In der zweiten Runde stürzt dann
`embedding_lookup` mit einer unsinnigen Form ab.

- spec/50 §14 sagt nur „resets the graph“.
- **Vorschlag:** Dort beschreiben, wie Adam-Zustände geschützt werden (`adam_step` frischt die Epoche auf).
  Oder ein Weg, einen Tensor als dauerhaft zu markieren (`t.keep`).

## Diagnose und Bedienung

### 9. Verschachtelte Anführungszeichen in `${ { call: … } }`

`"… ${ { call: [<.f, "x"] } } …"` endet als YAML-Fehler („end of map not found“), oft Zeilen entfernt vom
Fehler. Abhilfe ist eine Zwischenvariable.

- **Vorschlag:** Den Fall in spec/09_1 nennen, und im Compiler bei YAML-Fehlern in Zeilen mit `${ {` auf die
  Ursache hinweisen.

### 10. Elementschreiben schwer zu finden

`idx:` steht nur im Kopf von spec/10 §2. Wer `list.set` sucht, findet nichts; ich habe stattdessen
`remove`/`insert` benutzt und bin in Punkt 2 gelaufen.

- **Vorschlag:** Bei `Unknown list method: set` auf `idx: [liste, i, wert]` hinweisen.

## Beobachtet, nicht klein nachgestellt

### 11. `try` in verschachteltem `if` einer langen Methode

In cai `Cli.run` (über 250 Zeilen) ließ ein `try` innerhalb eines `if` danach `this.out` und andere Methoden
nicht mehr auflösen („Unknown method“). Nach dem Auslagern in eine eigene Methode ging es. Ein kleines
Programm dafür steht noch aus.

## Programme

Die Programme liegen als `p1.aiml` bis `p5.aiml` im Scratchpad der cai-Sitzung. Hier stehen die nötigen Teile:

- **p1 (Punkt 2):** `class Item {name, part: string}`. Drei Elemente pushen, dann pro Zeile get → `part`
  ändern → remove → insert, dann ausgeben.
- **p2 (Richtigstellung):** `idx: [items, 0, neu]` und danach `{ at: [items, 0] }`. Ergebnis `neu`, richtig.
- **p3/p4 (Punkte 1, 4, 5):** siehe die Blöcke oben.
- **p5 (Punkt 3):** `class Holder { scores: list<Score>; line(score: Score); show() }`. In `show`:
  `{ call: [<.line, { call: [<.scores.get, 0] }] }`.
