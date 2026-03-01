#include "compiler.h"
#include "exec_impl.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>
#include <sys/time.h>
#include <unistd.h>
#include "src/sys/time_runtime.h"
#include "src/sys/process_runtime.h"
#include "src/core/path_runtime.h"
#include "src/sys/net_runtime.h"
#include "src/sys/db_runtime.h"

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

// Core compiler headers (pulled up so CompilerContext is available in
// the constructor, destructor and all methods below)
#include "src/core/context.h"
#include "src/core/ir_generator.h"
#include "src/core/ast_builder.h"
#include "src/core/ast_compiler.h"
#include <unordered_set>

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
    // Destroy in reverse-construction order:
    // CompilerContext holds refs to module + builder → destroy first.
    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;
}

const llvm::Module& AIMLCompiler::getModule() const {
    return *module_;
}

// ... (Functions compile, compileToObject, etc.)


void AIMLCompiler::compile(const std::string& filename, const std::string& outputFilename) {
    LOG_DEBUG("Compiling " + filename);
    YAML::Node config = YAML::LoadFile(filename);

    // 1. (Legacy __aiml_init creation removed. AST::Compiler now handles it)

    // 2. Initialize Compiler Context — heap-allocated so it survives into jitRun()
    //    allowing aiml_exec_code to access the symbol table during JIT execution.
    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;

    builder_inst_ = new llvm::IRBuilder<>(*context_);
    auto& builder  = *static_cast<llvm::IRBuilder<>*>(builder_inst_);

    compiler_ctx_ = new CompilerContext(*context_, *module_, builder);
    CompilerContext& ctx = *static_cast<CompilerContext*>(compiler_ctx_);

    ctx.compilerPath = compilerPath;
    ctx.ir_gen = AIML::createLLVMGenerator(ctx);
    
    // 3. Setup Globals: __argc, __argv for sys commands
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());

    ctx.resolveImportCallback = [this, &ctx](const std::string& path) {
        static std::unordered_set<std::string> parsedImports;
        if (parsedImports.count(path)) return;
        parsedImports.insert(path);
        
        this->resolveImport(path);
        
        // Important: also copy over the linked objects discovered!
        for (const auto& obj : this->linkedObjects) {
             if (std::find(ctx.objectFilesToLink.begin(), ctx.objectFilesToLink.end(), obj) == ctx.objectFilesToLink.end()) {
                 ctx.objectFilesToLink.push_back(obj);
             }
        }
        
        // Header-Only Mode for current compiler: Parse the import for signatures!
        std::string prevNs = ctx.currentNamespace;
        bool prevHeaderOnly = ctx.headerOnlyMode;
        ctx.headerOnlyMode = true;
        
        AIML::AST::Builder subBuilder;
        std::ifstream fin(path);
        if (fin.is_open()) {
            try {
                YAML::Parser parser(fin);
                parser.HandleNextDocument(subBuilder);
                auto subProg = subBuilder.takeProgram();
                if (subProg) {
                    AIML::AST::Compiler subCompiler(ctx);
                    subCompiler.registerTypes(subProg.get());
                    for (const auto& decl : subProg->declarations) {
                        if (auto func = dynamic_cast<AIML::AST::FunctionDecl*>(decl.get())) {
                            subCompiler.visitFunction(func);
                        }
                    }
                }
            } catch (...) {
                LOG_ERROR("Failed to parse import signatures: " + path);
            }
        }
        ctx.currentNamespace = prevNs;
        ctx.headerOnlyMode = prevHeaderOnly;
    };
    
    // 4. Parse YAML to AST & Compile (Two-Pass)
    AIML::AST::Builder astBuilder;
    try {
        std::ifstream fin(filename);
        if (!fin.is_open()) {
             LOG_ERROR("Could not open file: " + filename);
             return;
        }
        YAML::Parser parser(fin);
        parser.HandleNextDocument(astBuilder);

        auto programAST = astBuilder.takeProgram();
        if (programAST) {
            // Assign sourceNode to each top-level declaration for sys.bundle.
            // Load the file as a YAML tree and zip entries with AST nodes.
            try {
                YAML::Node rawSeq = YAML::LoadFile(filename);
                if (rawSeq.IsSequence()) {
                    size_t n = std::min(rawSeq.size(), programAST->declarations.size());
                    for (size_t i = 0; i < n; ++i)
                        programAST->declarations[i]->sourceNode = rawSeq[i];
                }
            } catch (...) {}

            AIML::AST::Compiler astCompiler(ctx);

            // Pass 1: Global Type and Signature Registration
            astCompiler.registerTypes(programAST.get());

            // Pass 2: Full Code Generation
            astCompiler.compile(programAST.get());
        } else {
            LOG_ERROR("AST Builder failed to produce a valid program.");
        }
    } catch (const YAML::Exception& e) {
        LOG_ERROR("YAML Parsing Error during AST Build: " + std::string(e.what()));
    }
    
    // Propagate linked objects from pass 1
    this->linkedObjects = ctx.objectFilesToLink;
    
    // 5. (Legacy __aiml_init terminator removed)
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

