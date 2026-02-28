// exec_impl.cpp — Runtime implementation of sys.exec with Hot-Reload support.
//
// Hot-Reload flow (context=false, default):
//   1. cache_fptable_globals() — locate __aiml_fptable_current et al. in engine/dlsym
//   2. For each code_chunk:
//        - Parse YAML → get name + version
//        - bundle_version <= current → SKIP (version guard)
//        - Else: compileYamlChunk → new machine code address (deferred: stub returns 0)
//                aiml_fptable_update → atomic pointer swap in fptable_current
//   3. Execute start_call via fptable_current (or engine lookup fallback)

#include "exec_impl.h"
#include "src/core/context.h"
#include "src/core/fptable.h"          // AIML_MAX_VERSIONS
#include "src/core/schema_registry.h"  // AimlClassSchema, AIML_MAX_FIELDS
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <yaml-cpp/yaml.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef __linux__
#  include <dlfcn.h>
#endif

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

// fptable globals (cached after first call to cache_fptable_globals)
static void**       g_fptable_current  = nullptr;
static void***      g_fptable          = nullptr;
static int32_t*     g_fptable_versions = nullptr;
static const char** g_func_names       = nullptr;
static int32_t      g_func_count       = 0;

// AOT mode: MCJIT engines created for hot-reloaded chunks must stay alive
static std::vector<llvm::ExecutionEngine*> g_aot_engines;

extern "C" void aiml_set_active_context(void* ctx, void* ee) {
    g_active_context = static_cast<CompilerContext*>(ctx);
    g_active_ee      = static_cast<llvm::ExecutionEngine*>(ee);
    // Invalidate cached fptable pointers — new engine may have different addresses
    g_fptable_current  = nullptr;
    g_fptable          = nullptr;
    g_fptable_versions = nullptr;
    g_func_names       = nullptr;
    g_func_count       = 0;
}

// ── cache_fptable_globals ─────────────────────────────────────────────────────

static void cache_fptable_globals() {
    if (g_fptable_current) return;  // already cached

    if (g_active_ee) {
        // ── JIT mode: ask MCJIT engine for global addresses ──────────────────
        auto ga = [](const char* nm) -> uint64_t {
            return g_active_ee->getGlobalValueAddress(nm);
        };
        uint64_t cur  = ga("__aiml_fptable_current");
        uint64_t tbl  = ga("__aiml_fptable");
        uint64_t vers = ga("__aiml_fptable_versions");
        uint64_t nms  = ga("__aiml_func_names");
        uint64_t cnt  = ga("__aiml_func_count");

        if (!cur) {
            LOG_WARNING("exec_impl: __aiml_fptable_current not found in engine");
            return;
        }
        g_fptable_current  = reinterpret_cast<void**>(cur);
        g_fptable          = reinterpret_cast<void***>(tbl);
        g_fptable_versions = reinterpret_cast<int32_t*>(vers);
        g_func_names       = reinterpret_cast<const char**>(nms);
        g_func_count       = cnt ? *reinterpret_cast<int32_t*>(cnt) : 0;
    } else {
#ifdef __linux__
        // ── AOT mode: globals are in the binary — use dlsym + -rdynamic ─────
        g_fptable_current  = static_cast<void**>(
            dlsym(RTLD_DEFAULT, "__aiml_fptable_current"));
        g_fptable          = static_cast<void***>(
            dlsym(RTLD_DEFAULT, "__aiml_fptable"));
        g_fptable_versions = static_cast<int32_t*>(
            dlsym(RTLD_DEFAULT, "__aiml_fptable_versions"));
        g_func_names       = static_cast<const char**>(
            dlsym(RTLD_DEFAULT, "__aiml_func_names"));
        int32_t* cnt       = static_cast<int32_t*>(
            dlsym(RTLD_DEFAULT, "__aiml_func_count"));
        g_func_count       = cnt ? *cnt : 0;
        if (!g_fptable_current)
            LOG_WARNING("exec_impl: fptable not found via dlsym (missing -rdynamic?)");
#endif
    }
}

// ── aiml_fptable_find / aiml_fptable_update ───────────────────────────────────

