# AIML Pointer And Struct Array Implementation Plan - 2026-04-16

## Ziel

`array<ptr<T>:N>` und `array<MyStruct:N>` sowie mehrdimensional `array<...:N:M:...>` in AIML unterstuetzen.

Beispiel:

```aiml
- decl: [nodes, "array<ptr<MyNode>:16>"]
- idx: [nodes, 0, some_node_ptr]
- decl: [n0, "ptr<MyNode>", { at: [nodes, 0] }]

- decl: [points, "array<Point:16>"]
- idx: [points, 0, p0]
- decl: [p1, Point, { at: [points, 0] }]
```

## Warum das aktuell nicht sauber spezifiziert ist

Die aktuelle Array-Implementierung ist auf primitive Werte ausgerichtet.

Relevante Stellen:

- Typauflösung: [context.h](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h:480>)
- Array-Read via `at`: [ast_compiler_expr.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp:882>)
- Array-Write via `idx`: [ast_compiler_stmt.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp:854>)
- Array-Properties `.len/.data_ptr/.dimX`: [ast_compiler.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp:1212>)
- Aktuelle Tests: [test_array.aiml](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_array.aiml:1>)

Die Implementierung baut LLVM-Arrays schon generisch aus `elemType + count`. Das ist gut:

- `ptr<T>` ist auf LLVM-Ebene nur ein Pointer-Typ und damit array-faehig
- `MyStruct` ist auf LLVM-Ebene ein echter Struct-Typ und damit ebenfalls array-faehig

Das eigentliche Thema ist also nicht LLVM, sondern:

1. Sprachregel
2. Typvalidierung
3. Initialisierung/Null-Semantik
4. Copy-/Store-Semantik
5. Cast-Regeln bei `idx`
6. Tests und Doku

## Grundentscheidung

### Empfehlung

Explizit erlauben:

1. `array<ptr<T>:N>`
2. `array<MyStruct:N>` mit C-naher Inline-Semantik

### Staffelung

Sofort erlauben:

- `array<ptr<T>:N>`
- `array<MyStruct:N>` fuer Structs mit klarer Value-Semantik

Noch nicht erlauben:

- `array<string:N>`
- `array<list<i32>:N>`
- `array<any:N>`
- `array<map<K:V>:N>`

Grund:

- Pointer-Arrays sind einfach und klar.
- Struct-Arrays sind mit C-naher Value-Semantik gut machbar.
- Heap-Container als direkte Array-Elemente oeffnen sofort Fragen zu Ownership, Serialize und Copy-Regeln.

## Erwartetes Semantikmodell

### 1. Pointer-Arrays

Erlaubt:

- `ptr<T>`
- `ref<T>` nur wenn `ref<T>` intern ohnehin Pointer-Semantik hat und im Array nicht anders behandelt wird
- roher `ptr`

Nicht Teil dieses ersten Schritts:

- Pointer plus automatische Lebensdauerverwaltung

### 2. Default-Wert

`array<ptr<T>:N>` wird null-initialisiert.

Das bedeutet:

- jedes Element ist initial `null`
- Read auf ungesetztes Element liefert `null`

### 3. Zugriff

Lesen:

```aiml
- decl: [p, "ptr<MyNode>", { at: [nodes, 3] }]
```

Schreiben:

```aiml
- idx: [nodes, 3, p]
- idx: [nodes, 4, null]
```

### 4. `.data_ptr`

`.data_ptr` bleibt als rohes `i8*` erlaubt.

Das ist fuer C-Interop ausreichend und konsistent zur aktuellen Array-API.

### 2. Struct-Arrays

`array<MyStruct:N>` folgt C-naher Value-Semantik:

1. Das Array ist ein zusammenhaengender Block aus `N` Struct-Werten.
2. `at` liest den Struct-Wert am Index.
3. `idx` schreibt den Struct-Wert am Index.
4. Kein implizites Heap-Management.
5. Keine Pointer-Indirektion, ausser sie steckt bereits in den Feldern des Structs.

Wichtig:

- `array<MyStruct:N>` ist **nicht** dasselbe wie `array<ptr<MyStruct>:N>`
- das erste speichert Werte inline
- das zweite speichert nur Adressen

### 3. C-nahe Copy-Semantik

Fuer `array<MyStruct:N>` gilt zunaechst:

