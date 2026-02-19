#include "compiler.h"
#include <iostream>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <sys/time.h>
#include <unistd.h>
#include "src/sys/time_runtime.h"
#include "src/sys/process_runtime.h"

// LLVM includes
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/DynamicLibrary.h>
#include <optional>

AIMLCompiler::AIMLCompiler() {
    context_ = std::make_unique<llvm::LLVMContext>();
    module_ = std::make_unique<llvm::Module>("AIML_Module", *context_);

    // Initialize Target
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    
    std::string error;
    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    
    if (!target) {
        LOG_ERROR("Failed to lookup target: " + error);
        return;
    }
    
    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    auto targetMachine = target->createTargetMachine(targetTriple, "generic", "", opt, rm);
    
    module_->setDataLayout(targetMachine->createDataLayout());
    module_->setTargetTriple(targetTriple);
}

AIMLCompiler::~AIMLCompiler() {
}

const llvm::Module& AIMLCompiler::getModule() const {
    return *module_;
}

#include "src/core/context.h"
#include "src/commands/registry.h"

// ... (Functions compile, compileToObject, etc.)

void AIMLCompiler::compile(const std::string& filename, const std::string& outputFilename) {
    LOG_DEBUG("Compiling " + filename);
    YAML::Node config = YAML::LoadFile(filename);

    llvm::IRBuilder<> builder(*context_);

    // Create main function: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = { builder.getInt32Ty(), builder.getInt8Ty()->getPointerTo()->getPointerTo() };
    llvm::FunctionType* funcType = llvm::FunctionType::get(builder.getInt32Ty(), mainArgs, false);
    llvm::Function* mainFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main", *module_);
    
    // Create Globals for argc/argv
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    llvm::GlobalVariable* argcGlobal = module_->getNamedGlobal("__argc");
    argcGlobal->setLinkage(llvm::GlobalValue::InternalLinkage);
    argcGlobal->setInitializer(builder.getInt32(0));
    
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());
    llvm::GlobalVariable* argvGlobal = module_->getNamedGlobal("__argv");
    argvGlobal->setLinkage(llvm::GlobalValue::InternalLinkage);
    argvGlobal->setInitializer(llvm::ConstantPointerNull::get(builder.getInt8Ty()->getPointerTo()->getPointerTo()));

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", mainFunc);
    builder.SetInsertPoint(entry);
    
    // Store argc/argv inputs into globals
    llvm::Function::arg_iterator args = mainFunc->arg_begin();
    llvm::Value* argcVal = args++;
    argcVal->setName("argc");
    llvm::Value* argvVal = args++;
    argvVal->setName("argv");
    
    builder.CreateStore(argcVal, argcGlobal);
    builder.CreateStore(argvVal, argvGlobal);
    
    // Initialize Compiler Context
    CompilerContext ctx(*context_, *module_, builder);
    
    // Set Personality Function
    if (ctx.getPersonalityFunction().getCallee()) {
         mainFunc->setPersonalityFn(llvm::cast<llvm::Constant>(ctx.getPersonalityFunction().getCallee()));
    }
    
    // Initialize Command Registry
    CommandRegistry registry;

    // Define Recursive Compile Callback
    ctx.params.compileCallback = [&](CompilerContext& c, const YAML::Node& nodes) {
        if (nodes.IsSequence()) {
            for (const auto& node : nodes) {
                if (node.IsMap()) {
                    std::string commandName = node.begin()->first.as<std::string>();
                    auto cmd = registry.create(commandName);
                    if (cmd) {
                        cmd->compile(c, node);
                    } else {
                        LOG_ERROR("Unknown command: " + commandName);
                    }
                }
            }
        }
    };

    // Process Root Nodes
    ctx.params.compileCallback(ctx, config);
    
    builder.CreateRet(builder.getInt32(0));

    // Output IR
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        LOG_ERROR("Could not open file: " + EC.message());
        return;
    }
    module_->print(dest, nullptr);
    LOG_DEBUG("Compiled to " + outputFilename);
}

void AIMLCompiler::compileToObject(const std::string& /*filename*/, const std::string& outputFilename) {
    // Reuse compile logic to generate IR in module_
    // TODO: Refactor compile() to separate IR generation from file output to avoid double work or side effects
    
    // IMPLEMENTATION:
    // 1. Initialize targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module_->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        LOG_ERROR("Target not found: " + error);
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    // Set Relocation Model to PIC (Position Independent Code)
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module_->setDataLayout(targetMachine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);

    if (EC) {
        LOG_ERROR("Could not open file: " + EC.message());
        return;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        LOG_ERROR("TargetMachine can't emit a file of this type");
        return;
    }

    pass.run(*module_);
    LOG_INFO("Object code written to " + outputFilename);
}

