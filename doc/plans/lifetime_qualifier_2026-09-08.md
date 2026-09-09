# AIML `lifetime` — Ownership-Qualifier (Design → Spec)

Stand: 2026-09-08. Status: Design, vom Nutzer festgelegt; Grundlage für die
Compiler-Umsetzung. Ersetzt den verworfenen „WP-C escape-analysis"-Ansatz
(Pseudo-GC, opak) durch **explizite, compile-zeitliche Single-Owner-Semantik**.

## 1. Motivation

Das Speicherleck entsteht, weil Heap-tragende Werte (string/list/map/model) beim
Scope-Ende oder Überschreiben nicht freigegeben werden. Die Lösung ist **kein GC**
(Laufzeit-Reachability, opak) und **keine Escape-Analyse** (Ganzprogramm-Inferenz,
für Mensch/KI nicht lokal ablesbar), sondern **Ownership**: ein Wert hat genau einen
Besitzer, dessen Tod (Scope-Ende / Überschreiben / Container-Drop) den Heap
deterministisch zur Übersetzungszeit freigibt. Transfers sind **syntaktisch sichtbar**.

Effizienz: alles Compile-Zeit; `free` an bekannter Stelle (wie C++-Destruktor/Rust-
drop), **kein** Laufzeit-Tracing, **kein** Refcount. Move = Zeiger-Transfer (zero-copy).
Das ersetzt „nie freigeben" (OOM) durch begrenzten Speicher — netto effizienter.

## 2. Der Qualifier

Am **Variablen-Decl** (wie bestehende Decl-Qualifier, z.B. `access`):

```
- decl: [x, MatchSession, { lifetime: scope }, <init>]     # scope = Default, weglassbar
- decl: [buf, MatchSession, { lifetime: persistent }]      # überlebt die Scope
- decl: [h, FileHandle, { lifetime: consumed }, <init>]    # MUSS rausgemoved werden
```

Werte:

| Wert | Bedeutung | Drop am Scope-Ende? |
|---|---|---|
| **`scope`** (Default) | Owned; stirbt am Scope-Ende — außer vorher konsumiert. | ja, sofern nicht konsumiert |
| **`persistent`** | Owned, aber an einen längerlebigen Besitzer gebunden (Member/Box/static). | nein |
| **`consumed`** | Lineare Pflicht: MUSS genau einmal rausgemoved werden (Coding-Hilfe). | nein (Move ist Pflicht) |

Nur Heap-tragende Werte sind betroffen. Skalare (i32/f32/i64/bool/enum) haben keinen
Heap → Qualifier bedeutungslos/ignoriert.

## 3. Ownership aus Typ + Initializer (0 Qualifier für den Normalfall)

Der Besitz ist für ~alle Symbole schon **lokal sichtbar**, ohne Qualifier:

- **Wertetyp `T`** (string/list/map/model) = **owned** (Default `scope`).
- **`ptr<T>` aus `at`/`get`/`&`/Parameter** = **Borrow** — besitzt nie, wird nie
  gedroppt, gilt nur innerhalb der Lebenszeit des Besitzers. **Borrows escapen nicht**
  (Speichern in Member/Container = Compile-Fehler → erzwingt Copy/Move).
- **`ptr<T>` aus `{new: T}`** = **owned box** (Default `scope`: Pointee am Scope-Ende frei).

Der Qualifier ist also der **Escape-Hatch**, den man nur bei Abweichung schreibt
(`persistent`, `consumed`) — nicht pro Deklaration.

## 4. Konsum: push / return / consuming functions

Vereinbarung: **`push` und `return` konsumieren immer** (Move). Generell gilt: jede
**konsumierende Funktion** nimmt den Besitz ihres Arguments.

- Ein **`scope`**-Local, das konsumiert wird, ist danach **moved/tot** → **kein** Drop
  am Scope-Ende (der Default `scope` wird durch den Konsum überschrieben). Use-after-move
  = Compile-Fehler.
- Ein **`persistent`**-Local ist vom Konsum **ausgenommen**: die konsumierende Op
  **kopiert** stattdessen (Original überlebt), und es wird am Scope-Ende nicht gedroppt.
