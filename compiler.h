// Compiler header
#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <unordered_set>
#include "logger.h"
#include "src/core/prefab_meta.h"

// Forward declarations
namespace llvm {
    class Module;
    class LLVMContext;
    class IRBuilderBase;
}

namespace YAML {
    class Node;
}

class CompilerContext;

namespace llvm {
    class MemoryBuffer;
    class Function;
    class Value;
    class Type;
    class AllocaInst;
    class ExecutionEngine;
}

class AIMLCompiler {
public:
    AIMLCompiler();
    ~AIMLCompiler();

    void compile(const std::string& filename, const std::string& outputFilename);
    const llvm::Module& getModule() const; // Helper for JIT

    // Override the target before compile(). Used for cross-compilation, e.g.
    // --target wasm32 for emcc consumers. Defaults to the native triple.
    void setTargetTriple(const std::string& triple);

    // Pass --asset-roots=a,b,c through to the AssetScanner before compile.
    // Parsed lazily at compile-start; empty roots disables the scan.
    void setAssetRoots(const std::string& commaList);

    // Generates the standard C main() entry point wrapper.
    // Call this ONLY when building an executable or running JIT.
    void generateEntryPoint();

    void compileToObject(const std::string& filename, const std::string& outputFilename);
    // Write the current module as text-format LLVM IR (.ll) to outputFilename.
    // Used by --emit-main mode to re-emit IR after generateEntryPoint() so a
    // single .ll file is self-contained for external toolchains (emcc, …).
    void saveModule(const std::string& outputFilename);
    int jitRun();                       // JIT execution (dummy argv)
    int jitRun(int argc, char** argv);  // JIT execution with real argv passed to AIML program

    // Compile a complete AIML program from an in-memory YAML string (no file I/O).
    // Used by aiml_isolated_exec to run Code objects in a fresh LLVM context.
    void compileFromString(const std::string& yaml_source);

    // spec/29_2 §14a: ONE compiler, MANY bundles — the reusable
    // context.  Every call compiles into a FRESH module (the previous one has
    // passed to its engine) and runs it; the engines stay
    // alive so that the addresses stay valid.  What was defined earlier
    // is only DECLARED in the new module and resolved via DynamicLibrary
    // — the same route as for `sys.lib_load` (§15).
    //
    // The reason is the base cost: a fresh context costs ~96 ms,
    // regardless of how little it does (§15).
    int execInContext(const std::string& yaml_source);
    // spec/29_2 §14b (F29_2-…n): compile and link in, WITHOUT calling the
    // entry point — for a host that wants to check and read foreign source text
    // without running it (cosmos F96-28).
    int loadInContext(const std::string& yaml_source);
  private:
    int compileInContext(const std::string& yaml_source, bool run_entry);
  public:
    // Address of a symbol defined in the context, 0 if unknown.
    uint64_t symbolAddress(const std::string& name) const;

    // spec/29_2 §14b (F29_2-…l): the messages of the LAST `execInContext`.
    // They belong to the context, not to the process — two contexts must not mix
    // their errors, otherwise one submission explains the error
    // of the other.
    const std::vector<std::string>& ctxErrors() const { return ctx_errors_; }
    // spec/21 §3.1: the package table for `@name/…` imports of this compiler
    // ("" = AIML_PACKAGES, then aiml.packages.json); a host sets it per context.
    void setPackageTable(const std::string& path);
    // The symbol-table state of this context.  `void*`, because context.h
    // cannot be included here (cycle) — the reader casts to
    // CompilerContext*.  Read only: whoever changes it changes the
    // context behind its owner's back.
    void* contextState() const { return compiler_ctx_; }

    // Convenience: compileFromString + generateEntryPoint + jitRun.
    int compileAndRun(const std::string& yaml_source);

    // Compile yaml_source and return the address of funcName (0 on failure).
    // For classes/models (no callable function), returns 1 as a success sentinel.
    // The internal JIT engine is kept alive as long as this AIMLCompiler exists.
    // Used by compileYamlChunk in exec_impl.cpp for hot-reload.
    // spec/29_2 §15: compile a library ONCE and make its functions
    // known process-wide as symbols.  The engine stays attached to this
    // compiler so that the addresses stay valid.
    // Returns: number of registered functions, -1 on error.
    int compileLibraryAndRegister(const std::string& yaml_source);

