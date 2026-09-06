# Plan: List-Builtins fuer AIML

Stand: 2026-08-21

Status: **Implementiert (2026-08-21).** Alle neun Methoden (M1: contains/index_of/
count/extend/remove_value; M2: reverse/sort/sort_desc/shuffle) sind in `ListImpl`
(`src/core/list_impl.{h,cpp}`) + Dispatch (`src/core/ast_compiler_call.cpp`)
umgesetzt und laufen identisch nativ + WASM (`tests/test_list_builtins.aiml`).
Typ-Diagnosen (Struct-Suche, extend-Mismatch, sort-Elementtyp) mit Quellort.
Spec: `spec/10_collections.md`, `spec/aiml_reference_guide.md`.

## 1. Ziel

`list<T>` besitzt derzeit nur die grundlegenden mutierenden Operationen `push`,
`pop`, `get`, `insert`, `remove`, `clear`, `free` sowie die Properties `len` und
`empty`. In groesseren AIML-Projekten entstehen deshalb immer wieder lokale
Schleifen fuer Suche, Mitgliedschaft, Kopieren, Entfernen nach Wert, Umdrehen,
Sortieren und Mischen.

Dieses Vorhaben fuehrt einen kleinen, typgesicherten Satz von List-Builtins ein:

| Methode | Signatur | Prioritaet | Mutation |
|---|---|---:|---|
| `contains` | `(value: T) -> i32` | M1 | nein |
| `index_of` | `(value: T) -> i32` | M1 | nein |
| `count` | `(value: T) -> i32` | M1 | nein |
| `extend` | `(other: list<T>) -> void` | M1 | ja |
| `remove_value` | `(value: T) -> i32` | M1 | ja |
| `reverse` | `() -> void` | M2 | ja |
| `sort` | `() -> void` | M2 | ja |
| `sort_desc` | `() -> void` | M2 | ja |
| `shuffle` | `() -> void` | M2 | ja |

`map`, `filter`, `find(predicate)` und comparator-basierte Sortierung gehoeren
nicht in diesen ersten Umfang. AIML unterstuetzt noch keine benutzerdefinierten
Generics; universelle Callback-Signaturen wuerden daher eine separate
Spracherweiterung benoetigen.

## 2. Bestehende Einbaupunkte

Die Zeilennummern beziehen sich auf den Repository-Stand vom 2026-08-21. Sie
sind Navigationshilfen; vor der Implementierung muss die jeweilige Funktion
noch einmal per Symbol gesucht werden.

| Bereich | Aktuelle Stelle | Bedeutung |
|---|---|---|
| Sprachreferenz | `spec/10_collections.md:16-30` | dokumentiert die aktuellen List-Methoden |
| Kurzreferenz | `spec/aiml_reference_guide.md:155-166` | kompakte Builtin-Liste |
| Call-Dispatch | `src/core/ast_compiler_call.cpp:1223-1321` | erkennt `list<T>` und dispatcht die Methoden |
| List-API | `src/core/list_impl.h:21-32` | Deklarationen der IR-Emitter |
| List-Layout | `src/core/list_impl.cpp:6-20` | `{i32 size, i32 capacity, ptr data}` |
| Wachstum | `src/core/list_impl.cpp:56-111` | `push` inklusive `realloc` |
| Elementzugriff | `src/core/list_impl.cpp:129-146` | `get` und `getElementPtr` |
| Schleifenbeispiel | `src/core/list_impl.cpp:148-187` | IR-Erzeugung fuer `slice` |
| Mutation | `src/core/list_impl.cpp:200-324` | `clear`, `insert`, `remove` |
| Properties | `src/core/utils.cpp:158-159` | `.len` und `.empty` |
| Stringvergleich | `src/core/ast_compiler_expr.cpp:437-457` | null-sicherer `strcmp`-Pfad |
| Primitive Vergleiche | `src/core/ast_compiler_expr.cpp:571-737` | `fcmp`, `icmp`, Pointer-/String-Semantik |
| RNG-Builtin | `src/core/builtin_registry.cpp:3846-3858` | Emit fuer `math.random_int` |
| RNG-Runtime | `src/sys/math_runtime.cpp:406-422` | thread-lokaler `mt19937` |
| Basistest | `tests/test_list.aiml:1-30` | bisherige List-Abdeckung |
| WASM-Testweg | `tools/run_wasm_test.sh` | Einzeltest unter WASM |

