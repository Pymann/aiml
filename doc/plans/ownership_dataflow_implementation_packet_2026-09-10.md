# Implementierungspaket: §4 Ownership-Dataflow

Stand: 2026-09-10. Grundlage: `lifetime_hard_packages_progress_2026-09-09.md`, Kapitel 61 und direkte Lektüre des aktuellen Compilers. Kein Git verwendet. Dieses Dokument enthält vorgeschlagene C++-Änderungen; es ist kein bereits angewendeter Patch und kein bestandener Testlauf.

## 1. Auftrag und Abgrenzung

Implementiere zunächst einen intraprozeduralen Ownership-Vorlauf und binde dessen Ergebnis an die bereits vorhandenen normalen Scope-Drops. Nicht erneut gleichzeitig Return-Cleanup, Box-Drop oder vollständigen Map-Drop einschalten. Der zurückgenommene Return-Versuch bleibt bis zur Abnahme dieses Pakets zurückgenommen.

Adoptierte Entscheidungen aus dem Fortschrittsbericht bleiben sichtbar:

- Q1: Warning-first. Diagnose allein ist keine Sicherheitsmaßnahme; konservative Behandlung unsicherer neuer Drops gehört zwingend dazu.
- Q2: Der bestehende `void`-Deep-Copy-Vertrag bleibt für dieses Paket unverändert. Kein Free-old-target ergänzen, bevor Erfolg und Alias-Sicherheit gesondert geklärt sind.
- Q3: Kleine Schritte, jeweils Build und passende Prüfungen; keine Freigabe durch bloßes Bestehen der bisherigen Suite.

WP-C bleibt verworfen. Benötigt wird ein endlicher, funktionslokaler Datenfluss mit bekannten Call-Verträgen, keine Ganzprogramm-Escape-Analyse und keine Runtime-Drop-Flags.

## 2. Konkreter Ausgangsfehler

`tests/test_list_list_inner_values.aiml` enthält:

```aiml
- decl: [var_cb, list<i32>]
- set: [var_cb, { call: [var_ab.activation_cost.get, 0] }]
```

`var_cb` erhält die Listenhülle eines fremden, im Modell enthaltenen Buffers. Sein Typ bleibt `list<i32>`, er besitzt den Buffer dadurch aber nicht. Die bei `decl` registrierte List-Freigabe passt nach `set` nicht mehr zum Wert im Slot. Gleichzeitig kann der Modell-Drop von `var_ab` denselben Buffer rekursiv freigeben.

`tests/test_class_member_shared_tensor.aiml` enthält außerdem `set: [<.inner, an_inner]` aus einem Modellparameter. Das ist ein anderes Problem: gespeicherte flache Teilung über eine Funktionsgrenze. Ein rein lokaler Variablenstatus im Callee reicht dafür nicht. Ein offengelegter Retain-/Escape-Effekt muss an den Aufrufer gelangen; andernfalls darf der neue rekursive Drop nicht als sicher gelten. Tensor-Handles nicht pauschal klonen oder freigeben.

Die berichteten Exitcodes 134/139 stammen aus dem Fortschrittsbericht. In dieser Planerstellung wurden sie nicht erneut reproduziert.

## 3. Code-Landkarte

Zeilen sind Anker des gelesenen Standes, keine stabilen Patch-Offsets.

