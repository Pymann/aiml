#include "compiler.h"
#include "exec_impl.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>
#include <sys/time.h>
#include <unistd.h>
#include <dlfcn.h>
#include "src/sys/time_runtime.h"
#include "src/sys/process_runtime.h"
#include "src/core/path_runtime.h"
#include "src/sys/net_runtime.h"
#include "src/sys/tls_runtime.h"
#include "src/sys/ws_runtime.h"
#include "src/sys/db_runtime.h"
#include "src/sys/http_runtime.h"
#include "src/sys/math_runtime.h"
#include "src/sys/hash_runtime.h"

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
    // engine_ is only set by compileChunkAndGetAddress (hot-reload path).
    // The ExecutionEngine took ownership of module_ via std::move, so do NOT
    // delete module_ separately when engine_ is set.
    if (engine_) { delete engine_; engine_ = nullptr; }
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

uint64_t AIMLCompiler::compileChunkAndGetAddress(const std::string& yaml_source,
                                                   const std::string& funcName) {
    // Compile the YAML (may contain class/model/def nodes — full dependency context).
    compileFromString(yaml_source);

    // Create a JIT engine without running any entry point.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    std::string errStr;
    module_->setTargetTriple(llvm::sys::getDefaultTargetTriple());

    llvm::EngineBuilder eb(std::move(module_));
    eb.setErrorStr(&errStr);
    eb.setEngineKind(llvm::EngineKind::JIT);

    llvm::ExecutionEngine* eng = eb.create();
    if (!eng) {
        LOG_ERROR("compileChunkAndGetAddress: engine creation failed: " + errStr);
        return 0;
    }
    eng->finalizeObject();

    // Try to look up the function by name.
    uint64_t addr = eng->getFunctionAddress(funcName);
    if (addr) {
        engine_ = eng;
        return addr;
    }

    // No callable function found — check if it's a class/model (sentinel 1 = success).
    CompilerContext* ctx = static_cast<CompilerContext*>(compiler_ctx_);
    if (ctx && ctx->classes.count(funcName)) {
        engine_ = eng;
        return 1;  // sentinel: class/model compiled successfully
    }

    LOG_WARNING("compileChunkAndGetAddress: '" + funcName + "' not found in compiled chunk");
    delete eng;
    return 0;
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

    // Pre-load user libraries declared in exodus.json linker.libraries so that
    // extern "C" symbols are resolvable in JIT mode (AOT lets the linker handle it).
    {
        std::string exodusPath = "exodus.json";
        std::ifstream ef(exodusPath);
        if (ef.good()) {
            try {
                YAML::Node ecfg = YAML::LoadFile(exodusPath);
                if (ecfg["linker"] && ecfg["linker"]["libraries"]) {
                    for (const auto& lib : ecfg["linker"]["libraries"]) {
                        std::string libName = lib.as<std::string>();
                        // Convert short name → full .so name: "SDL2" → "libSDL2.so"
                        std::string soName = "lib" + libName + ".so";
                        std::string err;
                        if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(soName.c_str(), &err)) {
                            LOG_DEBUG("JIT: could not load " + soName + ": " + err);
                        } else {
                            LOG_DEBUG("JIT: loaded " + soName);
                        }
                    }
                }
            } catch (...) {
                LOG_DEBUG("JIT: could not parse exodus.json for library preloading");
            }
        }
        // Also expose dlopen/dlsym so sys.load_lib/load_sym work at JIT time
        llvm::sys::DynamicLibrary::AddSymbol("dlopen",  (void*)(intptr_t)dlopen);
        llvm::sys::DynamicLibrary::AddSymbol("dlsym",   (void*)(intptr_t)dlsym);
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

    // tls namespace (OpenSSL TLS sockets)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_socket",          (void*)(intptr_t)aiml_tls_socket);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_connect",         (void*)(intptr_t)aiml_tls_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_bind",            (void*)(intptr_t)aiml_tls_bind);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_listen",          (void*)(intptr_t)aiml_tls_listen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_accept",          (void*)(intptr_t)aiml_tls_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_send",            (void*)(intptr_t)aiml_tls_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_recv",            (void*)(intptr_t)aiml_tls_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_send_string",     (void*)(intptr_t)aiml_tls_send_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_recv_string",     (void*)(intptr_t)aiml_tls_recv_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_close",           (void*)(intptr_t)aiml_tls_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_fd",              (void*)(intptr_t)aiml_tls_fd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_timeout",     (void*)(intptr_t)aiml_tls_set_timeout);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_cert",        (void*)(intptr_t)aiml_tls_set_cert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_ca",          (void*)(intptr_t)aiml_tls_set_ca);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_verify",      (void*)(intptr_t)aiml_tls_set_verify);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_peer_cert_cn",    (void*)(intptr_t)aiml_tls_peer_cert_cn);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_peer_cert_valid", (void*)(intptr_t)aiml_tls_peer_cert_valid);

    // ws/wss namespace (WebSocket — RFC 6455, from scratch)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_connect",      (void*)(intptr_t)aiml_ws_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_wss_connect",     (void*)(intptr_t)aiml_wss_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_server",       (void*)(intptr_t)aiml_ws_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_wss_server",      (void*)(intptr_t)aiml_wss_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_accept",       (void*)(intptr_t)aiml_ws_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_send",         (void*)(intptr_t)aiml_ws_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_send_binary",  (void*)(intptr_t)aiml_ws_send_binary);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_recv",         (void*)(intptr_t)aiml_ws_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_ping",         (void*)(intptr_t)aiml_ws_ping);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_close",        (void*)(intptr_t)aiml_ws_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_fd",           (void*)(intptr_t)aiml_ws_fd);

    // db namespace (AionDB)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open",          (void*)(intptr_t)aiml_db_store_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_insert",        (void*)(intptr_t)aiml_db_store_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_update",        (void*)(intptr_t)aiml_db_store_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get",           (void*)(intptr_t)aiml_db_store_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get_by_key",    (void*)(intptr_t)aiml_db_store_get_by_key);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_delete",        (void*)(intptr_t)aiml_db_store_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_len",           (void*)(intptr_t)aiml_db_store_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open_persist",  (void*)(intptr_t)aiml_db_store_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_checkpoint",    (void*)(intptr_t)aiml_db_store_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_checkpoint_all",      (void*)(intptr_t)aiml_db_checkpoint_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open_col",      (void*)(intptr_t)aiml_db_store_open_col);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open",             (void*)(intptr_t)aiml_db_kv_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open_persist",     (void*)(intptr_t)aiml_db_kv_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_checkpoint",       (void*)(intptr_t)aiml_db_kv_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_set",              (void*)(intptr_t)aiml_db_kv_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_get",              (void*)(intptr_t)aiml_db_kv_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_has",              (void*)(intptr_t)aiml_db_kv_has);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_len",              (void*)(intptr_t)aiml_db_kv_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_delete",           (void*)(intptr_t)aiml_db_kv_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_clear",            (void*)(intptr_t)aiml_db_kv_clear);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_open",            (void*)(intptr_t)aiml_db_vec_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_insert",          (void*)(intptr_t)aiml_db_vec_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_search",          (void*)(intptr_t)aiml_db_vec_search);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_len",             (void*)(intptr_t)aiml_db_vec_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_open",             (void*)(intptr_t)aiml_db_ts_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_open_persist",     (void*)(intptr_t)aiml_db_ts_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_checkpoint",       (void*)(intptr_t)aiml_db_ts_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_append",           (void*)(intptr_t)aiml_db_ts_append);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_at",               (void*)(intptr_t)aiml_db_ts_at);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_range",            (void*)(intptr_t)aiml_db_ts_range);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_len",              (void*)(intptr_t)aiml_db_ts_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_total",         (void*)(intptr_t)aiml_db_store_total);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_is_active",     (void*)(intptr_t)aiml_db_store_is_active);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get_ptr",       (void*)(intptr_t)aiml_db_store_get_ptr);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_query",         (void*)(intptr_t)aiml_db_store_query);

    // HTTP runtime
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_parse",              (void*)(intptr_t)aiml_url_parse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_to_string",          (void*)(intptr_t)aiml_url_to_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_is_https",           (void*)(intptr_t)aiml_url_is_https);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_protocol",           (void*)(intptr_t)aiml_url_protocol);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_host",               (void*)(intptr_t)aiml_url_host);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_path",               (void*)(intptr_t)aiml_url_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_fragment",           (void*)(intptr_t)aiml_url_fragment);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_port",               (void*)(intptr_t)aiml_url_port);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_get_param",          (void*)(intptr_t)aiml_url_get_param);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_credential",         (void*)(intptr_t)aiml_url_credential);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_cred_user",          (void*)(intptr_t)aiml_url_cred_user);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_cred_pass",          (void*)(intptr_t)aiml_url_cred_pass);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_new",           (void*)(intptr_t)aiml_http_map_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_put",           (void*)(intptr_t)aiml_http_map_put);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_get",           (void*)(intptr_t)aiml_http_map_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_len",           (void*)(intptr_t)aiml_http_map_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_free",          (void*)(intptr_t)aiml_http_map_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_get",               (void*)(intptr_t)aiml_http_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_post",              (void*)(intptr_t)aiml_http_post);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_put",               (void*)(intptr_t)aiml_http_put);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_delete",            (void*)(intptr_t)aiml_http_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_head",              (void*)(intptr_t)aiml_http_head);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_new_request",       (void*)(intptr_t)aiml_http_new_request);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_new",        (void*)(intptr_t)aiml_http_client_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_send",       (void*)(intptr_t)aiml_http_client_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_status",       (void*)(intptr_t)aiml_http_resp_status);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_ok",           (void*)(intptr_t)aiml_http_resp_ok);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_headers",      (void*)(intptr_t)aiml_http_resp_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_body",         (void*)(intptr_t)aiml_http_resp_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_free",         (void*)(intptr_t)aiml_http_resp_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_send",         (void*)(intptr_t)aiml_http_resp_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_write",        (void*)(intptr_t)aiml_http_resp_write);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_write_bytes",  (void*)(intptr_t)aiml_http_resp_write_bytes);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_flush",        (void*)(intptr_t)aiml_http_resp_flush);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_close",        (void*)(intptr_t)aiml_http_resp_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_status",   (void*)(intptr_t)aiml_http_resp_set_status);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_body",     (void*)(intptr_t)aiml_http_resp_set_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_body_bytes",(void*)(intptr_t)aiml_http_resp_set_body_bytes);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_get_headers",  (void*)(intptr_t)aiml_http_resp_get_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_method",        (void*)(intptr_t)aiml_http_req_method);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_path",          (void*)(intptr_t)aiml_http_req_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_body",          (void*)(intptr_t)aiml_http_req_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_url",           (void*)(intptr_t)aiml_http_req_url);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_params",        (void*)(intptr_t)aiml_http_req_params);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_cookies",       (void*)(intptr_t)aiml_http_req_cookies);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_header",        (void*)(intptr_t)aiml_http_req_header);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_cookie",        (void*)(intptr_t)aiml_http_req_cookie);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_body",   (void*)(intptr_t)aiml_http_client_set_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_timeout",(void*)(intptr_t)aiml_http_client_set_timeout);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_stream", (void*)(intptr_t)aiml_http_client_set_stream);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_get_headers",(void*)(intptr_t)aiml_http_client_get_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server",            (void*)(intptr_t)aiml_http_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_tls",        (void*)(intptr_t)aiml_http_server_tls);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_route",      (void*)(intptr_t)aiml_http_server_route);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_use",        (void*)(intptr_t)aiml_http_server_use);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_group",      (void*)(intptr_t)aiml_http_server_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_set_header", (void*)(intptr_t)aiml_http_server_set_header);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_start",      (void*)(intptr_t)aiml_http_server_start);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_start_async",(void*)(intptr_t)aiml_http_server_start_async);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_stop",       (void*)(intptr_t)aiml_http_server_stop);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_filesystem", (void*)(intptr_t)aiml_http_server_filesystem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_route",       (void*)(intptr_t)aiml_http_route_route);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_use",         (void*)(intptr_t)aiml_http_route_use);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_group",       (void*)(intptr_t)aiml_http_route_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_filesystem",  (void*)(intptr_t)aiml_http_route_filesystem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_new",         (void*)(intptr_t)aiml_fs_options_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_index",   (void*)(intptr_t)aiml_fs_options_set_index);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_spa",     (void*)(intptr_t)aiml_fs_options_set_spa);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_cache_max_age",(void*)(intptr_t)aiml_fs_options_set_cache_max_age);

    // llvm::sys::DynamicLibrary::AddSymbol("stderr", (void*)(intptr_t)stderr); // Removed to avoid macro issues for now

    // ── math namespace C runtime ─────────────────────────────────────────
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_add",       (void*)(intptr_t)aiml_math_vec2_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_sub",       (void*)(intptr_t)aiml_math_vec2_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_mul",       (void*)(intptr_t)aiml_math_vec2_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_scale",     (void*)(intptr_t)aiml_math_vec2_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_div",       (void*)(intptr_t)aiml_math_vec2_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_neg",       (void*)(intptr_t)aiml_math_vec2_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_dot",       (void*)(intptr_t)aiml_math_vec2_dot);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_length",    (void*)(intptr_t)aiml_math_vec2_length);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_length_sq", (void*)(intptr_t)aiml_math_vec2_length_sq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_normalize", (void*)(intptr_t)aiml_math_vec2_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_lerp",      (void*)(intptr_t)aiml_math_vec2_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_distance",  (void*)(intptr_t)aiml_math_vec2_distance);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_angle",     (void*)(intptr_t)aiml_math_vec2_angle);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_perp",      (void*)(intptr_t)aiml_math_vec2_perp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_eq",        (void*)(intptr_t)aiml_math_vec2_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_add",       (void*)(intptr_t)aiml_math_vec3_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_sub",       (void*)(intptr_t)aiml_math_vec3_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_mul",       (void*)(intptr_t)aiml_math_vec3_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_scale",     (void*)(intptr_t)aiml_math_vec3_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_div",       (void*)(intptr_t)aiml_math_vec3_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_neg",       (void*)(intptr_t)aiml_math_vec3_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_dot",       (void*)(intptr_t)aiml_math_vec3_dot);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_cross",     (void*)(intptr_t)aiml_math_vec3_cross);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_length",    (void*)(intptr_t)aiml_math_vec3_length);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_length_sq", (void*)(intptr_t)aiml_math_vec3_length_sq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_normalize", (void*)(intptr_t)aiml_math_vec3_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_lerp",      (void*)(intptr_t)aiml_math_vec3_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_distance",  (void*)(intptr_t)aiml_math_vec3_distance);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_reflect",   (void*)(intptr_t)aiml_math_vec3_reflect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_refract",   (void*)(intptr_t)aiml_math_vec3_refract);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_eq",        (void*)(intptr_t)aiml_math_vec3_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_add",       (void*)(intptr_t)aiml_math_vec4_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_sub",       (void*)(intptr_t)aiml_math_vec4_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_mul",       (void*)(intptr_t)aiml_math_vec4_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_scale",     (void*)(intptr_t)aiml_math_vec4_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_div",       (void*)(intptr_t)aiml_math_vec4_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_neg",       (void*)(intptr_t)aiml_math_vec4_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_lerp",      (void*)(intptr_t)aiml_math_vec4_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_eq",        (void*)(intptr_t)aiml_math_vec4_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_identity",     (void*)(intptr_t)aiml_math_quat_identity);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_axis_angle",   (void*)(intptr_t)aiml_math_quat_axis_angle);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_euler",        (void*)(intptr_t)aiml_math_quat_euler);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_mul",          (void*)(intptr_t)aiml_math_quat_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_normalize",    (void*)(intptr_t)aiml_math_quat_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_inverse",      (void*)(intptr_t)aiml_math_quat_inverse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_rotate_vec3",  (void*)(intptr_t)aiml_math_quat_rotate_vec3);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_lerp",         (void*)(intptr_t)aiml_math_quat_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_slerp",        (void*)(intptr_t)aiml_math_quat_slerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_to_mat4",      (void*)(intptr_t)aiml_math_quat_to_mat4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_to_quat",      (void*)(intptr_t)aiml_math_mat4_to_quat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_identity",     (void*)(intptr_t)aiml_math_mat4_identity);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_mul",          (void*)(intptr_t)aiml_math_mat4_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_mul_vec4",     (void*)(intptr_t)aiml_math_mat4_mul_vec4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_translate",    (void*)(intptr_t)aiml_math_mat4_translate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate",       (void*)(intptr_t)aiml_math_mat4_rotate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_x",     (void*)(intptr_t)aiml_math_mat4_rotate_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_y",     (void*)(intptr_t)aiml_math_mat4_rotate_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_z",     (void*)(intptr_t)aiml_math_mat4_rotate_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_scale",        (void*)(intptr_t)aiml_math_mat4_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_perspective",  (void*)(intptr_t)aiml_math_mat4_perspective);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_ortho",        (void*)(intptr_t)aiml_math_mat4_ortho);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_lookat",       (void*)(intptr_t)aiml_math_mat4_lookat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_inverse",      (void*)(intptr_t)aiml_math_mat4_inverse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_transpose",    (void*)(intptr_t)aiml_math_mat4_transpose);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_to_buffer",    (void*)(intptr_t)aiml_math_mat4_to_buffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_get",          (void*)(intptr_t)aiml_math_mat4_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_set",          (void*)(intptr_t)aiml_math_mat4_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_add",         (void*)(intptr_t)aiml_math_ivec2_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_div",         (void*)(intptr_t)aiml_math_ivec2_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_to_vec2",     (void*)(intptr_t)aiml_math_ivec2_to_vec2);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_to_ivec2",     (void*)(intptr_t)aiml_math_vec2_to_ivec2);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec3_add",         (void*)(intptr_t)aiml_math_ivec3_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_fmin_list",         (void*)(intptr_t)aiml_math_fmin_list);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_fmax_list",         (void*)(intptr_t)aiml_math_fmax_list);
    // hash namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha256",         (void*)(intptr_t)aiml_hash_sha256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha512",         (void*)(intptr_t)aiml_hash_sha512);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha3_256",       (void*)(intptr_t)aiml_hash_sha3_256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_keccak256",      (void*)(intptr_t)aiml_hash_keccak256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_ripemd160",      (void*)(intptr_t)aiml_hash_ripemd160);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2b",        (void*)(intptr_t)aiml_hash_blake2b);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2s",        (void*)(intptr_t)aiml_hash_blake2s);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_md5",            (void*)(intptr_t)aiml_hash_md5);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_crc32",          (void*)(intptr_t)aiml_hash_crc32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha256",    (void*)(intptr_t)aiml_hash_hmac_sha256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha512",    (void*)(intptr_t)aiml_hash_hmac_sha512);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha256_raw",     (void*)(intptr_t)aiml_hash_sha256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha512_raw",     (void*)(intptr_t)aiml_hash_sha512_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha3_256_raw",   (void*)(intptr_t)aiml_hash_sha3_256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_keccak256_raw",  (void*)(intptr_t)aiml_hash_keccak256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_ripemd160_raw",  (void*)(intptr_t)aiml_hash_ripemd160_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2b_raw",    (void*)(intptr_t)aiml_hash_blake2b_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha256_raw",(void*)(intptr_t)aiml_hash_hmac_sha256_raw);
    // libm scalar functions (resolved from libc.so at runtime, but AddSymbol ensures JIT finds them)
    llvm::sys::DynamicLibrary::AddSymbol("sinf",   (void*)(intptr_t)sinf);
    llvm::sys::DynamicLibrary::AddSymbol("cosf",   (void*)(intptr_t)cosf);
    llvm::sys::DynamicLibrary::AddSymbol("tanf",   (void*)(intptr_t)tanf);
    llvm::sys::DynamicLibrary::AddSymbol("asinf",  (void*)(intptr_t)asinf);
    llvm::sys::DynamicLibrary::AddSymbol("acosf",  (void*)(intptr_t)acosf);
    llvm::sys::DynamicLibrary::AddSymbol("sqrtf",  (void*)(intptr_t)sqrtf);
    llvm::sys::DynamicLibrary::AddSymbol("logf",   (void*)(intptr_t)logf);
    llvm::sys::DynamicLibrary::AddSymbol("log2f",  (void*)(intptr_t)log2f);
    llvm::sys::DynamicLibrary::AddSymbol("floorf", (void*)(intptr_t)floorf);
    llvm::sys::DynamicLibrary::AddSymbol("ceilf",  (void*)(intptr_t)ceilf);
    llvm::sys::DynamicLibrary::AddSymbol("roundf", (void*)(intptr_t)roundf);
    llvm::sys::DynamicLibrary::AddSymbol("fabsf",  (void*)(intptr_t)fabsf);
    llvm::sys::DynamicLibrary::AddSymbol("atan2f", (void*)(intptr_t)atan2f);
    llvm::sys::DynamicLibrary::AddSymbol("powf",   (void*)(intptr_t)powf);
    llvm::sys::DynamicLibrary::AddSymbol("fmodf",  (void*)(intptr_t)fmodf);

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
