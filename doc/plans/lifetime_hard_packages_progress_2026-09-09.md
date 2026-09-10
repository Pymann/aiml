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
- **P2a Return-Cleanup**: bleibt AUS (Paket §1). Jetzt de-risked — die `earlyExits`-Fakten +
  `mayDrop` liegen bereit; Re-Aktivierung braucht zusätzlich die Return-Wert-Owner-Exemption
  (§6.2) und ist der benannte nächste Schritt.

---

## Laufende Imperfektions-/Deferral-Liste
1. deep_copy „unfehlbar" angenommen; kein free-old-target bei Reassignment (P1b inc1).
2. Map-Drop-Registrierung weiter zurückgestellt bis occ/slot-Audit (map.put-UAF ist bereits als
   sicherer Leak gefixt via consumeStringTemp).
3. use-after-move zunächst Warning, nicht Fehler.
4. Server-e2e für ALLE Pakete ausstehend.