    // Take over declarations only (headerOnlyMode): signatures become known,
    // bodies are NOT generated — that is the cheap part that is repeated per
    // run, while the bodies were compiled only once.
    void compileDeclarationsOnly(const std::string& yaml_source);

    uint64_t compileChunkAndGetAddress(const std::string& yaml_source,
                                        const std::string& funcName);
    
    // Dependency & Caching Methods
    void resolveImport(const std::string& path);
    // Compute the cache key for an import (spec/37 §3): source hash +
    // transitive imports + library mode + compiler binary id.
    // outObjPath is set to the keyed object path (always; whether hit or miss).
    bool checkCache(const std::string& sourcePath,
                    bool libMode,
                    std::string& outCacheKey,
                    std::string& outObjPath);
    // Helpers
    std::string computeImportCacheKey(const std::string& sourcePath,
                                      bool libMode) const;
    void collectTransitiveImports(const std::string& path,
                                  std::set<std::string>& visited) const;
    
    std::vector<std::string> linkedObjects;
    std::vector<std::string> importedSourceFiles;  // source paths of all imports (for .d file)
    // Tier-3 dead-prefab analysis (spec/44_8): union of all `- prefab:` names
    // declared, and all names instantiated via engine.instantiate("<literal>"),
    // across this module AND every (transitive) import.  Merged up through
    // resolveImport() exactly like importedSourceFiles.  At the top-level
    // (non-library) build, prefabs in allPrefabDecls \ allPrefabUses are warned
    // as declared-but-never-instantiated.
    // Name -> file in which the prefab was declared.  Previously only the names:
    // the warning about a dead prefab therefore could not name a file, and
    // in a build across several files "somewhere" is not a location
    // (spec/49 F49-100).
    std::map<std::string, std::string> allPrefabDecls;
    // Tier 4 (spec/44_8): declarations and the methods called on their instances,
    // merged across all modules -- the same construction as allPrefabDecls.
    std::map<std::string, aiml::prefab::Meta> allPrefabMeta;
    std::map<std::string, std::set<std::string>> allPrefabInstMethods;
    std::set<std::string> allPrefabDynamicChild;
    std::map<std::string, std::set<std::string>> allPrefabVisibleManaged;
    std::set<std::string> allPrefabUses;
    std::map<std::string, std::string> allServiceDecls;  // spec/66 §5.6
    std::set<std::string> allServiceUses;
    // Unused-enumerator analysis (spec/49): union across this module AND every
    // (transitive) import of declared enum members (qualified enum → members),
    // referenced members, and each enum's decl location.  Merged up through
    // resolveImport() like allPrefab*.  At the top-level build, members in
    // allEnumDeclMembers \ allEnumUsedMembers are warned as never-referenced.
    std::map<std::string, std::set<std::string>> allEnumDeclMembers;
    std::map<std::string, std::set<std::string>> allEnumUsedMembers;
    std::map<std::string, std::string>            allEnumDeclLoc;
    // `--prefab-report`: analysis-only mode.  Collects prefab wiring facts
    // (declarations, instantiate sites, spawn/payload_spawn wiring, child
    // accesses) across the whole build, prints a grouped report at the top
    // level, and skips object/entry emission.  See spec/44_8_prefab_static.md.
    bool reportPrefabsMode = false;
    std::string prefabReportFilter;   // --prefab-report=<scene>: focus one scene
    std::vector<std::string> allPrefabReport;
    std::string compilerPath;
    std::string entryPointName = "main";
    // Stored target triple (set via setTargetTriple). Empty = use native default.
    // Propagated to sub-compilers spawned for imports so they emit IR for the
    // same target.
    std::string targetTripleStr;
    // Raw --asset-roots=a,b,c value, parsed at compile-start.
    std::string assetRootsStr;
    // When true, resolveImport emits each import as text-format LLVM IR (.ll)
    // instead of a native object (.o). Set automatically by main.cpp when the
    // top-level output is .ll (typically a WASM build via emcc). Native builds
    // keep .o as before so other projects (9rooms etc.) are unaffected.
    bool emitImportsAsLL = false;
    // Library mode: skip __aiml_init, fptable, schema globals (for import-only objects).
    bool libraryMode = false;
    // When true, skip the internal import cache (build tool handles caching).
    bool noCache = false;
    // spec/70 §11.3 — measurement switches.  They change NOTHING in the generated code
    // (F70-93); a measurement with a switch that shifts the result
    // would be worthless.  parseOnly stops after the parse/AST pass.
    bool parseOnly = false;
    bool phaseTimes = false;
    double phaseParseMs = 0.0;    // stream parse AND AST build: one pass (§11.2)
    double phaseReparseMs = 0.0;  // config load + sourceNode tree: the two additional parses
    double phaseIrMs = 0.0;       // registerTypes / synthesize* / astCompiler.compile
    double phaseEmitMs = 0.0;     // writing IR text or object codegen
    double phaseTotalMs = 0.0;    // whole compile() duration, including what is not a phase
    void reportPhaseTimes() const;   // F70-90: counters on stderr
    // Output directory for import .o files (used when noCache=true).
    std::string outputDir;
    // Per-instance guard: each compile root has its own "already-compiled-this-run"
    // set, keyed by full cache_key (not just path). Prevents stale cross-run reuse
    // and avoids the prior process-wide static that ignored semantic context.
    std::shared_ptr<std::unordered_set<std::string>> compiledKeys
        = std::make_shared<std::unordered_set<std::string>>();

