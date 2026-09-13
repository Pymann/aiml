# Leak-Klasse 3: String-Local-Reassignment / Selbst-Akkumulation (spec/61 §7)

Stand: 2026-09-12. Volatiler Report. Zeilenangaben gelten für den gelesenen Arbeitsstand und
verschieben sich; Codepfade sind zusätzlich per Funktionsname benannt. Alle Pfade relativ zum
AIML-Projektroot (`.../test/aiml`).

Kontext: Jagd auf den 9rooms_v2-Client-Leak (Deckset-Editor, ~2 MB/min WASM-Heap-Wachstum). Über die
neue Diagnose-Kette (memlog-Server + `emscripten_get_callstack`-Attribution im
`AIML_MEM_DIFF`-Runtime + gebatchtes console→memlog-Forwarding) wurde der Leak vollständig auf
`populate_card_full` (jeden Frame pro sichtbarer Kartenzeile) lokalisiert. Dahinter stehen **drei
getrennte Compiler-Leak-Klassen**. Klasse 1 und 2 sind gefixt und gegen `tests/run_memory_sanitizers.sh`
grün. **Dieser Report dokumentiert Klasse 3 — noch OFFEN** — und den fehlgeschlagenen Lösungsversuch.

---

## 1. Die drei Leak-Klassen (Überblick)

| # | Klasse | Beispiel-Shape | Status |
|---|---|---|---|
| 1 | Producer-String an ein **Local gebunden** wird nicht gedroppt | `decl [x, string, {call: producer}]` | **gefixt** (`freshOwned`+`visitDeclStmt`: `ownsReturnFns`-Call-Init → kind-1-Drop) |
| 2 | Producer-String als **transientes Argument** wird nicht gedroppt | `set_text(…, build_card_text(card))`, `gl.tex(mana_stem(col))` | **gefixt** (`visitCallExpr`: Producer-String-Call-Ergebnis in `stmtTempStack` → Statement-Ende-decref; `stringProducerFns`) |
| 3 | **String-Local-Reassignment / Selbst-Akkumulation** gibt den alten Wert nicht frei; **Kopie** aus einem Borrow inkref't nicht | `set:[acc, "${acc}…"]`, `decl [blk, string, line]; set:[blk, "${blk}…"]` | **OFFEN** (dieser Report) |

Klasse 3 ist der **größte Anteil** des Client-Leaks.

---

## 2. Was Klasse 3 genau ist

### 2.1 Wurzelursache

Der Plain-String-Store in `visitSetStmt` (`src/core/ast_compiler_stmt.cpp`, Zweig `propName.empty()`,
`sym->typeName == "string"`) ist ein reiner Pointer-Store:

```cpp
ctx.ir_gen->createStore(val, baseAlloca);   // kein incref der Quelle, kein decref des alten Werts
```

Analog der Decl-Init-Store. Damit implementiert die String-**Local**-Zuweisung das spec/61-§7-Modell
(Copy = incref/independent, Move = Transfer, Reassign = alten Wert freigeben) **nicht**. §7 sagt
ausdrücklich, dass der frühere „shallow-share middle ground" ersetzt wurde — für **Strings-Locals**
wurde er es nie. Konkret zwei Defekte:

1. **Reassign gibt den alten Wert nicht frei.** `set:[acc, neu]` überschreibt den Slot, ohne den
   vorherigen RC-String zu decref'en → bei einem in einer Schleife akkumulierten String leakt jede
   Iteration die vorherige Fassung.
2. **Copy aus einem Borrow inkref't nicht.** `set:[blk, member_or_local]` speichert den rohen
   Pointer → `blk` **aliast** die Quelle (kein eigener RC-Ref). Die Quelle und `blk` würden beim
   Drop denselben Buffer freigeben (Double-Free), bzw. wenn die Quelle zuerst gedroppt wird, dangelt
   `blk` (UAF).