| Datei / Stelle | Änderung |
|---|---|
| `src/core/context.h:47`, `Symbol` | stabile `ownershipId` ergänzen, Qualifier nicht als Besitzbeweis verwenden |
| `src/core/context.h:279`, `ScopeDrop` | Besitzer-ID ergänzen; Cleanup bleibt Drop-Rezept, nicht Zustandsdatenbank |
| `src/core/context.h:292–299` | aktiven Ownership-Plan neben Cleanup-/Parameter-Frames halten |
| `src/core/ast_compiler.h:64` | `consumeLocalDrop` ersetzen/überführen; neue Plan-Schnittstellen deklarieren |
| `src/core/ast_compiler.cpp:4354`, `visitFunction` | Analyse nach Signaturauflösung und vor Body-IR einbauen; Kontext sichern/wiederherstellen |
| `src/core/ast_compiler_stmt.cpp:314–377` | Decl-Drop-Kandidaten an analysierte Deklarations-ID binden |
| `:813–945`, `visitSetStmt` | Default-Move bei `:910` entfernen; pro Statement Copy/Move/View-Plan anwenden |
| `:1473`, `consumeLocalDrop` | kein Löschen aus allen Frames mehr; Ownership-Plan bestimmt Sink-Transfer |
| `:1495`, `emitScopeDrop` | Primitive beibehalten; Aufrufer prüft Freigaberecht am konkreten Ausgang |
| `:1527–1554`, `visitBlockStmt` | Drop-Zulässigkeit aus Block-Exit-Fakten statt Decl-Typ ableiten |
| `:1556`, `visitIfStmt`; `:1601`, `visitWhileStmt` | Datenfluss im Vorlauf; nicht erst beim Emittieren Branches entdecken |
| `src/core/ast_compiler_call.cpp` | aufgelöste Call-/Container-Verträge, Argumenttransfer und Retain-Effekte liefern |
| `src/core/ast_compiler_expr.cpp:15ff.` | Expression-Herkunft beim Lowering gegen Vorlauf prüfen |
| neue `src/core/ownership_flow.h/.cpp` | reine Zustands-/Merge-/Analyse-Logik, unabhängig von LLVM-Emission |

`exodus.json:821–825` enthält bereits `src/**/*.cpp`. Vor einer Build-Konfigurationsänderung prüfen, ob die neuen Dateien dadurch automatisch erfasst werden.

## 4. Inkrement A: Datenmodell und reine Merge-Funktionen

### 4.1 Neue Datei `src/core/ownership_flow.h`

Folgender C++-Kern ist als konkrete Vorlage gedacht. Weitere Adapter/Visitor werden in den nachfolgenden Abschnitten ausdrücklich beschrieben; dieser Ausschnitt allein implementiert noch keinen vollständigen Analyzer.

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace AIML {
class Expr;
class Stmt;
class DeclStmt;
class BlockStmt;

using OwnershipId = std::uint32_t;
using ResourceId = std::uint32_t;
constexpr OwnershipId NoOwnership = 0;

enum class Origin : std::uint8_t {
    None, Owned, Borrowed, SharedView, Static, Unknown
};
enum LifeBits : std::uint8_t {
    Uninit = 1, Live = 2, Moved = 4
};
struct OwnFact {
    Origin origin = Origin::Unknown;
    std::uint8_t life = Uninit;
    std::set<ResourceId> resources;
};
struct FlowState {
    bool reachable = true;
    std::map<OwnershipId, OwnFact> slots;
};
enum class Transfer : std::uint8_t {
    None, SelfAssign, DeepCopy, Move, BindView, LegacyUnknown
};
struct StatementPlan {
    Transfer transfer = Transfer::None;
    OwnershipId source = NoOwnership;
    OwnershipId target = NoOwnership;
    OwnFact result;
};
struct OwnershipPlan {
    std::map<const DeclStmt*, OwnershipId> declarations;
    std::map<const Stmt*, StatementPlan> statements;
    std::map<const Expr*, OwnFact> expressions;
    std::map<const BlockStmt*, FlowState> normalExits;
    std::map<const Stmt*, FlowState> earlyExits;
    std::set<ResourceId> suppressNewDrops;
    // Unaufgelöste Alias-/Retain-Effekte: keine neuen Drops dieser Funktion.
    bool suppressFunctionDrops = false;
};

OwnFact joinFact(const OwnFact& a, const OwnFact& b);
FlowState joinStates(const FlowState& a, const FlowState& b);
bool sameState(const FlowState& a, const FlowState& b);
}
```

`SharedView` bedeutet hier Legacy-Teilung ohne eigenen Freigabeanspruch, NICHT Reference Counting und NICHT `shared<T>` als neues Sprachfeature. Owner-Qualifikation und Pfad-Lebendigkeit sind orthogonal: ein einzelnes Enum `owned/borrowed/moved` würde an Merges Information verlieren.

### 4.2 Neue Datei `ownership_flow.cpp`: ausführbarer Merge-Kern

```cpp
#include "ownership_flow.h"