Wichtig: List-Methoden sind aktuell kein Eintrag in `builtin_registry.cpp`.
`Compiler::visitCallExpr` behandelt sie direkt in
`src/core/ast_compiler_call.cpp`. Die neuen Methoden muessen fuer M1/M2 in
demselben Dispatch ergaenzt werden, solange die bestehende Architektur nicht
separat refaktoriert wird.

## 3. Sprachvertrag

### 3.1 `contains`

```aiml
- decl: [ids, list<string>]
- call: [ids.push, "a"]
- decl: [has_a, i32, { call: [ids.contains, "a"] }]
# has_a == 1
```

- Gibt exakt `1` oder `0` zurueck.
- Veraendert die Liste nicht.
- Leere Liste ergibt `0`.
- Verwendet dieselbe Gleichheitssemantik wie AIML-`eq`.

### 3.2 `index_of`

```aiml
- decl: [idx, i32, { call: [ids.index_of, "a"] }]
# erster Treffer; -1 wenn nicht vorhanden
```

- Gibt den ersten passenden Index zurueck.
- Gibt bei leerer Liste oder fehlendem Wert `-1` zurueck.
- Keine Exception fuer "nicht gefunden".

### 3.3 `count`

```aiml
- decl: [hits, i32, { call: [ids.count, "a"] }]
```

- Zaehlt alle Werte mit AIML-`eq`-Semantik.
- Ergebnis ist `i32` analog zu `.len`.

### 3.4 `extend`

```aiml
- decl: [left, list<i32>]
- decl: [right, list<i32>]
- call: [right.push, 2]
- call: [right.push, 3]
- call: [left.extend, right]
# left == [2, 3], right unveraendert
```

- Fuegt Wertkopien in Reihenfolge an.
- `left.extend(left)` muss definiert funktionieren. Vor der ersten Mutation wird
  deshalb die Quelllaenge festgehalten; bei moeglichem `realloc` darf kein alter
  Datenpointer ueber einen `push` hinweg behalten werden.
- `list<Model>` kopiert jedes Model wie `push` als Value.
- `list<ptr<T>>` kopiert Pointeridentitaeten.
- Ein inkompatibler Elementtyp ist ein Compile-Fehler.

### 3.5 `remove_value`

```aiml
- decl: [removed, i32, { call: [ids.remove_value, "a"] }]
```

- Entfernt nur den ersten Treffer.
- Gibt `1` bei Entfernung, sonst `0` zurueck.
- Baut intern auf `index_of` plus dem bestehenden `ListImpl::remove` auf.
- Bewusst nicht `remove`, weil `remove(index)` bereits eine andere Semantik hat.

### 3.6 `reverse`

```aiml
- call: [ids.reverse]
```

- In-place, O(n), O(1) zusaetzlicher Speicher.
- Fuer alle Elementtypen erlaubt, da nur ganze LLVM-Werte getauscht werden.

### 3.7 `sort` und `sort_desc`

```aiml
- call: [scores.sort]
- call: [scores.sort_desc]
```

M2 unterstuetzt zunaechst:

- Integer und Enums: signed Vergleich entsprechend dem AIML-Wertetyp.
- Unsigned Integer: unsigned Vergleich.
- `f32`/`f64`: `fcmp olt` beziehungsweise `ogt`; NaN bleibt stabil am Ende.
- `string`: lexikographisch per `strcmp`, null wird wie leerer String behandelt.

Nicht in M2:

- Models/Structs, Maps, verschachtelte Listen, `any` und Interface-Fat-Pointer.
- Pointer werden nicht nach Adresse sortiert; ein solcher Aufruf ist ein
  Compile-Fehler mit Typ und Methodenname.