// ── compileFromString ─────────────────────────────────────────────────────────
// Like compile() but parses from an in-memory YAML string instead of a file.
// No IR file is written; the compiled module stays in module_ for jitRun().
void AIMLCompiler::compileFromString(const std::string& yaml_source) {
    LOG_DEBUG("compileFromString: " + std::to_string(yaml_source.size()) + " bytes");

    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;

    builder_inst_ = new llvm::IRBuilder<>(*context_);
    auto& builder  = *static_cast<llvm::IRBuilder<>*>(builder_inst_);

    compiler_ctx_ = new CompilerContext(*context_, *module_, builder);
    CompilerContext& ctx = *static_cast<CompilerContext*>(compiler_ctx_);

    ctx.compilerPath = compilerPath;
    ctx.ir_gen = AIML::createLLVMGenerator(ctx);

    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());

    try {
        YAML::Node rawSeq = YAML::Load(yaml_source);

        AIML::AST::Builder astBuilder;
        // Feed each node to the AST builder via a synthesised YAML stream
        std::istringstream ss(yaml_source);
        YAML::Parser parser(ss);
        parser.HandleNextDocument(astBuilder);

        auto programAST = astBuilder.takeProgram();
        if (programAST) {
            // Assign sourceNodes (zip raw sequence with AST declarations)
            if (rawSeq.IsSequence()) {
                size_t n = std::min(rawSeq.size(), programAST->declarations.size());
                for (size_t i = 0; i < n; ++i)
                    programAST->declarations[i]->sourceNode = rawSeq[i];
            }

            AIML::AST::Compiler astCompiler(ctx);
            astCompiler.registerTypes(programAST.get());
            astCompiler.compile(programAST.get());
        } else {
            LOG_ERROR("compileFromString: AST builder produced no program");
        }
    } catch (const YAML::Exception& e) {
        LOG_ERROR("compileFromString: YAML parse error: " + std::string(e.what()));
    }

    linkedObjects = ctx.objectFilesToLink;
    entryPointName = ctx.config.entryPointName;
}

