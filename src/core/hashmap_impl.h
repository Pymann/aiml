#pragma once
// hashmap<K:V> — hash table as an opaque, reference-counted runtime handle (spec/69_hashmap.md).
// Runtime: src/sys/hashmap_runtime.cpp.  Lifetime like chan<T> (spec/61 class 5):
// binding from a named source = retain, drop/overwrite = release.
#include "context.h"
#include <llvm/IR/IRBuilder.h>
#include <string>
#include <vector>

namespace HashmapImpl {

inline bool isHashmapType(const std::string& t) {
    return t.size() > 9 && t.rfind("hashmap<", 0) == 0 && t.back() == '>';
}

// key: 0 = string, 1 = integer/enum.   val: 0 = integer/bool/enum, 1 = f32/f64, 2 = string.
// -1 = not supported (the error has been reported).
struct Kinds {
    int key = -1;
    int val = -1;
    std::string keyName;
    std::string valName;
};

Kinds kinds(CompilerContext& ctx, const std::string& typeName, bool report = true);

// Neue leere Tabelle: aiml_hashmap_new(key_kind, val_kind) -> i8*.
llvm::Value* create(CompilerContext& ctx, const std::string& typeName);

// Create a new table and store it in `slot` (address of an i8* slot).
void initSlot(CompilerContext& ctx, llvm::Value* slot, const std::string& typeName);

// Method call on the handle in `slot`.  Returns nullptr for void.  `known` = false if the
// method is unknown (the error has been reported).
llvm::Value* method(CompilerContext& ctx, const std::string& typeName, llvm::Value* slot,
                    const std::string& methodName, const std::vector<llvm::Value*>& args, bool& known);

} // namespace HashmapImpl
