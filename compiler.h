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
    void compileToObject(const std::string& filename, const std::string& outputFilename); // New method
    const llvm::Module& getModule() const; // Helper for JIT
    int jitRun(); // JIT execution method

private:
    void generateIR(const YAML::Node& node);
    llvm::Function* getPrintfPrototype(llvm::Module& module, llvm::IRBuilderBase& builder);
    
    // LLVM Context & Module
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
};