- Ein **`consumed`**-Local: siehe §5.

Kopierende Gegenstücke bleiben explizit: `at_copy`, `push_from`, Zuweisung = Deep-Copy
(Quelle behält ihres, wird normal am Scope-Ende gedroppt).

## 5. `consumed` = lineare Coding-Hilfe

`consumed` erzwingt, dass ein owned Wert **weitergereicht** und nicht versehentlich
gedroppt wird (z.B. ein Handle/Resource, der übergeben werden MUSS):

- Erreicht ein Pfad das Scope-Ende, **ohne** das Local konsumiert zu haben → **Warnung**
  („consumed-Wert wird nur bedingt/nicht konsumiert").
- Doppelter Konsum → Compile-Fehler (Use-after-move).

Es ist reine Diagnose-Hilfe; das Laufzeitverhalten entspricht einem konsumierten `scope`.

## 6. Kein Runtime-Drop-Flag → compile-zeit-eindeutiger Konsum

Bewusste Entscheidung: **keine versteckten Runtime-Bools** (kein „drop flag"). Folge:
der Konsum-Status muss zur Übersetzungszeit pro Kontrollfluss-Pfad eindeutig sein.

- Wird ein `scope`-Local in verschiedenen Branches unterschiedlich konsumiert und die
  Branches mergen, kann der Drop ohne Flag nicht eindeutig emittiert werden → **Diagnose**:
  „bedingter Konsum — in allen Zweigen oder in keinem konsumieren" (bzw. Konsum vor die
  Verzweigung ziehen, oder eine Copy nutzen). Bei `consumed` ist es eine **Warnung**.
- Das ist eine leicht striktere **Compile-Regel** statt Laufzeit-Overhead — null
  Runtime-Kosten, lokal ablesbar. Realer Code konsumiert meist unbedingt oder symmetrisch.

## 7. Was die heutige kaputte Mitte ersetzt

Heute teilen `at`/`get`/`set`/`push` per flacher Kopie den Heap (zwei Besitzer) — die
Ursache, warum Ownership bisher nicht bestimmbar war. Neu, eindeutig:

| Op | Semantik |
|---|---|
| `at`/`get` | Borrow (`ptr<T>`-View), besitzt nicht |
| `at_copy` | owned Deep-Copy |
| `push` | Move (Quelle konsumiert) |
| `push_from` | Deep-Copy in den Container |
| Zuweisung `set: [dst, src]` | Deep-Copy (bzw. Move, wenn src ein konsumierbares scope-Local ist); altes `dst` wird vorher gedroppt |

## 8. Compiler-Aufgaben (Umsetzung, gegatet durch die Sanitizer-Harness)

1. Ownership pro Symbol aus Typ + Initializer + Qualifier bestimmen (lokal, keine
   Ganzprogramm-Analyse).
2. Drop (`aiml_ser_free_decoded` / `decref` / Container-Drop) an Scope-Ausgängen für
   owned, nicht-konsumierte Locals emittieren — pro Kontrollfluss-Pfad.
3. Konsum an `push`/`return`/konsumierenden Funktionen als Move behandeln; Quelle als
   moved markieren; Use-after-move diagnostizieren.
4. `persistent`: kein Auto-Drop; Konsum → Copy.
5. `consumed`: Warnung bei un-konsumiertem Pfad; Fehler bei Doppel-Konsum.
6. Borrows scope-lokal halten (Speichern = Fehler).
7. `set`/Überschreiben: RHS zuerst, altes `dst` droppen, dann übernehmen (self-assign-sicher).

Jede Iteration gegen `tests/run_memory_sanitizers.sh` (Kontrolle erkannt + Proben
memory-safe, „definitely lost" sinkt, kein neuer Invalid-Free/UAF).

## 9. Abgrenzung

Kein GC, kein Refcount als Default (`shared` bleibt separate, seltene Ausnahme, falls je
nötig — bewusst NICHT Teil dieses Qualifiers). Keine Escape-Analyse. Bestehende
Wertsemantik (by-ref Listen/Maps, by-value Model/Class-Args) bleibt; neu ist nur die
deterministische Freigabe + die sichtbaren Transfer-Regeln.