extern "C" int aiml_fptable_find(const char* name) {
    cache_fptable_globals();
    if (!g_func_names || !name) return -1;
    for (int i = 0; i < g_func_count; i++) {
        if (g_func_names[i] && strcmp(g_func_names[i], name) == 0)
            return i;
    }
    return -1;
}

extern "C" void aiml_fptable_update(int func_idx, int version, void* fn_ptr) {
    if (!g_fptable_current || !g_fptable || !g_fptable_versions) {
        LOG_ERROR("aiml_fptable_update: fptable not cached");
        return;
    }
    int vi = version - 1;  // 1-based version → 0-based index
    if (vi < 0 || vi >= AIML_MAX_VERSIONS) {
        LOG_ERROR("aiml_fptable_update: version out of range: " + std::to_string(version));
        return;
    }
    // 2D table is stored row-major: row i starts at g_fptable + i*AIML_MAX_VERSIONS
    // g_fptable points to the start of [N x [AIML_MAX_VERSIONS x i8*]]
    // Cast to flat pointer for row arithmetic:
    void** flat = reinterpret_cast<void**>(g_fptable);
    flat[func_idx * AIML_MAX_VERSIONS + vi] = fn_ptr;

    // Atomically update the 1D fast-path array (8-byte aligned → naturally atomic on x86-64)
    __atomic_store_n(&g_fptable_current[func_idx], fn_ptr, __ATOMIC_SEQ_CST);

    // Update version counter
    if (version > g_fptable_versions[func_idx])
        g_fptable_versions[func_idx] = version;

    const char* fname = (g_func_names && func_idx < g_func_count)
        ? g_func_names[func_idx] : "(?)";
    LOG_INFO("fptable: " + std::string(fname) + " → v" + std::to_string(version));
}

// ── compileYamlChunk ──────────────────────────────────────────────────────────