- Keine comparator-basierte Variante, bis Callback-Typen sauber spezifiziert sind.

Die erste Implementierung soll einen stabilen Insertion-Sort in IR emittieren.
Er ist fuer typische AIML-Gameplay-/UI-Listen ausreichend, vermeidet eine
typ-erased `qsort`-Callback-ABI und funktioniert identisch in AOT und WASM. Ein
spaeterer Runtime-Sort kann nach Profiling folgen.

### 3.8 `shuffle`

```aiml
- call: [deck.shuffle]
```

- Fisher-Yates in-place von `len - 1` bis `1`.
- Fuer alle Elementtypen erlaubt.
- Verwendet denselben RNG wie `math.random_int(0, hi)`.
- Ergebnisreihenfolge ist absichtlich nicht deterministisch.
- Seedbare/replay-faehige Zufallsquellen sind ein separates Vorhaben. Gameplay,
  das deterministische Replays braucht, darf den Builtin bis dahin nicht nutzen.

## 4. Konkrete Compiler-Aenderungen

### 4.1 Gemeinsame Vergleichsemitter extrahieren

Die vorhandene `eq`-Logik in `src/core/ast_compiler_expr.cpp:437-457` und
`src/core/ast_compiler_expr.cpp:571-737` darf nicht als zweite, leicht
abweichende Implementierung in `ListImpl` kopiert werden.

Vorschlag: in `Compiler` oder einem neuen kleinen Helper
`src/core/value_compare.{h,cpp}` zentralisieren:

```cpp
enum class AimlCompareOp { Eq, Lt, Gt };

llvm::Value* emitAimlCompare(
    CompilerContext& ctx,
    llvm::Value* lhs,
    llvm::Value* rhs,
    llvm::Type* valueType,
    const std::string& typeName,
    AimlCompareOp op);
```

Anforderungen:

- `string` verwendet den bestehenden null-sicheren `strcmp`-Pfad.
- Enums verwenden ihren zugrunde liegenden Integerwert.
- Float-Eq bleibt `oeq`, Sortierung definiert das NaN-Verhalten explizit.
- Opaque Handles und normale Pointer verwenden fuer `contains/index_of/count`
  Identitaet, sofern es kein `string` ist.
- Struct-Value-Eq bleibt in M1 unzulaessig, bis eine kanonische Modellgleichheit
  existiert. `extend`, `reverse` und `shuffle` funktionieren trotzdem.

Wenn die Extraktion zu riskant fuer M1 ist, darf M1 lokal nur primitive,
enum-, string- und pointerfaehige Vergleiche implementieren. Dann muss jedoch ein
Follow-up zur Zusammenfuehrung fest im selben Change dokumentiert werden.

### 4.2 `ListImpl` erweitern

In `src/core/list_impl.h:21-32` folgende Deklarationen ergaenzen:

```cpp
llvm::Value* indexOf(CompilerContext&, llvm::Value* listPtr,
                     llvm::Value* needle, llvm::Type* elemType,
                     const std::string& elemTypeName);
llvm::Value* count(CompilerContext&, llvm::Value* listPtr,
                   llvm::Value* needle, llvm::Type* elemType,
                   const std::string& elemTypeName);
void extend(CompilerContext&, llvm::Value* dstPtr, llvm::Value* srcPtr,
            llvm::Type* elemType);
void reverse(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType);
void sort(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType,
          const std::string& elemTypeName, bool descending);
void shuffle(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType);
```

`contains` wird im Dispatch als `indexOf(...) >= 0` erzeugt. `remove_value`
verwendet ebenfalls `indexOf` und ruft nur bei Treffer
`ListImpl::remove(...)`. Dadurch existiert nur eine Suchschleife.

Die neuen Implementierungen kommen in `src/core/list_impl.cpp` nach
`ListImpl::remove`, aktuell ab Zeile 325. Gemeinsame private IR-Helfer fuer
Loop-Blocks und Swap sollen im anonymen Namespace oberhalb von `namespace
ListImpl` stehen.