int AIMLCompiler::compileAndRun(const std::string& yaml_source) {
    compileFromString(yaml_source);
    generateEntryPoint();
    return jitRun();
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

    // 3.5. Disable stdout buffering: setvbuf(stdout, NULL, _IONBF, 0)
    {
        llvm::Type* i8p = builder.getInt8Ty()->getPointerTo();
        llvm::GlobalVariable* stdoutGV = module_->getGlobalVariable("stdout");
        if (!stdoutGV) {
            stdoutGV = new llvm::GlobalVariable(
                *module_, i8p, false,
                llvm::GlobalValue::ExternalLinkage,
                nullptr, "stdout");
        }
        llvm::Value* stdoutVal = builder.CreateLoad(i8p, stdoutGV, "stdout_ptr");
        llvm::FunctionType* setvbufTy = llvm::FunctionType::get(
            builder.getInt32Ty(),
            {i8p, i8p, builder.getInt32Ty(), builder.getInt64Ty()},
            false);
        llvm::FunctionCallee setvbufFn = module_->getOrInsertFunction("setvbuf", setvbufTy);
        builder.CreateCall(setvbufFn, {
            stdoutVal,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
            builder.getInt32(2),   // _IONBF
            builder.getInt64(0)    // size (ignored for _IONBF)
        });
    }

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
    llvm::sys::DynamicLibrary::AddSymbol("strlen", (void*)(intptr_t)strlen);
    llvm::sys::DynamicLibrary::AddSymbol("strcpy", (void*)(intptr_t)strcpy);
    llvm::sys::DynamicLibrary::AddSymbol("strcat", (void*)(intptr_t)strcat);
    llvm::sys::DynamicLibrary::AddSymbol("sprintf", (void*)(intptr_t)sprintf);
    llvm::sys::DynamicLibrary::AddSymbol("strcmp", (void*)(intptr_t)strcmp);
    llvm::sys::DynamicLibrary::AddSymbol("fdopen", (void*)(intptr_t)fdopen);
    
    // Runtime Library Symbols
    llvm::sys::DynamicLibrary::AddSymbol("time_now_ms", (void*)(intptr_t)time_now_ms);
    llvm::sys::DynamicLibrary::AddSymbol("time_sleep_ms", (void*)(intptr_t)time_sleep_ms);
    llvm::sys::DynamicLibrary::AddSymbol("sys_exe_runtime", (void*)(intptr_t)sys_exe_runtime);

    // Path runtime symbols
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_intern",      (void*)(intptr_t)aiml_path_intern);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_join",        (void*)(intptr_t)aiml_path_join);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_parent",      (void*)(intptr_t)aiml_path_parent);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_name",        (void*)(intptr_t)aiml_path_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_stem",        (void*)(intptr_t)aiml_path_stem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_extension",   (void*)(intptr_t)aiml_path_extension);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_root",        (void*)(intptr_t)aiml_path_root);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_absolute", (void*)(intptr_t)aiml_path_is_absolute);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_relative", (void*)(intptr_t)aiml_path_is_relative);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_empty",    (void*)(intptr_t)aiml_path_is_empty);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_depth",       (void*)(intptr_t)aiml_path_depth);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_relative_to", (void*)(intptr_t)aiml_path_relative_to);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_safe_join",   (void*)(intptr_t)aiml_path_safe_join);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_to_string",   (void*)(intptr_t)aiml_path_to_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_home",        (void*)(intptr_t)aiml_path_home);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_temp_dir",    (void*)(intptr_t)aiml_path_temp_dir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_cwd",         (void*)(intptr_t)aiml_path_cwd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_current_exe", (void*)(intptr_t)aiml_path_current_exe);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_config_dir",  (void*)(intptr_t)aiml_path_config_dir);
    // IO / filesystem symbols
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_exists",     (void*)(intptr_t)aiml_io_exists);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_is_dir",     (void*)(intptr_t)aiml_io_is_dir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_is_file",    (void*)(intptr_t)aiml_io_is_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_file_size",  (void*)(intptr_t)aiml_io_file_size);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_mkdir",      (void*)(intptr_t)aiml_io_mkdir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_mkdir_all",  (void*)(intptr_t)aiml_io_mkdir_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_remove",     (void*)(intptr_t)aiml_io_remove);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_remove_all", (void*)(intptr_t)aiml_io_remove_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_rename",     (void*)(intptr_t)aiml_io_rename);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_file",      (void*)(intptr_t)aiml_io_read_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_write_file",     (void*)(intptr_t)aiml_io_write_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_list_dir_fill",  (void*)(intptr_t)aiml_io_list_dir_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_read",       (void*)(intptr_t)aiml_io_can_read);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_write",      (void*)(intptr_t)aiml_io_can_write);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_exec",       (void*)(intptr_t)aiml_io_can_exec);

    // net namespace (POSIX sockets)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_socket",       (void*)(intptr_t)aiml_net_socket);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_connect",      (void*)(intptr_t)aiml_net_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_bind",         (void*)(intptr_t)aiml_net_bind);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_listen",       (void*)(intptr_t)aiml_net_listen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_accept",       (void*)(intptr_t)aiml_net_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_send",         (void*)(intptr_t)aiml_net_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_recv",         (void*)(intptr_t)aiml_net_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_send_string",  (void*)(intptr_t)aiml_net_send_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_recv_string",  (void*)(intptr_t)aiml_net_recv_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_close",        (void*)(intptr_t)aiml_net_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_fd",           (void*)(intptr_t)aiml_net_fd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_set_timeout",  (void*)(intptr_t)aiml_net_set_timeout);

    // db namespace (AionDB in-memory store)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open",    (void*)(intptr_t)aiml_db_store_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_insert",  (void*)(intptr_t)aiml_db_store_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get",     (void*)(intptr_t)aiml_db_store_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_delete",  (void*)(intptr_t)aiml_db_store_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_len",     (void*)(intptr_t)aiml_db_store_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open",       (void*)(intptr_t)aiml_db_kv_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_set",        (void*)(intptr_t)aiml_db_kv_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_get",        (void*)(intptr_t)aiml_db_kv_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_has",        (void*)(intptr_t)aiml_db_kv_has);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_len",        (void*)(intptr_t)aiml_db_kv_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_open",      (void*)(intptr_t)aiml_db_vec_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_insert",    (void*)(intptr_t)aiml_db_vec_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_search",    (void*)(intptr_t)aiml_db_vec_search);

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

    // Expose CompilerContext + engine to aiml_exec_code (called from JIT)
    aiml_set_active_context(compiler_ctx_, engine);
    llvm::GenericValue result = engine->runFunction(mainFunc, args);
    aiml_set_active_context(nullptr, nullptr);

    LOG_DEBUG("Main finished. Result: " + std::to_string(result.IntVal.getSExtValue()));

    return result.IntVal.getSExtValue();
}

