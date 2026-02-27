// exec_impl.cpp — Runtime implementation of sys.exec
//
// aiml_exec_code is called from LLVM JIT IR (emitted by code_impl.cpp::emitExecFn).
// aiml_set_active_context is called from jitRun() to register the active
// CompilerContext and ExecutionEngine before/after engine->runFunction().
//
// Initial implementation (Phase 2):
//   - context=false (shared, default):
//       * Checks symbol table; logs warning for already-defined and for new
//         declarations (dynamic loading not yet supported).
//       * Executes start_call by looking up the compiled function in the engine
//         and calling it as a function pointer (0-arg i32 or void functions only).
//   - data_chunks:  execution deferred (logs warning).
//   - context=true: same as context=false for now (isolated context deferred).

#include "exec_impl.h"
#include "src/core/context.h"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <yaml-cpp/yaml.h>
#include <cstdint>
#include <cstring>
#include <string>

// ── Mirror structs (must match code_runtime.cpp + code_impl.cpp) ─────────────

struct ExecList {
    int32_t size;
    int32_t capacity;
    char**  data;
};

struct ExecCode {
    char*     id;
    char*     start_call;
    ExecList* code_chunks;
    ExecList* data_chunks;
    int8_t    new_context;
};

// ── Global state ──────────────────────────────────────────────────────────────

static CompilerContext*       g_active_context = nullptr;
static llvm::ExecutionEngine* g_active_ee      = nullptr;

extern "C" void aiml_set_active_context(void* ctx, void* ee) {
    g_active_context = static_cast<CompilerContext*>(ctx);
    g_active_ee      = static_cast<llvm::ExecutionEngine*>(ee);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Parse start_call JSON/YAML string → first element of the "call" sequence.
// Expected format: {call: [func_name, ...]}
static std::string parseStartCallFuncName(const char* sc) {
    if (!sc || sc[0] == '\0') return "";
    try {
        YAML::Node n = YAML::Load(std::string(sc));
        if (n["call"] && n["call"].IsSequence() && n["call"].size() >= 1)
            return n["call"][0].as<std::string>();
    } catch (...) {}
    return "";
}

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" int aiml_exec_code(void* raw) {
    if (!raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);

    if (!g_active_ee) return -2;

    // --- 1. Code chunks: check symbol table, log status -----------------------
    ExecList* cc = code->code_chunks;
    if (cc && cc->size > 0 && g_active_context) {
        for (int i = 0; i < cc->size; i++) {
            if (!cc->data[i]) continue;
            try {
                YAML::Node node = YAML::Load(std::string(cc->data[i]));
                // Chunk may be a bare mapping node or a 1-element sequence
                YAML::Node decl = (node.IsSequence() && node.size() == 1)
                                      ? node[0] : node;

                auto checkSkip = [&](const std::string& key, auto& table) {
                    if (!decl[key]) return;
                    YAML::Node nameNode = decl[key]["name"];
                    if (!nameNode) return;
                    std::string nm = nameNode.as<std::string>();
                    if (table.count(nm))
                        LOG_WARNING("sys.exec: skipping already-defined " + key + ": " + nm);
                    else
                        LOG_WARNING("sys.exec: new " + key + " '" + nm +
                                    "' — dynamic loading not yet supported");
                };

                checkSkip("def",   g_active_context->functions);
                checkSkip("class", g_active_context->classes);
                checkSkip("enum",  g_active_context->enums);
            } catch (...) {}
        }
    }

    // --- 2. data_chunks: deferred for initial implementation ------------------
    ExecList* dc = code->data_chunks;
    if (dc && dc->size > 0)
        LOG_WARNING("sys.exec: data_chunks execution not yet implemented");

    // --- 3. Execute start_call ------------------------------------------------
    if (!code->start_call || code->start_call[0] == '\0')
        return 0;   // No start_call → success

    std::string funcName = parseStartCallFuncName(code->start_call);
    if (funcName.empty()) {
        LOG_WARNING("sys.exec: cannot parse start_call: " +
                    std::string(code->start_call));
        return 0;
    }

    // Look up function address in the engine
    uint64_t addr = g_active_ee->getFunctionAddress(funcName);
    if (!addr) {
        LOG_ERROR("sys.exec: function not found in engine: " + funcName);
        return -1;
    }

    // Determine calling convention via symbol table
    llvm::Function* llvmFn = nullptr;
    if (g_active_context && g_active_context->functions.count(funcName))
        llvmFn = g_active_context->functions.at(funcName).func;

    // Initial impl: only supports 0-arg functions
    if (llvmFn && llvmFn->arg_size() > 0) {
        LOG_WARNING("sys.exec: start_call '" + funcName +
                    "' requires arguments — skipped");
        return 0;
    }

    // Call the function; assume i32() unless LLVM says void
    if (llvmFn && llvmFn->getReturnType()->isVoidTy()) {
        typedef void (*VoidFn)();
        ((VoidFn)(intptr_t)addr)();
        return 0;
    }

    typedef int32_t (*I32Fn)();
    return ((I32Fn)(intptr_t)addr)();
}
