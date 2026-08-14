// Compiler header
#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <unordered_set>
#include "logger.h"

// Forward declarations
namespace llvm {
    class Module;
    class LLVMContext;
    class IRBuilderBase;
}

namespace YAML {
    class Node;
}

namespace llvm {
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

    // Convenience: compileFromString + generateEntryPoint + jitRun.
    int compileAndRun(const std::string& yaml_source);

    // Compile yaml_source and return the address of funcName (0 on failure).
    // For classes/models (no callable function), returns 1 as a success sentinel.
    // The internal JIT engine is kept alive as long as this AIMLCompiler exists.
    // Used by compileYamlChunk in exec_impl.cpp for hot-reload.
    uint64_t compileChunkAndGetAddress(const std::string& yaml_source,
                                        const std::string& funcName);
    
    // Dependency & Caching Methods
    void resolveImport(const std::string& path);
    // Compute the full semantic cache key for an import: source hash +
    // transitive imports + sibling imports + library mode + compiler binary id.
    // outObjPath is set to the keyed object path (always; whether hit or miss).
    bool checkCache(const std::string& sourcePath,
                    const std::vector<std::string>& siblings,
                    bool libMode,
                    std::string& outCacheKey,
                    std::string& outObjPath);
    // Helpers
    std::string computeImportCacheKey(const std::string& sourcePath,
                                      const std::vector<std::string>& siblings,
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
    std::set<std::string> allPrefabDecls;
    std::set<std::string> allPrefabUses;
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
    // Output directory for import .o files (used when noCache=true).
    std::string outputDir;
    // Sibling import files for cross-import type resolution.
    // Set by the parent compiler so sub-compilers can header-only parse them.
    std::vector<std::string> siblingImports;
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
};