Zusätzlich markiert der Ownership-Analyzer (`src/core/ownership_analyzer.cpp`, `analyzeStmt`, Zweig
`SetStmt`) einen **rebound** Plain-Local konservativ als `Origin::Unknown` („rebound → not provably
owned"). `mayDrop` (`src/core/ownership_flow.cpp`) verlangt `Owned + Live + resources` → für einen
reassign'ten String liefert `mayDrop` **false** → selbst der **finale** Wert wird am Scope-Exit nicht
gedroppt. Das ist die bewusst zurückgestellte Entscheidung; vgl. die Notiz in `visitSetStmt` beim
lokalen Model-Deepcopy: *„a reassigned target leaks the old heap (under-drop, safe) … Free-old-target
+ a fallible-copy contract come with the assignment-dataflow step."*

### 2.2 Wo es im Client auftritt (aus der `AIML_MEM_DIFF`-Attribution)

Alle Sites hängen an `DeckEditorScreen_render → DeckEditorScreen_refresh_library_rows →
populate_card_full` (pro Frame, ~8 sichtbare Zeilen):

| Δ Bytes / Dump | Blöcke | Call-Stack (Leaf → …) | Klasse |
|---|---|---|---|
| +262080 | 3360 | `__aiml_str_concat` ← `CardArtCache_fetch` ← `art_ensure` ← `populate_card_full` | 3 (interner String-Aufbau) |
| +147350 | 1263 | `__aiml_str_concat` ← `ability_block_cached` ← `build_card_text` | 3 (interner `key`/`blk`-Aufbau) |
| +109039 | 5894 | `__aiml_str_concat` ← `mana_stem` ← `render_mana_cost_columns` | 2 (gefixt via `producer`) |
| +40416 | 421 | `__aiml_str_concat` ← `build_card_text` (`out_txt`) | 3 |

Die betroffenen Builder (`client/mock_card.aiml:build_card_text`/`ability_block_cached`,
`client/net_card_art_cache.aiml:CardArtCache_fetch`) bauen ihren String per
`set:[acc, "${acc}…"]`-Selbst-Akkumulation und/oder `set:[blk, line]` (Borrow aus einem
Schleifen-Local `line = generate_ability_text(...)`) auf.

⚠️ **Sicherheitsrelevant für jeden Fix:** `ability_block_cached` gibt auf dem Cache-Hit-Pfad eine
**Cache-View** zurück (`return {call: g_abil_text_cache.get, key}`). Diese Funktion darf **nicht**
`producer` annotiert werden (der Aufrufer würde die Cache-eigene String decref'en → Cache-Korruption).
`build_card_text` kann `card.ability_text` (ein geborgtes Member) zurückgeben — ebenfalls kein
sicherer `producer` ohne incref-on-copy. Klasse 3 muss diese Funktionen also über den
Assignment-Pfad heilen, nicht über Return-Annotationen.

### 2.3 Reproduktion (headless, nativ)

`tests/_mem_sanitize/string_reassign_drop.aiml` (im Baum belassen, **nicht** in
`run_memory_sanitizers.sh` registriert, da OFFEN). Zwei Subjects, RSS-Delta nativ:

- `loop_self_accum`: `decl [acc,string,"seed"]; while { set:[acc,"${acc}-seg-…"] }` — Selbst-Akkumulation.
- `loop_share_accum`: `decl [blk,string,""]; while { decl [line,string,{call producer}]; if … set:[blk,"${blk}|${line}"] else set:[blk,line] }` — Mix aus Borrow-Copy und Selbst-Akkumulation (die `ability_block_cached`-Form).

**Baseline (Parts 1&2, ohne Klasse-3-Fix), N=300000, ~8 Segmente:**

```
string_reassign_drop: self-accum=230400000 B  share+accum=527990784 B  (limit=16777216 B)
```

(Ein früherer Minimal-Probe `set:[acc,"${acc}…"]`, N=200000×8, ergab 281 MB; ≈176 B/Reassignment,
also genau ein geleakter String pro Reassignment.)

---

## 3. Wie ich versucht habe, es zu lösen (fehlgeschlagen → revertiert)

Ziel: das vollständige spec/61-§7-Modell für String-**Locals**, rein additiv, gegatet über die
Sanitizer-Suite.

### 3.1 Die vier vorgenommenen Änderungen

1. **`ownership_analyzer.cpp` `freshOwned`:** für `t == "string"` **immer** `return true` (jeder
   String-Local ist owned → bekommt einen Scope-Drop). Begründung: Codegen sollte Borrows beim
   Kopieren inkref'en und Owned-Temps moven, sodass der Drop immer balanciert ist.
2. **`ownership_analyzer.cpp` Rebind:** `slotType[id]` (aus `DeclStmt::typeName`) eingeführt; beim
   Plain-Local-Rebind den Downgrade `Owned→Unknown` für **String**-Slots übersprungen (Slot bleibt
   `Owned`, damit `mayDrop` den finalen Wert droppt).
3. **`ast_compiler_stmt.cpp` `visitDeclStmt`:** kind-1-Drop für **alle** nicht-`persistent`,
   nicht-`extern` String-Locals registriert (statt nur Interpolation/Producer/`text_value`).
   Zusätzlich **incref-on-copy** am Decl-Init: wenn der Initializer **kein** Owned-Temp aus dem
   `stmtTempStack` ist (also ein Borrow: Variable/Member/`at`/Nicht-Producer-Call/Literal), den
   Init-Pointer inkref'en.
4. **`ast_compiler_stmt.cpp` `visitSetStmt` (Plain-String-Store):**
   - **incref-on-copy:** RHS war kein Owned-Temp (`rhsWasOwnedStringTemp`, geprüft **vor**
     `consumeStringTemp`) → `aiml_str_incref(neu)` (Local bekommt einen eigenen RC-Ref); Owned-Temp =
     Move (kein incref).
   - **decref-old bei Selbst-Referenz:** wenn die RHS eine `StringInterpolationExpr` ist, deren
     `parts` den Ziel-Namen enthalten (`set:[acc,"${acc}…"]`), den **alten** Slot-Wert decref'en. Nur
     diese Form, weil hier die Liveness des alten Werts ohne Move-Dataflow garantiert ist (einen
     bereits gemoveten Wert zu lesen ist ein Compile-Fehler).

   Hilfsfunktionen `inStringTempStack()` (Membership-Check vor `consumeStringTemp`) und
   `emitStrIncref()` (magic-guarded `aiml_str_incref`).

Absicherungen, auf die ich baute: `aiml_str_incref`/`decref` sind magic-guarded
(`string_runtime.cpp`, No-op bei `magic != 'STRN'` und bei Null/Low-Pointer); jede Alloca wird bei
Decl genullt (`ast_compiler_stmt.cpp`, `CreateStore(getNullValue, alloca)`) → No-Init-String ist Null
→ `decref(null)` No-op; Selbst-Referenz garantiert Liveness des alten Werts.

### 3.2 Was funktionierte

- `producer_string_drop` (Parts 1&2) blieb grün.
- `string_reassign_drop` **self-accum**: 230 MB → **0 B**. `decref-old` bei Selbst-Referenz greift
  (feuert unbedingt im Codegen, unabhängig vom Ownership-Plan).

### 3.3 Was fehlschlug (Grund des Reverts)

- **share+accum blieb bei ~229 MB** (halbiert, aber nicht gelöst). Der finale Wert (und Teil-Pfade)
  wurden trotz „Slot bleibt Owned" nicht gedroppt.
- **Isolationsprobe `iso3` (build_a):** eine Funktion `decl [line0,string,{call producer}]; return
  line0.len` (Scalar-Return, in Schleife aufgerufen) **leakt 128 B/Iter**. D.h. der
  **Return-Cleanup-Pfad** (`visitRetStmt` → `emitReturnCleanup`, mayDrop-gegatet) droppt owned
  String-Locals in einer aufgerufenen Funktion nicht — eine tiefere, von der breiten Änderung erst
  freigelegte Lücke (inline im messenden Frame, über den Block-Exit-Drop, funktioniert es dagegen —
  `loop_self_accum` = 0 B).
- **KRITISCH — Sicherheit:** die volle Suite meldete unter valgrind **`Invalid read of size 4`**
  (Lesen eines 4-Byte-Werts = RC-Header `magic`/`refcount` an `ptr-8`). Also inkref/decref auf einem
  Pointer, dessen `[-8]` nicht gemappt ist: ein String-Local, der einen **Nicht-RC-Pointer** hält
  (z. B. `req.body`, eine buffer-as-string, ein `.rodata`-Literal an einer Page-Grenze) oder einen
  **gemoveten/freigegebenen** Wert, den mein bedingungsloses incref-on-copy / decref-old berührt hat.
  Das ist die Double-Free/UAF-Klasse — nicht auslieferbar.

Konsequenz: **alle vier Änderungen revertiert** auf den grünen Parts-1&2-Stand.

### 3.4 Warum eine naive Lösung unsicher ist

- **incref-on-copy breit** setzt voraus, dass jeder in einen String-Local geschriebene Pointer ein
  RC-String ist. Es gibt aber String-typisierte Werte, die **keine** RC-Header haben (rohe
  malloc/strdup-Pointer, Buffer-Views, manche Builtin-Rückgaben). Der Magic-Guard fängt einen
  **falschen** Header ab, **nicht** aber einen Read an nicht-gemappter Adresse `ptr-8` (Page-Grenze)
  bzw. an einem uninitialisierten/gemoveten Slot. → tatsächlich beobachteter `Invalid read`.
- **decref-old allgemein** ist nur sicher, wenn der alte Wert **owned und nicht gemovet** ist. Ohne
  Move-aware-Dataflow (push/return/consuming-call markieren den String-Slot als `Moved`) kann
  `push acc; set:[acc, …]` den bereits weggegebenen Wert erneut decref'en → Double-Free. Die
  Selbst-Referenz-Teilmenge ist sicher (RHS liest den alten Wert), deckt aber `set:[blk, line]`
  (Borrow-Copy) und Nicht-Selbst-Reassign nicht ab.

---

## 4. Was der korrekte Fix braucht (für die dedizierte Umsetzung)

Dies ist der von spec/61 zurückgestellte **„assignment-dataflow step"** — bewusst als eigenes,
schwer abzusicherndes Vorhaben, nicht als Bolt-on.

1. **Move-aware Ownership-Dataflow für String-Locals:** der Analyzer muss einen String-Slot bei
   push/return/consuming-call als `Moved` markieren (heute nur bei Member-Store, `analyzeStmt`
   `SetStmt`-Zweig). Nur dann darf `decref-old` bei Reassignment allgemein feuern (Vorbedingung:
   alter Wert `Owned + Live`, nicht `Moved`).
2. **incref-on-copy nur für nachweislich RC-owned Quellen** — oder ein robusterer Guard, der den
   `ptr-8`-Read niemals auf nicht-gemappten/uninitialisierten Adressen ausführt. Idee: RC-Strings von
   Roh-Pointern typseitig trennen, statt sich allein auf den Magic-Guard zu verlassen. Alternativ ein
   `aiml_str_is_rc(ptr)` mit sicherem, gemappten Probe (z. B. Registry statt Header-Read).
3. **Return-Cleanup-Pfad** (`emitReturnCleanup`) muss owned String-Locals in Funktionen mit
   Scalar-/Void-Return tatsächlich droppen (heute Lücke — `iso3 build_a`).
4. **share+accum** (`decl [blk,string,""]; set:[blk,line]; set:[blk,"${blk}|…"]`, `line` ein
   Schleifen-Local) muss end-to-end balanciert sein: `blk` erhält beim Borrow-Copy einen eigenen Ref,
   das alte `blk` wird beim Selbst-Reassign freigegeben, der finale `blk` am Scope-Exit.
5. **Gate:** volle `tests/run_memory_sanitizers.sh` (Safety, keine `Invalid read/free`) **plus** die
   9rooms-Suite (148/149). `string_reassign_drop.aiml` als Regressionsnetz registrieren, wenn grün.
   Die breite Änderung berührt **jeden** String-Local (Server, Client, alle Tests) — entsprechend
   sorgfältig staffeln.

---

## 5. Artefakte

- **Reproduktion:** `tests/_mem_sanitize/string_reassign_drop.aiml` (self-accum + share+accum),
  unregistriert; Notiz dazu in `tests/run_memory_sanitizers.sh` beim `producer_string_drop`-Eintrag.
- **Gefixt & grün (bleiben):** `producer_string_drop.aiml` (Klassen 1&2); Producer-Annotationen an
  `card_type_to_string`/`card_display_type` (`sources/models/card_types.aiml`) und `mana_stem`
  (`client/card_full_populator.aiml`, fixt die 109-KB-Site als Klasse 2).
- **Diagnose-Infrastruktur (bleibt):** `AIML_MEM_DIFF` mit WASM-`emscripten_get_callstack`-Sites
  (`src/sys/mem_diff_runtime.cpp`), memlog-Server (`9rooms_v2/client/web/memlog_main.aiml`,
  `exodus-memlog.json`), gebatchtes console→memlog-Forwarding (`9rooms_v2/client/web/shell.html`).

---

# 6. Lösungsanalyse & Vorschläge (2026-09-12, Nachtrag)

Tiefergehende Analyse *wie* man Klasse 3 sicher lösen kann. **Nichts hiervon ist umgesetzt** — es ist
der Entwurf für die dedizierte Umsetzung. Alle Aussagen sind mit Repro-Messungen auf dem
revertierten (Parts-1&2-grünen) Compiler belegt.

## 6.1 Präzise Faktenlage (belegt)

Fünf voneinander trennbare Mechanismen bilden zusammen den Leak. Wichtig: sie sind **nicht** ein
einziger Bug.

1. **String-Local-Store kopiert/moved nicht (Kern von Klasse 3).** Der Plain-String-Store in
   `visitSetStmt` und der Decl-Init-Store sind reine `createStore(ptr, slot)`:
   - kein `aiml_str_incref` beim Kopieren aus einem Borrow → das Ziel **aliast** die Quelle;
   - kein `aiml_str_decref` des alten Slot-Werts beim Reassign → jede Selbst-Akkumulations-Iteration
     leakt die vorherige Fassung.
2. **Analyzer downgradet rebound Locals auf `Unknown`** (`ownership_analyzer.cpp`, `analyzeStmt`,
   `SetStmt`-Zweig, „rebound → not provably owned"). `mayDrop` verlangt `Owned+Live+resources` → der
   **finale** Wert eines reassign'ten Strings wird am Scope-Exit nicht gedroppt (bewusster
   Under-Drop).
3. **Return-Cleanup-Lücke — §6.2-Owner-Exemption feuert zu breit.** `emitReturnCleanup`
   (`ast_compiler_stmt.cpp`) nimmt den Owner des Rückgabewerts von der Bereinigung aus (`return
   x.field` darf x-Heap nicht freigeben). Die Base-Extraktion behandelt aber `return str_local.len`
   so, dass `retBaseId == str_local` — obwohl `.len` einen **skalaren** i32 liefert, der den
   String-Heap **nicht** aliasen kann. Folge: str_local wird exemptiert und leakt.
   **Belegt:** `return str_local.len` → 18,5 MB / 61 B/Iter (kein DROP im `AIML_TRACE_SYMBOL`);
   `decl [nlen,i32,str_local.len]; return nlen` → **0 B** (str_local wird gedroppt). Das ist ein
   **eigenständiger** Bug, unabhängig von Reassignment.
4. **Kein Move-Tracking für Strings im Analyzer.** `life = Moved` wird nur beim **Member-Store**
   gesetzt. push/return/consuming-call konsumieren einen String-Local nur beim Lowering
   (`consumeLocalDrop`), **ohne** den Analyzer-Slot als `Moved` zu markieren. Deshalb weiß kein
   späterer Store, ob der alte Wert bereits weggegeben (und vom Konsumenten freigegeben) wurde.
5. **RC- vs. Roh-Strings.** Heap-Strings sind RC (einziger Chokepoint `aiml_str_alloc`, 8-Byte
   `'STRN'`-Header); `incref`/`decref` sind magic-guarded und Null-/Low-Pointer-sicher
   (`string_runtime.cpp`). Literale sind `.rodata` ohne Header (Guard greift, `[-8]` liegt in der
   Praxis gemappt — mit `lit_decref.aiml` valgrind-clean nachgewiesen). Es gibt viele rohe `strdup`
   in Runtime-Structs (http_map, url-Teile, gltf), aber überwiegend als interne Felder; als
   AIML-String-Local landen sie mitten im Heap → `[-8]` gemappt → Guard-No-op, **safe**.

## 6.2 Warum der Versuch scheiterte — Diagnose des `Invalid read`

Der `Invalid read of size 4` unter valgrind war **kein** Literal-an-Page-Grenze (mit `lit_decref`
ausgeschlossen), sondern das **Lesen eines bereits freigegebenen RC-Headers**: mein bedingungsloses
`incref`-on-copy / `decref`-old berührte `[-8]` eines Strings, der zuvor **gemovet und vom Konsumenten
freigegeben** wurde (Fakt 4 — kein Move-Tracking). Valgrind meldet den Read auf freed memory, obwohl
der Magic-Guard ihn (wegen `magic=0`-Poison) neutralisieren würde; nach einem Realloc mit
zufällig passendem Magic wäre es ein echter Double-Free. **Kernlehre: jeder `incref`/`decref` auf
einen String-Local ist nur sicher, wenn der Wert nachweislich lebendig (nicht gemovet) ist.** Der
Magic-Guard ersetzt kein Liveness-Wissen.

Zusätzlich zeigte der Versuch zwei „Under-Drop"-Lücken, die die breite Änderung erst freilegte:
share+accum blieb bei 229 MB (finaler Wert nicht gedroppt) und die Return-Cleanup-Lücke (6.1/3).

## 6.3 Sofort sichere, unabhängige Teil-Fixes (empfohlen als erste Schritte)

Diese sind **nicht** vom Move-Tracking abhängig und können einzeln landen + gaten:

- **(T1) §6.2-Exemption verengen.** In `emitReturnCleanup` den Owner **nur** exemptieren, wenn der
  Rückgabetyp ein Pointer/Struct ist (kann den Owner-Heap aliasen). Bei skalarem Rückgabetyp ist der
  Wert eine Kopie → Owner **nicht** exemptieren. Fixt Fakt 3 (belegt: 0 B via-temp). Reines
  Zusatz-Droppen bereits-owned Locals → kann nicht doppelt freigeben. **Risiko: niedrig.**
- **(T2) decref-old nur bei Selbst-Referenz.** `set:[acc,"${acc}…"]`: der alte Wert wird in der RHS
  **gelesen** → provably live → sicher zu decref'en; der neue Wert ist ein distinktes frisches
  Concat. Fixt reine Selbst-Akkumulatoren (belegt: self-accum 230 MB → 0). **Voraussetzung für
  Sicherheit:** der Akkumulator darf kein geborgtes Alias sein — gilt, wenn sein Init ein Literal/
  Owned-Temp ist (der `build_card_text`/`ability_block`-`key`-Fall). Für den `blk=line`-Borrow-Fall
  greift T2 allein nicht (siehe Kern-Optionen). **Risiko: niedrig-mittel**, mit Probe absichern.

T1+T2 zusammen adressieren die reinen Selbst-Akkumulatoren und die Skalar-Return-Lecks — ein
messbarer Teil des Client-Leaks — **ohne** die riskante breite Umstellung.

## 6.4 Kern-Optionen für die vollständige Lösung

### Option A — Deep-Copy statt Shared-Refcount bei Kopie
String-Local-Copy-from-Borrow macht `aiml_str_dup` (frisches rc1), statt `incref` zu teilen. Jeder
Local besitzt einen **eindeutigen** Buffer → kein Aliasing, `decref`-old semantisch simpel.
- **Pro:** eliminiert die Aliasing-Klasse ganz; kein geteilter Refcount-Feinschliff.
- **Contra:** löst das **Move-Problem NICHT** (ein gemovter eindeutiger Buffer, vom Konsumenten
  freigegeben, dann von `decref`-old berührt → dieselbe UAF). Zusätzliche Kopierkosten pro
  Zuweisung. **Allein unzureichend.**

### Option B — Vollständiger move-aware Ownership-Dataflow für Strings (empfohlener Endzustand)
Den Analyzer auf ein per-Slot-Lattice bringen, das **auch** push/return/consuming-call als `Moved`
markiert (Fakt 4 schließen). Dann:
- `decref`-old beim Reassign nur, wenn Pre-Set-State `Owned+Live` (nicht `Moved`);
- `incref` (oder dup) beim Kopieren aus einem Borrow;
- Slot bleibt nach Reassign `Owned` (die frühere Analyzer-Änderung — jetzt **korrekt**, weil Move
  getrackt wird);
- Return-Cleanup nutzt dieselben Fakten (subsumiert T1).
- **Pro:** die eine, konsistente, spec/61-§7-treue Lösung; fixt alle drei Sub-Lücken.
- **Contra:** berührt **jeden** String-Local (Server, Client, alle Tests); die Analyzer-Erweiterung
  (Move über Calls/Push/Return) ist der eigentliche Aufwand. **Risiko: hoch** — nur mit voller
  Sanitizer-Suite **und** 9rooms-148/149 gestaffelt einführen.

### Option C — Region/Arena für transiente Strings (verworfen)
Per-Frame/Scope-Arena, wholesale-frei. Umgeht Einzel-Ownership, kollidiert aber mit dem bestehenden
RC-Modell und langlebigen (Member-)Strings. Nicht zum Rest passend, zu invasiv. **Verworfen.**

### Ergänzung zu B — RC sauber von Roh-Pointern trennen
Der `Invalid read` zeigt: sich allein auf den Magic-Guard zu verlassen ist fragil (Read auf freed
memory). Robuster wäre, RC-Strings verlässlich erkennbar zu machen, ohne `[-8]` blind zu lesen —
z. B. ein separater Typ-/Tag-Pfad oder eine gemappte Registry statt Header-Probe. Das entschärft
auch Zukunftsfälle, in denen ein String-Local einen Roh-Pointer hält.

## 6.5 Empfohlene Staffelung

1. **T1** (Return-Cleanup-Exemption verengen) — klein, sicher, sofort messbar; eigene Probe
   (`iso4/iso5`-Muster → registrieren).
2. **T2** (self-ref decref-old) — deckt reine Selbst-Akkumulatoren; `string_reassign_drop`
   `loop_self_accum` als Gate (bereits 0 B im Versuch).
3. **Option B** als abschließender, gekoppelter Schritt: Analyzer-Move-Tracking für Strings +
   incref-on-copy + decref-old(Owned+Live) + Return-Cleanup via Fakten. Erst wenn `string_reassign_drop`
   **beide** Subjects (self-accum **und** share+accum) auf 0 bringt UND die volle Sanitizer-Suite +
   9rooms-Suite grün sind. Die share+accum-Form (`decl blk=""; set blk=line; set blk="${blk}|…"`)
   ist der Akzeptanztest für die Kopplung.

## 6.6 Betroffene Codestellen (Ankerliste für die Umsetzung)

- `src/core/ast_compiler_stmt.cpp` — `visitSetStmt` Plain-String-Store (`createStore(val, baseAlloca)`);
  Decl-Init-Store + `consumeStringTemp`; `visitDeclStmt` String-Drop-Registrierung; `emitReturnCleanup`
  + `retBaseId`-Extraktion (T1); `visitBlockStmt` mayDrop-Gate.
- `src/core/ownership_analyzer.cpp` — `freshOwned` (String-Fall); `analyzeStmt` `SetStmt`
  (Rebind-Downgrade, Move-Marking); Move-Marking für push/return/consuming-call ergänzen (B).
- `src/core/ownership_flow.cpp` — `mayDrop` (Owned+Live+resources), Grundlage aller Drops.
- `src/sys/string_runtime.cpp` — `aiml_str_incref/decref` (magic-guarded); ggf. RC-Erkennung härten.
- Reproduktion/Gates: `tests/_mem_sanitize/string_reassign_drop.aiml`; neue Probe fürs T1-Muster.

---

## 6. Ergänzende Analyse: konkrete Befunde und Codelösungen

Die folgenden Vorschläge wurden am aktuellen Quellstand geprüft. **Sie sind noch nicht in den
Compiler eingebaut.** Bestehende Messwerte aus Abschnitt 1–5 bleiben historische Befunde; hier
neu ausgeführt wurde nur die kleine Return-Cleanup-Probe in §6.1. Die Snippets sind getrennt in
lokalen Patchvorschlag und Architektur-/Integrationsentwurf. Neue Hilfsnamen sind ausdrücklich
Vorschläge, keine bereits vorhandenen APIs.

### 6.1 Separater kleiner Fix: Besitzer einer skalaren Rückgabe nicht ausnehmen

**Konkrete Stelle:** `src/core/ast_compiler_stmt.cpp`, `Compiler::visitRetStmt`, derzeit
Zeilen 1636–1665; insbesondere die unbedingte Ausnahme in `emitReturnCleanup` bei Zeile 1663.

Der Code leitet aus jedem `VariableExpr` den Basisnamen ab:

```cpp
std::string base = rv->name;
size_t dot = base.find('.');
if (dot != std::string::npos) base = base.substr(0, dot);
if (Symbol* bs = resolveVariable(base)) retBaseId = bs->ownershipId;
```

Damit wird bei `return line0.len` **line0** zum ausgenommenen Besitzer. `retScalar` ist zwar true
und aktiviert Cleanup, aber die innere Schleife überspringt genau diesen String trotzdem:

```cpp
if (it->ownershipId != NoOwnership && it->ownershipId == retBaseId) continue;
```

Das ist eine konkrete Erklärung für `iso3`, keine generelle Unfähigkeit des Scalar-Return-Pfads.
Die Funktion berechnet die Länge bereits mit `visitExpression` in ein SSA-Ergebnis, **bevor**
`emitReturnCleanup` läuft. Danach darf der String freigegeben werden.

**Patchvorschlag, direkt an der Ausnahme:**

```cpp
// Scalar/void returns do not transfer or borrow the owner's heap.
const bool returnMayAliasHeap = retTy &&
    (retTy->isPointerTy() || retTy->isStructTy());

// Inside emitReturnCleanup, replace the unconditional owner exemption:
if (returnMayAliasHeap && it->ownershipId != NoOwnership &&
    it->ownershipId == retBaseId)
    continue;
if (mayDrop(*retPlan, *retExit, it->ownershipId))
    emitScopeDrop(ctx, *it);
```

Die bestehenden `safeToCleanup`-/`mayDrop`-Schranken bleiben erhalten. Kein pauschales
`emitScopeDrop` für alle Strings und keine Änderung an pointer-/modellwertigen Rückgaben.
Die Skalarklassifikation entspricht dabei der bestehenden AIML-Regel; explizite Pointer→Integer-
Escapes wären als eigenes Sprach-/Escape-Problem zu behandeln.

**Neu verifiziert, unveränderter Compiler:** Eine temporäre Probe unter
`AIML_LIFETIME_DROPS=1 AIML_TRACE_SYMBOL=line_direct,line_expr AIML_CACHE=0`:

```yaml
- def:
    name: make_line
    args: [[num, i32]]
    return: { type: string, lifetime: producer }
    body: [{ return: "payload-${num}" }]
- def:
    name: direct_length
    return: i32
    body:
      - decl: [line_direct, string, { call: [make_line, 1] }]
      - return: line_direct.len
- def:
    name: expression_length
    return: i32
    body:
      - decl: [line_expr, string, { call: [make_line, 2] }]
      - return: { +: [line_expr.len, 0] }
```

Beide Funktionen liefern dieselbe Länge und die Probe endet mit Exit 0. Der Trace enthält
`DECL line_direct`, `DECL line_expr`, **nur `DROP line_expr`**. Beide DECLs melden
`cleanup=registered`. Das isoliert die falsche Owner-Ausnahme, ohne Reassignment oder Cache.
Probe/Log dieser Analyse: `/tmp/aiml_string_return_cleanup_probe.aiml` und `.log`; diese
Temporärdateien sind kein dauerhaftes Regressionsnetz. Bei Umsetzung in `tests/_mem_sanitize/`
übernehmen und um `return model.scalar_field`, `return list.len` und eine weiterhin gültige
Heap-/View-Rückgabe als Gegenprobe ergänzen.

### 6.2 Empfohlene größere Lösung: String-Local hat RC-Referenz oder Null

**Zielinvariante für migrierte lokale String-Slots:**

> Ein Slot enthält entweder `nullptr` oder eine eigene, gültige Referenz auf eine
> `aiml_str_alloc`-Allokation. Borrowed/raw/static Eingänge werden vor dem Speichern normalisiert.
> Ein Move hinterlässt Null. Ein Scope-Drop bleibt für den Slot registriert.

Damit wird nicht behauptet, dass alle heute als `string` typisierten Werte RC-Strings sind.
Es wird eine **neue, explizit durch Codegen erzwungene Grenze** für lokale Bindungen eingeführt.
`persistent`, `extern`, Globals und geliehene/by-ref Parameter sind zunächst ausgeschlossen.
Eine Übernahme dieser Kategorien braucht jeweils eigene Lifetime-Regeln.

**Warum zunächst duplizieren statt überall incref?** `aiml_str_dup` in
`src/sys/string_runtime.cpp:65` liest den gültigen Stringinhalt und erzeugt einen RC-String;
es liest keinen Header vor einer fremden Adresse. Ein Borrow muss am Kopierpunkt weiterhin
lebendig und NUL-terminiert sein. Einen schon dangling Pointer repariert auch Kopieren nicht.

Neue Runtime-Helfer, vorgeschlagene Einfügestelle bei `aiml_str_dup`/`aiml_str_decref` in
`src/sys/string_runtime.cpp`; Deklarationen nach `src/sys/string_runtime.h`:

```cpp
// PRE: dst is a managed local slot: null or one owned RC reference.
// PRE: src is a live NUL-terminated borrowed string, or null.
// Returns 0 on allocation failure, leaving dst unchanged.
extern "C" int32_t aiml_str_assign_copy(char** dst, const char* src) {
    char* next = aiml_str_dup(src);       // acquire BEFORE releasing old
    if (!next) return 0;
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);                // safe by dst invariant, not magic guessing
    return 1;
}

// PRE: next transfers a separate owned RC reference, or null.
// A self-assignment with no transferred reference is excluded by the caller.
extern "C" void aiml_str_assign_move(char** dst, char* next) {
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);
}

// Use at a real ownership-transfer point, after capturing the value.
extern "C" char* aiml_str_take_local(char** src) {
    char* value = *src;
    *src = nullptr;
    return value;
}
```

Diese Helfer sind **nur für normalisierte Slots** sicher. Sie dürfen nicht auf vorhandene
raw/borrowed Member-Slots angewandt werden. Auch `aiml_str_assign_move(dst, next)` benötigt
belegtes RC-Ownership; das Label `producer` alleine ist nur dann ausreichend, wenn der
betreffende Return-Vertrag tatsächlich RC-managed Ergebnisse garantiert.

Für `next == old` gilt: Zwei eigene RC-Referenzen auf dieselbe Adresse sind zulässig; dann
muss die alte Referenz dennoch abgezogen werden. `set:[x,x]` ohne zweite Referenz bleibt dagegen
am bestehenden Self-Assignment-Guard ein No-op. Ein bloßer Pointergleichheits-Guard im Helfer
würde diese beiden Fälle unzulässig vermischen.

**Allokationsfehler:** `assign_copy` darf nicht stillschweigend einen erfolgreichen Store vortäuschen.
Der Codegen muss bei Rückgabe 0 den definierten Fehlerpfad betreten; der alte Slot bleibt bis dahin
intakt. Bei Owned-Temporaries bleibt deren Cleanup bis zum erfolgreichen Transfer zuständig.
Ein Rückgabewert Null aus einem Producer ist nicht ohne dessen Vertrag mit OOM gleichzusetzen.

**Integrationsstellen:**

| Stelle im aktuellen Baum | Änderung |
|---|---|
| `ast_compiler_stmt.cpp:446–486`, String-Drop-Registrierung | Für ausdrücklich normalisierte lokale Slots einen Drop registrieren; nicht zuerst global alle Strings als owned deklarieren. |
| `ast_compiler_stmt.cpp:640–670`, Initializer-Auswertung / `consumeStringTemp` | Owned-Temp-Eigenschaft vor dem Konsumieren erfassen. Borrow-Eingang duplizieren; belegten OwnedRC-Temp übernehmen. No-init-Slot bleibt Null. |
| `ast_compiler_stmt.cpp:926`, generischer Init-Store | String-Normalisierung davor durchführen und keinen zweiten generischen Store emittieren. |
| `ast_compiler_stmt.cpp:1054–1101`, SelfAssign / generisches `consumeLocalDrop` | Plain-string-local Copy vor dem generischen Move-Pfad abzweigen; `set:[blk,line]` darf line nicht konsumieren. |
| `ast_compiler_stmt.cpp:1132`, `consumeStringTemp` | Für den neuen Pfad Temp erst nach erfolgreicher Übernahme konsumieren. |
| `ast_compiler_stmt.cpp:1321`, generischer Plain-Local-Store | Bei managed String-Slots `assign_copy` oder `assign_move`; alten Slot unabhängig von syntaktischer Selbstreferenz freigeben. |
| `compiler.cpp`, `jitRun`-`AddSymbol`-Block | Neue Runtime-Helfer für JIT registrieren; native/WASM-Runtime gemeinsam neu bauen. |

**Codegen-Skizze; neue Helper-/Metadaten-Namen sind Entwurf:**

```cpp
// This branch must precede generic consumeLocalDrop(value).
if (isManagedStringLocalTarget(*sym, propName, isDash)) {
    if (sym->is_const) { reportConstAssignment(); return; }
    if (isExactSelfAssignment(setStmt, baseName)) return;

    IRValue* rhsIR = visitExpression(setStmt->value.get());  // old value still alive
    if (!rhsIR) return;
    auto* rhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhsIR));
    const bool ownedRC = isOwnedRCStringTemp(rhs);  // BEFORE consumeStringTemp

    if (ownedRC) {
        emitAssignMove(baseAlloca, rhs);
        consumeStringTemp(rhs);                    // after successful transfer
    } else {
        emitAssignCopyOrFailure(baseAlloca, rhs);  // dup, store, decref old
        // Do NOT consume the source local's cleanup.
    }
    return;
}
```

`isOwnedRCStringTemp` benötigt mehr als den AIML-Typnamen: temporäre Ownership und
RC-Repräsentation müssen belegt sein. Ein Borrow-Return unbekannter Herkunft ist kein Temp-Move.
Die bestehenden `stmtTempStack`-Einträge können für bereits garantierte RC-Producer verwendet
werden; langfristig sollte die Klassifikation am Ergebnis des aufgelösten Calls hängen.

**Effekt auf die Repro:** `blk = line` dupliziert die Quelle; `line` darf am Iterationsende droppen.
`blk = "${blk}|${line}"` berechnet zuerst den neuen RC-String, ersetzt den Slot und gibt das alte
`blk` frei. Das finale `blk` wird am Blockende freigegeben. Auch `acc = producer()` ohne
Selbstreferenz und `acc = "literal"` erhalten denselben sicheren Reassign-Pfad.
Der Preis ist zunächst eine zusätzliche Kopie für geliehene RC-Strings; die Optimierung zu
incref kommt erst nach bewiesener Herkunft (§6.5).

### 6.3 Moves und Kontrollfluss: Drop nicht global aus der Cleanup-Liste löschen

**Konkrete Stellen:** `ast_compiler_stmt.cpp:1799`, `consumeLocalDrop`; normale Drops ab
Zeile 2034; Return-Drops ab Zeile 1657; `ast_compiler_call.cpp` bei consumed-Argumenten,
`list.push` um Zeile 1341 und `map.put` um Zeile 1576. Dort werden heute Cleanup-Einträge
beim Kompilieren gelöscht. Für bedingte Modell-Moves gibt es um Zeile 1304 bereits einen
Kommentar/Guard zu diesem Problem; das ist keine allgemeine String-Lösung.

```yaml
- decl: [acc, string, { call: [producer] }]
- if: { condition: flag, then: [{ call: [sink.push, acc] }] }
- set: [acc, { call: [producer] }]
```

Auf dem true-Pfad gehört der erste Wert dem Sink; auf dem false-Pfad noch `acc`.
Ein bei der Codegenerierung global entferntes Cleanup kann das nicht ausdrücken. Außerdem
benötigt der neu zugewiesene Wert wieder ein Cleanup, selbst wenn vorher ein Move stattfand.

**Für managed String-Slots:** Cleanup-Eintrag für die gesamte Slot-Lebensdauer behalten;
bei einem echten Move im ausgeführten Pfad den Wert übernehmen und den Slot nullen.
Spätere Reassignments brauchen dann keinen erneut angehängten Scope-Drop.

```cpp
// Lowering sketch for a known, committed consuming operation:
auto* transferred = emitTakeManagedStringLocal(sourceSlot);  // load + store null
emitConsumingSink(transferred);                              // sink receives captured value
// cleanupStack entry remains; sourceSlot is null on this runtime path.
```

**Nicht** einfach `CreateStore(null, addr)` in den heutigen `consumeLocalDrop` einbauen:
Dieser wird unter anderem vor `visitExpression`/vor dem eigentlichen Argument-Load aufgerufen.
Das würde Null anstelle des Strings weitergeben. Der Take muss am ausgewerteten Wert und dem
bestätigten Transfer sitzen. Bei fehlbaren/werfenden Sinks ist die Ownership bis zum Commit über
Temporary-Cleanup/Unwind-Guard zu sichern oder der Slot bei Abbruch wiederherzustellen. Ein
`consumed`-Vertrag muss festlegen, ob der Callee die Ownership auch bei Fehler übernimmt.

Beim Aufruf eines lediglich borgenden/retaining Sinks ist das **kein Move**. Ein gespeicherter
Borrow muss eine eigene Referenz/Kopie erhalten oder als unzulässiger Escape diagnostiziert
werden; Nullsetzen darf kein unbekanntes Retention-Verhalten kaschieren.

**Drop-Skizze für diese explizit markierten Slots:**

```cpp
// New ScopeDrop flag, set ONLY after the canonical-RC-slot lowering is complete:
if (drop.managedStringSlot) {
    auto* value = builder.CreateLoad(ptrTy, drop.addr);
    builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy), drop.addr);
    builder.CreateCall(strDecref, {value});
} else {
    // Existing mayDrop + legacy emitScopeDrop behavior stays in force.
}
```

Die Null-Invariante kann einen gemischten Laufzeitpfad `Live|Moved` sicher aufräumen. Diese
Sonderbehandlung darf erst aktiviert werden, wenn **alle** Schreib-/Move-/Escape-Pfade des Slots
migriert sind. Nicht `mayDrop` für beliebige Unknown-/Borrowed-Strings lockern. Ungeklärte Aliase,
`persistent`, externe Mutation oder nicht modellierte Exceptions schließen den Slot zunächst aus.

### 6.4 Analyzer: stabile Reassign-Ressourcen und gemeinsame Call-Effekte

**Stellen:** `ownership_analyzer.cpp:130` (`assignIdsStmt`), `:195–245` (SetStmt),
`:250–260` (While-Fixpunkt), `:265–273` (Return und bisher ignorierte Call-Effekte);
`ownership_flow.h`, `StatementPlan`/`OwnershipPlan`; `ownership_flow.cpp:55`, `mayDrop`.

Der Analyzer muss einen normalisierten Rebind als neuen eigenen Wert beschreiben, statt nur
den bisherigen `Owned→Unknown`-Downgrade wegzulassen. Insbesondere darf die Ressourcenmenge
nicht leer bleiben, wenn der Slot ursprünglich ein Literal/Borrow war.

```cpp
// NEW members in Analyzer; allocated in pass 1, never inside the loop fixpoint:
std::map<OwnershipId, std::string> slotTypes;
std::map<const SetStmt*, ResourceId> assignmentResources;

// assignIdsStmt, declaration branch:
slotTypes[id] = d->typeName;

// assignIdsStmt, NEW SetStmt branch; one stable abstract reference per source site:
if (auto* set = dynamic_cast<const SetStmt*>(s))
    assignmentResources.emplace(set, nextRes++);

// analyzeStmt(SetStmt), after resolving target and validating normalized lowering:
if (isManagedStringSlot(id) && !selfAssign) {
    OwnFact next;
    next.origin = Origin::Owned;
    next.life = Live;
    next.resources = {assignmentResources.at(set)};
    st.slots[id] = next;
    auto& sp = plan.statements[set];
    sp.target = id;
    sp.result = next;
    // Add explicit string-copy / temp-adopt variants to Transfer;
    // do not misuse generic DeepCopy if codegen treats it as model memcpy/serialize.
}
```

`ResourceId` steht hier für die abstrakte **eigene Referenz/Erzeugungsstelle**, nicht für eine
über den ganzen Loop konstante physische Adresse. Bei späterer incref-Optimierung sind
RC-Referenzbesitz und gemeinsame Buffer-Identität auseinanderzuhalten. Immer neue IDs bei jedem
Analyse-Durchlauf würden Fixpunkt-Konvergenz verhindern.

Call-Effekte sollen Analyzer und Codegen aus derselben aufgelösten Signatur lesen:

```cpp
// Proposed resolved-call metadata, shared by analysis and lowering:
enum class StringResultKind { Borrowed, OwnedRC, Unknown };
enum class ArgumentEffect { Borrow, CopyIntoSink, Consume };
```

Damit werden consumed-Parameter, `push`, `put`, Return und verschachtelte Call-Ausdrücke
konsistent behandelt. Der aktuelle Catch-all am Ende von `analyzeStmt` ignoriert Call-Moves.
Auch Calls in Initializern, Set-RHS, Bedingungen und Return-Ausdrücken benötigen Effekte, nicht
nur `ExprStmt`. `MemberStore` darf nur bei `Owned` **und exakt `Live`** einen Move planen.
Verwendung eines `Moved` oder `Live|Moved`-Strings braucht eine Diagnose bzw. einen zulässigen
Reinitialisierungspfad.

**Zusätzlicher konkreter Drift:** `visitDeclStmt:472` kennt `text_value`/`dropdown_value` als
Owned-String-Builtins; `freshOwned` im String-Zweig nennt sie nicht explizit. Ob sie über
`ownsReturnFns` erfasst werden, muss pro aufgelöstem Call nachgewiesen werden. Die doppelte
Short-Name-Heuristik durch gemeinsame Ergebnis-Metadaten ersetzen; ein gleichnamiger Getter
einer anderen Klasse darf nicht irrtümlich als Producer gelten.

### 6.5 Alternative/Optimierung: sicherer RC-Nachweis statt Header-Probe

`string_runtime.cpp:88–108` liest vor der Magic-Prüfung bereits `s - sizeof(AimlStrHeader)`.
Eine numerische Low-Pointer-Prüfung oder `magic == STRN` macht diesen Zugriff nicht sicher.
Ein pauschales Mapping-Probe schützt außerdem nicht vor einem bereits freigegebenen Block,
Adresswiederverwendung oder konkurrierender Freigabe.

**Alternative A, bevorzugte spätere Optimierung:** belegte Provenienz am IR-Ergebnis speichern.
Nur bei `OwnedRC` bzw. einem nachweislich lebendigen Borrow eines RC-Owners `incref` verwenden;
bei `BorrowedRaw`/`Unknown` weiterhin duplizieren. Ein eigener RC-Ref auf denselben Buffer ist
für `blk` ausreichend und spart gegenüber §6.2 die Inhaltskopie.

**Alternative B, größere Runtime-Änderung:** immer aktive Registry aller lebenden RC-String-
Basisadressen, mit Aufnahme in `aiml_str_alloc` und Entfernung beim letzten `decref`.
Lookup muss erfolgen, **bevor** irgendein Header gelesen wird:

```cpp
// Sketch inside a locked RC-registry implementation:
auto it = liveRC.find(s);                // pointer key only, no dereference of s[-8]
if (it == liveRC.end()) return false;
AimlStrHeader* hdr = it->second;
++hdr->refcount;
return true;
```

Nicht die optionale `AIML_MEM_DIFF`-Registry dafür zweckentfremden: Lifetime-Sicherheit darf
nicht von einem Diagnose-Flag abhängen. Registry-Synchronisation, Allocationsfehler,
Prozess-/Thread-Shutdown und native/WASM-Kosten müssen separat gelöst werden. Ein Registry-Hit
beweist weiterhin nicht die Gültigkeit eines stale Pointers nach Adresswiederverwendung (ABA).
Datenfluss-/Lifetime-Regeln bleiben nötig. Daher ist diese Variante keine kleine Ersatzzeile
für den heutigen Magic-Guard.

### 6.6 Gemischte Returns und Cache-Put: eigener Teil der Lösung

**Aktuelle Stellen in 9rooms (hier nur gelesen, keine Änderung):**
`../9rooms_v2/client/mock_card.aiml:366–395` (`build_card_text`),
`:404–440` (`ability_block_cached`). Korrigierter Pfad zur Art-Cache-Datei:
`../9rooms_v2/client/net/card_art_cache.aiml`.

Der Cache-Miss endet derzeit mit:

```yaml
- call: [g_abil_text_cache.put, key, blk]
- return: blk
```

Ein put, das `blk` konsumiert, und die anschließende Rückgabe desselben lokalen Werts sind
zwei Ownership-Abnehmer. Ein Klassen-3-Fix darf daraus nicht durch blindes Nullsetzen einen
leeren Return machen. Eine der folgenden Verträge muss explizit gelten:

1. **Borrowed Cache-Return:** Der Cache übernimmt `blk`; anschließend wird die Cache-View
   zurückgegeben, während der Cache gültig bleibt. Der Lookup-Key muss seinerseits gültig sein,
   obwohl `put` auch ihn konsumieren kann. Das ist keine Producer-Rückgabe.
2. **OwnedRC-Return:** Cache und Aufrufer erhalten je eine eigene Referenz oder Kopie. Cache-Hit
   dupliziert/retained die Cache-View vor der Rückgabe; Cache-Miss kopiert/retained für den Cache
   und transferiert eine unabhängige Referenz an den Aufrufer. Erst dann ist `producer` korrekt.

Konkrete C++-Semantik für Variante 2, als Vertragsbeispiel, nicht als vorhandene Cache-API:

```cpp
// Assume borrowed_cache_lookup returns a live borrowed char*.
if (const char* hit = borrowed_cache_lookup(key))
    return aiml_str_dup(hit);             // caller owns a NEW RC reference

char* result = build_owned_text();        // one owned reference
char* cacheCopy = aiml_str_dup(result);    // second owned reference
if (!cacheCopy) {                         // handle failure without losing result
    aiml_str_decref(result);
    return nullptr;                       // only if contract defines failure as null
}
cache_put_consuming_value(key, cacheCopy); // key ownership handled separately
return result;
```

Der aufrufende Compiler muss alle Return-Pfade als `OwnedRC` behandeln und die Rückgabe
bzw. transienten Call-Ergebnisse entsprechend droppen. Bloßes Hinzufügen von `producer` zum
jetzigen Cache-Getter bleibt falsch. Auch ein unannotierter gemischter Return lässt sich durch
lokales Reassignment allein nicht vollständig bilanzieren: Wenn ein aufgebauter String
entkommt und beim Caller als Borrow gilt, bleibt die letzte Referenz verloren.

Das präzisiert die Aussage aus §2.2: **Der Assignment-Pfad muss korrigiert werden, reicht für
vollständige End-to-End-Freigabe über gemischte Return-Grenzen aber nicht allein aus.**
Ein unbekannter malloc-basierter „Borrow“-Return, der in Wahrheit Ownership überträgt, benötigt
zuerst einen korrekten Vertrag; Duplizieren allein würde dessen ursprünglichen Buffer leaken.

## 7. Empfohlene Umsetzung und Abnahme

1. **Scalar-Return-Ausnahme (§6.1) isoliert korrigieren.** Kleine native/WASM-Repro und Valgrind;
   damit den klaren `line0.len`-Fehler vor der breiten Änderung entfernen.
2. **RC-/Borrow-Verträge zentralisieren; normalisierte Slots separat kennzeichnen.**
   Decl, Reassign und Copy gemeinsam umstellen, nicht `freshOwned(string)=true` vorwegnehmen.
3. **Move-/Return-/Container-Grenzen für diese Slots migrieren.** Cleanup-Eintrag behalten,
   nur den tatsächlich übertragenen Slot leeren; Analyzer und Codegen dieselben Effekte nutzen.
4. **Cache-Hit/Miss und gemischte Return-Verträge schließen.** Keine eigenständigen
   Producer-Annotationen ohne entsprechende Änderung aller Rückgabepfade.
5. **Korrektheit zuerst, danach RC-incref-Optimierung statt Borrow-Kopie.**

Zusätzlich zur vorhandenen großen `string_reassign_drop`-Probe braucht es kleine, gezielte Fälle:

| Fall | Erwartung |
|---|---|
| Producer-Local → `return local.len` / `return model.scalar` | Besitzer nach Ermittlung des Skalars gedroppt |
| `x=x` und zwei Slots mit demselben RC-Buffer | Keine versehentliche Freigabe; Referenzen korrekt bilanziert |
| Borrow von Literal, `strdup`-Buffer, gültigem Buffer-Inneren | Kein Read vor dem Quellpointer; Kopie bleibt nach Quelle-Freigabe gültig |
| Seitenrand-Probe mit geschützter vorheriger Page (nativ) | Normalisieren liest nur Stringinhalt, keine Header-Probe auf Borrow |
| `blk=line`, Quelle verlässt inneren Scope | `blk` bleibt gültig; beide eigenen Referenzen werden freigegeben |
| Reassign nach garantiertem/bedingtem Move | Sink bleibt gültig, neu belegter Slot wird später gedroppt |
| Bedingter Move ohne anschließendes Reassign | true-Pfad droppt Null, false-Pfad den ursprünglichen Slot |
| Leerer Loop / mehrere Backedges / Shadowing | Keine falschen Drops; Analyzer-Fixpunkt stabil |
| Fehlgeschlagene Borrow-Kopie / fehlbarer consuming Sink | Alter Wert bzw. temporärer Owner bleibt korrekt verantwortlich |
| Cache-Hit/Miss, danach Cache-Eviction | Keine doppelte Freigabe oder verlorene Return-Referenz |
| Lifetime-Feature aus | Bisheriger Off-Modus unverändert |

Für reproduzierbare Leak-Messung Warmup und feste Eingaben verwenden; Allocator-/Live-Allokations-
Deltas zusätzlich zu RSS betrachten. Valgrind prüft Safety und Allocation-Stacks; Compiler-/JIT-
Baselineverluste getrennt behandeln. Die große RSS-Probe nicht als Leak-Maß unter Valgrind werten.

Bei Umsetzung: **aktive `exvenv`, `exodus build --all`**, gezielte native/WASM-Tests,
volle `tests/run_memory_sanitizers.sh`, anschließend die relevante 9rooms-Abnahme. Den bislang
unregistrierten Klassen-3-Test erst nach grüner Umsetzung ins Gate aufnehmen.

---

# 8. Umsetzungsstand (2026-09-12, Nachtrag 2)

Umgesetzt und gegen die **volle** `tests/run_memory_sanitizers.sh` grün (0 Safety-Violations):

- **T1 — Scalar-Return-Owner-Exemption verengt** (`emitReturnCleanup`, `returnMayAliasHeap`): der
  Owner wird nur noch bei Pointer/Struct-Rückgaben exemptiert. `return s.len` droppt jetzt den
  owned String-Local. Probe `tests/_mem_sanitize/return_cleanup_scalar_drop.aiml` (registriert):
  scalar-return 0 B, heap-return 0 B.
- **T1b — direkter `return "${…}"` gab seine Interpolations-INTERMEDIÄRE nicht frei** (neuer, bei
  der T1-Probe entdeckter Bug): der `createRet`-Terminator übersprang die per-Statement-Temp-Decref-
  Schleife in `visitStatement`. Fix in `visitRetStmt`: verbleibende `stmtTempStack`-Temps vor dem
  Terminator decref'en (der finale Rückgabewert ist zuvor via `consumeStringTemp` entfernt). Probe
  `tests/_mem_sanitize/return_interp_temp_drop.aiml` (registriert): 0 B (vorher ~18–24 MB).
- **Runtime-Helfer als Fundament gelegt** (`string_runtime.{h,cpp}`, JIT-registriert in
  `compiler.cpp`): `aiml_str_assign_copy` (dup + decref-old), `aiml_str_assign_move` (store +
  decref-old), `aiml_str_take_local` (load + null). **Noch NICHT vom Codegen aufgerufen** — Fundament
  für den Kern-Schritt. Kein Verhaltensänderung; Build grün.

**KERN GELANDET (2026-09-12, Nachtrag 3) — managed String-Local-Modell, volle Sanitizer-Suite grün
(0 Safety-Violations, 0 FAILs, `Invalid read` weg):**

- `freshOwned(string)=true`; Analyzer hält managed String-Slots über Rebind Owned mit stabiler
  Decl-Ressource (`ownership_analyzer.cpp`, `slotType`).
- `visitDeclStmt`: null-sicherer kind-1 `managedStringSlot`-Drop für jeden managed String; Init
  normalisiert (Borrow → `aiml_str_dup`, Owned-Temp → Move).
- `visitSetStmt` (plain-local): `aiml_str_assign_move` (Owned-Temp) / `aiml_str_assign_copy`
  (Borrow, dup + decref-old) → alter Wert wird freigegeben.
- Member-Store: managed String-Local-Quelle wird per `incref` kopiert (Quelle bleibt owned);
  ihr `consumeLocalDrop` an der pre-load Set-Quelle-Site übersprungen.
- Move-Sites: `consumeLocalDrop` nullt den managed Slot + behält den Drop (post-capture:
  push/put/consuming-arg); Return nutzt Erase-Modus (pre-load, Owner exempt); managed Scope-Drop ist
  `decref(load); store null` (null-sicher bei Live|Moved-Merge).
- `aiml_str_assign_copy/move/take_local` in `string_runtime.{h,cpp}`, JIT-registriert.

**Ergebnis:** `string_reassign_drop` self-accum **0 B**, share+accum **0 B** (vorher 230/528 MB);
alle Regressions-Probes grün; Probe registriert. **Noch offen:** 9rooms-Abnahme (148/149) — der
Kern berührt JEDEN String-Local, daher zwingend vor „fertig".

Historische Begründung, warum der Kern nur als **eine gekoppelte Einheit** sicher ist:

- Genaue Faktenlage aus der Umsetzungsvorbereitung: es gibt **~7 `consumeLocalDrop`-Move-Sites**
  (Set-Store in Member/Container `ast_compiler_stmt.cpp:1101`, Return `:1618`, consuming-Args
  `ast_compiler_call.cpp:423`/`:737`/`:2581`, `list.push` `:1341`, `map.put` `:1576/:1577`). Für ein
  managed String-Slot muss **jede** Site den Slot nach dem Wert-Capture nullen (take), sonst
  decreft ein späteres decref-old/Scope-Drop einen bereits gemoveten (freigegebenen) Wert →
  Double-Free.
- **Timing-Falle:** die Set-Store-Site ruft `consumeLocalDrop` **vor** `visitExpression` des Werts
  (Wert-Load erst ~`:1146`). Ein Nullen im zentralen `consumeLocalDrop` würde dort dem Sink `null`
  übergeben. Das Nullen muss also **pro Site nach dem Capture** sitzen, nicht zentral — genau die
  „ALLE Pfade migrieren"-Anforderung aus §6.3.

Ein erster breiter Versuch (alle Strings owned + incref-on-copy, ohne diese Move-Site-Abdeckung)
erzeugte den `Invalid read` (§6.2). Deshalb wird der Kern als eigene, vollständig abgedeckte
Einheit umgesetzt (dup-on-copy statt incref; take+null an allen 7 Sites; Analyzer hält managed
Strings Owned mit stabiler per-Set-Ressource; Scope-Drop `decref(load)` ist null-sicher). Akzeptanz:
`string_reassign_drop` **beide** Subjects → 0 UND volle Sanitizer- + 9rooms-Suite grün, dann den
Test registrieren.

---

# 9. Open-Match-Crash: Producer-Short-Name-Kollision (2026-09-13)

**Symptom:** Server-Crash (`malloc(): unaligned tcache chunk detected`, signal 6) beim Match-Start,
**nur** nachdem man im Deck-Editor war. Unter valgrind kein Abort (eigener Allocator), aber valgrind
meldete **8× Invalid free() + 9× Invalid read**.

**Wurzelursache (VORBESTEHEND, NICHT das class-3-Modell):** valgrind-Stack zeigte
`__free_GetLobbyHandler ← RpcDispatcher_dispatch`, freigegeben ein Block, der EINMAL in
`GameServer_install_handlers` (`{new: …}`) alloziert wurde. Der Dispatcher tut:
`- decl: [hnd, ptr<mirror.RpcHandler>, { call: [<.handlers.get, rpc.method] }]` — ein **Map-Borrow**
des registrierten Handlers. Die Return-Ownership-Klassifikation (`freshOwned` ptr<>-Interface-Box)
matchte Producer nur per **Kurznamen**: es gibt `producer`-Methoden namens **`get`**
(deck/lobby/match/deckset-Service → `*Resp`), also `"get" ∈ ownsReturnFns`. Damit galt `handlers.get`
als owned Interface-Box → **kind-5-Drop bei JEDEM Dispatch** → der persistente Handler wurde
freigegeben; wiederholte Dispatches (Deck-Editor pollt `net_list_*`) → Double-Free → späterer
tcache-Abort beim Match-Start.

**Fix:** Producer per **Rückgabetyp** matchen, nicht nur Name. Neu: `ctx.ownsReturnFnTypes`
(Kurzname → Menge deklarierter Rückgabetypen) + `ctx.ownsReturnMatches(shortName, declType)`. Alle
vier `freshOwned`-Zweige (list/model/box/string) und die Lockstep-Drop-Registrierungen in
`visitDeclStmt` nutzen es. `handlers.get` (`ptr<mirror.RpcHandler>`) matcht die `get`-Producer
(`*Resp`) nicht mehr → `hnd` nicht mehr owned (`cleanup=unregistered` per AIML_TRACE_SYMBOL bestätigt)
→ kein Drop → kein Double-Free. `get_base` (echter `ptr<CardBase>`-Producer) matcht weiter korrekt.

**Gate:** volle `run_memory_sanitizers.sh` grün (0 Safety-Violations), inkl. producer_list/get_base/
string_reassign/producer_string. class-3-Kill-Switch bleibt **AN**.

**Lehre:** der Kurznamen-Match (spec/61 §6.4) ist kollisionsanfällig; die Rückgabetyp-Prüfung ist die
robuste Fassung. Container-Builtins (`map/list/kv .get/.at`) liefern Borrows und dürfen nie als
owned klassifiziert werden.

# 10. Container-String-Ownership: die "kein Mana / kaputte Event-Logs"-Regression (2026-09-13)

**Symptom:** Nach Aktivierung des Managed-String-Modells (class 3): "man produziert plötzlich kein
Mana mehr, auch die Event-Logs sind komisch bis kaputt". Der erste Verdacht (null-on-move →
dup-on-move in `consumeLocalDrop`) griff NICHT — die Ursache lag tiefer, in den Containern selbst.

**Reproduktion (isolierte Proben, kein Ratespiel):**
- `mana_pool_key_drop.aiml` (spiegelt `ManaPool`): `total=20.000.000` statt `2.000.000` — **exakt 10×**.
  Alle 10 Mana-Farbschlüssel `"1".."10"` **kollabierten in einen Map-Slot**.
- `event_log_push_drop.aiml` (spiegelt `log_event`): `bad=200.000` — **jede** gepushte Zeile war
  nach Rückkehr des Helpers unlesbar (dangling).

**Wurzelursache — Container speicherten GELIEHENE String-Pointer:**
Sowohl `MapImpl::put` (Keys/Values) als auch `list<string>.push` (Elemente) speicherten den `char*`
**roh** (kein dup). Das war "ok", solange Strings nie freigegeben wurden (Leak, aber jeder Pointer
distinkt). Mit dem Managed-String-Modell werden String-Locals am Scope-Exit **freigegeben** →
freigewordene Adresse wird nächste Iteration wiederverwendet → alle Container-Einträge zeigen auf
denselben (zuletzt geschriebenen) Puffer → Kollaps (Map) bzw. dangling (Liste).

Die bisherige "MOVE-in-Container + Caller-Drop-unterdrücken"-Strategie (`consumeLocalDrop`/
`consumeStringTemp`) funktioniert NUR, wenn `put`/`push` im **selben Scope** wie die String-Decl
steht. Über einen Helfer hinweg (`ManaPool.add_color → colors.put`; `log_event(msg) → lines.push(msg)`)
ist der echte Owner ein Frame höher, außer Reichweite der Suppression → er gibt den Container-String
frei.

**Fix (spec/61 §8) — Container BESITZEN ihre Strings (dup-on-insert, free-on-remove/drop):**
1. `MapImpl::put`: dupt String-**Keys** (Append) und String-**Values** (Append + decref-old bei
   Update). Neuer Param `valIsString`. Nicht-String-Values (Model/Ptr) bleiben MOVE.
2. `MapImpl::remove`: decreft entfernten String-Key/-Value vor dem memmove.
3. `list<string>.push`: dupt das Element (`aiml_str_dup`) — ersetzt+vereinheitlicht die alten
   Split-Pfade (MOVE für Locals/Temps, incref für Member-Strings) zu einer robusten Regel. Nicht-
   String-Elemente (Model/Ptr/Interface) bleiben MOVE/DeepCopy.
4. `ListImpl::remove`: decreft entferntes String-Element (fixt das churnende `event_log`-40-Zeilen-
   Ring-Leak: 8,6 MB → 65 KB).
5. `aiml_map_free`: läuft jetzt **dense** über `[0,size)` statt über das vestigiale `occ`-Bitmap
   (das `put` nie schrieb; die `[0,capacity)`-occ-Wanderung nach einem Grow war das alte
   "Invalid read of size 1", das den Map-Drop blockierte).
6. Caller-Suppression entfernt: String-Key/-Value (Map) und String-Element (Liste) sind jetzt reine
   **Borrows** — der Caller behält seinen Scope-Drop/Stmt-Temp und gibt seine eigene Kopie frei,
   egal wie viele Frames entfernt sie gebaut wurde.
7. Deserialisierung (`serialize_impl`): `aiml_jr_map_key/_val_str` liefern owned dups; da `put` jetzt
   dupt, wird die Reader-Kopie nach `put` freigegeben (fixt auch ein vorbestehendes Leak, inkl. des
   `ks` int-Key-Strings).

**Ergebnis (LIFETIME_DROPS=1):** `mana_pool_key_drop` `total=2.000.000` ✓, `event_log_push_drop`
`bad=0` ✓, beide bounded RSS. Volle `run_memory_sanitizers.sh` als Gate (Memory-Safety).

**Offen / bewusst außerhalb Scope (Folge-Arbeit):** `list<string>.insert`/`.set`/`.clear` haben
dasselbe latente Muster (nicht auf dem gemeldeten Pfad — event_log nutzt push/remove). Der
**kind-3-Map-Scope-Drop** für LOKALE Maps bleibt vorerst deregistriert (dense `aiml_map_free` ist
jetzt korrekt, aber die Move-Detection/Ownership-Analyse für producer-returned Map-Locals muss erst
auditiert werden, bevor der Drop scharf geschaltet wird) — Member-Maps werden über den Model-Free-
Pfad freigegeben, lokale Producer-Maps (`spend_amorph → alloc`) sind Move-out.

**Lehre:** Container-Ownership gehört IN den Container (dup-on-insert), nicht in fragile Caller-seitige
Drop-Suppression. Sobald Werte tatsächlich freigegeben werden, ist "roh speichern + Caller-Drop
unterdrücken" nur über einen einzigen Scope hinweg korrekt.

# 11. Value-Model-Sharing Double-Free-Klasse (drops-on-weit, 2026-09-13)

**Kontext:** Nach dem Container-Ownership-Fix (§10) lief Mana/Event-Log wieder, aber das Spielen
einer Kreatur/eines Zaubers crashte (`free(): double free`). Ursache war NICHT der Container-Fix —
sie ist VORBESTEHEND und trat auf, weil `AIML_LIFETIME_DROPS=1` (Deploy-Default der Game-Configs)
Modelle nun tatsächlich freigibt. spec/61 war „DEFAULT OFF", Re-Enable-Gate „volle Suite grün" nie
erfüllt. Auf User-Wunsch läuft die Testsuite jetzt per Default mit Drops AN
(`run_all_tests.sh` exportiert `AIML_LIFETIME_DROPS=1`), damit genau dieser Pfad gated wird.

**Wurzelursache (systemisch):** Value-Modelle mit Heap-Feldern werden an vielen Stellen FLACH
kopiert (Struct-Copy teilt die inline list/map-`data`-Pointer). Mit Drops-AN gibt dann jeder
Eigentümer denselben Puffer frei → Double-Free. Betroffene Escape-Routen (jeweils per valgrind+gdb
auf Modell-`serNodes` + AIML-Funktion lokalisiert), alle drops-on-gated gefixt:

1. **Producer gibt eine VIEW zurück** (`return at(..)` / `return obj.field` / Accessor) →
   Deep-Copy in ein Temp vor `ret` (visitRetStmt, strukturelle View-Erkennung; kein Fehlalarm auf
   owned gebaute Locals wie `MatchResp out`).
2. **`set [local, view]`** (z. B. `load_entry_at` `set [out, {at:[lez,pos]}]`) → Deep-Copy in
   visitSetStmt (AtExpr/Member/Accessor-Quelle).
3. **`decl [x, Model, view]` + späteres `return x`** → View-Taint `Symbol::is_view_alias` bei der
   Decl (read-only Views bleiben billig, keine Kopie), Deep-Copy erst beim Return.
4. **`idx: [list, i, model]`** Index-Store (z. B. `store_entry_at` `sez[pos]=ent`, überall im
   Combat) → free-old + Deep-Copy. **KRITISCHE Reihenfolge:** die Quelle kann das ZIEL-Slot
   ALIASEN (Write-Back-Idiom `idx:[c,i,at(c,i)]`), daher Deep-Copy zuerst in ein unabhängiges Temp,
   DANN das alte Element freigeben, DANN Temp in den Slot memcpy'n. Free-vor-Copy las freigegebenen
   Speicher in `enc()` → SIGSEGV (das war der zweite, tiefer liegende Crash).
5. **cascade_class-Instanz** (`new MkpEffPool`, spec/58) → aus dem Owned-Box-Drop AUSGESCHLOSSEN
   (`!ctx.cascadeClasses.count(boxed)` in freshOwned + visitDeclStmt, analog zu
   `classGroupContainers`). Jede Instanz BORGT den einmalig geladenen, geteilten Cascade-Puffer
   (alle `new` zeigen auf dieselben globalen Rows) — ein kind-4-Drop hätte den geteilten Global
   freigegeben → Double-Free über Instanzen hinweg.

**Ergebnis (drops-on):** vorher crashende Tests jetzt grün — `test_match_play`,
`test_creature_costs`, `test_creature_ability_reaction`, `test_target_soulmage_or_creature`,
`test_trigger_per_match`, `test_mkp_effect_cascade`, `test_mkp_multi_trigger`. Container-Sanitizer-
Suite bleibt grün (Compiler-Änderungen memory-safe).

**Debug-Technik (dokumentiert für Wiederverwendung):** `MALLOC_CHECK_=3` + gdb bricht am ECHTEN
Double-Free ab (nicht erst später bei `malloc_consolidate`); gdb symbolisiert JIT-Funktionsnamen
(`fre`/`aiml_ser_free_decoded` in libaiml.so + AIML-Funktion darüber) und die `ns=__aiml_sernodes_X`
nennt das Modell. VORSICHT: Adress-Wiederverwendung (free→alloc-selbe-Addr→free) sieht in reinen
Adress-Logs wie ein Double-Free aus — nur der MALLOC_CHECK-Abbruch bzw. valgrind ist verlässlich.

**Offen:** Leak-Prüfung der Suite via `AIML_MEM_DIFF` (User-Wunsch) — nachdem die Crashes weg sind.
Perf: die Escape-Deep-Copies laufen nur an Escape-Stellen (Return/Set-from-View/Index-Store), NICHT
bei jedem read-only View-Zugriff.

# 12. Drops-on-Suite-Härtung: per-Route-Fixes (2026-09-13, Fortsetzung)

Auf User-Wunsch läuft die 9rooms-Suite jetzt drops-on per Default. Sie deckte ~28 Fehler auf; ~18
davon waren drops-on-SPEZIFISCHE Crashes (rc=139 SIGSEGV / rc=134 double-free; alle grün mit
drops-off) — die VORBESTEHENDE, nie-gegatete drops-on-Klasse. Kein Wurzel-Fix erreichte alle auf
einmal; es sind MEHRERE eigenständige Wurzeln. Debug durchweg via `MALLOC_CHECK_=3`+gdb
(symbolisiert JIT-Funktionen + `ns=__aiml_sernodes_X` nennt das Modell).

Gefixte Wurzeln (alle drops-on-gated):
- **Value-Model-Write-Back-Klasse** (§11): `store_entry_at`-Self-Alias-Index-Store (Deep-Copy-zuerst-
  dann-free), Producer-gibt-View-zurück, cascade_class-Box-Drop-Ausschluss. → 7 Tests grün
  (match_play, creature_costs, creature_ability_reaction, target_soulmage_or_creature,
  trigger_per_match, mkp_effect_cascade, mkp_multi_trigger).
- **Model-String-Feld-Default = IMMORTAL "" statt `.rodata`-Literal.** `initClassContainerMembers`
  setzte unbelegte String-Felder auf `CreateGlobalStringPtr("")` (rodata). aiml_ser_free_decoded
  decreft beim Drop JEDES String-Feld — decref eines rodata-Literals liest dessen `-8`-Header
  (Seitenrand-Fault ODER STRN-Magic-False-Positive → `free()` von rodata → SIGSEGV). Fix: neuer
  `aiml_str_empty()` liefert ein statisches, refcounted "" mit Sentinel-Refcount
  `AIML_STR_IMMORTAL` (incref/decref no-op, nie freigegeben). `.len`/strlen sehen weiter "".
  → +5 Tests grün (add_life_clamp, buff_expiry, x_mana_cost, ability_text, cost_processor);
  enchantment_picker von Crash→Logik-Fail. **12 Crash-Sites gesamt gefixt.**

**Verworfen (net-negativ):** volle Value-Semantik (Deep-Copy von Views bei der Decl). Brach
9rooms-Logik (enchantment_picker: In-Place-View-Mutation `myc=at(..); apply_buff(myc)` verliert den
Buff) UND Perf (match_play-Timeout: Deep-Copy in Hot-Loops). 9rooms-Code SETZT auf geteilte
Views-mit-In-Place-Mutation → echte Value-Semantik erfordert Game-Code-Rewrite. Daher: Views bleiben
GETEILT, Deep-Copy NUR an echten Escape-Stellen (return/set/idx-store).

**Verbleibende drops-on-Crashes (eigenständige Wurzeln, offen):**
- `typed_card_persist`: rodata-"" in einem CreatureCard-String-Feld MIT explizitem Default
  (`m.initValue`-Pfad in initClassContainerMembers, ODER embedded-Model-Feld) — der Immortal-Fix
  deckt nur `!m.initValue`. Fix-Richtung: String-Literal-Defaults ebenfalls immortal/dup.
- `card_states`: `list<Buff>`/`list<CardState>`-Element mit GARBAGE-String (nicht rodata — echter
  Müll-Pointer) → Init-/Korruptions-Wurzel bei list<Model>-Elementen.
- `any_deck_draw`: C++ `__cxa_throw` → abort (AIML-Exception, KEIN Memory-Crash) — separate Ursache.