- Lesen per `at` ist ein Value-Load
- Schreiben per `idx` ist ein Value-Store
- semantisch wie bei einem C-Array von Structs

Das ist besonders sauber fuer triviale/POD-artige Structs.

### 4. Nicht-triviale Structs

Wenn `MyStruct` Felder wie diese enthaelt:

- `string`
- `list<T>`
- `map<K:V>`
- `buffer`
- `ptr<U>`

dann bleibt die Semantik trotzdem zunaechst **Value-Copy auf Feldebene**, also C-nah und nicht automatisch deep.

Das bedeutet:

- Pointerfelder werden als Pointerwerte kopiert
- Heap-Containerfelder verhalten sich nach ihrer bestehenden Repräsentation

Diese Semantik soll nicht versteckt "verbessert" werden. Falls spaeter Deep-Copy-Regeln gewuenscht sind, ist das ein eigenes Feature.

## Implementierungsschritte

## P1 - Typsystem sauber definieren

### Ziel

`array<ptr<T>:N>` und `array<MyStruct:N>` duerfen nicht mehr nur zufaellig funktionieren, sondern muessen explizit als erlaubte Faelle gelten.

### Stellen

- [context.h](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h:480>)
- optional Diagnose-/Validation-Code in den AST-Buildern oder Type-Checker-Stellen

### Aenderung

1. Beim Parsen von `array<...>` den Elementtyp nicht nur an `getLLVMType(parts[0])` delegieren.
2. Zusaetzlich eine Sprachregel einziehen:
   - primitive Typen erlaubt
   - `ptr<...>` erlaubt
   - `ref<...>` optional erlaubt
   - Struct-/Model-Typen erlaubt
   - Heap-Container-Typen wie `string`, `list<T>`, `map<K:V>`, `buffer` weiterhin erstmal nicht als direkte Array-Elementtypen

### Warum

Ohne diese Regel ist die aktuelle Situation instabil:

- LLVM kann Pointer-Arrays zwar bauen
- aber die Sprache behauptet in der Doku bisher das Gegenteil

### Gewuenschte Fehlermeldung

Beispiel:

`array element type 'string' is not supported; allowed: primitive scalars, ptr<T>, struct/model value types`

## P2 - `at` fuer Pointer- und Struct-Arrays absichern

### Ziel

Lesen aus `array<ptr<T>:N>` und `array<MyStruct:N>` muss korrekt typisiert zurueckkommen.

### Stelle

- [ast_compiler_expr.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp:882>)

### Ist-Zustand

Der Code macht:

1. `elemTy = getLLVMType(parts[0])`
2. `gep`
3. `load elemTy`

Das ist fuer Pointer und Structs grundsaetzlich schon richtig.

### Aenderung

1. Keine Sonderbehandlung im GEP-Pfad noetig.
2. Aber:
   - sicherstellen, dass `sym->typeName` wirklich `array<ptr<T>:...>` oder `array<MyStruct:...>` traegt
   - ggf. Typcheck beim Zielausdruck/Assignment verbessern
3. Tests fuer:
   - load eines gesetzten Pointers
   - load eines `null`-Slots
   - Vergleich mit `null`
   - load eines Struct-Werts
   - load eines 2D Struct-Arrays

## P3 - `idx` fuer Pointer- und Struct-Arrays korrigieren

### Ziel

Schreiben in `array<ptr<T>:N>` muss Pointerwerte und `null` sauber akzeptieren.
Schreiben in `array<MyStruct:N>` muss Value-Stores fuer Structs sauber akzeptieren.

### Stelle

- [ast_compiler_stmt.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp:854>)

### Problem

Die aktuelle Cast-Logik ist stark numerisch gedacht:

- int -> float
- float -> int
- int width casts

Fuer Pointer fehlt eine explizite Regel.
Fuer Struct-Werte muss abgesichert werden, dass keine unnoetige numerische Cast-Logik dazwischenlaeuft.

### Aenderung

Im Array-`idx`-Pfad diese Faelle hinzufuegen:

1. `storeVal` ist Pointer und `elemTy` ist derselbe Pointertyp:
   - direkt speichern

2. `storeVal` ist `null` und `elemTy` ist Pointertyp:
   - `ConstantPointerNull` oder passender null-pointer cast

