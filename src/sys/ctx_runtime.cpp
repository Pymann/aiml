// spec/29_2 §14a — `ctx`: Griffe auf lebende Compiler-Instanzen.
//
// Die Tabelle haelt die Instanzen; der Griff ist ihr Zeiger.  Freigegeben
// wird ueber `aiml_ctx_free` (der Besitzer-Drop in aiml ruft es auf) — und
// nicht frueher: mit dem Compiler sterben seine Engines, und mit ihnen zeigen
// alle Adressen aus diesem Kontext ins Leere.
#include "ctx_runtime.h"
#include "../../compiler.h"
#include "../core/context.h"
#include "../../logger.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct CtxSlot {
    std::unique_ptr<AIMLCompiler> comp;
    // Die Typtabelle des Kontexts: Name -> aiml-Typ bzw. Signatur, nach JEDEM
    // Bundle fortgeschrieben.  Sie ist noetig, weil `compileFromString` den
    // CompilerContext jedes Mal verwirft: die lebende Tabelle kennt nur, was
    // das LETZTE Bundle uebersetzt (oder deklariert) hat.  Was der Kontext
    // sieht, ist aber die Reihenfolge seiner Aufrufe (F29_2-…f) — also wird
    // mitgeschrieben.  Spaeter definiert gewinnt (Neuladen, §4).
    std::map<std::string, std::string> types;
};

std::mutex                              g_ctx_mu;
std::vector<std::unique_ptr<CtxSlot>>   g_ctxs;

CtxSlot* slot_of(void* handle) {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    for (auto& s : g_ctxs)
        if (s.get() == handle) return s.get();
    return nullptr;
}

// Zum Vergleich: Leerzeichen sind Schmuck.  Wer `fn(i32, string) -> i32`
// schreibt, meint dasselbe wie `fn(i32,string)->i32`.
std::string squeeze(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char ch : in) if (ch != ' ' && ch != '\t') out += ch;
    return out;
}

// Eine aiml-Liste, wie der Uebersetzer sie anlegt (dieselbe Form wie bei
// `io.list_dir`): {size, capacity, data}.  Gefuellt wird mit refcounted
// aiml-Strings.
struct AimlListRaw { int32_t size; int32_t capacity; void** data; };
extern "C" char* aiml_str_ndup(const char* s, size_t len);

static void list_push(AimlListRaw* list, const std::string& text) {
    if (!list) return;
    if (list->size >= list->capacity) {
        int newCap = list->capacity > 0 ? list->capacity * 2 : 8;
        void** grown = static_cast<void**>(
            std::realloc(list->data, (size_t)newCap * sizeof(void*)));
        if (!grown) return;
        list->data     = grown;
        list->capacity = newCap;
    }
    list->data[list->size++] = aiml_str_ndup(text.c_str(), text.size());
}

// Der Typ eines Symbols, wie der Kontext ihn vermerkt hat.
//
// Bei Funktionen ist das die VOLLE Signatur (F29_2-…d3): `fn(i32,string) -> i32`.
// „Es ist eine Funktion" genuegt nicht — wer eine Adresse mit der falschen
// Signatur ruft, bekommt keinen Fehler, sondern einen kaputten Stapel.  Der
// Kontext kennt Argumenttypen und Rueckgabetyp, also wird beides verglichen.
std::string signature_of(const CompilerContext::FunctionInfo& fi) {
    // Die Stelligkeit kommt aus argNames: fehlt ein Typname, steht dort "?",
    // statt dass das Argument aus der Signatur verschwindet.
    std::string sig = "fn(";
    for (size_t i = 0; i < fi.argNames.size(); ++i) {
        if (i) sig += ",";
        sig += i < fi.argTypeNames.size() && !fi.argTypeNames[i].empty()
                   ? fi.argTypeNames[i] : "?";
    }
    sig += ") -> ";
    sig += fi.returnTypeName.empty() ? "void" : fi.returnTypeName;
    return sig;
}

std::string type_in_context(CompilerContext* c, const std::string& name, bool& found) {
    found = false;
    if (!c) return std::string();
    auto fit = c->functions.find(name);
    if (fit != c->functions.end()) { found = true; return signature_of(fit->second); }
    // Globale Werte stehen in der Symboltabelle des Kontexts.
    auto git = c->symbol_table.find(name);
    if (git != c->symbol_table.end()) {
        found = true;
        return git->second.typeName.empty() ? std::string("?") : git->second.typeName;
    }
    return std::string();
}

}  // namespace