    // Per-run guard keyed by normalized source path. Shared across sub-compilers
    // within a single top-level compile so that the lambda's deferredImports
    // push_back is deduped across the entire import graph (replaces the prior
    // process-wide static compiledObjects set, but scoped to one compile root).
    std::shared_ptr<std::unordered_set<std::string>> parsedPathsShared
        = std::make_shared<std::unordered_set<std::string>>();

private:
    void generateIR(const YAML::Node& node);
    llvm::Function* getPrintfPrototype(llvm::Module& module, llvm::IRBuilderBase& builder);

    // LLVM Context & Module
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;

    // Heap-allocated state kept alive across compile() → jitRun()
    // so that aiml_exec_code can access the symbol table during JIT execution.
    // context.h cannot be included here (circular), so void* is used.
    void* builder_inst_ = nullptr;  // owned: llvm::IRBuilder<>*
    void* compiler_ctx_ = nullptr;  // owned: CompilerContext*

    // JIT engine kept alive for compileChunkAndGetAddress (hot-reload chunks).
    // Null for normal compile/jitRun flows (engine is local there).
    llvm::ExecutionEngine* engine_ = nullptr;

    // spec/29_2 §14a: one engine per `execInContext` (it owns its module)
    // and the source text, to repeat its declarations in the next
    // module.  Both lists grow with the number of bundles; everything is
    // freed with the compiler.
    std::vector<llvm::ExecutionEngine*> ctx_engines_;
    std::vector<std::string>            ctx_sources_;

    // Applies to the NEXT compileFromString: declarations only, no
    // bodies (spec/29_2 §15).  As a member and not in the context, because
    // compileFromString creates the context ANEW — a flag that one sets beforehand in the
    // old one is gone afterwards.  This is exactly why `compileDeclarationsOnly`
    // ran into nothing until 2026-10-25.
    bool headerOnlyNext_ = false;
    std::vector<std::string> ctx_errors_;   // §14b F29_2-…l

    // spec/21 §3.1 packages: the host's table ("" = default lookup), and per
    // context which import objects / sources are already in an engine.
    std::string packageTable_;
    std::set<std::string> ctx_loaded_objects_;
    std::set<std::string> ctx_imported_paths_;
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> ctx_obj_buffers_;

    std::shared_ptr<std::vector<std::string>> installImportResolver(
        CompilerContext& ctx,
        std::shared_ptr<std::unordered_set<std::string>> seenFileSizeDiagnostics,
        bool memorySource);
    void addImportObjects(llvm::ExecutionEngine* eng);
};
