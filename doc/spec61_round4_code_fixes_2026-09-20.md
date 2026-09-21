# Spec 61 – konkrete C++-Vorschläge für Review-Runde 4

Stand: 2026-09-20. **Implementierungsvorschlag, noch kein angewendeter oder kompilierter Patch.** Die Ausschnitte zeigen die einzubauenden Änderungen; neue Hilfstypen und Funktionen sind entsprechend gekennzeichnet.

## Nachgewiesene Fehler und Abnahme

| Fehler | Reproduktion | Erwartung nach dem Fix |
|---|---|---|
| Interner schwacher Verweis zeigt nach Clone ins Original | [Probe 33](../tests/lifetime/33_round4_clone_internal_weak.aiml) | `duplicate.view == duplicate.owned`, Original bleibt unabhängig |
| Tensor-Feld wird nur als Pointer kopiert | [Probe 34](../tests/lifetime/34_round4_clone_tensor_member.aiml) | Mutation des Clone-Tensors lässt Originalwert bei 1 |
| Move innerhalb eines Initialisierers wird nicht verfolgt | [Negativprobe n12](../tests/lifetime/neg/n12_round4_move_in_initializer.aiml) | Compile-Fehler `use-after-move` |

Letzter vollständiger Lauf: 45 erfolgreiche Prüfungen, diese drei Fehlschläge. Getestetes Binary: 20.09.2026, 18:07 Uhr. Vor der Umsetzung Build und parallele Änderungen erneut abgleichen. Nur die betroffenen Pfade ändern; die vorhandenen Regressionstests nicht abschwächen.

## 1. Gemeinsame Grundlage: eigene Beschreibungen für Clone-Semantik

Betroffen: [ast_compiler.cpp](../src/core/ast_compiler.cpp), `serBuildSubtree`, `serFieldSubtree`, Clone-Lowering; [ser_runtime.h](../src/sys/ser_runtime.h) und [ser_runtime.cpp](../src/sys/ser_runtime.cpp).

Der aktuelle Descriptor-Builder behandelt schwache Referenzen und Tensor-Handles als `AIML_TD_RAWBYTES`. Der Clone-Walker kann dadurch weder Referenzen korrigieren noch Tensor-Daten klonen. Die gleichen Builder werden außerdem für Serialisierung und Freigabe benutzt. Deshalb die neue Semantik nicht ungeprüft in die vorhandenen Wire-Deskriptoren übernehmen.

### Vorschlag für die Builder-Schnittstelle

```cpp
// NEU: intern im Compiler; kein AIML-Sprachmerkmal.
enum class DescriptorPurpose { Existing, Clone };

static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl,
    DescriptorPurpose purpose = DescriptorPurpose::Existing);
```

`purpose` auch durch `serFieldSubtree` und sämtliche rekursiven Aufrufe reichen. Bestehende Aufrufer bleiben zunächst bei `Existing`; **alle** memV3-Clone-Einstiege, einschließlich Listen-/Map-Clone und normalem Modell-Clone, wählen `Clone`.

Neue Blattarten in `AimlTDKind` ergänzen, ohne bestehende Nummern zu ändern:

```cpp
// Vorschlag: aktuell endet die Enumeration bei AIML_TD_PTR_REF = 15.
// Nummern vor dem Einbau mit dem aktuellen Header abgleichen.
AIML_TD_WEAK_REF = 16, // Pointer-Slot; weder owning noch rekursiv zu verfolgen
AIML_TD_TENSOR   = 17, // owned tensor-Handle; Clone über aiml_tensor_clone
```

Im Builder **vor** Pointer-/Builtin-/RAWBYTES-Fallback:

```cpp
if (ctx.memV3 && purpose == DescriptorPurpose::Clone) {
    if (typeName.rfind("ref<", 0) == 0 && typeName.back() == '>') {
        return {{AIML_TD_WEAK_REF, 0, 0, offset, 0, 0}};
    }
    if (typeName == "tensor") {
        return {{AIML_TD_TENSOR, 0, 0, offset, 0, 0}};
    }
}
```

**Notwendige Integration:**

