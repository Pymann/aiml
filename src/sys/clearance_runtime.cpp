// spec/65 F65-37 — `sys.export_symbol(name, fn, level)`: der Wirt bietet ein eigenes
// Symbol an.  EIN Aufruf fuer beide Haelften der Uebergabe (F65-24):
//
//   Adresse  -> `DynamicLibrary::AddSymbol`, damit der JIT den Namen aufloest
//   Name+Level -> die Export-Tabelle (§5a), aus der die Zugriffspruefung liest
//
// Zwei getrennte Aufrufe waeren zwei Listen, die driften — genau das verbietet F65-23.
//
// Die Adresse kommt aus einem Funktionszeiger auf den eigenen `def` (spec/12_1), nicht
// aus `dlsym`: so braucht es kein `-rdynamic`, und bei einem statischen Build geht es
// ueberhaupt nur so (gemessen: 0 von 38 aiml-Funktionen stehen in `.dynsym`).
//
// In einer eingeschraenkten Sitzung ist der Aufruf nicht erreichbar — `sys` ist als
// Namensraum zu (F65-35), der Waechter aus §5 lehnt ihn mit `clearance-builtin` ab.  Ein
// Skript, das sich selbst exportieren koennte, setzte sein eigenes Level.
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
        LOG_ERROR("sys.export_symbol: Nullzeiger fuer '" + std::string(name) +
                  "' — ein Funktionszeiger auf den eigenen `def` wird erwartet "
                  "(spec/65 F65-37, spec/12_1)");
        return -1;
    }
    if (level < 0 || level > aiml::clearance::kMax) {
        LOG_ERROR("invalid-level: `sys.export_symbol` fuer '" + std::string(name) +
                  "' mit Level " + std::to_string(level) +
                  " — gueltig ist 0..255 (spec/65 F65-4)");
        return -1;
    }
    llvm::sys::DynamicLibrary::AddSymbol(name, fn);
    aiml::clearance::registerExport(name, level);
    LOG_DEBUG("sys.export_symbol: " + std::string(name) + " -> Level " +
              std::to_string(level));
    return 0;
}

// spec/65 F65-32 — die drei Leser.  Nur lesen: ein Setzer waere Selbstbeglaubigung.
extern "C" int aiml_clearance_level()         { return aiml::clearance::sessionClearance(); }
extern "C" int aiml_clearance_create_level()  { return aiml::clearance::createLevel(); }
extern "C" int aiml_clearance_replace_level() { return aiml::clearance::replaceLevel(); }
