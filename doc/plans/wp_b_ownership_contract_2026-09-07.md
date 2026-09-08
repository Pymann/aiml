# WP-B: AIML Heap-Ownership-Vertrag

Stand: 2026-09-07. Status: Design/Analyse (kein Compiler-Code). Umsetzung von
[Arbeitspaket B](memory_lifetime_fix_plan_2026-09-07.md#L68) des Memory-Lifetime-Plans.

Grundlage: die empirischen Baselines in
[findings_2026-09-07.md](../../../9rooms_v2/doc/analysis/memory_checks/findings_2026-09-07.md)
(Leak-Probe A–G, Render-Probe A/A0/B/BL/C/CF) und der aktuelle Quellstand von
`ser_runtime`, `list_impl`, `string_runtime` und den Deep-Copy-Sites im Codegen.

Zweck: BEVOR WP-C automatische Drops emittiert, für Compiler UND Runtime EINE
gemeinsame, entscheidbare Ownership-Semantik festlegen. Der Vertrag ist die
Voraussetzung für sicheres Cleanup ohne Double-Free/Use-After-Free.

## 1. Ownership-Taxonomie

Jeder Wert (Local, Temporary, Feld, Container-Element, Rückgabe) trägt zur
Compilezeit genau eine dieser Klassen; wo sie statisch nicht entscheidbar ist,
braucht es ein Laufzeitflag oder eine Compilerdiagnose (kein stilles Default-Free).

| Klasse | Bedeutung | Drop bei Scope-Ende? |
|---|---|---|
| **Static** | Zeigt auf unveränderliche Programmdaten (String-Literal in `.rodata`, konstante Deskriptoren). | **Nie freigeben.** |
| **Borrowed** | Geliehene Sicht auf fremden Besitz (`kv.get`-Cache-View, `list.get`/`at`-Element-Alias, Feldreferenz, by-ref-Parameter). Besitzer und Invalidierung liegen woanders. | **Nie freigeben** (Besitzer räumt auf). |
| **Owned** | Dieser Slot besitzt den Heap allein (frische Model-Instanz, Deep-Copy-Ziel, Interpolations-/Concat-Ergebnis, decode-Ergebnis in einen eigenen Puffer). | **Ja**, genau einmal, in umgekehrter Init-Reihenfolge. |
| **Shared** | Mehrere Besitzer, refcount-verwaltet (managed `aiml_str_*`; künftige `chan<T>`-Handoffs). | **Release** (Refcount−−), nicht `free`. |

Datenflusszustände orthogonal zur Klasse (Plan §8.3): `initialized`,
`maybe-initialized` (bedingte Init → Laufzeitflag), `moved` (Besitz übertragen →
nicht mehr droppen). Ein reines C++-`bool` zur Compilezeit reicht bei bedingter
Init/Move NICHT.

## 2. Per-Herkunft-Vertrag

Spalten: **Ist** = aktuelles Laufzeit-/Codegen-Verhalten (belegt); **Soll** =
Zielvertrag; **Risiko** = was ein naives Cleanup hier brechen würde.

| Herkunft / Operation | Klasse | Ist (Quellstand) | Soll (WP-C) | Risiko |
|---|---|---|---|---|
| Stringliteral | Static | kein Header, `.rodata`-Adresse | nie droppen; nie Header davor lesen (string_runtime.cpp:81 liest Magic VOR beliebiger Adresse) | Free/Decref eines Literals → Crash/UB |
| Interpolation `"${…}"` | Owned | frischer Heap-String, **wird NICHT gedroppt** (Probe F: ~144 B/iter) | am Ende des vollständigen Ausdrucks droppen, außer bei Transfer (store/return/push) | doppeltes Drop, wenn zugleich als Rückgabe transferiert |
| Concat-Akkumulator (ast_compiler_expr.cpp:43–68) | Owned | neuer Akkumulator je Segment, ersetzter nicht released | ersetzten Akkumulator releasen, Endergebnis als owned weiterreichen | Borrowed-Segmente/Literale nicht freigeben |
| String-returnende Runtime (`strdup`/`aiml_str_dup`) | Owned bzw. Shared | gemischt: `aiml_prefab_text_value` = `strdup` (prefab_runtime.cpp:6021), anderes refcounted | pro Producer als owned ODER shared kennzeichnen; Konsumenten daran angleichen | `free` auf refcounted / Decref auf `strdup` |
| Frische Model-Instanz (`decl: [m, T]`) | Owned | Heap-Felder **nicht gedroppt** (Probe B: ~80 B/iter) | typspezifisches Feld-Drop bei Scope-Ende | gemischte Felder (Literal/Borrowed) im Model → kein Blanket-Free |
| Model-Deep-Copy (`decl: [m, T, kv.get_copy]`, ast_compiler_stmt.cpp:586–607) | Owned | `aiml_ser_deep_copy` in zero-init dst; **nicht gedroppt** (Probe A: ~800 B/iter) | dst als owned markieren + Cleanup registrieren; KV-Cache-Slot NICHT mitfreigeben | Cache-Slot-Aliasing → UAF im Cache |
| `kv.get` (Cache-View) | Borrowed | Zeiger in `decoded_cache` (LRU 1024, evict deep-freet) | nie droppen; Invalidierung = Cache-Eviction, dokumentieren | Drop einer Cache-View → Double-Free bei Eviction |
| `kv.get_copy` (Rückgabe) | Owned | decoded_cache-Slot, aber Compiler deep-copyt in unabhängige Local | Local = Owned; Cache-Slot bleibt cache-verwaltet | s.o. Cache-Aliasing |
| `kv.get_copy_at(key, dst)` | Owned (in fremdem Puffer) | frei-dann-decode in Caller-Puffer; **NEU**: `-1` bei ungültigem Blob (dst genullt) | dst = Owned; letzter Inhalt am Scope-Ende droppen; nur bei Ergebnis `1` lesen | dst-Alias auf Cache verbieten (Codegen-Check) |
| `at`/`get` (Element, ENTSCHIEDEN) | Borrowed | heute: flache Kopie in Value-Local, Heap **geteilt** mit Container (kaputte Mitte) | **`at`/`get` → `ptr<T>` Borrowed-View** in den Container-Slot (kein Copy, nie gedroppt), analog `kv.get`. **Neu `at_copy` → `T` Owned-Deep-Copy** (droppable), analog `kv.get_copy`. | View invalidiert bei `push`/`remove`/`clear`/Grow (Realloc) — nicht über Mutation halten |
| `push`/`insert` (list_impl.cpp:56, ENTSCHIEDEN) | Transfer bzw. Copy | kein Element-Lifetime-Descriptor; heute `push` von ptr in Value-Liste = Compile-Error („Zwingende Böe") | **`push` → MOVE**: Struct-Slot flach übernehmen (Heap-Pointer transferieren, kein Deep-Copy) **und Quelle als `moved` neutralisieren**. **Neu `push_from` → Deep-Copy** in den Container (Quelle bleibt gültig). | `push` OHNE Quell-Neutralisierung → zwei Owner → genau „Zwingende Böe"-Crash |
| `clear` (list_impl.cpp:200) | — | setzt **nur size=0** (Elemente lecken; Probe D/G: ~48 B/iter = Buffer) | owned Elemente vorher droppen; Kapazität darf bleiben | Borrowed-Elemente nicht droppen |
| `remove` (list_impl.cpp:287) | — | Slot-Verschiebung ohne Element-Drop | entferntes owned Element droppen; Slots als Transfer verschieben; letzten Slot neutralisieren | verschobene Elemente nicht zusätzlich releasen |
| Container-Drop (Scope-Ende) | Owned | Buffer bleibt (Probe D: ~48 B/iter) | erst Elemente droppen, dann Buffer | Borrowed-Parameter-Container nicht zerstören |
| Zuweisung/Überschreiben | Replace | RHS-Auswertung, altes Ziel bleibt | RHS zuerst sicher auswerten, altes Ziel droppen, neues übernehmen (Plan §8.4-Skizze) | Selbstzuweisung/Quelle zeigt auf alte Felder → nicht zuerst Ziel freigeben |
| by-ref-Parameter (`ptr<T>`, `is_byref_arg`) | Borrowed | Callee besitzt nicht | Callee droppt nie; Caller behält Besitz | Drop im Callee → UAF beim Caller |
| Return / dauerhafte Speicherung | Transfer | Return-Deep-Copy vorhanden (ast_compiler_stmt.cpp:1219–1236) | Ergebnis vor Cleanup sichern, an Caller übertragen, vom Callee-Cleanup ausnehmen | Callee droppt übertragenen Besitz erneut |
| `set_text` (prefab_runtime.cpp:3662) | Borrowed input | `std::string`-Zuweisung kopiert synchron; Eingabe bleibt Caller-Sache (BL-Probe = 0 → set_text leckt NICHT) | Call-Vertrag „borrowed input, synchron kopiert"; owned Eingabe-Temporary danach vom Caller droppen | `free(text)` in set_text (Eingabe evtl. Literal) |
| Pointer/Interface/Klasseninstanz | Borrowed (i.d.R.) | Repräsentation ≠ Besitz | nicht allein wegen Ptr-Repräsentation als besitzendes Model droppen; escaping erkennen | Klasseninstanz/Interface als Model gedroppt → UAF |
| Explizites `free` | manueller Drop | vorhanden | Slot als `moved` markieren → kein zweites Auto-Drop | Doppel-Free Auto+manuell |

## 3. Entscheidungen (Tore für WP-C)

1. **Kein pauschales `aiml_ser_free_decoded` auf beliebige Models.** Ein Model kann
   Literal-, Borrowed- und Owned-Felder mischen sowie später ersetzte/flache Kopien
   enthalten. Eine Owned-Markierung nur am ganzen Model genügt nicht → einheitlicher
   **Feldvertrag** oder Per-Feld-Metadaten. `aiml_ser_free_decoded`/`fre`
   (ser_runtime.cpp:471) nur für kompatibel besessene Decode-Felder; nach Listen-Free
   auch size/capacity nullen.
2. **Cache-Aliasing bleibt tabu.** Weder `kv.get`-Views noch der von `get_copy`
   zurückgereichte Cache-Slot dürfen von Scope-Cleanup gedroppt werden. Nur die vom
   Compiler erzeugte unabhängige Local-Kopie ist Owned.
3. **Verb-Split für Container (ENTSCHIEDEN 2026-09-08), spiegelt `kv.get`/`get_copy`:**
   - `at`/`get` → **Borrowed `ptr<T>`-View** in den Container-Slot (kein Copy, nie
     gedroppt). `at_copy` → **Owned `T` Deep-Copy**. Die heutige kaputte flache
     Value-Kopie (Heap geteilt) entfällt damit ganz.
   - `push` → **MOVE, UNBEDINGT** (eine Bedeutung, KEIN typabhängiger Fork);
     `push_from` → **Deep-Copy** (Quelle bleibt gültig).
   - **`push` = Move-aus-Owned:** `push(x)` transferiert `x`s Heap-Pointer in den Slot
     und **nullt `x`** (Heap-Felder = 0). Der Container ist danach einziger Owner
     (gleiche Technik wie `get_copy_at` `-1`: Ziel genullt, sicher lesbar).
   - **Sicherheits-Reichweite des Nullens (präzise, korrigiert):** das Nullen schützt
     GEGEN Double-Free über den **Quell-Slot `x`** — ein Zeiger auf `x` selbst liest
     danach ein leeres Objekt (definiert). Es macht **NICHT** automatisch bereits
     entkommene Aliase auf `x`s **Heap-Felder** sicher (z.B. ein vor dem Move
     genommenes `ptr` auf `x.name`): dieser Heap gehört jetzt dem Container, der Alias
     zeigt weiter gültig darauf — wird der Container später zerstört, wird der Alias
     zu **UAF**. D.h. Aliase auf gemovte Heap-Felder folgen der Lebensdauer des
     **neuen Owners** (dieselbe Invalidierungsregel wie `at`-Views). „Kein UAF" gilt
     also für den Quell-Slot, nicht unbedingt für vorbestehende Feld-Aliase.
   - Kein Doppel-Owner über den Quell-Slot → kein Wiederaufleben des früheren
     Listen-Einfüge-Crashes ([[aiml_list_push_ptr_value_check]]; Ursache damals war
     Pointer- vs. Model-Größe beim Einfügen, KEIN nachgewiesenes Double-Free — der
     Move-Kontrakt ist hier die saubere Semantik, nicht die „einzig mögliche" Rettung).
   - **Vorbedingung (feste Regel, keine Verhaltens-Gabelung):** `push` verlangt eine
     **Owned**-Quelle. Move aus einer Borrowed-`at`/`get`-View = **Compile-Error** →
     `push_from` nutzen. Zusätzlich lexikalische **Use-after-move-Diagnose** auf `x`
     (Logik-Hilfe, für Speichersicherheit dank Nullen nicht zwingend).
   - **View-Invalidierung** ist eine Eigenschaft von `at` (Borrowed), NICHT von `push`:
     `push`/`remove`/`clear`/Grow reallozieren den Buffer → offene `at`-Views ungültig
     (wie `kv.get`/C++-Iteratoren) — dokumentiert, wo lexikalisch sichtbar diagnostiziert.
   - Borrowed-Referenz-in-Container (Pointee überlebt Container) bleibt separat nur
     über explizites `list<ptr<T>>` möglich.
4. **Replace vor Free bei Überschreiben** (Plan §8.4): RHS zuerst in Temp auswerten,
   dann Ziel droppen, dann Move — sicher bei Selbstzuweisung und Quelle-zeigt-auf-Ziel.
   `aiml_ser_deep_copy` liefert `void` → eine **checked Replace-Variante** mit
   Fehlervertrag ist nötig, bevor belegte Ziele überschrieben werden.
5. **String-Refcounting nicht breiter einsetzen**, bis Managed-Herkunft im Codegen
   gesichert ist. `aiml_str_incref/decref` (string_runtime.cpp:74/81) lesen den
   Magic-Header VOR der übergebenen Adresse; der Größencheck beweist keinen gültigen
   Speicher davor. Für gemischte Runtime-Grenzen sicheren Registry-/Handle-Vertrag
   vorsehen, Registry nebenläufig absichern.

## 4. Semantik: was sich ändert vs. was bleibt

**Bewusst geänderte Semantik (vom Nutzer freigegeben 2026-09-08, §3.3):** `at`/`get`
werden Borrowed-Views statt flacher Value-Kopien; `push` wird Move (Quelle konsumiert);
neu sind `at_copy` und `push_from`. Das ändert beobachtbares Verhalten und ist damit
KEIN „Semantik nur wenn nötig"-Fall mehr, sondern eine bewusste Entscheidung. **Migration
erforderlich:** bestehender 9rooms_v2-Code, der (a) `at`/`get` als unabhängige Kopie nutzt
oder (b) nach `push(x)` `x` weiterverwendet, muss auf `at_copy`/`push_from` umgestellt
werden. WP-C bringt dafür Diagnosen (Use-after-move; Move-aus-Borrowed = Fehler), sodass
betroffene Stellen beim Build sichtbar werden statt still zu brechen. Ein Audit dieser
Muster (Plan-Vorschlag) geht der Aktivierung voraus.

**Unverändert bleibt:**
- Zuweisung/Parameterübergabe (by-ref-Listen/Maps, by-value-Model/Class-Args) — Cleanup
  ist additive Freigabe, KEINE Umdeutung zu Deep-Copy-Zuweisungen.
- KV decoded_cache-Verhalten (LRU 1024, Evict-Deep-Free) unverändert.
- `get_copy_at`-Puffer-Wiederverwendung (spec/31 §6.7.2) unverändert; nur der
  Fehler-Rückgabewert `-1` ist neu.
- Nur wenn eine Semantik-/ABI-Änderung nachweislich unvermeidbar ist, wird der
  konkrete Fall VOR der Umsetzung mit dem Nutzer geklärt (Plan §7.4).

## 5. Abnahme des Vertrags (Plan §3)

Abgedeckt sind Locals, Temporaries, Parameter, Felder, Container, Rückgaben und
Runtime-Grenzen. Ungeklärte Fälle werden NICHT unsicher automatisch freigegeben,
sondern erhalten eine gezielte Compilerdiagnose. Die WP-C-Aktivierung folgt der
Reihenfolge: Container-Element-Regeln → Replace/Return-Transfer → Model-Feld-Drops.
Jeder Schritt wird gegen die isolierten Proben UND ASan/LSan (verbindlich) geprüft.