extern "C" {

void aiml_ctx_record_symbols(void* handle) {
    CtxSlot* slot = slot_of(handle);
    if (!slot || !slot->comp) return;
    auto* c = static_cast<CompilerContext*>(slot->comp->contextState());
    if (!c) return;
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    for (const auto& fn : c->functions)
        slot->types[fn.first] = signature_of(fn.second);
    for (const auto& sym : c->symbol_table)
        slot->types[sym.first] = sym.second.typeName.empty() ? std::string("?")
                                                             : sym.second.typeName;
}

void* aiml_ctx_new(void) {
    auto slot = std::make_unique<CtxSlot>();
    slot->comp = std::make_unique<AIMLCompiler>();
    void* handle = slot.get();
    {
        std::lock_guard<std::mutex> lock(g_ctx_mu);
        g_ctxs.push_back(std::move(slot));
    }
    LOG_INFO("ctx: Kontext geoeffnet");
    return handle;
}

void aiml_ctx_free(void* handle) {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    for (auto it = g_ctxs.begin(); it != g_ctxs.end(); ++it) {
        if (it->get() == handle) {
            g_ctxs.erase(it);
            LOG_INFO("ctx: Kontext geschlossen");
            return;
        }
    }
}

void* aiml_ctx_compiler(void* handle) {
    CtxSlot* slot = slot_of(handle);
    return (slot && slot->comp) ? static_cast<void*>(slot->comp.get()) : nullptr;
}

int32_t aiml_ctx_symbol(void* handle, const char* name, const char* want_type,
                        int64_t* out_addr, const char** out_msg) {
    static std::string s_msg;   // haelt die Begruendung fuer den Werfer
    s_msg.clear();
    if (out_msg)  *out_msg = "";
    if (out_addr) *out_addr = 0;

    const std::string nm   = name ? name : "";
    const std::string want = want_type ? want_type : "";

    CtxSlot* slot = slot_of(handle);
    if (!slot || !slot->comp || nm.empty()) {
        s_msg = "ctx.symbol: kein Kontext oder kein Name";
        if (out_msg) *out_msg = s_msg.c_str();
        return 1;
    }

    bool found = false;
    std::string have;
    {
        std::lock_guard<std::mutex> lock(g_ctx_mu);
        auto it = slot->types.find(nm);
        if (it != slot->types.end()) { found = true; have = it->second; }
    }
    if (!found) {
        auto* c = static_cast<CompilerContext*>(slot->comp->contextState());
        have = type_in_context(c, nm, found);
    }
    if (!found) {
        s_msg = "ctx.symbol: '" + nm + "' kennt dieser Kontext nicht";
        if (out_msg) *out_msg = s_msg.c_str();
        return 1;
    }

    if (squeeze(want) != squeeze(have)) {
        s_msg = "ctx.symbol: '" + nm + "' — erwartet '" + want + "', gefunden '" + have + "'";
        if (squeeze(want) == "fn")
            s_msg += " (Funktionen brauchen die volle Signatur)";
        if (out_msg) *out_msg = s_msg.c_str();
        return 2;
    }

    if (have.rfind("fn(", 0) == 0) {
        uint64_t addr = slot->comp->symbolAddress(nm);
        // Gefunden in der Tabelle, aber ohne Adresse: das Bundle hat die
        // Funktion DEKLARIERT und nie definiert.  Das ist kein Typfehler,
        // sondern ein fehlendes Symbol — und genau so wird es gemeldet.
        if (!addr) {
            s_msg = "ctx.symbol: '" + nm + "' ist deklariert, aber nirgends definiert";
            if (out_msg) *out_msg = s_msg.c_str();
            return 1;
        }
        if (out_addr) *out_addr = (int64_t)addr;
        return 0;
    }

    // Werte: die Adresse des Globals.  Der Leser hat den Typ genannt und er
    // stimmt mit dem ueberein, den der Kontext vermerkt hat.
    uint64_t addr = slot->comp->symbolAddress(nm);
    if (!addr) {
        s_msg = "ctx.symbol: '" + nm + "' hat keine Adresse";
        if (out_msg) *out_msg = s_msg.c_str();
        return 1;
    }
    if (out_addr) *out_addr = (int64_t)addr;
    return 0;
}

// spec/29_2 §14b (F29_2-…i): nach TYP fragen, nicht nach Namen raten.  Der
// Kontroller nimmt eine EINGEREICHTE Pipeline an und kennt ihren Namen nicht;
// ihn aus dem Quelltext zu lesen heisst, den Quelltext zu lesen — also genau
// das, was der Kontext uebernehmen soll.
int32_t aiml_ctx_names_fill(void* handle, const char* want_type, void* list_ptr) {
    CtxSlot* slot = slot_of(handle);
    auto* list = static_cast<AimlListRaw*>(list_ptr);
    if (!slot || !list) return 0;
    const std::string want = squeeze(want_type ? want_type : "");
    int32_t found = 0;
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    for (const auto& entry : slot->types) {
        if (squeeze(entry.second) != want) continue;
        list_push(list, entry.first);
        ++found;
    }
    return found;
}

// spec/29_2 §14b (F29_2-…l): die Meldungen des LETZTEN `exec`.
int32_t aiml_ctx_errors_fill(void* handle, void* list_ptr) {
    CtxSlot* slot = slot_of(handle);
    auto* list = static_cast<AimlListRaw*>(list_ptr);
    if (!slot || !slot->comp || !list) return 0;
    int32_t count = 0;
    for (const std::string& why : slot->comp->ctxErrors()) {
        list_push(list, why);
        ++count;
    }
    return count;
}

}  // extern "C"
