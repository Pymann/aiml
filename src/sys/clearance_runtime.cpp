// spec/65 F65-37 — `sys.export_symbol(name, fn, level)`: the host offers a symbol of its own.
// ONE call for both halves of the handover (F65-24):
//
//   address    -> `DynamicLibrary::AddSymbol`, so that the JIT resolves the name
//   name+level -> the export table (§5a), which the access check reads from
//
// Two separate calls would be two lists that drift — exactly what F65-23 forbids.
//
// The address comes from a function pointer to the host's own `def` (spec/12_1), not
// from `dlsym`: that way no `-rdynamic` is needed, and with a static build it is
// the only way at all (measured: 0 of 38 aiml functions are in `.dynsym`).
//
// In a restricted session the call is not reachable — `sys` is closed as a
// namespace (F65-35), the guard from §5 rejects it with `clearance-builtin`.  A
// script that could export itself would set its own level.
#include <string>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/DynamicLibrary.h>
#include "../core/clearance.h"
#include "../core/clearance_exports.h"
#include "../../logger.h"

extern "C" int aiml_clearance_export(const char* name, void* fn, int level) {
    if (!name || !*name) {
        LOG_ERROR("sys.export_symbol: leerer Name (spec/65 F65-37)");
        return -1;
    }
    if (!fn) {
        LOG_ERROR("sys.export_symbol: null pointer for '" + std::string(name) +
                  "' — a function pointer to the host's own `def` is expected "
                  "(spec/65 F65-37, spec/12_1)");
        return -1;
    }
    if (level < 0 || level > aiml::clearance::kMax) {
        LOG_ERROR("invalid-level: `sys.export_symbol` for '" + std::string(name) +
                  "' with level " + std::to_string(level) +
                  " — valid is 0..255 (spec/65 F65-4)");
        return -1;
    }
    llvm::sys::DynamicLibrary::AddSymbol(name, fn);
    aiml::clearance::registerExport(name, level);
    LOG_DEBUG("sys.export_symbol: " + std::string(name) + " -> Level " +
              std::to_string(level));
    return 0;
}

// spec/65 F65-32 — the three readers.  Read only: a setter would be self-certification.
extern "C" int aiml_clearance_level()         { return aiml::clearance::sessionClearance(); }
extern "C" int aiml_clearance_create_level()  { return aiml::clearance::createLevel(); }
extern "C" int aiml_clearance_replace_level() { return aiml::clearance::replaceLevel(); }
