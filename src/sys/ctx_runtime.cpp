// spec/29_2 §14a — `ctx`: handles on live compiler instances.
//
// The table holds the instances; the handle is their pointer.  Freeing
// happens via `aiml_ctx_free` (the owner drop in aiml calls it) — and
// not earlier: with the compiler its engines die, and with them
// all addresses from this context point into nothing.
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
    // The context's type table: name -> aiml type or signature, updated after EVERY
    // bundle.  It is necessary because `compileFromString` discards the
    // CompilerContext each time: the live table only knows what
    // the LAST bundle compiled (or declared).  But what the context
    // sees is the sequence of its calls (F29_2-…f) — so it is
    // recorded as we go.  Defined later wins (reloading, §4).
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

// For comparison: whitespace is decoration.  Whoever writes `fn(i32, string) -> i32`
// means the same as `fn(i32,string)->i32`.
std::string squeeze(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char ch : in) if (ch != ' ' && ch != '\t') out += ch;
    return out;
}

// An aiml list as the compiler creates it (the same form as for
// `io.list_dir`): {size, capacity, data}.  It is filled with refcounted
// aiml strings.
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

// The type of a symbol as the context has noted it.
//
// For functions this is the FULL signature (F29_2-…d3): `fn(i32,string) -> i32`.
// "It is a function" is not enough — whoever calls an address with the wrong
// signature gets no error but a broken stack.  The
// context knows argument types and return type, so both are compared.
std::string signature_of(const CompilerContext::FunctionInfo& fi) {
    // The arity comes from argNames: if a type name is missing, "?" stands there,
    // instead of the argument disappearing from the signature.
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
    // Global values are in the context's symbol table.
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
    LOG_INFO("ctx: context opened");
    return handle;
}

void aiml_ctx_free(void* handle) {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    for (auto it = g_ctxs.begin(); it != g_ctxs.end(); ++it) {
        if (it->get() == handle) {
            g_ctxs.erase(it);
            LOG_INFO("ctx: context closed");
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
    static std::string s_msg;   // holds the reason for the thrower
    s_msg.clear();
    if (out_msg)  *out_msg = "";
    if (out_addr) *out_addr = 0;

    const std::string nm   = name ? name : "";
    const std::string want = want_type ? want_type : "";

    CtxSlot* slot = slot_of(handle);
    if (!slot || !slot->comp || nm.empty()) {
        s_msg = "ctx.symbol: no context or no name";
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
        s_msg = "ctx.symbol: '" + nm + "' is not known to this context";
        if (out_msg) *out_msg = s_msg.c_str();
        return 1;
    }

    if (squeeze(want) != squeeze(have)) {
        s_msg = "ctx.symbol: '" + nm + "' — expected '" + want + "', found '" + have + "'";
        if (squeeze(want) == "fn")
            s_msg += " (functions need the full signature)";
        if (out_msg) *out_msg = s_msg.c_str();
        return 2;
    }

    if (have.rfind("fn(", 0) == 0) {
        uint64_t addr = slot->comp->symbolAddress(nm);
        // Found in the table, but without an address: the bundle
        // DECLARED the function and never defined it.  That is not a type error
        // but a missing symbol — and it is reported exactly as such.
        if (!addr) {
            s_msg = "ctx.symbol: '" + nm + "' is declared, but defined nowhere";
            if (out_msg) *out_msg = s_msg.c_str();
            return 1;
        }
        if (out_addr) *out_addr = (int64_t)addr;
        return 0;
    }

    // Values: the address of the global.  The reader named the type and it
    // matches the one the context has noted.
    uint64_t addr = slot->comp->symbolAddress(nm);
    if (!addr) {
        s_msg = "ctx.symbol: '" + nm + "' has no address";
        if (out_msg) *out_msg = s_msg.c_str();
        return 1;
    }
    if (out_addr) *out_addr = (int64_t)addr;
    return 0;
}

// spec/29_2 §14b (F29_2-…i): ask by TYPE, do not guess by name.  The
// controller accepts a SUBMITTED pipeline and does not know its name;
// reading it from the source text means reading the source text — i.e. exactly
// what the context is supposed to take over.
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

// spec/29_2 §14b (F29_2-…l): the messages of the LAST `exec`.
// spec/21 §3.1: the package table for this context's `@name/…` imports.
int32_t aiml_ctx_use_packages(void* handle, const char* path) {
    CtxSlot* slot = slot_of(handle);
    if (!slot || !slot->comp) return -1;
    slot->comp->setPackageTable(path ? path : "");
    return 0;
}

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