llvm::Function* AIMLCompiler::getPrintfPrototype(llvm::Module& module, llvm::IRBuilderBase& builder) {
    std::vector<llvm::Type*> printfArgs;
    printfArgs.push_back(builder.getInt8Ty()->getPointerTo()); // char* format
    llvm::FunctionType* printfType = llvm::FunctionType::get(builder.getInt32Ty(), printfArgs, true); // true = varargs
    
    return llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module);
}
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <yaml-cpp/yaml.h>
#include "compiler.h"

namespace fs = std::filesystem;

static std::string getCacheDir() {
    const char* envCache = std::getenv("AIML_CACHE");
    if (envCache && std::strlen(envCache) > 0) {
        return std::string(envCache);
    }
    return "__aiml_cache";
}

static std::string computeSHA512(const std::string& path) {
    std::string command = "sha512sum \"" + path + "\" 2>/dev/null";
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    // sha512sum outputs: "<hash>  <filename>"
    size_t spacePos = result.find(' ');
    if (spacePos != std::string::npos) {
        return result.substr(0, spacePos);
    }
    return result;
}

bool AIMLCompiler::checkCache(const std::string& sourcePath, std::string& outHash) {
    if (!fs::exists(sourcePath)) {
        LOG_ERROR("Import source not found: " + sourcePath);
        return false;
    }
    
    outHash = computeSHA512(sourcePath);
    if (outHash.empty()) {
        LOG_WARNING("Failed to compute SHA512 for " + sourcePath + ". Falling back to modification time.");
        auto ftime = fs::last_write_time(sourcePath);
        auto fsize = fs::file_size(sourcePath);
        std::stringstream ss;
        ss << ftime.time_since_epoch().count() << "_" << fsize;
        std::hash<std::string> hasher;
        outHash = std::to_string(hasher(ss.str()));
    }
    
    std::string cacheDir = getCacheDir();
    if (!fs::exists(cacheDir)) {
        fs::create_directory(cacheDir);
    }
    
    std::string manifestPath = cacheDir + "/compilations.yaml";
    YAML::Node manifest;
    if (fs::exists(manifestPath)) {
        try {
            manifest = YAML::LoadFile(manifestPath);
        } catch (...) {}
    }
    
    if (manifest[sourcePath] && manifest[sourcePath].as<std::string>() == outHash) {
        // Cache hit. Ensure object file exists
        std::string objPath = cacheDir + "/" + fs::path(sourcePath).filename().string() + ".o";
        if (fs::exists(objPath)) return true;
    }
    
    return false;
}

void AIMLCompiler::resolveImport(const std::string& path) {
    std::string outHash;
    if (checkCache(path, outHash)) {
        LOG_INFO("Cache HIT for import: " + path);
        std::string objPath = getCacheDir() + "/" + fs::path(path).filename().string() + ".o";
        linkedObjects.push_back(objPath);
        return;
    }
    
    LOG_INFO("Cache MISS for import: " + path + ". Compiling...");
    
    // Create isolated compiler instance for the imported file
    AIMLCompiler subCompiler;
    subCompiler.compilerPath = this->compilerPath;
    
    std::string objPath = getCacheDir() + "/" + fs::path(path).filename().string() + ".o";
    subCompiler.compile(path, "/dev/null");
    subCompiler.compileToObject(path, objPath);
    
    // Update manifest
    std::string manifestPath = getCacheDir() + "/compilations.yaml";
    YAML::Node manifest;
    if (fs::exists(manifestPath)) {
        try { manifest = YAML::LoadFile(manifestPath); } catch (...) {}
    }
    manifest[path] = outHash;
    
    std::ofstream fout(manifestPath);
    fout << manifest;
    
    linkedObjects.push_back(objPath);
}