namespace AIML {
OwnFact joinFact(const OwnFact& a, const OwnFact& b) {
    OwnFact out;
    out.origin = a.origin == b.origin ? a.origin : Origin::Unknown;
    out.life = static_cast<std::uint8_t>(a.life | b.life);
    out.resources = a.resources;
    out.resources.insert(b.resources.begin(), b.resources.end());
    return out;
}

FlowState joinStates(const FlowState& a, const FlowState& b) {
    if (!a.reachable) return b;
    if (!b.reachable) return a;
    FlowState out = a;
    for (const auto& [id, rhs] : b.slots) {
        auto it = out.slots.find(id);
        if (it == out.slots.end())
            out.slots.emplace(id, joinFact(OwnFact{}, rhs));
        else
            it->second = joinFact(it->second, rhs);
    }
    for (const auto& [id, lhs] : a.slots) {
        if (b.slots.find(id) == b.slots.end())
            out.slots[id] = joinFact(lhs, OwnFact{});
    }
    return out;
}

bool sameState(const FlowState& a, const FlowState& b) {
    if (a.reachable != b.reachable || a.slots.size() != b.slots.size())
        return false;
    for (const auto& [id, x] : a.slots) {
        auto it = b.slots.find(id);
        if (it == b.slots.end()) return false;
        const OwnFact& y = it->second;
        if (x.origin != y.origin || x.life != y.life ||
            x.resources != y.resources) return false;
    }
    return true;
}
}
```

Vor Block-Merges blocklokale Symbole aus dem Zustand entfernen. Fehlende äußere Zustände werden oben bewusst konservativ behandelt; dieser Fall sollte nach korrekt aufgebauten Lexikalscopes selten sein.

### 4.3 Stabile Identitäten und Ressourcen

IDs einmal vor der Fixpunktanalyse vergeben: pro Deklaration und Parameter eine `OwnershipId`, pro Allocation-/Copy-Stelle eine `ResourceId`. Im Schleifenlauf niemals neue IDs erzeugen, sonst terminiert die Analyse nicht. Shadowing erhält andere IDs; Namen sind ausschließlich für Lexikalauflösung und Diagnosen.

Für einen Modell-Drop muss bekannt sein, welche Ressourcen er rekursiv freigibt. Eine Liste als Modellfeld gehört deshalb zur Ressourcenmenge des Modell-Owners. Beim Binden eines Views diese Ressource mitgeben. Beim Einbau eines fremden Feldes aktualisiert sich auch die transitive Ressourcenmenge des Zielmodells.

Erste konservative Darstellung: Ressourcenverband pro Modell/Container einschließlich seiner Heap-Felder. Verschiedene dynamische Indizes dürfen zunächst zusammenfallen. Das erzeugt zusätzliche Unterdrückungen/Lecks, aber darf kein Alias übersehen. Nicht einfach jedem Feldzugriff eine unabhängige neue Allocation-ID geben.

## 5. Inkrement B: Herkunft aller relevanten Ausdrücke

### 5.1 Analyzer-Adapter passend zum vorhandenen AST

Im neuen Modul einen `OwnershipAnalyzer` mit `analyzeBlock`, `analyzeStatement`, `classifyExpr` und einer lexikalen Symbolumgebung implementieren. Tatsächliche AST-Felder:

- `VariableExpr::name`;
- `CallExpr::functionName`, `args`;
- `AtExpr::container`, `indices`;
- `NewExpr::typeName`, `initValues`;
- `StringInterpolationExpr::parts`;
- `SetStmt::target`, `value`;
- `IfStmt::condition`, `thenBranch`, `elseBranch`;
- `WhileStmt::condition`, `body`;
- `BlockStmt::statements`, `RetStmt::value`.

`classifyExpr` erhält aufgelöste Typ-/Call-Metadaten. Keine Klassifikation allein nach dem Text `.get`: Tensor-Get, Listen-Get und KV-Get besitzen unterschiedliche Rückgaben. Bestehende Auflösungslogik in einen gemeinsam genutzten Helfer auslagern, statt im Analyzer einen zweiten, abweichenden Dispatch zu erfinden. Bei noch nicht aufgelösten Overloads/Imports konservativ `Unknown` liefern.

### 5.2 Verbindliche Herkunftstabelle

| Ausdruck | Fact / Lowering-Vertrag |
|---|---|
| Skalar | `None`, kein Heap-Drop |
| Stringliteral | `Static`, niemals Header-Decref allein wegen Typ `string` |
| frischer Container | `Owned`, Allocation-Site-Ressource |
| Variable | Fact des Slots, keine neue Ressource |
| Modellfeld / Container-View | `Borrowed` oder `SharedView`, Ressourcen des wirklichen Owners |
| `at` / aufgelöstes Listen-Get mit Heap-Wert | View; eine flache Struct-Hülle macht daraus keinen Owner |
| `kv.get` | Borrow auf externen Cache-Owner, nicht scope-owned |
| `kv.get_copy` als Runtime-Pointer | ebenfalls cachegebunden; erst die tatsächlich erzeugte lokale Deep-Copy ist owned |
| `kv.get_copy_at` | Statuswert; Zielinhalt wird bei Erfolg owned, bei Fehler gemäß Runtime-Vertrag leer; missing erhält vorherigen Zustand |
| `new` | bekannte Box-Herkunft, Box-Drop dennoch in diesem Paket aus |
| Interpolation | frisch nur bei tatsächlicher Allocation; Ein-String-Durchleitung bleibt View/Static |
| gewöhnlicher Parameter | Borrow, auch bei Value-Modell oder by-ref-Liste |
| consumed-Parameter | owned entsprechend vorhandenem Signaturvertrag; Root und Drop-Art getrennt |
| unbekannte Call-Rückgabe / externes Handle | `Unknown`; keine generische rekursive Freigabe behaupten |

Für `get_copy_at` entweder seinen Rückgabewert pfadbezogen auswerten oder die Zustandsalternativen zusammenführen. Ist das Ziel vorher borrowed und missing möglich, ist das Ergebnis nicht ohne Weiteres Owned.

## 6. Inkrement C: Zuweisungen ohne pauschalen Move

### 6.1 `visitSetStmt` umbauen

Den Fallback `if (setStmt->value) consumeLocalDrop(...)` bei `:910` entfernen. Den Transfer aus `OwnershipPlan::statements.at(setStmt)` lesen. Ergebniszustand erst nach tatsächlich erfolgreichem Lowering installieren; ein Diagnose-/Fehlerpfad darf keinen Move verbuchen.

Konkrete Struktur als Einfügevorlage, Helfernamen sind neu zu implementierende Adapter:

```cpp
const StatementPlan& op = ctx.ownershipPlan->statements.at(setStmt);
if (op.transfer == Transfer::SelfAssign) return;

