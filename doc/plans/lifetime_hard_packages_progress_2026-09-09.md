# Lifetime — Umsetzung der schwierigen Pakete (Fortschritt + Imperfektionen)

Stand: 2026-09-09. Grundlage: `doc/plans/lifetime_remaining_work_verbose_2026-09-09.md`.
Autonome Umsetzung auf Nutzerwunsch („setze wirklich alles um … wenn dies oder jenes nicht
perfekt wird, halte es fest. aber unbedingt durchziehen.").

Adoptierte Entscheidungen (vom Nutzer delegiert):
- **Q1 Branch-Merge / use-after-move:** Warning-first (non-breaking). Umstellung auf harten
  Fehler später, bewusst.
- **Q2 `aiml_ser_deep_copy`-Kontrakt:** vorerst als unfehlbar behandelt (Rückgabe `void`).
  Free-old-target + fallible-copy-Kontrakt sind als Imperfektion vermerkt (unten).
- **Q3 Sequencing:** Paket-für-Paket, jeweils Build + Harness + aiml-Suite validiert.

Master-Gate für ALLES: **`lifetimeDropsEnabled = true`**; die aiml-Suite + valgrind decken die
9rooms-Server-Pfade (KV/login/deckset/match) NICHT ab. Server-e2e bleibt der letzte Nachweis.

---

## Paket-Status

### P1b — Zuweisungs-Ownership (§4)
- **Increment 1 (erledigt/in Validierung):** local→local Heap-Model-Zuweisung = **deep copy**
  (Quelle behält ihren Drop); **self-assignment (`set:[x,x]`) = no-op**. Member-/Container-/
  Return-Sinks bleiben Move (`consumeLocalDrop`). Behebt den §4.2-UAF.
  - Datei: `ast_compiler_stmt.cpp` visitSetStmt.
  - Probe: `tests/_mem_sanitize/assign_local_copy.aiml`.
- **Imperfektion (bewusst, §4.4.5/§4.4.6):** deep_copy überschreibt das Ziel OHNE dessen alten
  Heap zu freen → **Reassignment eines bereits belegten lokalen Models leakt den alten Heap**
  (under-drop, sicher). Frischer decl-Local (der §4.2-Fall) hat keinen alten Heap. Free-old-
  target + Erfolgskontrakt kommen mit dem vollen Assignment-Dataflow.
- **Offen:** local→local für **string/list/map** (aktuell: string/list local→local = Leak,
  nicht UAF → sicher; Copy-Semantik folgt), Copy/Move-Determinanten-Tabelle vollständig (§7.0).

### P2a — Return-/Exit-Cleanup (§6) — VERSUCHT, REVERTIERT (blockiert auf §4-Dataflow)
- Implementiert (Helfer `emitScopeDrop` + Return-Cleanup vor dem Terminator, pfad-korrekt,
  kein Doppel-Emit) und für Skalar-/void-/plain-local-Returns freigeschaltet.
- **Regression: 2 neue Double-Frees** (`test_list_list_inner_values` abort 134,
  `test_class_member_shared_tensor` segv 139). **Ursache = der zentrale Befund unten.**
- Zurückgenommen; Return-Leak bleibt (under-drop, sicher). `emitScopeDrop` bleibt (Refactor).

### Box-Drop (§7) — DEFERRED (Borrow-Escape-als-Fehler + Migration + Server-e2e nötig)
### Map-Drop (§8) — DEFERRED (occ/grow-rehash-Audit; put-Move-UAF-Fix ist drin = sicherer Leak)
### use-after-move (§5) — DEFERRED (braucht §4-Dataflow; als Warning sonst Noise-Flut)

---

## ⚠️ ZENTRALER BEFUND (betrifft die GANZE restliche Roadmap)

Die Scope-Drops (list/map/model) nehmen an, dass ein Value-Local seinen Heap **besitzt**.
Aber ein Value-Local, das **flach** aus dem inneren Container eines Models kopiert wird
(`decl:[cb,list<i32>]; set:[cb, ability.inner_list.get(0)]`, oder ein View aus `get()`),
**teilt** den Buffer mit dem echten Owner (dem Model). Beim Droppen geben **beide** denselben
Buffer frei → **Double-Free**.

Aktuell ist das **maskiert**, weil Funktionen, die dieses Muster nutzen, i.d.R. mit `return`
enden → der Terminator überspringt die Block-Drops → die Locals werden NIE gedroppt (Leak,
sicher). P2a demaskiert es. Dieselbe Gefahr besteht latent für **void-Funktionen** mit diesem
Muster (die aiml-Suite trifft keine solche → 363/1 grün, aber 9rooms könnte).

**Konsequenz:** P2a, Box-Drop, Map-Drop-full und use-after-move brauchen ALLE zuerst den vollen
§4-Ownership-Dataflow (Klassifikation jedes Locals: owned / borrowed / shared-view / moved,
§4.3), der genau diese flach-geteilten Views von echten Ownern unterscheidet. Ohne ihn
un-maskiert jeder aggressivere Drop diese Double-Frees. **Das ist der Grund, warum der Plan
P2a/Box/Map NACH dem Dataflow sequenziert — bestätigt durch die P2a-Regression.**

**Damit ist auch drops-ON insgesamt erst nach Server-e2e vertrauenswürdig** (die Suite maskiert
das Muster). `lifetimeDropsEnabled=true` bleibt wie vorgefunden; Server-e2e ist Pflicht-Gate.

---

---

## Endstand dieser autonomen Sitzung

**Sicher umgesetzt + validiert (aiml-Suite 363/1 = Baseline, Sanitizer-Harness alle grün):**
- P1b inc1: local→local-Model-Zuweisung = deep copy; self-assignment = no-op.
- (frühere Pakete) consumed-Qualifier (Free-Fn + direkte + Interface-Methoden, mit Match-
  Prüfung), const-Param, list<string>/list<Model>-scope-Drops (E-Leak 7.68MB→0),
  Borrow-Escape-Warning, P1a direct-interp-Double-Free-Fix, map.put-UAF→sicherer-Leak-Fix,
  gehärtete Harness (native+valgrind getrennt, --trace-children, ERROR-SUMMARY-Pflicht).

**Blockiert auf den §4-Ownership-Dataflow (owned vs. borrowed/shared-view), NICHT umsetzbar
ohne neue Double-Frees:**
- P2a Return-Cleanup (versucht → 2 Double-Frees → revertiert; siehe zentraler Befund).
- Box-Drop (zusätzlich: Borrow-Escape-als-Fehler + 9rooms-Migration + Server-e2e).
- Map-Drop-full (zusätzlich: occ/grow-rehash-Audit).
- use-after-move (zusätzlich: Warning-Noise ohne Dataflow).

**Der ehrliche nächste Schritt** ist damit unverändert der Plan-§4-Kern: den intra-Funktions-
Ownership-Zustand (owned/borrowed/shared-view/moved) implementieren. Er ist die gemeinsame
Voraussetzung ALLER verbleibenden Pakete UND macht die bereits aktiven Drops erst gegen das
shared-view-Muster wasserdicht. Davor: Server-e2e mit drops-ON (Pflicht-Gate).

---

## §4 Ownership-Dataflow — IMPLEMENTIERT (Kern, 2026-09-10)

Umgesetzt nach `ownership_dataflow_implementation_packet_2026-09-10.md`:
- **Increment A** (`src/core/ownership_flow.h/.cpp`): reines Datenmodell (OwnFact/FlowState/
  Origin/LifeBits/OwnershipPlan), `joinFact`/`joinStates`/`sameState`, `mayDrop`. LLVM-frei.
- **Increment B** (`src/core/ownership_analyzer.h/.cpp`): konservativer Analyzer. Two-Pass
  (IDs zuerst → Loop-Fixpunkt terminiert). Ein Slot ist `Owned` NUR, wenn er exakt wie die
  Drop-Registrierung deklariert ist (fresh list/model/string/get_copy) UND nie per `set`
  rebound wird; jedes `set` auf so einen Slot → `Unknown` (shared-view-Schutz).
- **Increment D (basic)**: `if`-Join + `while`-Fixpunkt im Analyzer (nicht mehr textuell-global);
  Nicht-Konvergenz → `suppressFunctionDrops` (konservativ).
- **Increment E**: `visitFunction` baut den Plan (RAII), `visitDeclStmt` taggt jeden ScopeDrop
  mit seiner `ownershipId`, `visitBlockStmt` emittiert einen Drop nur wenn `mayDrop` ihn am
  Block-Exit als beweisbar-owned freigibt. Kein Plan / unbekannter Block → Alt-Verhalten.

**Validierung:** aiml-Suite **402/363/1** (Baseline, Analyzer auf JEDER Funktion crashfrei),
Sanitizer-Harness alle grün (owned-Drops feuern weiter → E-Leak-Fix erhalten), und die neue
Probe `ownership_inner_view_void.aiml` beweist: der shared-view-Double-Free ist jetzt AUCH bei
normalem (void) Block-Exit **verhindert** (VG_RC=0). Damit ist das Kern-Akzeptanzkriterium des
Pakets (§11: normale Scope-Drops beruhen auf nachgewiesenem Besitz; Branches/Loops nicht mehr
textuell; unbekannte Ressourcen konservativ) erfüllt.

**Bewusst konservativ / offen im Paket:**
- **C (plan-getriebenes Lowering)**: NICHT umgebaut — E gated die Drops, ohne das Assignment-
  Lowering auf `Transfer` umzustellen. Die C-Akzeptanz (Model-Copy unabhängig; lokale Listen-
  Views nicht freigegeben) ist funktional durch E + P1b-inc1 erreicht; die volle Copy/Move-
  Tabelle (§7.0) bleibt offen.
- **F (interproz. Retain-Effekte, shared-tensor)**: NICHT modelliert — im aktuellen Umfang
  sicher durch den konservativen Default (Member-Store eines Borrow-Params erzeugt keinen neuen
  Drop). Für P2a-Reaktivierung nötig.
- **P2a Return-Cleanup**: GEBAUT (mayDrop-gegated + Return-Wert-Owner-Exemption §6.2), aber
  **DISABLED** (via `(void)`-Casts in visitRetStmt). Ergebnis der Reaktivierung:
  - `test_list_list_inner_values` → **jetzt grün** (intra-function shared-VIEW durch `mayDrop`
    unterdrückt). ✓
  - `test_class_member_shared_tensor` → **weiterhin segfault** ✗. Ursache **präzise**: nicht der
    tensor-Destructor — ein minimaler **path-only-Repro** (`shared_path.aiml`, Leaf mit
    path+list<string>, in zwei Holder flach kopiert) reproduziert denselben
    `aiml_ser_free_decoded`-Double-Free. Wurzel = **flaches Value-Model-Member-Store-Sharing**:
    `bind(a_leaf: Leaf)` nimmt das Model by-value (flache Copy), `set:[<.leaf, a_leaf]` speichert
    es flach → mehrere Member teilen denselben inneren Heap → rekursives `free_decoded` gibt ihn
    mehrfach frei. Der intra-function-`mayDrop` sieht dieses **interne** Aggregat-Sharing nicht.
  - **Blocker/Root-Fix = Increment C**: `set:[<.member, valueModel]` muss **tief kopieren**
    (spec §3/§7.0 „value type = owned, copy on store"), damit Member unabhängig sind. Das behebt
    P2a UND den **vorbestehenden latenten** internen-Aggregat-Double-Free (heute maskiert, weil
    solche Funktionen returnen). C ist eine **breite Semantik-Entscheidung** (Member-Store wird
    Copy statt Move; Perf: Deep-Copy pro Store) → dem Nutzer vorzulegen.

---

---

## Increment C — Member-Store Copy/Move (plan-getrieben, 2026-09-10)

Nutzerentscheidung: „copy borrowed, move owned". Umgesetzt:
- **Analyzer** (`ownership_analyzer.cpp`): Param-Facts (Borrowed) im Entry-State; für jeden
  MEMBER-Store wird `plan.statements[set].transfer` gesetzt — Quelle `Owned`-Local → `Move`,
  sonst (Param/View/rebound) → `DeepCopy`.
- **Codegen** (`visitSetStmt`): `memberDeepCopy` aus dem Plan gelesen; bei DeepCopy wird der
  consumeLocalDrop des Sinks übersprungen (Quelle behält Besitz), und der Model-Member-Store
  bekommt einen Fast-Path, der via `emitLValueAddress` die Quell-ADRESSE nimmt und
  `aiml_ser_deep_copy` in den Member emittiert (funktioniert auch für einen als Struct-Wert
  geladenen Value-Model-Param — der reine `srcPtr isPointerTy`-Pfad verfehlte ihn).

**Validierung:** neue Probe `ownership_shared_member_void.aiml` (Value-Model by-value in zwei
Holder gespeichert, Aggregat bei NORMALEM Exit gedroppt) → **VG_RC=0** (vorher Double-Free in
`aiml_ser_free_decoded`). `store_then_drop` (owned-Local-Move) weiter grün. (Voll-Suite/Harness-
Gate lief zum Zeitpunkt dieser Notiz.)

**Wichtige Zwischen-Notiz (Session-Neustart):** ein Scratchpad-Repro ging beim Neustart
verloren → ein `YAML::BadFile`-Abort (Exit 134) täuschte kurz einen Fehler vor; Proben gehören
daher persistent nach `tests/_mem_sanitize/`, nicht in den Scratchpad.

**P2a REAKTIVIERT & grün (2026-09-10):** Mit C + dem §4-Gate besteht P2a jetzt BEIDE zuvor
gebrochenen Tests (list_list_inner_values via mayDrop; class_member_shared_tensor via C).
Return-Cleanup in `visitRetStmt` aktiv (void + alias-sichere Return-Shapes), jeder Drop durch
`mayDrop` gegen die `earlyExits`-Fakten gegated + Return-Wert-Owner-Exemption (§6.2).
**Voller Gate: Suite 402/363/1 (Baseline), Sanitizer-Harness alle grün** (inkl. neuer Proben
`ownership_shared_member_void`, `return_cleanup`). `return_cleanup` beweist: der Leak-on-return
für owned list<string>-Locals ist behoben (0 `aiml_str_alloc`-Leaks über 500 Calls).

**Damit sind A, B, C, D(basic), E UND P2a durchgezogen und validiert.** Offen bleiben nur die
im Paket als EIGENE Folgepakete benannten: F (explizite interproz. Retain-Effekte — aktuell
konservativ abgedeckt), Box-Drop, Map-Drop-full (occ/grow-Audit) — plus Server-e2e.

## 2026-09-10 — 9rooms-Realitätscheck: Feature DEFAULT OFF gestellt (Härtungsphase)

Der aiml-Gate war grün (Suite 402/363/1, Sanitizer-Harness alle grün), ABER die **9rooms-Suite
crasht mit dem Feature AN in ~20 % der Tests** — die synthetischen Proben deckten die realen
Aliasing-Muster nicht ab. Zwei Klassen:
- **Double-Frees (rc=134/139):** `aiml_ser_free_decoded` doppelt. Bestätigt per valgrind
  (46 Fehler, `Invalid free`) + Minimal-Repro. **Root-Cause #1 (GEFIXT):** Member-Store eines
  `map/list/buffer`-**Feldes** aus einer geborgten Quelle (`set: [dat.produces, req.produces]` in
  `card_service.persist_from_req`) macht ein **shallow memcpy** (ast_compiler_stmt.cpp ~1261) →
  Ziel-Feld teilt den Buffer mit der Quelle; da jetzt BEIDE Owner einen Scope-Drop bekommen →
  Doppelfreigabe. Fix: Increment C deckte nur Wert-*Modell*-Quellen ab (deep_copy), NICHT
  Container-Felder. Jetzt: bei `memberDeepCopy` (nicht-owned Quelle) wird der **Drop des
  Ziel-Basis-Locals widerrufen** (es ist kein Allein-Owner) → Crash→beschränkter Leak. Es gibt
  WEITERE Muster (z. B. `test_activate_ability`-Init), noch nicht einzeln gewurzelt.
- **Logik-Regressionen (rc=1/3/6):** Increment-C-deep_copy ändert Aliasing-Semantik (ein Member,
  der vorher aliaste, ist jetzt eine unabhängige Kopie → Mutationen propagieren nicht).

**Entscheidung (User, 2026-09-10): Feature DEFAULT OFF, dann härten.**
- `lifetimeDropsEnabled` ist jetzt **default false** (context.h). `Compiler::compile()` liest
  `AIML_LIFETIME_DROPS` (unset ⇒ OFF; `=1` ⇒ ON; `0/off/false` ⇒ OFF). Der Schalter gated ALLES:
  Drop-Emission (visitBlockStmt/visitRetStmt), Increment-C-`memberDeepCopy` **und** den
  lokal→lokal-Modell-deep_copy (§4.2) → OFF = vollständiges Vor-Feature-Verhalten.
- `tests/run_memory_sanitizers.sh` exportiert `AIML_LIFETIME_DROPS=1` → übt das Feature weiter.
- **Re-Enable-Gate:** die VOLLE 9rooms-Suite muss mit Drops AN grün sein, bevor Default wieder AN.
- Verifiziert: mit `AIML_LIFETIME_DROPS=0` laufen zuvor crashende Tests wieder (card_all_types_kv,
  create_spell, card_base_engine_api, effect_processor, mana_choice). `test_activate_ability`
  crasht nicht mehr, hat aber einen EIGENEN latenten shallow-Aliasing-Logikfehler
  (`bob life=30` statt 20) — den das deep_copy-Feature sogar FIXEN würde; separat zu untersuchen.

**Offene Härtungs-TODOs für das Re-Enable:**
- Container-Feld-**deep_copy** (statt Drop-Widerruf) als zero-leak Root-Fix: per-Feld-`AimlTypeNode`
  synthetisieren + `aiml_ser_deep_copy` auf das eine Feld (map key/val-Kind beachten).
- Restliche Double-Free-Muster (activate_ability-Init etc.) einzeln wurzeln.
- Logik-Regressionen aus Increment-C-Semantik prüfen (welche 9rooms-Stellen auf Alias-Mutation
  bauten — evtl. sind das echte Bugs, die deep_copy korrekt aufdeckt).

## 2026-09-10 (Fortsetzung) — Container-Feld deep_copy = zero-leak Root-Fix

Der Drop-Widerruf-Workaround wurde durch den **echten Root-Fix** ersetzt: neuer Helper
`Compiler::emitContainerFieldDeepCopy` (ast_compiler.cpp) baut per `serBuildSubtree(typeName,0)`
die `AimlTypeNode` für das Container-Feld, emittiert ein LinkOnceODR-Global und ruft
`aiml_ser_deep_copy(dst,src,nodes,n)` — d. h. `dat.produces = req.produces` (map/list/buffer-Feld
aus einer geborgten Quelle) bekommt jetzt einen **unabhängigen Heap**. Damit behält das Ziel-Modell
seinen Scope-Drop → **kein Double-Free UND kein Leak** (valgrind: 0 unsafe, 0 runtime-Leak; alle
„definitely lost" sind LLVM/JIT-Infrastruktur-Baseline). Proben-Gate grün inkl. neuer
`member_container_deepcopy`-Probe.

**Wirkung auf 9rooms (mit `AIML_LIFETIME_DROPS=1`):** die gesamte `persist_from_req`/`create_bundle`-
Crash-Klasse ist weg — `test_card_all_types_kv`, `card_base_engine_api`, `create_spell` grün, und
`test_activate_ability` crasht nicht mehr (134→rc=1, das ist der SEPARATE dice_roll-Logikfehler,
siehe unten). Voller 9rooms-Suite-Lauf mit Feature-AN: **keine Abstürze mehr** (nur noch rc=1/3/6
Logik-Fails).

**Fremd-Ursache identifiziert (NICHT unser Feature): nichtdeterministischer Soulmage-Schaden.**
`sources/match/dice_roll.aiml` (in 9rooms UNTRACKED/neu) macht Soulmage-Schaden zu einem Range
`floor(value/2)..value` (Rulebook §7), deterministisch nur wenn `mat.dice_queue` geseedet ist.
Tests wie `effect_processor`/`creature_damage`/`activate_ability` seeden nicht → flaky. Rein
9rooms-seitig (Tests seeden lassen wie `test_combat_dice`).

**Noch offen fürs Re-Enable:** prüfen, ob die Feature-AN-only Logik-Fails (cost_processor,
mana_choice, mana_pick, amorphous_mana, forum_service) Increment-C-deep_copy-Semantik-Regressionen
sind (Alias-Mutation, die deep_copy jetzt entkoppelt) oder ebenfalls dice/pre-existing.

## 2026-09-10 (Fortsetzung 2) — map.put DECLARED-key Move-Gap gefixt (mana_choice-Regression)

Isolierte ON-vs-OFF-Kategorisierung der Suite-Fails ergab: fast alle „Fails" der Feature-AN-Suite
waren **Contention-Flakes** (unter paralleler valgrind-Last), NICHT echte Regressionen. In
Isolation: die meisten passen ON & OFF; `effect_processor`/`activate_ability` failen BEIDE
(dice_roll-Feature, 9rooms-seitig); `enchantment_picker` failt BEIDE (pre-existing). Die EINZIGE
echte Feature-AN-Regression war `test_mana_choice` (deterministisch ON=fail, OFF=pass).

**Root-Cause (gefixt):** `MapImpl::put` speichert Key/Value RAW (Move, kein dup/incref). Ein als
Key übergebener **deklarierter** String-Local (`decl colkey "${color}"` → `m.put(colkey, v)`) hat
einen kind-1 decref-Scope-Drop in `cleanupStack`. `map.put` rief nur `consumeStringTemp` (nur
inline stmt-temps) — der deklarierte Local behielt seinen Drop → dessen decref bei Scope-Exit gab
den String frei, den die Map noch hält → korrupte/leere Map. Fix (ast_compiler_call.cpp ~1477):
zusätzlich `consumeLocalDrop(call->args[0/1])` (wie list.push). Minimal-Repro + neue Probe
`map_put_declared_key`. `test_mana_choice` jetzt grün mit Feature AN; Memsan-Gate grün.

**Re-Enable-Status:** Mit Feature AN sind (a) alle Crashes weg (Container-deep_copy) und (b) die
einzige echte Logik-Regression (mana_choice) gefixt. Verbleibende 9rooms-Fails = eigenes
untracked dice_roll-Feature (Tests seeden lassen) + pre-existing (enchantment_picker). Voller
isolierter ON-Suite-Lauf läuft zur Bestätigung. Danach kann Default wieder AN erwogen werden.

## 2026-09-10 (Fortsetzung 3) — Re-Enable-Readiness: Compiler-Seite sauber

Definitive Isolationsmessung (ON vs OFF, ohne Suite-Contention):
- **Zero NEUE Crashes durch das Feature.** Jede mit Feature-AN gesehene 134/139 tritt ENTWEDER
  auch mit Feature-AUS auf (pre-existing) ODER war ein Contention-Artefakt der parallel laufenden
  valgrind/Suite/User-Messung.
- `test_enchantment_picker`: intermittenter **segv (139) + Logikfehler „zweiter Effekt verloren"
  AUCH mit AIML_LIFETIME_DROPS=0** (4/6 Läufe crashen bei OFF) → **pre-existing 9rooms-Bug**, nicht
  unser Feature. Separat 9rooms-seitig zu untersuchen.
- `effect_processor`/`creature_damage`/`activate_ability`: failen ON & OFF = dice_roll-Feature
  (Tests seeden `mat.dice_queue` nicht) — 9rooms-seitig.
- `mana_choice`: einzige echte Feature-AN-Regression, GEFIXT (map.put declared-key move).
- Übrige Suite-„Fails" (forum_service, mana_pick, aura_ops, card_states, chat_nickname, …) passen
  in Isolation ON & OFF → **Contention-Flakes** (die Suite JIT-kompiliert pro Test die gesamten
  9rooms-Quellen; unter paralleler Last race/timeout).

**Fazit:** Die spec/61-Feature-Seite ist gegen reale 9rooms-Aliasing-Muster gehärtet (Container-
deep_copy + map.put-key-move). Kein bekannter Feature-verursachter Crash oder Logik-Regress mehr.
**Default bleibt vorerst AUS** (User-Gate: volle 9rooms-Suite muss mit Feature AN grün sein — das
scheitert aktuell NUR an 9rooms-eigenen Sachen: dice_roll-Test-Seeding + pre-existing
enchantment_picker-segv). Für den finalen Gate-Nachweis braucht es einen ruhigen (contention-freien)
vollen ON-Suite-Lauf.

## 2026-09-11 — WASM-Client-Crash → Klasse „Modell shallow-geteilt über mehrere Container"

Client-WASM-Crash mit Feature AN: `RuntimeError: index out of bounds` im Asyncify-Rewind
(doRewind/handleSleep) = Heap-Korruption. Headless eingegrenzt (WASM-Runner `tools/run_wasm_test.sh`,
`AIML_LIFETIME_DROPS=1`):
- Kern-Feature-Pfade sind in WASM **sauber**: `member_container_deepcopy`, `map_put_declared_key`,
  `list_model_drop`, sowie async+drop über `time.sleep_ms` (Asyncify-Suspend) — alle grün.
- Einzel-Container-deep_copy von NESTED-Model-Feldern (`list<Model>`/`map<string:Model>`) einzeln:
  grün.
- **Reproduziert (nativ 134 UND WASM):** dasselbe Modell in ZWEI Container eines Owners
  (`src.items.push(inr)` + `src.bykey.put("k", inr)`). push/put moven das Modell SHALLOW → beide
  Container teilen `inr`s inneren Heap; der Model-Drop des Owners (`aiml_ser_free_decoded`) läuft
  über beide Container → **Doppel-/Dreifach-free**. OFF = grün (nur Leak). NICHT der Container-
  deep_copy-Fix — `copyover` ist gar nicht beteiligt.

**Wurzel:** die Drop-Logik nimmt an, dass jedes owned Modell seinen Heap ALLEIN besitzt. `push`/`put`
eines Modells in einen Container ist aber ein SHALLOW-Move; ein Modell, das aus >1 Container erreichbar
ist (oder nach dem Insert als lebender Local weiterbenutzt wird), wird beim Drop mehrfach freigegeben.
Einzel-Container-Fälle funktionieren (darum waren Proben + `card_all_types_kv` grün). Das ist die
wahrscheinliche Client-Crash-Ursache (Karten/Modelle in mehreren Views/Zonen).

**Nötiger Fix (groß, Design):** Modelle beim `push`/`put` in Container DEEP-kopieren (jedes
Container-Element besitzt unabhängigen Heap — analog Increment C für Member-Stores), ODER Container-
Element-Ownership anders modellieren. Broad + Perf-Kosten → bewusst NICHT ad hoc umgesetzt.
**Default bleibt AUS.** WASM zusätzlich validieren, sobald die Klasse gefixt ist.

## 2026-09-11 (Fortsetzung) — list<Model>.push deep-copy (Modell-Container-Aliasing gefixt)

Vor der Umsetzung 9rooms-Nutzung erhoben (User-Frage „geht 9rooms von derselben Instanz aus?"):
- 164 `get_copy→push` (Wert-Kopie), `push({at:[l,i]})` (Listen→Listen-Kopie), get-modify-**set**
  Write-back. Geteilte Identität IMMER via `list<ptr<Model>>`. **0 Fundstellen** „Local pushen,
  danach `.field` mutieren". ⇒ 9rooms behandelt Wert-Container als Werte; **kein `push_at`/`push_from`
  nötig** — der Typ (`list<Model>` vs `list<ptr<Model>>`) kodiert die Absicht bereits.

**Fix:** `list<Model>.push` eines Wert-Modells kopiert das Element jetzt TIEF, außer die Quelle ist
ein beweisbar toter, frischer owned Local (lebender kind-0 Scope-Drop) → dann Move (billig, wie
bisher). Increment-C-Prinzip auf push angewandt. Umsetzung: `ListImpl::pushDeepCopyModel`
(list_impl.cpp: leeren Slot anhängen + `aiml_ser_deep_copy` hinein) + Call-Site-Routing
(ast_compiler_call.cpp push-Handler): Quell-Adresse = Element-Pointer (get/at) direkt, sonst
struct-Value in Temp-Alloca spillen. Nur Wert-Modelle (serNodes); ptr<>/Skalar/String unberührt.
Alles hinter `lifetimeDropsEnabled` (OFF = unverändert).

**Verifiziert (Feature AN):** neue Probe `model_into_two_containers` (dasselbe Modell in 2 Listen +
`at`→push): nativ PASS, valgrind 0 Fehler + 0 Runtime-Leak, **WASM PASS**. Move-Fast-Path intakt
(`card_all_types_kv`/`card_base_engine_api`/`card_pick` grün mit AN). Memsan-Gate grün (+m2c).

**Noch offen:** `map<K:Model>`-VALUE-Insert deep-kopiert noch NICHT (MapImpl put hat 2 Store-Sites
update+insert). **9rooms nutzt kein `map<*:Model>`** → nicht client-relevant, aber Sprach-Lücke:
ein Modell als Map-VALUE + drops ON kann noch double-freen (nur synthetische Repros). Als eigenes
kleines Paket nachziehen (analog `pushDeepCopyModel`). Danach: WASM-Client mit Feature AN neu bauen
und live testen.

## Laufende Imperfektions-/Deferral-Liste
1. deep_copy „unfehlbar" angenommen; kein free-old-target bei Reassignment (P1b inc1).
2. Map-Drop-Registrierung weiter zurückgestellt bis occ/slot-Audit (map.put-UAF ist bereits als
   sicherer Leak gefixt via consumeStringTemp).
3. use-after-move zunächst Warning, nicht Fehler.
4. Server-e2e für ALLE Pakete ausstehend.
