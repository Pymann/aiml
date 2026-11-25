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
#include "src/core/yaml_dump.h"
#include "compiler.h"                  // AIMLCompiler (for aiml_isolated_exec)
#include "src/sys/ctx_runtime.h"      // spec/29_2 §14a: context handles
#include "src/core/clearance.h"         // spec/65 F65-36: clearance per session
#include "src/core/context.h"
#include "src/core/fptable.h"          // AIML_MAX_VERSIONS
#include "src/core/schema_registry.h"  // AimlClassSchema, AIML_MAX_FIELDS
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/DynamicLibrary.h>
#include <yaml-cpp/yaml.h>
#include <memory>
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

// Hot-reloaded AIMLCompiler instances must stay alive (they own the JIT engines).
static std::vector<AIMLCompiler*> g_aot_compilers;

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

// Compiles all code_chunks of a bundle into machine code and returns the address
// of funcName (a def) or 1 (sentinel for class/model success), or 0 on failure.
//
// All chunks are compiled together so that class/model type definitions are
// available when the def function is compiled (full dependency context).
//
// Before compiling, all currently compiled user functions are exported to
// llvm::sys::DynamicLibrary so the new JIT module can resolve calls to them.
static uint64_t compileYamlChunk(ExecList* cc, const std::string& funcName) {
    if (!cc || cc->size == 0 || !g_active_context || !g_active_ee) return 0;

    // Step 1: Export all user-defined functions from the live engine so the new
    //         JIT module can resolve inter-function calls as external symbols.
    for (auto& [nm, fi] : g_active_context->functions) {
        uint64_t addr = g_active_ee->getFunctionAddress(nm);
        if (addr) llvm::sys::DynamicLibrary::AddSymbol(nm, (void*)addr);
    }

    // Step 2: Build a single YAML sequence from all chunks (preserves dep order).
    std::string yaml_source;
    try {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (int i = 0; i < cc->size; i++) {
            if (!cc->data[i]) continue;
            YAML::Node chunk = YAML::Load(std::string(cc->data[i]));
            if (chunk.IsDefined() && !chunk.IsNull())
                seq.push_back(chunk);
        }
        yaml_source = aiml::dumpPreservingQuotes(seq);
    } catch (...) {
        LOG_ERROR("compileYamlChunk: YAML parse error for bundle of '" + funcName + "'");
        return 0;
    }

    if (yaml_source.empty()) return 0;

    // Step 3: Compile in a fresh LLVM context and retrieve the function address.
    AIMLCompiler* comp = new AIMLCompiler();
    uint64_t addr = comp->compileChunkAndGetAddress(yaml_source, funcName);
    if (addr) {
        g_aot_compilers.push_back(comp);  // keep JIT engine alive
        LOG_INFO("compileYamlChunk: hot-compiled '" + funcName + "' @ 0x" +
                 std::to_string(addr));
    } else {
        delete comp;
    }
    return addr;
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

// ── aiml_isolated_exec ────────────────────────────────────────────────────────
// Runs a Code object (new_context == 1) in a completely fresh LLVM context:
//   1. Reconstruct YAML source from code_chunks.
//   2. Append start_call as a top-level call node.
//   3. Create a new AIMLCompiler, compile + JIT-run the source.
//   4. Restore the caller's active context/engine.

// ── Library registry (spec/29_2 §15) ─────────────────────────────────────────
// Process-wide, deliberately without a handle: an agent loads its library at start
// and then runs steps.  Several contexts isolated from each other would be the
// next step (an `exec_context` as a unique resource), but they need
// a rebindable ctx.module() — today a reference at 601 places.
//
// Two things are kept: the compiler (so that its engine and thus the
// registered addresses stay alive) and the source text (so that per run only
// the DECLARATIONS are repeated, not the bodies).
static std::vector<std::unique_ptr<AIMLCompiler>> g_lib_compilers;
static std::vector<std::string>                   g_lib_sources;

// Helper: build YAML source text from the code_chunks of a bundle.
//
// NOT by concatenation: a chunk is a YAML document of its own, and
// several documents glued together do not make a valid document with
// several entries — the compiler then only saw the first `- def:`.
// (Exactly this bug made `sys.lib_load` register only ONE symbol,
// although the bundle contained four functions.)  Instead, like
// aiml_isolated_exec: parse every chunk and put it into ONE sequence.
static std::string exec_chunks_to_yaml(ExecCode* code) {
    ExecList* cc = code ? code->code_chunks : nullptr;
    if (!cc || cc->size == 0) return std::string();
    try {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (int i = 0; i < cc->size; i++) {
            if (!cc->data[i]) continue;
            YAML::Node chunk = YAML::Load(std::string(cc->data[i]));
            if (!chunk.IsDefined() || chunk.IsNull()) continue;
            if (chunk.IsSequence()) {
                for (const auto& item : chunk) seq.push_back(item);
            } else {
                seq.push_back(chunk);
            }
        }
        return aiml::dumpPreservingQuotes(seq);
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("exec_chunks_to_yaml: ") + e.what());
        return std::string();
    }
}