// Vorhandene Typ-, Const- und Void-Prüfung VOR jeder Mutation beibehalten.
IRValue* rhs = visitExpression(setStmt->value.get());
if (!rhs) return;

switch (op.transfer) {
case Transfer::DeepCopy:
    // Dispatch auf unterstützte Copy-Primitive; einmalige RHS-Auswertung.
    emitPlannedCopy(setStmt, rhs, op);
    break;
case Transfer::Move:
    emitPlannedMove(setStmt, rhs, op);
    break;
case Transfer::BindView:
case Transfer::LegacyUnknown:
    emitLegacyAssignment(setStmt, rhs);
    break;
case Transfer::None:
    emitLegacyAssignment(setStmt, rhs);
    break;
case Transfer::SelfAssign:
    break;
}
```

`emitLegacyAssignment` entsteht durch Extraktion des bisherigen unteren Zuweisungsteils. Es darf RHS nicht erneut auswerten. `emitPlannedMove` entfernt passende String-Temporaries genau wie die bereits korrigierten Call-Sinks; es löscht keine globalen Cleanup-Rezepte. Das Freigaberecht folgt dem Exit-Plan.

### 6.2 Transferregeln des ersten Pakets

- Selbstzuweisung bleibt No-op nach erforderlicher Ziel-/Typprüfung.
- Unterstützte Model-local→local-Kopie bleibt Deep-Copy. Vorherigen Ziel-Heap wie bisher noch nicht freigeben; Leck explizit dokumentieren.
- Flaches Binden eines inneren Containers wird als `BindView` markiert. Neuer Slot darf dessen Buffer nicht freigeben. Eine eventuell zuvor angelegte leere Hülle ist eine andere Allocation; deren Verlust als offenes Replace-Leck erfassen.
- String/List/Map-local→local nur dann als unabhängiges Owned-Ergebnis markieren, wenn das Lowering wirklich unabhängig kopiert. Bis zum jeweiligen Copy-Primitive `SharedView`/Legacy, mit Aliasbeziehung und konservativem Drop-Verbot.
- Member-/Container-Sink ist nicht allein wegen eines Punkts im Namen ein gültiger Move. Gewöhnliche Parameter und Views können keinen Besitz übertragen. Nicht kopierbare Ressourcen nicht über Serializer „klonen“.
- Reinitialisierung eines Moved-Slots setzt ihn auf Live mit neuer Herkunft. Alte Ressourcen nicht erneut freigeben.

Ziel dieses Pakets ist ein korrektes Abbild tatsächlicher Besitzverhältnisse. Eine vollständige Umstellung jeder Legacy-Operation auf die neue Copy-Semantik ist ein separates Increment und muss pro Typ nachgewiesen werden.

## 7. Inkrement D: Branches, Schleifen und Warning-first

### 7.1 Analyse vor IR, nicht rückwirkend

Die vollständige Funktion vor ihrer Body-IR-Ausgabe analysieren. Andernfalls ist ein Drop im THEN-Block bereits emittiert, bevor im ELSE-Block ein Alias entdeckt wird. Die spätere Löschung eines Cleanup-Eintrags repariert dieses IR nicht.

`analyzeIf` als C++-Vorlage:

```cpp
FlowState OwnershipAnalyzer::analyzeIf(const IfStmt& node, FlowState in) {
    analyzeCondition(*node.condition, in); // Calls in Bedingung können Effekte haben
    FlowState left = node.thenBranch
        ? analyzeBlock(*node.thenBranch, in) : in;
    FlowState right = node.elseBranch
        ? analyzeBlock(*node.elseBranch, in) : in;
    return joinStates(left, right);
}
```

Die Bedingung wird in der Analyse auf beiden Kanten konservativ berücksichtigt; branchfeine Refinements optional später. Ein Return speichert seinen Exit-Zustand und setzt `reachable=false`. Kein Merge mit `ret_dead` als scheinbar lebendem Vorgänger.

### 7.2 Schleifenfixpunkt

```cpp
FlowState head = entry;
for (;;) {
    FlowState bodyIn = head;
    analyzeCondition(*loop.condition, bodyIn);
    FlowState back = analyzeBlock(*loop.body, bodyIn);
    FlowState next = joinStates(entry, back);
    if (sameState(next, head)) break;
    head = std::move(next);
}
```

Dies ist der Kern, nicht die komplette Exit-Behandlung: Break-Ausgänge sammeln und dem Schleifenexit zuführen; Continue/Rückkante dem Header; Return/Throw nicht der Rückkante. Die Bedingung kann auch beim Verlassen Ressourcen verändern, daher den Exit aus dem stabilen Header nach Bedingungsanalyse bilden. Unbekannte Konstrukte dürfen nicht als effektfrei gelten.

Diagnosen erst nach Konvergenz ausgeben oder pro AST-Stelle deduplizieren. Plan-Fakten pro Stelle bei wiederholter Analyse zusammenführen, nicht nur die letzte Iteration behalten. Bei Implementierungsfehler/Iteration-Limit Analyse konservativ abbrechen und neue Funktions-Drops unterdrücken; nicht den zufälligen letzten Zustand freigeben.

### 7.3 Warning-first technisch absichern

`Live|Moved`, unbekannte Eigentümerschaft oder ein nach Move verwendeter Alias erzeugen eine Warnung und markieren den betroffenen Ressourcenverband in `suppressNewDrops`. Das muss ALLE neu eingeführten Freigaben dieses Verbands umfassen, auch rekursive Modell-/Container-Drops und temporäre Drops.

Falls der Verband über eine Funktionsgrenze nicht verlässlich verfolgt werden kann, genügt eine Warnung mit lokal unterdrücktem Quell-Drop NICHT: Ein Callee kann bereits freigeben. Solchen consumed-/Escape-Fall im neuen Ownership-Modus nicht destruktiv absenken. Konservativer Legacy-Modus ist nur zulässig, wenn seine bisherigen Operationen unverändert bleiben; ansonsten gezielte Ablehnung des nicht implementierten Transfers. Dies ist keine pauschale Umstellung der Use-after-move-Diagnose auf Error, sondern eine Grenze des sicheren neuen Lowerings.

Dieser Übergangsmodus vermeidet zusätzliche unbewiesene Drops. Er beweist nicht, dass beliebiger bereits unsicherer Legacy-Code sicher wird. Insbesondere explizites `free`, unbekannte Retain-Callbacks und mutierende externe Funktionen brauchen ihren eigenen Vertrag.

## 8. Funktionsverträge: notwendig für das Shared-Tensor-Muster

Neben Parameter-Qualifiern minimale Effekte in `FunctionInfo` ergänzen: Ergebnis frisch/borrowed/unknown; borrowed Ergebnis abhängig von Argument N; Argument retained/stored; Argument mutiert oder konsumiert. Keine Methodennamen-Heuristik als Ersatz.

Für `Body.setup(an_inner, ...)` muss die Speicherung in `<.inner` einen Retain-/Share-Effekt für `an_inner` liefern. Beim Aufrufer muss diese Ressource damit auch im neuen Zielmodell auftauchen oder konservativ von rekursiven Drops ausgenommen sein. Eine reine Callee-Warnung reicht nicht.

Erste Ausbaustufe darf unaufgelöste, rekursive oder externe Calls als `UnknownRetain` behandeln. Alle erreichbaren Heap-Ressourcen ihrer Argumente für neue Drops sperren; bei unbekannter Reichweite funktionsweiten Fallback verwenden. Keine vollständige interprozedurale Fixpunktinferenz erforderlich: explizite bzw. lokal ableitbare Signatur-Effekte und konservative Defaults genügen. Bekannte reine Borrow-Calls benötigen einen belegten Vertrag, damit der Fallback nicht jede Funktion entwertet.

## 9. Inkrement E: Codegen ausschließlich aus Exit-Fakten

### 9.1 `context.h`

`#include "ownership_flow.h"` ergänzen. In `Symbol` und am ENDE von `ScopeDrop` jeweils ergänzen (am Ende, damit bestehende Aggregate-Initialisierungen nicht verschoben werden):