Beispiel fuer den `indexOf`-Emitter:

```cpp
llvm::Value* indexOf(CompilerContext& ctx, llvm::Value* listPtr,
                     llvm::Value* needle, llvm::Type* elemType,
                     const std::string& elemTypeName) {
    // entry: result = -1, i = 0
    // head:  i < size && result == -1
    // body:  value = load data[i]; eq = emitAimlCompare(..., Eq)
    // hit:   result = i
    // next:  i++
    // exit:  load result
}
```

### 4.3 Call-Dispatch erweitern

In `src/core/ast_compiler_call.cpp:1230-1319` werden nach `get` und vor den
mutierenden Methoden neue Zweige eingefuegt. Konkrete Form:

```cpp
} else if (methodName == "contains" && argVals.size() == 1) {
    llvm::Value* idx = ListImpl::indexOf(ctx, listPtr, arg0,
                                         llvmListElemType, listElemName);
    return ctx.ir_gen->wrap(ctx.builder.CreateZExt(
        ctx.builder.CreateICmpSGE(idx, ctx.builder.getInt32(0)),
        ctx.builder.getInt32Ty()));
} else if (methodName == "index_of" && argVals.size() == 1) {
    return ctx.ir_gen->wrap(ListImpl::indexOf(
        ctx, listPtr, arg0, llvmListElemType, listElemName));
} else if (methodName == "count" && argVals.size() == 1) {
    return ctx.ir_gen->wrap(ListImpl::count(
        ctx, listPtr, arg0, llvmListElemType, listElemName));
} else if (methodName == "extend" && argVals.size() == 1) {
    ListImpl::extend(ctx, listPtr, otherListPtr, llvmListElemType);
    return nullptr;
} else if (methodName == "remove_value" && argVals.size() == 1) {
    // indexOf + conditional remove; return i32 0/1
} else if (methodName == "reverse" && argVals.empty()) {
    ListImpl::reverse(ctx, listPtr, llvmListElemType);
    return nullptr;
} else if (methodName == "sort" && argVals.empty()) {
    ListImpl::sort(ctx, listPtr, llvmListElemType, listElemName, false);
    return nullptr;
} else if (methodName == "sort_desc" && argVals.empty()) {
    ListImpl::sort(ctx, listPtr, llvmListElemType, listElemName, true);
    return nullptr;
} else if (methodName == "shuffle" && argVals.empty()) {
    ListImpl::shuffle(ctx, listPtr, llvmListElemType);
    return nullptr;
}
```

Vor dem Dispatch muss `listPtr` einmal lokal berechnet werden, statt den langen
`static_cast`/`unwrap`-Ausdruck pro Zweig zu wiederholen:

```cpp
llvm::Value* listPtr =
    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
```

Argumentzahlfehler duerfen nicht in die generische Meldung `Unknown list method`
fallen. Fuer jede bekannte Methode soll eine ortsbezogene Diagnose entstehen:

```text
file.aiml:42: list<i32>.contains expects 1 argument, got 0
```

Die bestehende Diagnose bei `src/core/ast_compiler_call.cpp:1319` soll dafuer
`ctx.fmtLoc(call->sourceNode)` erhalten.

### 4.4 Elementtyp des `extend`-Arguments pruefen

Der Receiver liefert `listElemName`. Fuer das Argument muss der statische
`Symbol::typeName` beziehungsweise der Ausdruckstyp auf exakt dasselbe
`list<T>` normalisiert werden. `cascade<X>` darf wie im bestehenden Code bei
`src/core/ast_compiler_call.cpp:1223-1227` nur dann als `list<i32>` gelten, wenn
beide Seiten dieses Layout besitzen.

Fehlerbeispiel:

```text
list<i32>.extend expects list<i32>, got list<string>
```

Keine implizite numerische Konvertierung in M1. Das verhindert teilweise
konvertierte Listen und unklare Ownership bei Struct-/Pointerelementen.