- Descriptor-Caches und LLVM-Globalnamen um den Verwendungszweck unterscheiden. Dieselbe Modellbezeichnung darf nicht versehentlich den Serialisierungsdescriptor für einen Clone liefern.
- Rekursive `AIML_TD_PTR_REF`-Auflösungen müssen die **Clone-Variante** des referenzierten Typs erreichen. Dazu einen getrennten Clone-Registry-Pfad oder eine um den Zweck erweiterte Registry-Abfrage vorsehen. Nur den Root-Descriptor umzuschalten reicht nicht.
- Die neuen Arten sind Blätter; Funktionen zum Verschieben von Child-Indizes dürfen deren Parameter nicht als Subtree-Indizes behandeln.
- Clone-Deskriptoren dürfen nicht versehentlich in `enc`/`dec` landen. Dort unbekannte Arten explizit diagnostizieren; keine stillschweigende Auslassung von Feldern. Existing-/OFF-Deskriptoren und Wire-Format bleiben erhalten.
- Normale Scope-Drop-Deskriptoren sind ein eigener Vertrag. Neue Clone-Deskriptoren nicht ungeprüft als Ersatz verwenden. Falls sie für die Freigabe teilweise erzeugter Clones dienen, müssen Weak-Ref ein No-op und Tensor ein passender Resource-Drop sein.

## 2. Interne schwache Referenzen nach dem Graph-Walk korrigieren

### Ursache

`serBuildSubtree` kopiert unter memV3 alle `ref<T>` als rohe Pointer. Das ist nur für **externe** schwache Ziele korrekt. Für interne Ziele verlangt §5.1 eine Umleitung zum entsprechenden Clone. Ein sofortiger Memo-Lookup reicht nicht: Das besessene Ziel kann im Feldlayout erst nach dem schwachen Verweis auftauchen.

### Konkreter Runtime-Vorschlag

Die bestehende Eigentümer-Memoisierung erhalten, aber um eine getrennte Adresszuordnung und ausstehende Verweiskorrekturen erweitern:

```cpp
// NEU in ser_runtime.cpp
struct WeakCloneFixup {
    uint8_t* destinationSlot;
    const void* originalTarget;
};

struct CloneContext {
    // Ersatz für die bisherige g_clone_memo-Tabelle:
    std::unordered_map<const void*, void*> ownedMemo;

    // Zieladressen für schwache Referenzen, auch auf inline gespeicherte Werte.
    std::unordered_map<const void*, void*> relocated;
    std::vector<WeakCloneFixup> weakFixups;
};

static thread_local CloneContext* g_clone_ctx = nullptr;

static void finishWeakCloneRefs(CloneContext& context) {
    for (const auto& fixup : context.weakFixups) {
        const auto found = context.relocated.find(fixup.originalTarget);
        const void* target = found != context.relocated.end()
            ? found->second : fixup.originalTarget;
        // Kein incref: das Ergebnis bleibt eine schwache Referenz.
        std::memcpy(fixup.destinationSlot, &target, sizeof(target));
    }
}
```

Neuer `clw`-Zweig:

```cpp
case AIML_TD_WEAK_REF: {
    const void* target = nullptr;
    std::memcpy(&target, src, sizeof(target));
    std::memcpy(dst, &target, sizeof(target));
    if (target) g_clone_ctx->weakFixups.push_back({dst, target});
    break;
}
```

An jedem neu geklonten besessenen Heapobjekt die Zuordnung **vor der Rekursion** registrieren:

```cpp
g_clone_ctx->ownedMemo.emplace(originalObject, clonedObject);
g_clone_ctx->relocated.emplace(originalObject, clonedObject);
// Erst danach dessen Inhalte mit clw(...) bearbeiten.
```

Dies betrifft `MODEL`, `PTR_STRUCT`, `PTR_REF` und Container-Handles. Die vorhandenen Regeln für Unique-Owner und Shared-Refcounts erhalten; insbesondere bei einem wiedergefundenen Unique-Owner nicht einfach einen zweiten besitzenden Pointer erzeugen.

Zusätzlich Adressen kopierter inline Werte/Felder/Elemente in `relocated` erfassen. Dafür nur tatsächliche gültige Referent-Adressen aufnehmen, nicht pauschal beliebige Pointer-Slots als besessene Objekte interpretieren. Ein Borrow auf einen Wert oder ein Feld muss ebenfalls auf dessen neues Gegenstück zeigen können. `relocated` bleibt deshalb von `ownedMemo` getrennt.

Nach Abschluss des gesamten Walks genau einmal `finishWeakCloneRefs(context)` ausführen. Verschachtelte Runtime-Aufrufe müssen denselben Kontext nutzen oder den vorherigen Thread-local-Kontext per RAII wiederherstellen; das bisherige einfache Setzen auf `nullptr` genügt für Reentranz nicht.

### Wichtiger Compiler-Anschluss: endgültige Zieladresse