```cpp
OwnershipId ownershipId = NoOwnership;
```

Im `CompilerContext`:

```cpp
const OwnershipPlan* ownershipPlan = nullptr; // geliehener Plan von visitFunction
```

Bei Parametern/Declarations die im Vorlauf vergebenen IDs setzen. Keine ID-Zuordnung über den textuellen Namen nach Verlassen des Lexikalscopes.

### 9.2 Gemeinsame Drop-Prüfung

In `ownership_flow.h/.cpp` ergänzen:

```cpp
bool mayDrop(const OwnershipPlan& plan, const FlowState& exit,
             OwnershipId id) {
    if (!exit.reachable || plan.suppressFunctionDrops || id == NoOwnership)
        return false;
    auto it = exit.slots.find(id);
    if (it == exit.slots.end()) return false;
    const OwnFact& f = it->second;
    if (f.origin != Origin::Owned || f.life != Live || f.resources.empty())
        return false;
    for (ResourceId r : f.resources)
        if (plan.suppressNewDrops.count(r)) return false;
    return true;
}
```

Vor jedem `emitScopeDrop` in `visitBlockStmt`:

```cpp
const OwnershipPlan* plan = ctx.ownershipPlan;
if (!plan) {
    // Nicht analysiert bedeutet nicht bewiesen owned.
    // Diagnose zentral einmal pro Funktion; keine neue Freigabe hier.
} else {
    auto ex = plan->normalExits.find(block);
    if (ex != plan->normalExits.end()) {
        for (auto it = frame.rbegin(); it != frame.rend(); ++it) {
            if (mayDrop(*plan, ex->second, it->ownershipId))
                emitScopeDrop(ctx, *it);
        }
    }
}
```