### 4.5 RNG fuer `shuffle`

`shuffle` soll nicht fuer jedes Element einen AIML-Namespace-Dispatch erzeugen.
`ListImpl::shuffle` deklariert direkt dieselbe externe Runtime-Funktion wie
`math.random_int` in `src/core/builtin_registry.cpp:3846-3858`:

```cpp
llvm::Function* randomInt = ctx.getFunction(
    "aiml_math_random_int", ctx.builder.getInt32Ty(),
    {ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()});
```

Der Runtime-Code bleibt in `src/sys/math_runtime.cpp:406-422` unveraendert.
Damit gibt es keine zweite RNG-Quelle und Native/WASM verwenden denselben
exportierten ABI-Namen.

## 5. Sicherheit und Randfaelle

### 5.1 Bounds und leere Listen

- `contains`, `index_of`, `count`, `extend`, `reverse`, `sort` und `shuffle`
  duerfen bei `len == 0` keine Elementpointer bilden.
- `reverse` und `shuffle` muessen bei `len < 2` sofort beenden.
- Die neuen Methoden duerfen die vorhandenen Bounds-Probleme von `pop`,
  `insert` und `remove` nicht erweitern. Deren Haertung ist ein separater Patch.

### 5.2 Reallocation und Aliasing

- `extend` darf einen Quelldatenpointer nicht ueber `push` hinweg cachen.
- `self.extend(self)` muss explizit getestet werden.
- Zwei List-Structs koennen wegen flacher Value-Kopie denselben Datenpointer
  enthalten. Der Plan garantiert nur korrektes Verhalten bei identischem
  List-Struct-Pointer. Allgemeines Alias-Tracking bleibt ausserhalb des Umfangs.

### 5.3 Strings

- Stringgleichheit muss Inhalt vergleichen, nicht Pointeridentitaet.
- `null` und `""` folgen exakt der bestehenden `eq`-Semantik.
- `sort` nutzt `strcmp`; Pointer werden nur umgeordnet, Strings nicht kopiert.

### 5.4 Structs und Fat-Pointer

- `extend`, `reverse`, `shuffle`: erlaubt, weil ganze LLVM-Werte bewegt werden.
- `contains`, `index_of`, `count`, `remove_value`: in M1 nur erlaubt, wenn eine
  kanonische Eq-Semantik existiert; sonst Compile-Fehler.
- `sort`: Structs, normale Pointer und Interface-Fat-Pointer in M2 ablehnen.

## 6. Tests

Neue Datei: `tests/test_list_builtins.aiml`.

### 6.1 M1-Funktionstests

- `list<i32>`: Treffer am Anfang, in der Mitte, am Ende und Miss.
- `list<string>`: Inhaltsgleichheit mit getrennt erzeugten Strings.
- `list<Enum>`: `contains`, `index_of`, `count`.
- Duplikate: `count == n`, `remove_value` entfernt nur den ersten Treffer.
- Leere Liste fuer jede Suchmethode.
- `extend` bewahrt Reihenfolge und laesst die Quelle unveraendert.
- `extend` ueber Kapazitaetsgrenze, damit `realloc` ausgefuehrt wird.
- `self.extend(self)` ergibt exakt die verdoppelte urspruengliche Folge.
- `list<Model>.extend` kopiert ganze Values.
- Negativtest: `list<i32>.extend(list<string>)` kompiliert nicht.
- Negativtest: Struct-Suche meldet den nicht unterstuetzten Elementtyp.

### 6.2 M2-Funktionstests

- `reverse` fuer 0, 1, 2 und ungerade/gerade Elementzahlen.
- `sort` und `sort_desc` fuer Integer, Enum, Float und String.
- Sortierung mit Duplikaten ist stabil.
- Float-Sortierung dokumentiert und testet NaN.
- `shuffle` erhaelt Laenge und Multimenge der Werte.
- `shuffle` auf 0/1 Element crasht nicht.
- Negativtest: `list<Model>.sort` erzeugt eine klare Compile-Diagnose.

