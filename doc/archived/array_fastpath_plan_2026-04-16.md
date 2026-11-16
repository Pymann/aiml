# AIML Array Fast-Path Plan - 2026-04-16

## Entscheidung

Wir gehen den leistungsorientierten Weg:

1. `array<f32:N>` in AIML fuer kleine feste numerische Hotpaths nutzen
2. neue C++-Builtins einfuehren, die direkt auf `array.data_ptr` arbeiten
3. keine Rueckkonvertierung ueber `list<f32>` im Hotpath

Ziel ist nicht nur eleganterer AIML-Code, sondern ein real messbarer Fast-Path fuer kleine Feature-Vektoren in TUL.

## Zielbereich

Erste Zielstelle:

- `tul/math/scoring.aiml`

Spaetere Zielstellen:

- `tul/model/candidate_ranker.aiml`

Nicht erstes Ziel:

- grosser Transformer-Trainingspfad
- Listen mit dynamischer Laenge
- persistierte oder stringlastige Daten

## Technischer Befund aus dem C++-Unterbau

Die noetigen Grundlagen sind bereits da:

1. Arrays werden direkt als LLVM-`[N x T]` gebaut:
   [context.h](../../src/core/context.h#L480)

2. Array-Lesen ist direkter `GEP + load`:
   [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L882)

3. Array-Schreiben ist direkter `GEP + store`:
   [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L854)

4. Arrays haben bereits `.data_ptr`:
   [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1212)

5. Builtin-Seite kennt schon Pointer-basierte Stack-Array-Helfer:
   [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8068)

Das bedeutet:

- die Sprachseite ist fuer `array<f32:N>` fast fertig
- der fehlende Teil ist ein Satz kleiner numerischer Builtins, die `i8*`/Rohpointer direkt akzeptieren

## Kernproblem heute

TUL baut kleine Feature-Vektoren aktuell ueber `list<f32>` und `push`.

Beispiele:

- `test/tul/math/scoring.aiml:34`
- `test/tul/math/vector.aiml:12`

`list<f32>` ist im Compiler und Runtime-System ein Heap-Container mit Layout `{size, capacity, data*}`:

- Listenmethoden: [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L911)
- `ListImpl` API: [list_impl.h](../../src/core/list_impl.h#L21)

Das ist flexibel, aber fuer feste 10er-Features unnötig teuer.

## Zielarchitektur

### Phase-1-Typmodell

Wir fuehren **keinen** komplett neuen universellen Vektortyp ein.

Stattdessen:

1. kleine Hotpath-Funktionen in AIML nutzen lokale `array<f32:10>`
2. neue Builtins lesen direkt aus `array.data_ptr`
3. allgemeine `Vector`-/`list<f32>`-Pfade bleiben fuer den Rest des Systems bestehen

Das minimiert Risiko und macht den ersten Fast-Path eng und kontrolliert.

## P1 - Neue Native Builtins definieren

### Ziel

Direkte numerische Operationen auf Rohdatenpointer, ohne `list<f32>`.

### Neue Builtins

Empfohlener erster Satz:

1. `ai.dot_ptr(ptr_a, ptr_b, n) -> f32`
2. `ai.affine_score_ptr(w_ptr, x_ptr, n, bias) -> f32`
3. `ai.argmax_ptr(x_ptr, n) -> i32`
4. optional `ai.copy_f32_ptr(src_ptr, dst_ptr, n) -> void`

### Warum genau diese

Sie decken den ersten Fast-Path fast komplett ab:

- Dot-Produkt
- Affine Score
- Argmax ueber wenige Scores

### C++-Dateien

- `src/sys/ai_runtime.h`
- `src/sys/ai_runtime.cpp`
- `src/core/builtin_registry.cpp`

### API-Vorschlag

```cpp
float aiml_dot_ptr(const float* a, const float* b, int32_t n);
float aiml_affine_score_ptr(const float* w, const float* x, int32_t n, float bias);
int32_t aiml_argmax_ptr(const float* x, int32_t n);
void aiml_copy_f32_ptr(const float* src, float* dst, int32_t n);
```

### AIML-Signatur

Compilerseitig als `i8*`-basierte Builtins registrieren:

- `ai.dot_ptr(i8*, i8*, i32) -> f32`
- `ai.affine_score_ptr(i8*, i8*, i32, f32) -> f32`
- `ai.argmax_ptr(i8*, i32) -> i32`

## P2 - Builtin-Registry anbinden

### Ziel

Die neuen Runtime-Funktionen aus AIML aufrufbar machen.

### Datei

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

### Vorgehen

An den bestehenden `ai.*`-Builtins orientieren.

Vorbild:

- `tensor.from_data`: [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8325)

### Implementierung

1. `i8*`-Pointerargumente direkt entgegennehmen
2. `i32` fuer `n`
3. `CreateCall(...)` auf die neuen Runtime-Funktionen

### Vorteil

`array.data_ptr` passt bereits natuerlich in dieses Modell.

## P3 - AIML-Scoring auf Array-Fast-Path umstellen

### Ziel

`tul/math/scoring.aiml` soll fuer feste 10 Features keinen `list<f32>`-Container mehr aufbauen.

### Datei

- `test/tul/math/scoring.aiml:1`

### Umsetzung

#### 1. Neue lokale Builder

Statt `Vector` direkt:

```aiml
- decl: [feat, "array<f32:10>"]
- idx: [feat, 0, token_count]
- idx: [feat, 1, unique_token_count]
...
```

#### 2. Neue Scoring-Helfer

Zusatzfunktionen:

- `affine_score_fixed10(w: Vector, x: array<f32:10>, bias: f32) -> f32`
- `compute_mode_scores_fixed10(x: array<f32:10>, mode_weights: list<Vector>, mode_biases: list<f32>) -> array<f32:K>` oder zunaechst `list<f32>`

#### 3. Erster Kompromiss

In Phase 1:

- Features als `array<f32:10>`
- Gewichte bleiben `Vector` / `list<f32>`
- `ai.affine_score_ptr(w.data_as_ptr?, x.data_ptr, 10, bias)` ist noch nicht direkt moeglich, solange `Vector` nur `list<f32>` hat

Deshalb gibt es zwei Unteroptionen.

## P4 - Zugriff auf Gewichtsvektoren klaeren

Das ist der einzige echte Designknoten.

### Problem

`WeightVector.weights` und `Vector.data` sind heute `list<f32>`.

`ai.affine_score_ptr(...)` braucht aber Rohpointer auf beide Seiten.

### Option A - Zusaetzliches Listen-Builtin fuer Rohdaten

Neue Property oder Builtin fuer `list<f32>`:

- `list.data_ptr`

Oder Builtin:

- `ai.list_data_ptr(list<f32>) -> i8*`

### Betroffene Stellen

- `ListImpl`-Layout ist bekannt: [list_impl.h](../../src/core/list_impl.h#L21)
- `tensor.from_int_list` extrahiert bereits das Data-Feld aus einem Listenwert:
  [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8337)

### Empfehlung

Fuer Phase 1 ist das die beste Loesung.

Warum:

1. minimal-invasive Erweiterung
2. keine neue generelle `Vector`-Repräsentation
3. sofortiger Nutzen fuer `scoring.aiml`

### Ergebnis

Dann ist moeglich:

```aiml
- decl: [score, f32, { call: [ai.affine_score_ptr, w.weights.data_ptr, feat.data_ptr, 10, bias] }]
```

## P5 - Listen-`data_ptr` fuer primitive Listen einfuehren

### Ziel

Primitive `list<T>` sollen optional einen rohen Datenpointer exponieren koennen.

### Datei

- `src/core/ast_compiler.cpp`
- evtl. `src/core/ast_compiler_call.cpp`

### Vorbild

Array-Property `.data_ptr`:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1231)

### Regel

Erlauben fuer:

- `list<i32>`
- `list<i64>`
- `list<f32>`
- `list<f64>`
- evtl. `list<u8>`

Nicht freigeben fuer:

- `list<string>`
- `list<Model>`
- `list<ptr<T>>`

### Property-Vorschlag

`my_list.data_ptr`

Rueckgabe:

- `i8*`

### Nutzen

Das ist nicht nur fuer TUL hilfreich, sondern generell fuer numerische Builtins.

## P6 - TUL-Scoring wirklich umstellen

### Ziel

Die festen 10er-Featurevektoren gehen von:

`push -> list<f32> -> Vector -> ai.dot`

zu:

`idx -> array<f32:10> -> data_ptr -> ai.affine_score_ptr`

### Zielstellen

- `build_shared_core_features`
- `build_router_features`
- `compute_mode_scores`
- `argmax`

### Phase-1-Pragmatik

`compute_mode_scores` darf in Phase 1 noch `list<f32>` zurueckgeben, wenn nur die Input-Seite bereits array-basiert ist.

Der groesste Gewinn liegt im:

1. Vermeiden des Feature-Listenaufbaus
2. direkten Dot-/Affine-Calls

## P7 - Candidate-Ranker danach

### Datei

- `tul/model/candidate_ranker.aiml`

### Ziel

Kleine feste Featurevektoren fuer Kandidatenscoring ebenfalls auf Arrays plus Pointer-Builtins umstellen.

### Reihenfolge

Erst nach erfolgreichem `math/scoring.aiml`-Umbau.

## P8 - Tests

### AIML-Compiler/Runtime-Tests

Neue Tests im AIML-Projekt:

1. `array<f32:10>` + `.data_ptr` an Native-Builtin
2. `list<f32>.data_ptr` fuer primitive Listen
3. Vergleich Array-vs-List-Ergebnis fuer Dot/Affine

### TUL-Tests

1. `build_shared_core_features` liefert identische numerische Werte wie vorher
2. `compute_mode_scores` liefert identische Scores
3. Router-/SharedCore-Training aendert sich numerisch nicht unkontrolliert

## P9 - Benchmark

### Ziel

Nicht nur „sollte schneller sein“, sondern belegen.

### Benchmark-Idee

Mini-Benchmark fuer 1 Mio Aufrufe:

1. alter Pfad:
   - `build_router_features` via `list.push`
   - `affine_score`

2. neuer Pfad:
   - `array<f32:10>`
   - `ai.affine_score_ptr`

### Erwartung

Der Gewinn sollte vor allem aus diesen Punkten kommen:

1. keine Heap-List fuer Features
2. keine `push`-Kette
3. direkter Pointer-Zugriff in Native Code

## Offene Designpunkte

Diese Punkte muessen noch final entschieden werden, sind aber klein:

### 1. `list.data_ptr` als Property oder Builtin?

Empfehlung:

- Property, weil es zu `array.data_ptr` passt

### 2. `argmax_ptr` nur fuer `f32`?

Empfehlung:

- ja, Phase 1 nur `f32`

### 3. eigener `FixedVector10`-Typ sofort oder spaeter?

Empfehlung:

- spaeter
- Phase 1 nur lokale `array<f32:10>`

## Empfohlene Umsetzungsreihenfolge

1. neue Runtime-Funktionen in `ai_runtime.*`
2. neue `ai.*_ptr`-Builtins in `builtin_registry.cpp`
3. `list<f32>.data_ptr` fuer primitive Listen
4. `tul/math/scoring.aiml` auf Array-Fast-Path umstellen
5. Tests
6. Benchmark
7. danach `candidate_ranker`

## Minimaler erster Sprint

### AIML/C++

1. `src/sys/ai_runtime.h/.cpp`
   - `aiml_dot_ptr`
   - `aiml_affine_score_ptr`
   - `aiml_argmax_ptr`

2. `src/core/builtin_registry.cpp`
   - `ai.dot_ptr`
   - `ai.affine_score_ptr`
   - `ai.argmax_ptr`

3. `src/core/ast_compiler.cpp`
   - `list<f32>.data_ptr` fuer primitive Listen

### TUL

4. `tul/math/scoring.aiml`
   - `array<f32:10>` fuer Features
   - Pointer-Builtins fuer Scoring

## Fazit

Das ist umsetzbar, ohne das Gesamtsystem umzubauen.

Der kritische Punkt ist nicht `array<>` selbst. Der kritische Punkt ist, den numerischen Hotpath von `list<f32>` zu entkoppeln, ohne gleich den gesamten AIML-Container-Stack neu zu designen.

Der sauberste erste Schritt ist deshalb:

1. Pointer-Builtins
2. `list.data_ptr` fuer primitive Listen
3. `tul/math/scoring.aiml`

Damit bekommt TUL einen echten kleinen Array-Fast-Path, der technisch eng begrenzt und messbar ist.