Wichtig: `normalExits[block]` speichert den Zustand VOR Entfernen der blocklokalen Slots, damit ihre Drops geprüft werden können. Der an den Elternblock zurückgegebene Zustand entfernt sie anschließend.

Freigabe-Rezepte müssen auf den aktuellen Slottyp passen. Zuweisung kann Owned zu View ändern, aber nicht unbemerkt die Destructor-Art eines Slots. Überlappende rekursive Owner-Ressourcen vor Emission als Konflikt erkennen; `mayDrop` alleine erkennt keine zwei gleichzeitig Owned markierten Aliase. Der Analyzer muss diesen Zustand sperren oder Ownership korrekt übertragen.

### 9.3 Funktionskontext sichern

`visitFunction` nach aufgelöster Signatur, vor `visitBlockStmt(func->body.get())`:

```cpp
OwnershipPlan plan = analyzer.analyze(*func); // neue Adapter-Schnittstelle
const OwnershipPlan* previous = ctx.ownershipPlan;
ctx.ownershipPlan = &plan;
// Body-Emission; in der tatsächlichen Implementierung RAII für alle Exits verwenden.
visitBlockStmt(func->body.get());
ctx.ownershipPlan = previous;
```

Auch `pendingParamDrops`, Cleanup-/Temporary-Frames und aktuelle Symbolbindung pro Funktion isolieren. Der lokale Plan muss bis zum Ende der Body-Emission leben. Ein RAII-Guard statt manuellem Restore auf nur einem Erfolgspfad ist Pflicht, besonders bei Diagnosen und verschachtelter Funktionsgenerierung.