3. `storeVal` ist anderer Pointertyp und `elemTy` ist Pointertyp:
   - erstmal nur erlauben, wenn exakt gleicher Typ
   - spaeter optional `bitcast` fuer kompatible Pointertypen

4. Pointer in Nicht-Pointer-Array:
   - klare Fehlermeldung

5. `storeVal` ist Struct und `elemTy` ist exakt derselbe Struct-Typ:
   - direkt speichern

6. Struct in Nicht-Struct-Array oder Struct-Typ-Mismatch:
   - klare Fehlermeldung

### Empfehlung

Am Anfang keine impliziten Pointer-Bitcasts zwischen `ptr<A>` und `ptr<B>`.

Warum:

- einfacher
- typsicherer
- weniger Debug-Horror

### Struct-Regel

Bei Struct-Arrays keine impliziten Typanpassungen.

Nur erlauben:

- identischer Struct-Typ

Nicht erlauben:

- "aehnliche" Structs
- Feld-fuer-Feld automatische Konvertierung
- verdeckte Reinterpretation

## P4 - Initialisierung und Copy-Semantik explizit testen

### Ziel

Pointer-Arrays muessen garantiert null-initialisiert sein.
Struct-Arrays muessen garantiert wertmaessig null-/zero-initialisiert sein, entsprechend der bestehenden Alloca-Initialisierung.

### Stellen

- Compiler-Alloca-Initialisierung fuer Arrays
- Tests in [test_array.aiml](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_array.aiml:1>)

### Aenderung

Neue Testfaelle:

1. `array<ptr<T>:4>` deklarieren
2. erstes Element lesen
3. gegen `null` pruefen
4. Pointer speichern
5. denselben Pointer wieder lesen
6. Slot wieder auf `null` setzen

Zusaetzliche Struct-Tests:

1. `array<MyStruct:4>` deklarieren
2. ersten Eintrag lesen
3. Felder auf Default/Zero pruefen
4. Struct-Wert speichern
5. denselben Struct-Wert wieder lesen
6. in 2D-Variante schreiben/lesen

## P5 - Properties fuer Pointer- und Struct-Arrays pruefen

### Ziel

`.len`, `.dimX` und `.data_ptr` muessen auch mit Pointer- und Struct-Elementen korrekt bleiben.

### Stelle

- [ast_compiler.cpp](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp:1212>)

### Bewertung

`.len` und `.dimX` sind unabhaengig vom Elementtyp schon korrekt.

`data_ptr` ist ebenfalls prinzipiell korrekt, weil nur auf das erste Element gezeigt und danach nach `i8*` gecastet wird.

### Was zusaetzlich noetig ist

Nur Tests:

1. `arr.len`
2. `arr.dim0`
3. `arr.data_ptr` fuer Pointer-Array kompilieren
4. `arr.data_ptr` fuer Struct-Array kompilieren

Nicht zwingend als Runtime-Test, aber mindestens Compile-Test.

## P6 - Dokumentation aktualisieren

### Stellen

- [52_arrays.md](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/52_arrays.md:1>)
- evtl. [aiml_reference_guide.md](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/aiml_reference_guide.md>)

### Aenderung

Die Doku sagt aktuell:

- keine Pointer/String/Struct-Elemente

Das muss fuer diese Erweiterung ersetzt werden durch:

- primitive Typen erlaubt
- `ptr<T>` erlaubt
- Struct-/Model-Wertetypen erlaubt
- `string`, `list<T>`, `map<K:V>`, `buffer` weiterhin nicht erlaubt

### Beispiele ergaenzen

```aiml
- model:
    name: Node
    fields:
      - [value, i32]

- decl: [nodes, "array<ptr<Node>:8>"]
- decl: [n, "ptr<Node>", { new: Node }]
- set: [n.value, 42]
- idx: [nodes, 0, n]
- decl: [r, "ptr<Node>", { at: [nodes, 0] }]
```

Und Struct-Beispiel:

```aiml
- model:
    name: Point
    fields:
      - [x, f32]
      - [y, f32]

- decl: [pts, "array<Point:8>"]
- decl: [p, Point]
- set: [p.x, 1.0]
- set: [p.y, 2.0]
- idx: [pts, 0, p]
- decl: [r, Point, { at: [pts, 0] }]
```

## P7 - Tests erweitern

### Datei

