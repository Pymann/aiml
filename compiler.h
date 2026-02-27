// Compiler header
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
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
    class AllocaInst; // Forward declare AllocaInst
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
    
    // Dependency & Caching Methods
    void resolveImport(const std::string& path);
    bool checkCache(const std::string& sourcePath, std::string& outHash);
    
    std::vector<std::string> linkedObjects;
    std::string compilerPath;
    std::string entryPointName = "main";

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
};