### 9.4 Alte Konsum-Helfer

`consumeLocalDrop` darf nach Migration keinen globalen Frame mehr zerstören. Alle Aufrufer auf geplanten Transfer umstellen: Set, Push, Return, freie/direkte/Interface-Calls, Index-/Store-Pfade. Ein leerer Helfer ohne vollständige Callsite-Migration wäre ebenfalls falsch, weil dann gültige Moves doppelt gedroppt würden.

String-Temporary-Tracking bleibt getrennt, muss aber mit dem Ownership-Plan übereinstimmen. Die bereits korrigierten direkten consumed-Temporaries nicht wieder regressieren lassen.

## 10. Prüfmatrix und sinnvolle Commit-/Änderungseinheiten ohne Git-Befehle

| Increment | Implementierung | Abnahme |
|---|---|---|
| A | reine Facts, IDs, Join | Unit-Checks für erreichbare/tote Branches, gemischte LifeBits, Shadowing |
| B | AST-/Call-Herkunft, Ressourcenverbände | Trace klassifiziert `var_cb/gcb` als Views desselben Ownerverbands |
| C | Assignment-Pläne | Model-Copy bleibt unabhängig; lokale Listen-Views werden nicht freigegeben; unbekannte Fälle klar dokumentiert |
| D | If/While/Exit-Analyse, konservativer Modus | kein pfadglobales Entfernen; Warnungen dedupliziert; Fixpunkt terminiert |
| E | normale Drops planbasiert | beide berichteten Regressionen plus void-/innerer-Scope-Varianten speichersicher |
| F | Call-Retain und Container-Verknüpfungen | Shared-Tensor-Muster bewahrt Owner; consumed-Temporary-Proben weiter sauber |