- [test_array.aiml](</media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_array.aiml:1>)

### Neue Testgruppe

#### Phase F: Pointer arrays

1. Declaration + null-init
2. Store/load same pointer
3. Store `null`
4. 2D pointer array
5. Optional `.data_ptr` compile smoke test

#### Phase G: Struct arrays

1. Declaration + zero-init
2. Store/load same struct value
3. 2D struct array
4. Optional struct array `.data_ptr`

### Konkrete Testfaelle

```aiml
- model:
    name: PtrBox
    fields:
      - [x, i32]

- decl: [boxes, "array<ptr<PtrBox>:4>"]
- decl: [p0, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect null

- decl: [b, "ptr<PtrBox>", { new: PtrBox }]
- set: [b.x, 7]
- idx: [boxes, 0, b]
- decl: [rb, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect rb != null and rb.x == 7

- idx: [boxes, 0, null]
- decl: [p1, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect null
```

Struct-Beispiel:

```aiml
- model:
    name: Point
    fields:
      - [x, i32]
      - [y, i32]

- decl: [pts, "array<Point:4>"]
- decl: [p, Point]
- set: [p.x, 3]
- set: [p.y, 7]
- idx: [pts, 0, p]
- decl: [r, Point, { at: [pts, 0] }]
# expect r.x == 3 and r.y == 7
```

## Nicht in Phase 1 machen

Diese Punkte bewusst verschieben:

1. `array<string:N>`
2. `array<MyStruct:N>`
3. implizite Pointer-Bitcasts
4. Serialize/Deserialize fuer Pointer- oder Struct-Arrays als neues Spezialfeature
5. Arrays von `any`
6. Listen/Maps/Strings/Buffers als direkte Array-Elemente

Grund:

Das zieht sofort Ownership-, ABI- und Persistenzfragen nach sich.

## Risiken

### 1. Typinkonsistenzen bei `null`

Wenn `null` im Compiler nicht sauber als Pointer-Null fuer den Zieltyp materialisiert wird, brechen `idx` oder Vergleiche.

### 2. Pointertyp-Mismatch

Wenn implizite Pointercasts zu frueh erlaubt werden, wird das Debugging spaeter schwer.

### 3. Structs mit Heap-Feldern

Wenn ein Struct Heap-Felder enthaelt, ist eine Value-Copy weiterhin bewusst nur C-nah und nicht deep.
Das muss dokumentiert werden, sonst erwarten Nutzer moeglicherweise automatisch "smarte" Semantik.

### 4. Doku und Implementierung laufen auseinander

Wenn Pointer-Arrays implementiert werden, aber die Spezifikation primitive-only behauptet, wird das wieder zu "zufaellig geht es"-Verhalten.

## Empfohlene Reihenfolge

1. Typregel fuer Array-Elementtypen einfuehren
2. `idx` Pointer-Casts + Struct-Value-Store sauber machen
3. Pointer- und Struct-Array-Tests schreiben
4. Doku aktualisieren
5. Optional 2D Pointer-/Struct-Array-Tests

## Minimaler erster Sprint

### Dateien

- `src/core/context.h`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_expr.cpp`
- `tests/test_array.aiml`
- `spec/52_arrays.md`

### Ziel

Nach Sprint 1 soll das hier stabil funktionieren:

```aiml
- decl: [nodes, "array<ptr<MyNode>:8>"]
- idx: [nodes, 0, null]
- idx: [nodes, 1, some_ptr]
- decl: [n, "ptr<MyNode>", { at: [nodes, 1] }]
```

und:

```aiml
- decl: [pts, "array<Point:8>"]
- idx: [pts, 0, some_point]
- decl: [p, Point, { at: [pts, 0] }]
```

## Fazit

`array<ptr<T>:N>` und `array<MyStruct:N>` sind in AIML technisch gut machbar.

Der LLVM-Unterbau ist bereits nah dran. Der eigentliche Aufwand liegt in:

1. expliziter Sprachfreigabe
2. Pointer-/`null`-Regeln im `idx`-Pfad
3. klarer Struct-Value-Semantik
4. Tests
5. Doku

Das ist ein sinnvoller, eng begrenzter Ausbau. Kritisch sind nicht Pointer- oder Struct-Arrays an sich, sondern erst direkte Arrays von Heap-Containern.