int AIMLCompiler::jitRun() {
    // Targets initialized in constructor

    
    // EXTREMELY IMPORTANT: Load host process symbols so JIT can find malloc, printf, etc.
    LOG_DEBUG("LoadLibraryPermanently...");
    bool loadFailed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    if (loadFailed) {
        LOG_ERROR("LoadLibraryPermanently failed!");
        return 1;
    }
    
    // Explicitly add malloc/free
    llvm::sys::DynamicLibrary::AddSymbol("malloc", (void*)(intptr_t)malloc);
    llvm::sys::DynamicLibrary::AddSymbol("free", (void*)(intptr_t)free);
    llvm::sys::DynamicLibrary::AddSymbol("fflush", (void*)(intptr_t)fflush);
    llvm::sys::DynamicLibrary::AddSymbol("fprintf", (void*)(intptr_t)fprintf);
    llvm::sys::DynamicLibrary::AddSymbol("dprintf", (void*)(intptr_t)dprintf);
    llvm::sys::DynamicLibrary::AddSymbol("fprintf", (void*)(intptr_t)fprintf);
    llvm::sys::DynamicLibrary::AddSymbol("dprintf", (void*)(intptr_t)dprintf);
    
    // Runtime Library Symbols
    llvm::sys::DynamicLibrary::AddSymbol("time_now_ms", (void*)(intptr_t)time_now_ms);
    llvm::sys::DynamicLibrary::AddSymbol("time_sleep_ms", (void*)(intptr_t)time_sleep_ms);
    llvm::sys::DynamicLibrary::AddSymbol("sys_exe_runtime", (void*)(intptr_t)sys_exe_runtime);
    
    // llvm::sys::DynamicLibrary::AddSymbol("stderr", (void*)(intptr_t)stderr); // Removed to avoid macro issues for now

    std::string errStr;
    if (!module_) {
        LOG_ERROR("Module is empty. Did you compile first?");
        return 1;
    }

    // Set Target Triple
    module_->setTargetTriple(llvm::sys::getDefaultTargetTriple());

    LOG_DEBUG("Creating EngineBuilder...");
    llvm::EngineBuilder builder(std::move(module_));
    builder.setErrorStr(&errStr);
    builder.setEngineKind(llvm::EngineKind::JIT);
    
    llvm::ExecutionEngine* engine = builder.create();
    if (!engine) {
        LOG_ERROR("Failed to create ExecutionEngine: " + errStr);
        return 1;
    }
    LOG_DEBUG("Engine created.");

    llvm::Function* mainFunc = engine->FindFunctionNamed("main");
    if (!mainFunc) {
        LOG_ERROR("Function 'main' not found in module.");
        return 1;
    }
    LOG_DEBUG("Found main. Running...");

    // Explicitly add printf
    llvm::sys::DynamicLibrary::AddSymbol("printf", (void*)(intptr_t)printf);

    LOG_DEBUG("Running main with fixed argv...");
    
    // Create dummy argv for JIT safety
    // We need to keep the memory alive during execution
    static char* dummyArgv[] = { (char*)"aiml_jit", nullptr };
    
    std::vector<llvm::GenericValue> args;
    args.push_back(llvm::GenericValue()); // argc
    args[0].IntVal = llvm::APInt(32, 1); 
    args.push_back(llvm::GenericValue()); // argv
    args[1].PointerVal = dummyArgv;

    llvm::GenericValue result = engine->runFunction(mainFunc, args);
    LOG_DEBUG("Main finished. Result: " + std::to_string(result.IntVal.getSExtValue()));
    
    return result.IntVal.getSExtValue();
}

llvm::Function* AIMLCompiler::getPrintfPrototype(llvm::Module& module, llvm::IRBuilderBase& builder) {
    std::vector<llvm::Type*> printfArgs;
    printfArgs.push_back(builder.getInt8Ty()->getPointerTo()); // char* format
    llvm::FunctionType* printfType = llvm::FunctionType::get(builder.getInt32Ty(), printfArgs, true); // true = varargs
    
    return llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module);
}
