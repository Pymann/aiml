#pragma once
// hashmap<K:V> — Hashtabelle als opaker, referenzgezählter Laufzeit-Griff (spec/68_hashmap.md).
// Laufzeit: src/sys/hashmap_runtime.cpp.  Lebensdauer wie chan<T> (spec/61 Class 5):
// Binden aus einer benannten Quelle = retain, Drop/Überschreiben = release.
#include "context.h"
#include <llvm/IR/IRBuilder.h>
#include <string>
#include <vector>

namespace HashmapImpl {

inline bool isHashmapType(const std::string& t) {
    return t.size() > 9 && t.rfind("hashmap<", 0) == 0 && t.back() == '>';
}

// key: 0 = string, 1 = Ganzzahl/Enum.   val: 0 = Ganzzahl/bool/Enum, 1 = f32/f64, 2 = string.
// -1 = nicht unterstützt (Fehler wurde gemeldet).
struct Kinds {
    int key = -1;
    int val = -1;
    std::string keyName;
    std::string valName;
};

Kinds kinds(CompilerContext& ctx, const std::string& typeName, bool report = true);

// Neue leere Tabelle: aiml_hashmap_new(key_kind, val_kind) -> i8*.
llvm::Value* create(CompilerContext& ctx, const std::string& typeName);

// Neue Tabelle erzeugen und in `slot` (Adresse eines i8*-Platzes) speichern.
void initSlot(CompilerContext& ctx, llvm::Value* slot, const std::string& typeName);

// Methodenaufruf auf dem Griff in `slot`.  Rückgabe nullptr bei void.  `known` = false, wenn die
// Methode unbekannt ist (Fehler wurde gemeldet).
llvm::Value* method(CompilerContext& ctx, const std::string& typeName, llvm::Value* slot,
                    const std::string& methodName, const std::vector<llvm::Value*>& args, bool& known);

} // namespace HashmapImpl