extern "C" int aiml_lib_load(void* raw) {
    if (!raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);
    std::string src = exec_chunks_to_yaml(code);
    if (src.empty()) {
        LOG_WARNING("aiml_lib_load: empty bundle — nothing to load");
        return 0;
    }
    auto comp = std::make_unique<AIMLCompiler>();
    int n = comp->compileLibraryAndRegister(src);
    if (n < 0) return -1;
    g_lib_compilers.push_back(std::move(comp));
    g_lib_sources.push_back(src);
    return n;
}

extern "C" int aiml_lib_count(void) { return (int)g_lib_sources.size(); }

extern "C" int aiml_isolated_exec(void* raw) {
    if (!raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);

    // ── Source text from the code_chunks (one place: exec_chunks_to_yaml)
    std::string yaml_source = exec_chunks_to_yaml(code);
    if (yaml_source.empty()) {
        LOG_WARNING("aiml_isolated_exec: no code_chunks — nothing to compile");
        return 0;
    }

    // ── Step 2: the start call as a top-level statement ──────────────────────
    // Without it the fresh compiler only DEFINES the bundled functions and its
    // main runs nothing: sys.exec(sys.bundle(fn, {context: true})) returned 0
    // and fn never ran (spec/29_2 §12: "worker_task runs").  It went unnoticed
    // where an earlier run had left the files the tests look for in logs/.
    if (code->start_call && code->start_call[0] != '\0') {
        try {
            YAML::Node sc = YAML::Load(std::string(code->start_call));
            if (sc["call"] && sc["call"].IsSequence() && sc["call"].size() >= 1) {
                YAML::Node seq = YAML::Load(yaml_source);
                if (seq.IsSequence()) {
                    YAML::Node stmt(YAML::NodeType::Map);
                    stmt["call"] = sc["call"];
                    seq.push_back(stmt);
                    yaml_source = aiml::dumpPreservingQuotes(seq);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARNING(std::string("aiml_isolated_exec: cannot parse start_call: ") + e.what());
        }
    }

    LOG_INFO("aiml_isolated_exec: compiled source (" +
             std::to_string(yaml_source.size()) + " bytes)");

    // ── Save caller's global context ──────────────────────────────────────────
    auto* saved_ctx      = g_active_context;
    auto* saved_ee       = g_active_ee;
    auto* saved_fptable  = g_fptable_current;

    // ── Compile + run in isolated AIMLCompiler ────────────────────────────────
    int rc = -1;
    {
        AIMLCompiler iso;
        if (g_active_context)
            iso.compilerPath = g_active_context->compilerPath;
        // Only DECLARE loaded libraries (spec/29_2 §15): signatures
        // become known, the bodies were compiled once when loading and
        // are reachable via DynamicLibrary::AddSymbol.
        for (const auto& lib : g_lib_sources) iso.compileDeclarationsOnly(lib);
        rc = iso.compileAndRun(yaml_source);
    } // iso destructor frees its LLVMContext, Module, IRBuilder, CompilerContext

    // ── Restore caller's global context ──────────────────────────────────────
    // (inner jitRun sets them to nullptr via aiml_set_active_context(nullptr,nullptr))
    g_active_context   = saved_ctx;
    g_active_ee        = saved_ee;
    g_fptable_current  = saved_fptable;
    // Invalidate other cached fptable pointers — will re-cache on next use
    g_fptable          = nullptr;
    g_fptable_versions = nullptr;
    g_func_names       = nullptr;
    g_func_count       = 0;

    LOG_INFO("aiml_isolated_exec: finished with rc=" + std::to_string(rc));
    return rc;
}

// ── aiml_ctx_exec_code (spec/29_2 §14a) ─────────────────────────────────────
//
// The same bundle, the same route to the source text as with `aiml_isolated_exec` —
// only it runs in a context that LIVES ON.  That is why it is here and
// not in ctx_runtime.cpp: the route from the code object to the YAML already exists,
// and two versions of it would be two that drift apart.
static int32_t aiml_ctx_exec_or_load(void* handle, void* raw, int run_entry);

// spec/29_2 §14b (F29_2-…n): the same, but the entry point is NOT
// called — a controller checks and reads foreign source text, it does not
// run it (cosmos F96-28).
extern "C" int32_t aiml_ctx_load_code(void* handle, void* raw) {
    return aiml_ctx_exec_or_load(handle, raw, /*run_entry=*/0);
}

extern "C" int32_t aiml_ctx_exec_code(void* handle, void* raw) {
    return aiml_ctx_exec_or_load(handle, raw, /*run_entry=*/1);
}

static int32_t aiml_ctx_exec_or_load(void* handle, void* raw, int run_entry) {
    if (!handle || !raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);
    std::string yaml_source = exec_chunks_to_yaml(code);
    if (yaml_source.empty()) {
        LOG_WARNING("aiml_ctx_exec_code: empty bundle — nothing to compile");
        return 0;
    }

    auto* saved_ctx     = g_active_context;
    auto* saved_ee      = g_active_ee;
    auto* saved_fptable = g_fptable_current;

    // The handle is a table entry; the instance fetches the table.
    auto* comp = static_cast<AIMLCompiler*>(aiml_ctx_compiler(handle));
    if (!comp) {
        LOG_ERROR("aiml_ctx_exec_code: unknown context handle");
        return -2;
    }
    int rc = run_entry ? comp->execInContext(yaml_source)
                       : comp->loadInContext(yaml_source);
    // The context remembers what this bundle defined (F29_2-…f):
    // the CompilerContext itself is discarded with the next bundle.
    aiml_ctx_record_symbols(handle);

    g_active_context   = saved_ctx;
    g_active_ee        = saved_ee;
    g_fptable_current  = saved_fptable;
    g_fptable          = nullptr;
    g_fptable_versions = nullptr;
    g_func_names       = nullptr;
    g_func_count       = 0;
    return rc;
}

extern "C" int aiml_exec_code(void* raw) {
    if (!raw) return -2;
    ExecCode* code = static_cast<ExecCode*>(raw);

    // Isolated context: compile code_chunks in a fresh LLVM engine (no shared state)
    if (code->new_context)
        return aiml_isolated_exec(raw);

    // Locate fptable (JIT: engine; AOT: dlsym)
    cache_fptable_globals();

    // ── 1. Code chunks: version-guarded hot-reload ────────────────────────────
    // spec/65 §5b: a refused write permission aborts the run.  Only logging
    // and running on would be the silent variant that F65-30 abolishes.
    bool policy_denied = false;
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

                        // spec/65 F65-28/F65-29: FIRST the permission, THEN the
                        // version decides.  The version is written by the bundle's author — at a
                        // trust boundary it is input, never permission.
                        if (!aiml::clearance::mayReplace()) {
                            LOG_ERROR("clearance-replace: `" + nm + "` exists in the host; a "
                                      "body for it means REPLACING, and this session (clearance " +
                                      std::to_string(aiml::clearance::sessionClearance()) +
                                      ") has replace_level " +
                                      std::to_string(aiml::clearance::replaceLevel()) +
                                      " — spec/65 F65-28.  A `version:` in the bundle does not grant "
                                      "the right (F65-29).");
                            policy_denied = true;
                            return;
                        }

                        if (bundle_ver <= cur_ver) {
                            // F65-30: in a restricted session this is an ERROR, not a
                            // skip — otherwise the script would silently get the host's
                            // function.  Shadowing must not happen silently at a boundary;
                            // it is a surprise in correctness before it is
                            // one in security.
                            if (aiml::clearance::restricted()) {
                                LOG_ERROR("clearance-collision: `" + nm + "` already exists in the "
                                          "host (live v" + std::to_string(cur_ver) +
                                          ", bundle v" + std::to_string(bundle_ver) +
                                          ").  SKIPPING the definition would silently give the script "
                                          "the host's symbol — "
                                          "spec/65 F65-30.");
                                policy_denied = true;
                                return;
                            }
                            LOG_INFO("sys.exec: skip " + nm +
                                     " (v" + std::to_string(bundle_ver) +
                                     " <= live v" + std::to_string(cur_ver) + ")");
                            return;
                        }
                        LOG_INFO("sys.exec: hot-reload " + nm +
                                 " v" + std::to_string(cur_ver) +
                                 " → v" + std::to_string(bundle_ver));

                        uint64_t new_addr = compileYamlChunk(cc, nm);
                        if (new_addr) {
                            if (fidx >= 0) {
                                // Function in fptable: atomic pointer swap
                                aiml_fptable_update(fidx, (int)bundle_ver, (void*)new_addr);
                            } else {
                                // Class/model: no fptable entry — bump compiled_version so
                                // subsequent sys.exec calls see the updated version and skip.
                                table.at(nm).compiled_version = bundle_ver;
                                LOG_INFO("sys.exec: " + key + " '" + nm +
                                         "' compiled_version → " + std::to_string(bundle_ver));
                            }
                        } else {
                            LOG_WARNING("sys.exec: hot-compile failed for " + nm +
                                        " — stays at v" + std::to_string(cur_ver));
                        }
                    } else {
                        // spec/65 F65-26: a NEW name gets into the host's table here
                        // (only possible at all in the shared context, F65-31).  That is the
                        // act that `create_level` governs — an own, isolated definition, on the
                        // other hand, is free, because it never reaches the host.
                        if (!aiml::clearance::mayCreate()) {
                            LOG_ERROR("clearance-create: `" + nm + "` would be a NEW symbol in "
                                      "the host's table; this session (clearance " +
                                      std::to_string(aiml::clearance::sessionClearance()) +
                                      ") has create_level " +
                                      std::to_string(aiml::clearance::createLevel()) +
                                      " — spec/65 F65-26.");
                            policy_denied = true;
                            return;
                        }
                        LOG_WARNING("sys.exec: new " + key + " '" + nm +
                                    "' — dynamic loading not yet supported");
                    }
                };

                tryHotReload("def",   g_active_context->functions);
                tryHotReload("class", g_active_context->classes);
                tryHotReload("model", g_active_context->classes);  // model: also in classes map
                // enums: baked immediates, no hot-reload

            } catch (...) {}
        }
        // -3 as for the refused clearance (F65-4): a refusal is a
        // refusal, no matter on which side of the boundary it arises.
        if (policy_denied) return -3;
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

// spec/65 F65-36: `sys.exec(pkg, clearance)` — the same execution, but with a clearance
// that applies ONLY to this run.  A host runs scripts for different users; a
// clearance remembered process-wide could not express that.
//
// `pushSessionClearance` only tightens (higher number = fewer rights), so that a call
// cannot take more than the process default allows.  It is reset in every case,
// even if the run fails — otherwise the restriction would stay attached to the host.
// spec/65 F65-26/F65-36: the policy of ONE session — three values, passed together, in
// the order of the F65-26 table.  It applies for the duration of this run and is
// reset afterwards, even on an exception: a policy that sticks is a
// property of the process and not of the session.
extern "C" int aiml_exec_code_policy(void* raw, int clearance, int create_level,
                                     int replace_level) {
    const aiml::clearance::Policy before =
        aiml::clearance::pushSessionPolicy(clearance, create_level, replace_level);
    int rc;
    try {
        rc = aiml_exec_code(raw);
    } catch (...) {
        aiml::clearance::popSessionPolicy(before);
        throw;
    }
    aiml::clearance::popSessionPolicy(before);
    return rc;
}