// Compiles a single YAML chunk (one def/class) into machine code.
// Returns the address of the named function, or 0 if not yet wired.
// Full wiring (addModule to engine) requires linking ast_builder/compiler into
// this TU — deferred until Phase 5b.
static uint64_t compileYamlChunk(const char* /*yaml*/, const std::string& /*funcName*/) {
    LOG_WARNING("compileYamlChunk: hot compilation not yet wired — skipping");
    return 0;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Parse start_call JSON/YAML string → first element of the "call" sequence.
static std::string parseStartCallFuncName(const char* sc) {
    if (!sc || sc[0] == '\0') return "";
    try {
        YAML::Node n = YAML::Load(std::string(sc));
        if (n["call"] && n["call"].IsSequence() && n["call"].size() >= 1)
            return n["call"][0].as<std::string>();
    } catch (...) {}
    return "";
}

// ── aiml_exec_code ────────────────────────────────────────────────────────────

extern "C" int aiml_exec_code(void* raw) {
    if (!raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);

    // Locate fptable (JIT: engine; AOT: dlsym)
    cache_fptable_globals();

    // ── 1. Code chunks: version-guarded hot-reload ────────────────────────────
    ExecList* cc = code->code_chunks;
    if (cc && cc->size > 0 && g_active_context) {
        for (int i = 0; i < cc->size; i++) {
            if (!cc->data[i]) continue;
            try {
                YAML::Node node = YAML::Load(std::string(cc->data[i]));
                YAML::Node decl = (node.IsSequence() && node.size() == 1)
                                      ? node[0] : node;

                auto tryHotReload = [&](const std::string& key, auto& table) {
                    if (!decl[key]) return;
                    YAML::Node nameNode = decl[key]["name"];
                    if (!nameNode) return;
                    std::string nm = nameNode.as<std::string>();

                    // Read bundle version (default 1)
                    uint64_t bundle_ver = 1;
                    if (decl[key]["version"]) {
                        try { bundle_ver = decl[key]["version"].as<uint64_t>(); } catch (...) {}
                    }

                    if (table.count(nm)) {
                        // Use live fptable version as current (functions: g_fptable_versions,
                        // classes: compiled_version — both immune to visitTopLevel re-def updates).
                        int fidx = aiml_fptable_find(nm.c_str());
                        uint64_t cur_ver = (fidx >= 0 && g_fptable_versions)
                            ? (uint64_t)g_fptable_versions[fidx]
                            : table.at(nm).compiled_version;

                        if (bundle_ver <= cur_ver) {
                            LOG_INFO("sys.exec: skip " + nm +
                                     " (v" + std::to_string(bundle_ver) +
                                     " <= live v" + std::to_string(cur_ver) + ")");
                            return;
                        }
                        LOG_INFO("sys.exec: hot-reload " + nm +
                                 " v" + std::to_string(cur_ver) +
                                 " → v" + std::to_string(bundle_ver));

                        uint64_t new_addr = compileYamlChunk(cc->data[i], nm);
                        if (new_addr) {
                            if (fidx >= 0) {
                                aiml_fptable_update(fidx, (int)bundle_ver, (void*)new_addr);
                            } else {
                                LOG_WARNING("sys.exec: " + nm +
                                            " not in fptable — new function, cannot hot-reload");
                            }
                        } else {
                            LOG_WARNING("sys.exec: compilation deferred for " + nm +
                                        " — function stays at v" + std::to_string(cur_ver));
                        }
                    } else {
                        LOG_WARNING("sys.exec: new " + key + " '" + nm +
                                    "' — dynamic loading not yet supported");
                    }
                };

                tryHotReload("def",   g_active_context->functions);
                tryHotReload("class", g_active_context->classes);
                // enums: baked immediates, no hot-reload

            } catch (...) {}
        }
    }

    // ── 2. data_chunks: deferred ──────────────────────────────────────────────
    ExecList* dc = code->data_chunks;
    if (dc && dc->size > 0)
        LOG_WARNING("sys.exec: data_chunks execution not yet implemented");

    // ── 3. Execute start_call ─────────────────────────────────────────────────
    if (!code->start_call || code->start_call[0] == '\0')
        return 0;

    std::string funcName = parseStartCallFuncName(code->start_call);
    if (funcName.empty()) {
        LOG_WARNING("sys.exec: cannot parse start_call: " +
                    std::string(code->start_call));
        return 0;
    }

    // Helper: get LLVM function metadata for calling-convention check
    auto getLlvmFn = [&]() -> llvm::Function* {
        if (!g_active_context) return nullptr;
        auto it = g_active_context->functions.find(funcName);
        return (it != g_active_context->functions.end()) ? it->second.func : nullptr;
    };

    // Try fptable_current first (covers hot-reload case for both JIT and AOT)
    if (g_fptable_current) {
        int fidx = aiml_fptable_find(funcName.c_str());
        if (fidx >= 0 && g_fptable_current[fidx]) {
            llvm::Function* llvmFn = getLlvmFn();
            if (llvmFn && llvmFn->arg_size() > 0) {
                LOG_WARNING("sys.exec: start_call '" + funcName +
                            "' requires arguments — skipped");
                return 0;
            }
            void* fp = g_fptable_current[fidx];
            if (llvmFn && llvmFn->getReturnType()->isVoidTy()) {
                ((void(*)())fp)();
                return 0;
            }
            return ((int32_t(*)())fp)();
        }
    }

    // Fallback: direct engine lookup (JIT, function not in fptable or fptable unavailable)
    if (!g_active_ee) return -2;

    uint64_t addr = g_active_ee->getFunctionAddress(funcName);
    if (!addr) {
        LOG_ERROR("sys.exec: function not found: " + funcName);
        return -1;
    }

    llvm::Function* llvmFn = getLlvmFn();
    if (llvmFn && llvmFn->arg_size() > 0) {
        LOG_WARNING("sys.exec: start_call '" + funcName +
                    "' requires arguments — skipped");
        return 0;
    }
    if (llvmFn && llvmFn->getReturnType()->isVoidTy()) {
        typedef void (*VoidFn)();
        ((VoidFn)(intptr_t)addr)();
        return 0;
    }
    typedef int32_t (*I32Fn)();
    return ((I32Fn)(intptr_t)addr)();
}