Neue notwendige Proben unter `tests/_mem_sanitize/` vorschlagen:

- `ownership_inner_view_void.aiml`: reproduziert inneren Listen-View mit normalem Funktionsende; der Return darf die Gefahr nicht maskieren.
- `ownership_branch_view.aiml`: ein Branch bindet View, anderer besitzt eigene Liste; gemischter Merge darf keinen falschen Drop erzeugen.
- `ownership_branch_move_return.aiml`: THEN terminiert, ELSE läuft weiter; dessen Quelle bleibt korrekt owned.
- `ownership_loop_outer_move.aiml`: äußere Quelle in wiederholtem Body; Zero-/Multi-Iteration prüfen.
- `ownership_shadowing.aiml`: gleichnamige Slots besitzen verschiedene IDs.
- `ownership_retained_model_param.aiml`: Callee speichert geteiltes Modell; Owner ist beim Aufrufer bekannt.
- `ownership_reassign_view_to_owned.aiml`: erneute echte Kopie nach View; nur neuer eigener Heap ist droppbar.

Die existierenden `test_list_list_inner_values` und `test_class_member_shared_tensor` unverändert weiter prüfen. Letztere schreibt Tensor-Dateien mit relativen Pfaden: isoliertes Test-Arbeitsverzeichnis verwenden. Keine Probe gegen produktive 9rooms-WALs ausführen.

Harness muss fachlichen Erfolg UND instrumentierten vollständigen Lauf nachweisen; keine neuen UAF/Double-Frees, kein ungeprüfter Skip. Unterdrückte Drops samt Ressourcenursache separat zählen, damit „alles unterdrückt“ nicht als abgeschlossene Ownership-Implementierung gilt. Q2-Replace-Lecks bleiben einzeln gelistet.

## 11. Abnahmegrenze und nächstes Paket

Dieses Paket ist fertig, wenn normale Scope-Drops auf nachgewiesenem Besitz beruhen, Branches/Loops nicht mehr textuell-global behandelt werden, die beiden regressierten Muster einschließlich normaler Ausgänge abgesichert sind und unbekannte/retained Ressourcen nachvollziehbar konservativ behandelt werden. Ein Trace-Modus alleine erfüllt das nicht.

Danach P2a separat wieder aufnehmen: dessen Return-Exit-Fakten liegen bereits in `earlyExits`, aber Rückgabewert und Borrow-Rückgaben müssen vor den aktiven Drops abgesichert werden. Box-/Map-Drop bleiben eigene Folgepakete mit jeweils passenden Runtime-Verträgen.

Server-e2e ist Abnahme nach den lokalen Sicherheitsnachweisen, kein Ersatz dafür und keine Vorbedingung dafür, den Analyzer überhaupt zu implementieren. Mit isolierter Datenkopie Login, Token, Deckset, Match und Disconnect prüfen; Produktionsdaten nicht als Repro-Material verändern.