### 6.3 Backends

Native:

```bash
./out/aiml/aiml tests/test_list_builtins.aiml
```

WASM:

```bash
tools/run_wasm_test.sh tests/test_list_builtins.aiml
```

Bestehende Regressionen:

```bash
./out/aiml/aiml tests/test_list.aiml
./out/aiml/aiml tests/test_list_list_inner_values.aiml
./out/aiml/aiml tests/test_list_of_iface_ptr.aiml
./out/aiml/aiml tests/test_return_list_from_call.aiml
```

Falls der regulaere Runner Output-basierte Tests erwartet, soll die neue Datei
bei Erfolg `PASS test_list_builtins` ausgeben und bei der ersten Abweichung via
`sys.exit(1)` abbrechen.

## 7. Dokumentation

Nach M1:

- `spec/10_collections.md:16-30`: Methoden, Rueckgaben und Beispiele ergaenzen.
- `spec/aiml_reference_guide.md:155-166`: Kurzreferenz synchronisieren.
- `spec/54_iterators.md:40-50`: den Eintrag `list.iter()` weiterhin als Future
  markieren; Such-Builtins sind eager und ersetzen keinen Iterator.
- `spec/49_static_analysis.md`: neue Typdiagnosen fuer `extend`, Struct-Suche und
  `sort` dokumentieren, sofern sie als always-on Static Checks implementiert
  werden.

Nach M2 zusaetzlich RNG-/Determinismus-Hinweis fuer `shuffle` in
`spec/43_0_math.md` verlinken.

## 8. Umsetzungsschritte

1. API-Vertrag fuer M1 in `spec/10_collections.md` festschreiben.
2. Vergleichsemitter fuer primitive, enum-, string- und pointerfaehige Werte
   bereitstellen oder aus `ast_compiler_expr.cpp` extrahieren.
3. `indexOf`, `count` und `extend` in `ListImpl` implementieren.
4. `contains` und `remove_value` aus `indexOf` zusammensetzen.
5. Dispatch, Argumentzahl- und Typdiagnosen ergaenzen.
6. M1 Native-Tests und danach WASM-Test ausfuehren.
7. Referenzdokumentation aktualisieren.
8. M2 mit `reverse` beginnen; diese Methode braucht keine Vergleichssemantik.
9. `sort`/`sort_desc` mit stabilem Insertion-Sort implementieren.
10. `shuffle` an `aiml_math_random_int` anbinden.
11. M2 Native-/WASM-Tests und bestehende List-Regressionen ausfuehren.

## 9. Abnahmekriterien

- Alle neun Methoden entsprechen den Signaturen aus Abschnitt 1.
- Native und WASM liefern dieselben Ergebnisse, abgesehen von der zufaelligen
  Reihenfolge von `shuffle`.
- Keine neue Runtime-List-ABI; das Layout bleibt `{i32, i32, ptr}`.
- Keine implizite Konvertierung zwischen unterschiedlichen `list<T>`.
- Stringsuche vergleicht Inhalt.
- Nicht unterstuetzte Typen erzeugen Compile-Fehler mit Quellort.
- `self.extend(self)` und Reallocation sind getestet.
- Bestehende List-, Serialisierungs-, Interface-List- und Return-List-Tests
  bleiben gruen.

## 10. Spaetere Erweiterungen

Erst nach stabilen Callback-/Generic-Semantiken bewerten:

- `find(predicate) -> i32` oder `T`
- `find_index(predicate) -> i32`
- `any(predicate)`, `all(predicate)`
- `map<U>(fn) -> list<U>`
- `filter(predicate) -> list<T>`
- `sort_by(comparator)`
- `unique`/`dedupe`
- seedbarer `shuffle(rng)` fuer deterministische Replays

Diese Funktionen duerfen nicht ueber `any`-Boxing simuliert werden, weil das
Typinformation, Performance und AOT-/WASM-Gleichheit verschlechtern wuerde.
1