Für Probe 33 liegt das Ziel im Heap und bleibt stabil. Allgemein können schwache Verweise aber auf die geklonte **Wurzel als Wert** oder deren inline Felder zeigen. Der Clone darf sie dann nicht auf ein temporäres Alloca binden, dessen Inhalt danach an einen anderen Ort kopiert wird.

Für Deklarations-/Zuweisungsziele bevorzugt unmittelbar in den endgültigen Zielslot klonen. Bei temporären Rückgabewerten entweder stabile Ergebnisablage bis zum Ownership-Handoff verwenden oder eine explizite Relokation mit erneuter Verweiskorrektur vorsehen. Den aktuellen abschließenden Load/Store eines Modellwerts im Clone-Lowering darauf prüfen. Die Runtime allein kann diesen Adresswechsel nicht reparieren.

### Tests

- Probe 33 wird grün; Probe 28 für externe Referenzen bleibt grün.
- Zusätzlicher Fall mit schwachem Feld **vor** dem besitzenden Feld, damit die zweite Phase wirklich benötigt wird.
- Schwacher Verweis auf Clone-Wurzel und inline Feld; kein Verweis auf temporäre Compilerablage.
- Null-Ref bleibt null; schwache Kanten erhöhen keinen Refcount und werden beim Drop nicht verfolgt.

## 3. Tensor-Felder über den Tensor-Clone kopieren

### Ursache

Der RAWBYTES-Fallback kopiert die Tensoradresse. Ein `tensor.fill` auf dem Clone verändert dadurch die Originaldaten. Für einen Tensor existiert bereits die passende Runtime-Funktion: `void* aiml_tensor_clone(void*)` in [tensor_runtime.h](../src/sys/tensor_runtime.h).

### Konkreter Runtime-Vorschlag

Header im Clone-Runtime-Modul einbinden und den neuen Blattknoten behandeln:

```cpp
#include "tensor_runtime.h"

// In clw(...):
case AIML_TD_TENSOR: {
    void* originalTensor = nullptr;
    std::memcpy(&originalTensor, src, sizeof(originalTensor));
    void* clonedTensor = originalTensor
        ? aiml_tensor_clone(originalTensor) : nullptr;
    // Bei einem nicht-null Original darf ein fehlgeschlagener Clone nicht
    // stillschweigend als erfolgreicher Null-Wert weitergereicht werden.
    // Hier an den festgelegten Runtime-Allokationsfehlerpfad anschließen.
    std::memcpy(dst, &clonedTensor, sizeof(clonedTensor));
    if (originalTensor && clonedTensor) {
        g_clone_ctx->relocated.emplace(originalTensor, clonedTensor);
    }
    break;
}
```

Dies ist ein owned Resource-Clone, kein Shared-Incref. Nicht denselben neuen Tensorhandle an mehrere Unique-Owner verteilen. Ein `ref<tensor>` bleibt ein `WEAK_REF` und wird nicht auf diesem Pfad dupliziert. Normale Modell-/Container-Rekursion muss den Tensor-Knoten erreichen; keine Sonderbehandlung ausschließlich für Top-level-`clone(tensor)` einbauen.

Die vorhandene Tensor-Runtime definiert, wie Daten, Views und Metadaten geklont werden. Diese Logik wiederverwenden statt Tensor-Layouts im Serializer nachzubauen. Bei fehlschlagenden Clones müssen bereits erzeugte Resources entsprechend dem festgelegten Fehlervertrag freigegeben werden, falls die Runtime den Prozess nicht beendet.

**Tests:** Probe 34 wird grün; Probe 14 bleibt grün. Zusätzlich Null-Tensorfeld und Tensor in verschachteltem Modell prüfen. Scope-Drop der Clones unter ASan auf doppelte Freigaben/UAF prüfen; die automatische Resource-Freigabe darf nicht durch manuelle Freigaben im Test verdeckt werden.

## 4. Ownership-Effekte aller Call-Ausdrücke auswerten

Betroffen: [borrow_check.cpp](../src/core/borrow_check.cpp), `scanUses`, `exprStmt`, `stmt(DeclStmt)`.

### Ursache

`stmt(DeclStmt)` ruft `scanUses` auf. Dessen `CallExpr`-Zweig prüft lediglich die Argumente auf Reads. Die tatsächliche Move-Logik liegt nur im separaten `exprStmt`-Pfad. Daher unterscheiden sich semantisch identische Calls danach, ob ihr Ergebnis gespeichert wird.

### Konkrete Extraktion der Call-Effekte

Die bereits vorhandenen Transferregeln in einen gemeinsamen Helper verschieben. Beispiel für den Teil der freien Funktionsaufrufe:

```cpp
// NEU: Methode von BorrowChecker.
void applyFunctionCallMoves(const CallExpr& call, int line) {
    const auto fit = ctx.functions.find(call.functionName);
    if (fit == ctx.functions.end()) return;

    const auto& types = fit->second.argTypeNames;
    const auto& lifetimes = fit->second.argLifetimes;
    for (size_t j = 0; j < call.args.size(); ++j) {
        const bool ptrParam = j < types.size()
            && types[j].rfind("ptr<", 0) == 0;
        const bool consumed = j < lifetimes.size()
            && lifetimes[j] == "consumed";
        if (!ptrParam && !consumed) continue;
        const std::string source = asPtrVar(call.args[j].get());
        if (!source.empty()) markMoved(source, line);
    }
}
```

Der bestehende `CallExpr`-Zweig von `scanUses` wird zu:

```cpp
if (auto* call = dynamic_cast<const CallExpr*>(e)) {
    for (const auto& argument : call->args) {
        scanUses(argument.get(), line);
    }
    applyFunctionCallMoves(*call, line);
    return;
}
```

**Der Helper darf nicht zusätzlich im alten `exprStmt`-Pfad aufgerufen werden.** Andernfalls würde derselbe Call doppelt verschieben und fälschlich `use-after-move` melden. `exprStmt` daher so umbauen, dass jeder Call genau einmal die gemeinsame Ausdrucksauswertung durchläuft. Bestehende Sonderbehandlung von `free` erhalten; Container-Moves und Borrow-Invalidierung ebenfalls in eindeutig einmal angewendete Call-Effekte auslagern.

Der Name `scanUses` ist dann missverständlich; nach der funktionalen Korrektur in beispielsweise `analyzeExpr` umbenennen. Vorhandene Tests auf Move-Zustände dürfen nicht selbst noch einmal den gesamten Ausdruck auswerten.

### Integrationsgrenzen

- Die gezeigte Funktionsauflösung entspricht dem vorhandenen Code. Bei Methoden/Namensräumen dieselbe aufgelöste Signatur wie im Call-Lowering verwenden; keinen neuen Kurzname-Heuristikpfad einführen.
- Initialisierer, Zuweisungs-RHS, Rückgabeausdrücke und verschachtelte Argumente müssen durch den gemeinsamen Pfad laufen.
- Die Auswertungsreihenfolge muss dem Lowering entsprechen. Bei Kurzschlussoperatoren werden Effekte eines eventuell nicht ausgeführten Operanden mit den bestehenden Flow-Zuständen zusammengeführt; nicht blind beide Seiten als unbedingt ausgeführt behandeln.
- Der dargestellte Fix verfolgt vorhandene `ptr`-Locals. Nicht behaupten, damit seien alle Aggregate, Ressourcen oder komplexen LValues automatisch abgedeckt.

**Tests:** n12 wird korrekt abgelehnt; n11 bleibt korrekt abgelehnt. Zusätzlicher positiver Kontrollfall: derselbe Initialisierer-Aufruf ohne spätere Owner-Verwendung kompiliert und läuft. Weiterhin prüfen: `ref<T>`-Argument bleibt benutzbar, verschachtelter besitzender Call wird erfasst und ein Call wird nicht doppelt als Move verarbeitet.

## 5. Reihenfolge und Verifikation

1. Aktuellen Diff und Buildstand prüfen; parallele Änderungen erhalten.
2. Den unabhängig lösbaren Call-Checker-Fix umsetzen und gezielt testen.
3. Clone-Descriptor-Zweck und neue Blattarten einführen, einschließlich rekursiver Registry-Auflösung.
4. Tensor-Clone und zweiphasige Weak-Ref-Korrektur samt endgültiger Zielablage integrieren.
5. Compiler und Runtime frisch bauen; anschließend:

```bash
ONLY=round4 bash tests/lifetime/run_lifetime.sh
bash tests/lifetime/run_lifetime.sh
```

Danach die geänderten Speicherpfade mit einem frisch gebauten ASan-Binary prüfen. Den vorhandenen OFF-/Serialisierungsbetrieb gezielt kontrollieren, weil Typbeschreibungen gemeinsam verwendete Infrastruktur sind. Alle vorherigen Regressionstests müssen weiter bestehen.

**Abschlusskriterium:** Probes 33 und 34 bestehen, n12 scheitert mit der vorgesehenen Diagnose, die bisher erfolgreichen 45 Prüfungen bleiben erfolgreich, und die neuen Kontrollfälle decken Feldreihenfolge, Null-Werte sowie einmalige Call-Effekte ab.
