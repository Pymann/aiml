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

    // Generates the standard C main() entry point wrapper.
    // Call this ONLY when building an executable or running JIT.
    void generateEntryPoint();

    void compileToObject(const std::string& filename, const std::string& outputFilename);
    int jitRun(); // JIT execution method

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
    std::string compilerPath;
    std::string entryPointName = "main";
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
