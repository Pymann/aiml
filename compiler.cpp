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
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Object/ObjectFile.h>
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
#include "src/core/ir_generator.h"
#include "src/commands/registry.h"
#include "src/core/ast_parser.h"
#include "src/core/ast_compiler.h"

// ... (Functions compile, compileToObject, etc.)


void AIMLCompiler::compile(const std::string& filename, const std::string& outputFilename) {
    LOG_DEBUG("Compiling " + filename);
    YAML::Node config = YAML::LoadFile(filename);

    llvm::IRBuilder<> builder(*context_);

    // 1. Create __aiml_init Function (Void, No Args)
    // This function will hold all top-level executable code.
    llvm::FunctionType* initFuncType = llvm::FunctionType::get(builder.getVoidTy(), false);
    llvm::Function* initFunc = llvm::Function::Create(initFuncType, llvm::Function::InternalLinkage, "__aiml_init", *module_);
    
    llvm::BasicBlock* initEntry = llvm::BasicBlock::Create(*context_, "entry", initFunc);
    builder.SetInsertPoint(initEntry);

    // 2. Initialize Compiler Context
    // builder points to __aiml_init entry block.
    CompilerContext ctx(*context_, *module_, builder);
    ctx.compilerPath = compilerPath;
    ctx.ir_gen = AIML::createLLVMGenerator(ctx);
    
    // 3. Initialize Command Registry and Callback
    CommandRegistry registry;

    // Define Recursive Compile Callback (Legacy, used by inner blocks for now)
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

    // Pre-declare globals so sys commands can reference them during compilation
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());

    // 4. Compile Root Nodes (Legacy Pipeline first for type registration)
    // Run the old legacy pipeline first so struct types are registered
    // before the AST compiler attempts to resolve them.
    ctx.params.compileCallback(ctx, config);

    // 5. Compile Root Nodes (AST Pipeline)
    AIML::AST::Parser astParser;
    auto programAST = astParser.parseProgram(config);
    
    if (programAST) {
        AIML::AST::Compiler astCompiler(ctx);
        astCompiler.compile(programAST.get());
    }
    
    // Propagate linked objects
    this->linkedObjects = ctx.objectFilesToLink;
    
    // 5. Terminate __aiml_init
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateRetVoid();
    }

    // 6. Output IR (Module Checkpoint)
    // Note: We do NOT generate 'main' here anymore. 
    // 'main' should be generated only if this is the entry point file.
    
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        LOG_ERROR("Could not open file: " + EC.message());
        return;
    }
    module_->print(dest, nullptr);
    // Save State for JIT/Linker
    linkedObjects = ctx.objectFilesToLink;
    entryPointName = ctx.config.entryPointName;

    LOG_DEBUG("Compiled to " + outputFilename);
}

void AIMLCompiler::generateEntryPoint() {
    // This method generates the 'int main(int argc, char** argv)' entry point.
    // It should only be called if we are building an executable or running JIT.
    
    LOG_DEBUG("Generating Entry Point...");
    llvm::IRBuilder<> builder(*context_);
    
    // 1. Check for User-Defined 'main' (or custom entry point)
    llvm::Function* userMain = module_->getFunction(entryPointName);
    if (userMain) {
        LOG_DEBUG("User defined entry point found. Renaming to __aiml_user_" + entryPointName);
        userMain->setName("__aiml_user_" + entryPointName);
    }

    // 2. Create Standard Main: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = { builder.getInt32Ty(), builder.getInt8Ty()->getPointerTo()->getPointerTo() };
    llvm::FunctionType* funcType = llvm::FunctionType::get(builder.getInt32Ty(), mainArgs, false);
    llvm::Function* mainFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main", *module_);
    
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", mainFunc);
    builder.SetInsertPoint(entry);
    
    // 3. Setup Globals: __argc, __argv
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    llvm::GlobalVariable* argcGlobal = module_->getNamedGlobal("__argc");
    argcGlobal->setLinkage(llvm::GlobalValue::InternalLinkage);
    argcGlobal->setInitializer(builder.getInt32(0));
    
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());
    llvm::GlobalVariable* argvGlobal = module_->getNamedGlobal("__argv");
    argvGlobal->setLinkage(llvm::GlobalValue::InternalLinkage);
    argvGlobal->setInitializer(llvm::ConstantPointerNull::get(builder.getInt8Ty()->getPointerTo()->getPointerTo()));

    // Store Arguments
    llvm::Function::arg_iterator args = mainFunc->arg_begin();
    llvm::Value* argcVal = args++;
    argcVal->setName("argc");
    llvm::Value* argvVal = args++;
    argvVal->setName("argv");
    
    builder.CreateStore(argcVal, argcGlobal);
    builder.CreateStore(argvVal, argvGlobal);
    
    // 4. Call __aiml_init()
    llvm::Function* initFunc = module_->getFunction("__aiml_init");
    if (initFunc) {
        builder.CreateCall(initFunc);
    } else {
        LOG_WARNING("__aiml_init not found, skipping initialization.");
    }
    
    // 5. Call User Main (if exists)
    if (userMain) {
        // Check signature of user main
        // We support:
        // - void main()
        // - int main()
        // - int main(int argc, char** argv)
        // - void main(int argc, char** argv)
        
        // Actually, LLVM Function Type check is strict.
        llvm::FunctionType* userType = userMain->getFunctionType();
        size_t numParams = userType->getNumParams();
        
        std::vector<llvm::Value*> callArgs;
        if (numParams == 2) {
             callArgs.push_back(argcVal);
             callArgs.push_back(argvVal);
        }
        // If 0 params, callArgs empty.
        
        llvm::Value* result = builder.CreateCall(userMain, callArgs);
        
        // Return Result
        if (result->getType()->isIntegerTy()) {
            builder.CreateRet(result);
        } else {
             builder.CreateRet(builder.getInt32(0));
        }
        
    } else {
        // No user main, just return 0 (Script mode finished)
        builder.CreateRet(builder.getInt32(0));
    }
    
    LOG_DEBUG("Entry Point generated.");
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

    // Load External Object Files (Imports) into Engine
    for (const auto& objPath : linkedObjects) {
        auto bufferOrErr = llvm::MemoryBuffer::getFile(objPath);
    }
    // Load External Object Files (Imports) into Engine
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> keptBuffers;
    for (const auto& objPath : linkedObjects) {
        auto bufferOrErr = llvm::MemoryBuffer::getFile(objPath);
        if (std::error_code EC = bufferOrErr.getError()) {
             LOG_ERROR("Failed to load object file for JIT: " + objPath + " -> " + EC.message());
        } else {
             std::unique_ptr<llvm::MemoryBuffer> buf = std::move(bufferOrErr.get());
             llvm::MemoryBufferRef bufRef = buf->getMemBufferRef();
             auto objOrErr = llvm::object::ObjectFile::createObjectFile(bufRef);
             
             if (!objOrErr) {
                 LOG_ERROR("Failed to create ObjectFile from buffer: " + objPath);
                 llvm::logAllUnhandledErrors(objOrErr.takeError(), llvm::errs(), "[JIT Error] ");
             } else {
                 engine->addObjectFile(std::move(objOrErr.get()));
                 keptBuffers.push_back(std::move(buf));
                 LOG_DEBUG("Loaded object file into Engine: " + objPath);
             }
        }
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
