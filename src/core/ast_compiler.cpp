#define DEBUG_MODE
#include "ast_compiler.h"
#include "builtin_registry.h"
#include "fptable.h"
#include "schema_registry.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "string_utils.h"
#include <iostream>
#include "../sys/ser_runtime.h"  // AimlTypeNode, AimlTDKind (spec 45)
#include "../sys/trace_runtime.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace AIML {
namespace AST {

void Compiler::compile(Program* prog) {
    if (!prog) return;

    registerBuiltins(ctx);

    if (ctx.libraryMode) {
        // Library mode: top-level DeclStmts become LLVM GlobalVariables (not stack
        // allocas) so that functions in this compilation unit can reference them
        // without cross-function dominance issues.  A small internal init function
        // is lazily created for any top-level code that needs an insert point
        // (initializers, set:/call: statements).

        // Deterministic init function name from source file path.
        std::string sanitized;
        for (char c : ctx.currentSourceFile)
            sanitized += (std::isalnum(c) ? c : '_');
        std::string initName = "__aiml_lib_init_" + sanitized;

        // Always create the lib init function (even if empty) so the linker
        // can resolve the call from the main module's __aiml_init.
        auto* voidTy_ = llvm::Type::getVoidTy(ctx.context);
        auto* ft_ = llvm::FunctionType::get(voidTy_, false);
        llvm::Function* libInitFunc = llvm::Function::Create(
            ft_, llvm::Function::ExternalLinkage, initName, ctx.module);
        auto* libInitEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", libInitFunc);
        ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(libInitEntryBB));

        // Restore insert point to the lib init function's last block.
        // After function compilation changes the insert point, we need to get back.
        auto ensureLibInit = [&]() {
            auto* lastBB = &libInitFunc->back();
            ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(lastBB));
        };

        // Pass 1: promote top-level DeclStmt to module-level GlobalVariables.
        // - {extern: true} → plain __lib_<varName> (ExternalLinkage, cross-file by intent)
        // - otherwise      → __lib_<sanitized_file>_<varName> (no linker conflicts for privates)
        std::string filePrefix;
        if (!ctx.currentSourceFile.empty()) {
            for (char c : ctx.currentSourceFile)
                filePrefix += (std::isalnum(c) ? c : '_');
            filePrefix += "_";
        }
        for (const auto& d : prog->declarations) {
            auto* decl = dynamic_cast<DeclStmt*>(d.get());
            if (!decl) continue;
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty) continue;
            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = decl->is_extern
                ? "__lib_" + decl->varName
                : "__lib_" + filePrefix + decl->varName;
            // {extern: true} → WeakAnyLinkage: multiple definitions allowed,
            // linker picks one → all references share the same storage.
            // Private globals → ExternalLinkage (file-qualified, unique per file).
            auto linkage = decl->is_extern
                ? llvm::GlobalValue::WeakAnyLinkage
                : llvm::GlobalValue::ExternalLinkage;
            // Reuse a forward-declared GV from sibling/import header pass if it
            // exists, so we don't end up with `<name>.N` LLVM auto-rename and
            // unresolved external references in importers.
            llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
            if (gv) {
                if (!gv->hasInitializer())
                    gv->setInitializer(llvm::Constant::getNullValue(llvmTy));
                gv->setLinkage(linkage);
            } else {
                gv = new llvm::GlobalVariable(
                    ctx.module, llvmTy, /*isConstant=*/false,
                    linkage,
                    llvm::Constant::getNullValue(llvmTy),
                    symName);
            }

            Symbol sym;
            sym.ir_alloca_inst = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
            sym.ir_type        = ctx.ir_gen->getPointerTy();
            sym.ir_element_type = ty;
            sym.typeName       = decl->typeName;
            sym.is_const       = decl->is_const;
            sym.is_volatile    = decl->is_volatile;
            ctx.symbol_table[decl->varName] = sym;

            // Emit type-specific initialization in the lib-init function
            // (mirrors what visitDeclStmt does for stack-allocated variables).
            std::string tName = decl->typeName;
            if (tName.find("list<") == 0 && tName.back() == '>') {
                ensureLibInit();
                std::string elemName = tName.substr(5, tName.length() - 6);
                llvm::Type* elemTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                if (elemTy) ListImpl::initList(ctx, gv, elemTy, 4);
            } else if (tName.find("map<") == 0 && tName.back() == '>') {
                ensureLibInit();
                std::string inner = tName.substr(4, tName.length() - 5);
                size_t colon = inner.find(':');
                if (colon != std::string::npos) {
                    std::string kName = inner.substr(0, colon);
                    std::string vName = inner.substr(colon + 1);
                    llvm::Type* kTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
                    llvm::Type* vTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                    if (kTy && vTy) MapImpl::initMap(ctx, gv, kTy, vTy, 4);
                }
            } else if (tName.find("chan<") == 0 && tName.back() == '>') {
                ensureLibInit();
                ChanImpl::init(ctx, gv, ctx.builder.getInt64(0));
            } else if (tName.rfind("buffer<", 0) == 0 && tName.back() == '>') {
                if (!decl->initialValue) {
                    ensureLibInit();
                    BufferImpl::alloc(ctx, gv, ctx.builder.getInt32(4096));
                }
            }

            // If there is an initializer, emit it in the lib-init function.
            if (decl->initialValue) {
                ensureLibInit();
                IRValue* initVal = visitExpression(decl->initialValue.get());
                if (initVal)
                    ctx.ir_gen->createStore(initVal, sym.ir_alloca_inst);
            }
        }

        // Pass 2: visit everything else (functions, classes, non-decl stmts).
        for (const auto& d : prog->declarations) {
            if (dynamic_cast<DeclStmt*>(d.get())) continue; // already handled
            if (dynamic_cast<Stmt*>(d.get())) {
                // Non-decl statement (set, call, etc.) — needs an insert point.
                ensureLibInit();
            }
            visitTopLevel(d.get());
        }

        // Terminate the library init function.
        {
            auto* curBB = static_cast<llvm::BasicBlock*>(
                ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
            if (curBB && !curBB->getTerminator())
                ctx.builder.CreateRetVoid();
            ctx.libInitFuncName = initName;
        }
    } else {
        // Create __aiml_init function to hold top-level statements
        IRType* voidTy = ctx.ir_gen->resolveType("void");
        std::vector<IRType*> noArgs;
        IRValue* initFunc = ctx.ir_gen->createFunction("__aiml_init", voidTy, noArgs);

        // Create entry block for __aiml_init
        llvm::Function* llvmInitFunc = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(initFunc));
        llvm::BasicBlock* initEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmInitFunc);
        ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(initEntryBB));

        // Call all imported lib init functions BEFORE user top-level code.
        for (const auto& libInitName : ctx.libInitFuncNames) {
            auto* vd = llvm::Type::getVoidTy(ctx.context);
            auto* ft = llvm::FunctionType::get(vd, false);
            llvm::FunctionCallee fc = ctx.module.getOrInsertFunction(libInitName, ft);
            ctx.builder.CreateCall(fc);
        }

        // Pre-pass: promote top-level DeclStmts to GlobalVariables so that
        // functions can reference them without cross-function alloca issues
        // (cross-function alloca references produce invalid LLVM IR).
        // Only the storage is created here; initializers are emitted in-order
        // below via visitDeclStmt, which detects is_global and skips alloca.
        for (const auto& d : prog->declarations) {
            auto* decl = dynamic_cast<DeclStmt*>(d.get());
            if (!decl) continue;
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty) continue;
            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = "__global_" + decl->varName;
            auto* gv = new llvm::GlobalVariable(
                ctx.module, llvmTy, /*isConstant=*/false,
                llvm::GlobalValue::ExternalLinkage,
                llvm::Constant::getNullValue(llvmTy),
                symName);

            Symbol sym;
            sym.ir_alloca_inst  = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
            sym.ir_type         = ctx.ir_gen->getPointerTy();
            sym.ir_element_type = ty;
            sym.typeName        = decl->typeName;
            sym.is_const        = decl->is_const;
            sym.is_volatile     = decl->is_volatile;
            sym.is_global       = true;
            ctx.symbol_table[decl->varName] = sym;
        }

        // Visit all declarations in file order (preserving execution order).
        // visitDeclStmt will detect is_global=true and skip alloca creation,
        // only emitting the initializer at the correct point in the sequence.
        ctx.inTopLevelInit = true;
        for (const auto& decl : prog->declarations) {
            visitTopLevel(decl.get());
        }
        ctx.inTopLevelInit = false;

        // Finish __aiml_init
        IRBlock* currentBlock = ctx.ir_gen->getInsertBlock();
        if (!static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(currentBlock))->getTerminator()) {
            ctx.ir_gen->createRet(nullptr);
        }

        // Emit hot-reload support tables (after all functions and types are registered)
        emitFptable(ctx);
        emitSchemaRegistry(ctx);
    }
}
void Compiler::registerTypes(Program* prog) {
    if (!prog) return;

    // Inject BuiltIn Exception Classes
    std::vector<std::string> builtinExceptions = {
        "Exception", "NullPointerException", "TypeMismatchException", "IndexOutOfBoundsException",
        "DivisionByZeroException", "StackOverflowException", "OutOfMemoryException", "TimeoutException",
        "SerializationException", "DeserializationException", "BufferOverflowException", "BufferUnderflowException",
        "InvalidThreadStateException"
    };

    for (const auto& excName : builtinExceptions) {
        if (!ctx.classes.count(excName)) {
             CompilerContext::ClassInfo classInfo;
             classInfo.name = excName;
             classInfo.version = 0;
             classInfo.parentName = excName == "Exception" ? "" : "Exception";
             
             CompilerContext::StructMember msgMember;
             msgMember.typeName = "string";
             msgMember.type = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType("string")));
             msgMember.index = 0;
             msgMember.access = AccessLevel::Public;
             classInfo.members["message"] = msgMember;
             
             std::vector<llvm::Type*> llvmMemberTypes;
             llvmMemberTypes.push_back(msgMember.type);
             
             llvm::StructType* structType = llvm::StructType::create(ctx.context, excName);
             structType->setBody(llvmMemberTypes);
             classInfo.type = structType;
             
             ctx.classes[excName] = classInfo;
        }
    }

    for (const auto& decl : prog->declarations) {
        registerTypeDecl(decl.get());
    }
}

// ── Spec-45: Heap-type descriptor helpers ────────────────────────────────────

// Shift the absolute child-index fields of a descriptor node by delta.
static AimlTypeNode snShift(AimlTypeNode n, int32_t delta) {
    switch (n.kind) {
        case AIML_TD_LIST:
        case AIML_TD_BUFFER: n.param1 += delta; break;
        case AIML_TD_MAP:    n.param1 += delta; n.param2 += delta; break;
        case AIML_TD_STRUCT: n.param2 += delta; break; // param1 = n_fields (not an index)
        default: break;
    }
    return n;
}

// Returns true if typeName contains at least one heap-allocated component
// (string, list<T>, map<K:V>, buffer<T>, or user struct with heap members).
static bool serHasHeap(const std::string& typeName, CompilerContext& ctx) {
    if (typeName == "string") return true;
    if (typeName.size() > 5 && typeName.substr(0,5) == "list<")   return true;
    if (typeName.size() > 4 && typeName.substr(0,4) == "map<")    return true;
    if (typeName.size() > 7 && typeName.substr(0,7) == "buffer<") return true;
    auto it = ctx.classes.find(typeName);
    if (it != ctx.classes.end() && !it->second.isBuiltin)
        for (auto& [mn, mem] : it->second.members)
            if (serHasHeap(mem.typeName, ctx)) return true;
    return false;
}

// Assemble the flat STRUCT node array from per-field subtrees.
// Each fsub[j] has been built by serBuildSubtree with the field's byte offset.
static std::vector<AimlTypeNode> serBuildStructNodes(
    const std::vector<std::vector<AimlTypeNode>>& fsubs,
    int32_t offset, int32_t struct_sz)
{
    int32_t nf = (int32_t)fsubs.size();
    // children_start[j] = index in result where fsubs[j][1..] begin
    std::vector<int32_t> cs;
    int32_t acc = 1 + nf;
    for (int j = 0; j < nf; ++j) { cs.push_back(acc); acc += (int32_t)fsubs[j].size() - 1; }

    std::vector<AimlTypeNode> out;
    out.push_back({AIML_TD_STRUCT, nf, 1, offset, struct_sz, 0}); // root
    for (int j = 0; j < nf; ++j)                                  // field roots
        out.push_back(snShift(fsubs[j][0], cs[j] - 1));
    for (int j = 0; j < nf; ++j) {                                // field children
        int32_t d = cs[j] - 1;
        for (size_t k = 1; k < fsubs[j].size(); ++k)
            out.push_back(snShift(fsubs[j][k], d));
    }
    return out;
}

// Forward declaration.
static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl);

// Build a flat DFS descriptor node array for typeName.  Root is at index 0.
// offset = byte offset of this value within its parent struct (0 = top-level).
static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl)
{
    std::vector<AimlTypeNode> res;

    // ── Primitive scalars ─────────────────────────────────────────────────────
    if (typeName=="string")                                    { res.push_back({AIML_TD_STRING, 0,0,offset,0,0}); return res; }
    if (typeName=="i8"||typeName=="u8"||typeName=="bool")      { res.push_back({AIML_TD_I8,     0,0,offset,0,0}); return res; }
    if (typeName=="i16"||typeName=="u16")                      { res.push_back({AIML_TD_I16,    0,0,offset,0,0}); return res; }
    if (typeName=="i32"||typeName=="u32"||typeName=="f32")     { res.push_back({AIML_TD_I32,    0,0,offset,0,0}); return res; }
    if (typeName=="i64"||typeName=="u64"||typeName=="f64")     { res.push_back({AIML_TD_I64,    0,0,offset,0,0}); return res; }

    // ── list<T> ───────────────────────────────────────────────────────────────
    if (typeName.size()>5 && typeName.substr(0,5)=="list<") {
        std::string inner = typeName.substr(5, typeName.size()-6);
        int32_t esz;
        // Determine element size without calling getLLVMType on unregistered types
        // (which would log spurious errors during Phase-A sibling type registration).
        if (ctx.classes.count(inner) && ctx.classes[inner].type) {
            esz = (int32_t)dl.getTypeAllocSize(ctx.classes[inner].type);
        } else if (ctx.enums.count(inner)) {
            esz = 4;  // enum = i32
        } else if (inner.size()>5 && inner.substr(0,5)=="list<") {
            esz = 16;  // sizeof(SerList) = {i32 size, i32 cap, void* data}
        } else if (inner.size()>7 && inner.substr(0,7)=="buffer<") {
            esz = 24;  // sizeof(SerBuffer) = {i64 size, i64 cap, void* data}
        } else if (!inner.empty() && std::isupper((unsigned char)inner[0])) {
            // Unregistered user-defined type during Phase-A — use placeholder.
            // The model depending on this list<T> should also have been deferred,
            // so this branch is a last-resort safety net.
            esz = 8;
        } else {
            llvm::Type* iT = ctx.getLLVMType(inner);
            esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 8;
        }
        res.push_back({AIML_TD_LIST, 1, 0, offset, esz, 0});
        for (auto& n : serBuildSubtree(inner, 0, ctx, dl)) res.push_back(snShift(n, 1));
        return res;
    }

    // ── buffer<T> ─────────────────────────────────────────────────────────────
    if (typeName.size()>7 && typeName.substr(0,7)=="buffer<") {
        std::string inner = typeName.substr(7, typeName.size()-8);
        llvm::Type* iT = ctx.getLLVMType(inner);
        int32_t esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 1;
        res.push_back({AIML_TD_BUFFER, 1, 0, offset, esz, 0});
        for (auto& n : serBuildSubtree(inner, 0, ctx, dl)) res.push_back(snShift(n, 1));
        return res;
    }

    // ── map<K:V> ──────────────────────────────────────────────────────────────
    if (typeName.size()>4 && typeName.substr(0,4)=="map<") {
        std::string inner = typeName.substr(4, typeName.size()-5);
        size_t col = inner.find(':');
        std::string kT = (col!=std::string::npos) ? inner.substr(0,col)   : "string";
        std::string vT = (col!=std::string::npos) ? inner.substr(col+1)   : inner;
        auto ksub = serBuildSubtree(kT, 0, ctx, dl);
        auto vsub = serBuildSubtree(vT, 0, ctx, dl);
        int32_t kc = (int32_t)ksub.size();
        llvm::Type* vLT = ctx.getLLVMType(vT);
        int32_t vsz = vLT ? (int32_t)dl.getTypeAllocSize(vLT) : 0;
        res.push_back({AIML_TD_MAP, 1, 1+kc, offset, vsz, 0});
        for (auto& n : ksub) res.push_back(snShift(n, 1));
        for (auto& n : vsub) res.push_back(snShift(n, 1+kc));
        return res;
    }

    // ── User struct (must already be in ctx.classes) ──────────────────────────
    {
        auto it = ctx.classes.find(typeName);
        if (it != ctx.classes.end() && !it->second.isBuiltin && it->second.type) {
            auto& ci = it->second;
            const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
            int32_t sz = (int32_t)dl.getTypeAllocSize(ci.type);
            std::vector<std::pair<std::string, CompilerContext::StructMember>> sm;
            for (auto& [mn, mem] : ci.members) sm.push_back({mn, mem});
            std::sort(sm.begin(), sm.end(),
                      [](auto& a, auto& b){ return a.second.index < b.second.index; });
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : sm)
                fsubs.push_back(serBuildSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            return serBuildStructNodes(fsubs, offset, sz);
        }
    }

    // ── Fallback: opaque / value-only type → raw bytes ────────────────────────
    // If the type name looks like a user-defined struct but is not yet registered
    // (e.g. a sibling type not yet processed), use a placeholder size without
    // triggering a spurious error in getLLVMType.
    if (!typeName.empty() && std::isupper((unsigned char)typeName[0]) &&
        !ctx.classes.count(typeName) && !ctx.enums.count(typeName)) {
        // Unknown user type during type registration phase — treat as opaque.
        res.push_back({AIML_TD_RAWBYTES, 8, 0, offset, 0, 0});
        return res;
    }
    llvm::Type* t = ctx.getLLVMType(typeName);
    int32_t sz = t ? (int32_t)dl.getTypeAllocSize(t) : 8;
    res.push_back({AIML_TD_RAWBYTES, sz, 0, offset, 0, 0});
    return res;
}

// Emit @__aiml_sernodes_<name> as a private constant global; update classInfo.
static void serEmitGlobal(
    const std::string& name,
    const std::vector<AimlTypeNode>& nodes,
    CompilerContext& ctx,
    CompilerContext::ClassInfo& classInfo)
{
    if (nodes.empty()) return;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32 = llvm::Type::getInt32Ty(lctx);
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32,i32,i32,i32,i32,i32});
    std::vector<llvm::Constant*> elems;
    for (auto& n : nodes) {
        llvm::Constant* f[6] = {
            llvm::ConstantInt::get(i32, (uint64_t)n.kind),
            llvm::ConstantInt::get(i32, (uint64_t)n.param1),
            llvm::ConstantInt::get(i32, (uint64_t)n.param2),
            llvm::ConstantInt::get(i32, (uint64_t)n.offset),
            llvm::ConstantInt::get(i32, (uint64_t)n.elem_size),
            llvm::ConstantInt::get(i32, 0),
        };
        elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
    }
    llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
    auto* gv = new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantArray::get(arrTy, elems),
        "__aiml_sernodes_" + name);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    // hasHeapFields is computed by the caller based on serHasHeap; do not force-set it.
    classInfo.serNodesVar   = gv;
    classInfo.serNodesCount = (int32_t)nodes.size();
}

void Compiler::registerTypeDecl(Node* node) {
    if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
        ctx.currentNamespace = ns->name;
        LOG_DEBUG("AST Passed Namespace: " + ns->name);
    } else if (auto cfg = dynamic_cast<ConfigDecl*>(node)) {
        if (!cfg->entryPoint.empty()) ctx.config.entryPointName = cfg->entryPoint;
        if (cfg->stackSize) ctx.config.stackSize = cfg->stackSize;
        if (cfg->heapSize) ctx.config.heapSize = cfg->heapSize;
        if (!cfg->target.empty()) ctx.config.target = cfg->target;
        if (!cfg->version.empty()) ctx.config.version = cfg->version;
    } else if (auto imp = dynamic_cast<ImportDecl*>(node)) {
        LOG_DEBUG("AST Processing Import: " + imp->path);
        if (ctx.resolveImportCallback) {
            ctx.resolveImportCallback(imp->path);
        } else {
            LOG_ERROR(ctx.fmtLoc(imp->sourceNode) + "resolveImportCallback not bound! Cannot process import: " + imp->path);
        }
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        std::string className = ctx.qualify(cls->name);
        if (ctx.classes.count(className)) return; // Already registered

        CompilerContext::ClassInfo classInfo;
        classInfo.name = className;
        classInfo.version = cls->version;
        classInfo.compiled_version = cls->version;  // locked at compile time
        classInfo.parentName = cls->parentName;
        classInfo.sourceYaml = YAML::Dump(cls->sourceNode);
        
        std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
        int currentIndex = 0;
        
        // 1. Parent
        if (!classInfo.parentName.empty()) {
             if (ctx.classes.count(classInfo.parentName)) {
                 const auto& parentInfo = ctx.classes[classInfo.parentName];
                 for (const auto& [name, member] : parentInfo.members) {
                     orderedMembers.push_back({name, member});
                 }
                 std::sort(orderedMembers.begin(), orderedMembers.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
             } else {
                 LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Unknown parent class: " + classInfo.parentName);
             }
        }
        
        // 1.5 Models (Mixins)
        for (const auto& modelName : cls->models) {
             if (ctx.classes.count(modelName)) {
                 const auto& modelInfo = ctx.classes[modelName];
                 // Models are appended sequentially after parent fields
                 for (auto [name, member] : modelInfo.members) {
                     // Need to re-index so they follow sequentially
                     orderedMembers.push_back({name, member});
                 }
             } else {
                 LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Unknown model: " + modelName);
             }
        }
        
        currentIndex = orderedMembers.size();
        
        // 2. Class Members (Parsed from AST)
        for (auto& astMember : cls->members) {
            std::string memberName = astMember.name;
            std::string typeName = astMember.type;
            
            bool duplicate = false;
            for(const auto& pair : orderedMembers) if(pair.first == memberName) duplicate = true;
            if(duplicate) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Duplicate member '" + memberName + "' in class '" + className + "'");
                continue;
            }
            
            llvm::Type* memberType = ctx.getLLVMType(typeName);
            if (!memberType) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Unknown type '" + typeName + "' for member '" + memberName + "'");
                continue; 
            }

            CompilerContext::StructMember member;
            member.index = currentIndex++;
            member.type = memberType;
            member.typeName = typeName;
            member.version = astMember.version;
            member.initValue = std::shared_ptr<Expr>(const_cast<Expr*>(astMember.initialValue.release()));
            
            if (astMember.access == "public") member.access = AccessLevel::Public;
            else if (astMember.access == "private") member.access = AccessLevel::Private;
            else if (astMember.access == "protected") member.access = AccessLevel::Protected;
            
            member.is_const = astMember.is_const;
            #ifdef LLVM 18
            // member.is_volatile not supported structurally in opaque pointers.
            // Requires attaching volatile flag to the underlying Load/Store instructions.
            #endif
            
            orderedMembers.push_back({memberName, member});
        }
        
        // Finalize Members
        std::vector<llvm::Type*> llvmMemberTypes;
        for(const auto& [name, member] : orderedMembers) {
            classInfo.members[name] = member;
            llvmMemberTypes.push_back(member.type);
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, className);
        structType->setBody(llvmMemberTypes);
        classInfo.type = structType;

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        // Spec 45: emit @__aiml_sernodes_<Name> if the struct has heap fields.
        {
            bool anyHeap = false;
            for (auto& [mn, mem] : classInfo.members)
                if (serHasHeap(mem.typeName, ctx)) { anyHeap = true; break; }
            classInfo.hasHeapFields = anyHeap;
            if (anyHeap) {
                const llvm::DataLayout& dl = ctx.module.getDataLayout();
                const llvm::StructLayout* sl = dl.getStructLayout(structType);
                int32_t sz = (int32_t)dl.getTypeAllocSize(structType);
                std::vector<std::vector<AimlTypeNode>> fsubs;
                for (auto& [mn, mem] : orderedMembers)
                    fsubs.push_back(serBuildSubtree(mem.typeName,
                        (int32_t)sl->getElementOffset(mem.index), ctx, dl));
                auto nodes = serBuildStructNodes(fsubs, 0, sz);
                serEmitGlobal(className, nodes, ctx, classInfo);
            }
        }

        // Track user-defined types for sys.bundle dependency closure.
        // collectUserTypes recurses into list<T> / map<K,V> to find inner types.
        auto track = [&](const std::string& typeName) {
            std::set<std::string> found;
            ctx.collectUserTypes(typeName, found);
            classInfo.referencedTypes.insert(found.begin(), found.end());
        };
        for (const auto& [mname, member] : classInfo.members) track(member.typeName);
        track(classInfo.parentName);
        for (const auto& modelName : cls->models)  track(modelName);
        for (const auto& ifaceName : cls->interfaces) track(ifaceName);
        for (const auto& method : cls->methods) {
            for (const auto& arg : method->args) track(arg.second);
            track(method->returnType);
            // Record the mangled method name so dfsCollect can recurse into body types
            classInfo.methodNames.push_back(ctx.qualify(method->name));
        }

        ctx.classes[className] = classInfo;
        LOG_DEBUG("AST Registered Class: " + className);
    } else if (auto mod = dynamic_cast<ModelDecl*>(node)) {
        std::string modelName = ctx.qualify(mod->name);
        if (ctx.classes.count(modelName)) return; // Already registered

        CompilerContext::ClassInfo classInfo;
        classInfo.name = modelName;
        classInfo.isModel = true;
        classInfo.primaryKey = mod->primaryKey;
        classInfo.version = mod->version;

        std::vector<llvm::Type*> fieldTypes;
        int index = 0;

        // Inherited parent model fields first
        if (!mod->parentName.empty()) {
            std::string parentQual = ctx.qualify(mod->parentName);
            if (ctx.classes.count(parentQual)) {
                const auto& parentInfo = ctx.classes[parentQual];
                std::vector<std::pair<std::string, CompilerContext::StructMember>> pFields;
                for (auto& [n, m] : parentInfo.members) pFields.push_back({n, m});
                std::sort(pFields.begin(), pFields.end(),
                          [](auto& a, auto& b){ return a.second.index < b.second.index; });
                for (auto& [n, m] : pFields) {
                    fieldTypes.push_back(m.type);
                    CompilerContext::StructMember member = m;
                    member.index = index++;
                    classInfo.members[n] = member;
                }
            } else {
                // Parent not yet registered — skip for now so that a later
                // registration attempt (once siblings are processed) can succeed.
                LOG_DEBUG("Deferring model '" + modelName + "': parent '" + mod->parentName + "' not yet registered");
                return;
            }
        }

        // Pre-check: ensure all field-type dependencies are registered.
        // If a field is list<T>, map<K:V>, or a direct user type that isn't
        // registered yet, defer to the next fixed-point iteration.
        {
            auto userTypeReady = [&](const std::string& tn) -> bool {
                if (tn.empty() || !std::isupper((unsigned char)tn[0])) return true;
                return ctx.classes.count(tn) > 0 || ctx.enums.count(tn) > 0;
            };
            for (const auto& astMember : mod->fields) {
                const std::string& tn = astMember.type;
                bool ok = true;
                if (tn.size() > 5 && tn.substr(0,5) == "list<") {
                    ok = userTypeReady(tn.substr(5, tn.size()-6));
                } else if (tn.size() > 4 && tn.substr(0,4) == "map<") {
                    size_t col = tn.find(':');
                    if (col != std::string::npos)
                        ok = userTypeReady(tn.substr(col+1, tn.size()-col-2));
                } else {
                    ok = userTypeReady(tn);
                }
                if (!ok) {
                    LOG_DEBUG("Deferring model '" + modelName + "': field '" + astMember.name + "' type '" + tn + "' not ready");
                    return;
                }
            }
        }

        for (const auto& astMember : mod->fields) {
            std::string fieldName = astMember.name;
            std::string typeName = astMember.type;

            llvm::Type* fieldType = ctx.getLLVMType(typeName);
            if (!fieldType) {
                 LOG_ERROR(ctx.fmtLoc(mod->sourceNode) + "Unknown type '" + typeName + "' for field '" + fieldName + "' in model '" + modelName + "'");
                 continue;
            }

            fieldTypes.push_back(fieldType);

            CompilerContext::StructMember member;
            member.index = index++;
            member.type = fieldType;
            member.typeName = typeName;
            member.access = AccessLevel::Public; // Models are DTOs

            classInfo.members[fieldName] = member;
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, modelName);
        structType->setBody(fieldTypes);
        classInfo.type = structType;

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        // Spec 45 / RPC payload v2: always emit @__aiml_sernodes_<Name> so any
        // model can be used as a typed RPC payload, not just those with heap fields.
        {
            bool anyHeap = false;
            for (auto& [mn, mem] : classInfo.members)
                if (serHasHeap(mem.typeName, ctx)) { anyHeap = true; break; }
            classInfo.hasHeapFields = anyHeap;
            const llvm::DataLayout& dl = ctx.module.getDataLayout();
            const llvm::StructLayout* sl = dl.getStructLayout(structType);
            int32_t sz = (int32_t)dl.getTypeAllocSize(structType);
            std::vector<std::pair<std::string, CompilerContext::StructMember>> sm;
            for (auto& [mn, mem] : classInfo.members) sm.push_back({mn, mem});
            std::sort(sm.begin(), sm.end(),
                      [](auto& a, auto& b){ return a.second.index < b.second.index; });
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : sm)
                fsubs.push_back(serBuildSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            auto nodes = serBuildStructNodes(fsubs, 0, sz);
            serEmitGlobal(modelName, nodes, ctx, classInfo);
        }

        // Mirror: propagate sync/rpc annotations
        classInfo.syncFields = mod->syncFields;
        classInfo.rpcMethods = mod->rpcMethods;

        ctx.classes[modelName] = classInfo;
        LOG_DEBUG("AST Registered Model: " + modelName);
    } else if (auto enm = dynamic_cast<EnumDecl*>(node)) {
        std::string enumName = ctx.qualify(enm->name);
        std::map<std::string, int> enumValues;
        for (const auto& val : enm->values) {
            enumValues[val.first] = val.second;
        }
        ctx.enums[enumName] = enumValues;
        ctx.enumSources[enumName] = YAML::Dump(enm->sourceNode);
        LOG_DEBUG("AST Registered Enum: " + enumName);
    } else if (auto iface = dynamic_cast<InterfaceDecl*>(node)) {
        std::string ifaceName = ctx.qualify(iface->name);
        ctx.interfaceSources[ifaceName] = YAML::Dump(iface->sourceNode);
        LOG_DEBUG("AST Registered Interface: " + ifaceName);
    } else if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Forward-declare function signature so forward references resolve.
        std::string mangledName = ctx.qualify(func->name);
        if (ctx.module.getFunction(mangledName)) return; // already declared

        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        std::vector<llvm::Type*> paramTys;
        for (const auto& arg : func->args) {
            IRType* irt = ctx.ir_gen->resolveType(arg.second);
            llvm::Type* t = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
            paramTys.push_back(t ? t : i8p);
        }
        llvm::Type* retTy = nullptr;
        if (func->returnType.empty() || func->returnType == "void")
            retTy = llvm::Type::getVoidTy(ctx.context);
        else {
            IRType* irt = ctx.ir_gen->resolveType(func->returnType);
            retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function::Create(ft, llvm::Function::ExternalLinkage, mangledName, ctx.module);
        LOG_DEBUG("registerTypeDecl: forward-declared " + mangledName);
    }
}
void Compiler::visitExternBlock(ExternBlock* ext) {
    // Register each extern "C" function as an LLVM ExternalLinkage declaration.
    // No function body is generated — the linker (AOT) or dlopen/dlsym (JIT) resolve it.
    auto* i8p = llvm::PointerType::get(ctx.context, 0);

    for (const auto& fd : ext->funcs) {
        // Build LLVM arg types
        std::vector<llvm::Type*> paramTys;
        for (const auto& arg : fd.args) {
            llvm::Type* t = nullptr;
            // `fn` type — passes an AIML function pointer as i8* (void*)
            if (arg.second == "fn") {
                t = i8p;
            } else {
                IRType* irt = ctx.ir_gen->resolveType(arg.second);
                t = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
            }
            paramTys.push_back(t ? t : i8p);
        }

        // Return type
        llvm::Type* retTy = nullptr;
        if (fd.returnType.empty() || fd.returnType == "void") {
            retTy = llvm::Type::getVoidTy(ctx.context);
        } else {
            IRType* irt = ctx.ir_gen->resolveType(fd.returnType);
            retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
        }

        // Skip if already declared (e.g. extern block appears twice)
        if (ctx.module.getFunction(fd.name)) {
            LOG_DEBUG("visitExternBlock: " + fd.name + " already declared, skipping");
            continue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function* f = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, fd.name, ctx.module);

        // Store in context so visitCall can find it via resolveFunction / ctx.module.getFunction
        CompilerContext::FunctionInfo fi;
        fi.func           = f;
        fi.fptable_index  = -1;   // external → no fptable slot
        fi.version        = 1;
        fi.compiled_version = 1;
        ctx.functions[fd.name] = fi;

        LOG_DEBUG("visitExternBlock: registered extern " + fd.name);
    }
}

void Compiler::visitTopLevel(Node* node) {
    if (auto ext = dynamic_cast<ExternBlock*>(node)) {
        visitExternBlock(ext);
        return;
    } else if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Prevent duplicate IR generation if function was already compiled.
        // BUT: if a newer version is declared (higher `version:` value), update
        // sourceYaml + version in the symbol table so sys.bundle picks up the
        // new YAML for hot-reload bundles at runtime.
        // NOTE: If the function was only header-only registered (forward-decl,
        // no body), we still need to compile it.  Check func->empty() on the
        // LLVM function to distinguish.
        std::string mangledName = func->name;
        if (ctx.functions.find(mangledName) != ctx.functions.end()) {
            auto& fi = ctx.functions[mangledName];
            if (func->version > fi.version) {
                fi.version    = func->version;
                fi.sourceYaml = YAML::Dump(func->sourceNode);
                if (ctx.functions.count(func->name))
                    ctx.functions[func->name] = fi;
                LOG_INFO("visitTopLevel: updated sourceYaml for " + mangledName +
                         " to v" + std::to_string(func->version));
            }
            // Skip only if function already has a body (not just forward-declared).
            if (fi.func && !fi.func->empty())
                return;
        }
        visitFunction(func);
    } else if (auto stmt = dynamic_cast<Stmt*>(node)) {
        visitStatement(stmt);
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        // If this is a higher-version re-definition, update sourceYaml + version (not compiled_version)
        std::string qualName = ctx.qualify(cls->name);
        if (ctx.classes.count(qualName) && cls->version > ctx.classes[qualName].version) {
            ctx.classes[qualName].version    = cls->version;
            ctx.classes[qualName].sourceYaml = YAML::Dump(cls->sourceNode);
            if (ctx.classes.count(cls->name)) {
                ctx.classes[cls->name].version    = cls->version;
                ctx.classes[cls->name].sourceYaml = YAML::Dump(cls->sourceNode);
            }
            LOG_INFO("visitTopLevel: updated sourceYaml for " + cls->name + " to v"
                     + std::to_string(cls->version));
        }
        for (const auto& method : cls->methods) {
            std::string oldName = method->name;
            method->name = cls->name + "_" + method->name;
            auto it = method->args.insert(method->args.begin(), {"this", "ref<" + cls->name + ">"});

            std::string oldClassName = ctx.currentClassName;
            ctx.currentClassName = cls->name;
            
            visitFunction(method.get());
            
            ctx.currentClassName = oldClassName;
            
            method->args.erase(it);
            method->name = oldName;
        }
    } else if (auto prefab = dynamic_cast<PrefabDecl*>(node)) {
        // Emit a call to aiml_prefab_register() that runs at top-level (program init)
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* f32 = ctx.builder.getFloatTy();
        auto* i32 = ctx.builder.getInt32Ty();

        // Serialize children to JSON string
        std::string childrenJson = "[";
        for (size_t i = 0; i < prefab->children.size(); i++) {
            auto& c = prefab->children[i];
            if (i > 0) childrenJson += ",";
            childrenJson += "{\"name\":\"" + c.name + "\","
                "\"type\":\"" + c.type + "\","
                "\"x\":" + std::to_string(c.x) + ","
                "\"y\":" + std::to_string(c.y) + ","
                "\"width\":" + std::to_string(c.width) + ","
                "\"height\":" + std::to_string(c.height) + ","
                "\"font_size\":" + std::to_string(c.font_size) + ","
                "\"color\":" + std::to_string(c.color) + ","
                "\"texture\":\"" + c.texture + "\","
                "\"anchor\":\"" + c.anchor + "\"}";
        }
        childrenJson += "]";

        llvm::Value* nameStr = ctx.builder.CreateGlobalStringPtr(prefab->name, "prefab_name");
        llvm::Value* texStr  = ctx.builder.CreateGlobalStringPtr(prefab->texture, "prefab_tex");
        llvm::Value* childStr = ctx.builder.CreateGlobalStringPtr(childrenJson, "prefab_children");
        llvm::Value* w = llvm::ConstantFP::get(f32, prefab->width);
        llvm::Value* h = llvm::ConstantFP::get(f32, prefab->height);
        llvm::Value* drag = ctx.builder.getInt32(prefab->draggable ? 1 : 0);

        ctx.builder.CreateCall(
            ctx.getFunction("aiml_prefab_register", vd, {i8p, f32, f32, i8p, i32, i8p}),
            {nameStr, w, h, texStr, drag, childStr});

        LOG_INFO("visitTopLevel: emitted prefab register for '" + prefab->name + "'");
    }
}
void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = ctx.qualify(func->name);
    ctx.currentFunctionName = func->name;
    
    std::vector<IRType*> argTypes;
    std::vector<std::string> argNames;
    
    for (const auto& arg : func->args) {
        IRType* t = ctx.ir_gen->resolveType(arg.second);
        if (t) {
            argTypes.push_back(t);
            argNames.push_back(arg.first);
        }
    }
    
    IRType* retTy = nullptr;
    if (!func->returnType.empty()) retTy = ctx.ir_gen->resolveType(func->returnType);
    if (!retTy) retTy = ctx.ir_gen->resolveType("void");
    
    // Reuse forward-declared function if it exists (from registerTypeDecl), else create new
    IRValue* funcVal = nullptr;
    llvm::Function* existingFunc = ctx.module.getFunction(mangledName);
    if (existingFunc && existingFunc->empty()) {
        // Forward declaration exists — reuse it
        funcVal = ctx.ir_gen->wrap(static_cast<llvm::Value*>(existingFunc));
    } else {
        funcVal = ctx.ir_gen->createFunction(mangledName, retTy, argTypes);
    }

    // Set Personality Function for exception handling — only on definitions, not declarations
    if (!ctx.headerOnlyMode && ctx.ir_gen->getPersonalityFunction()) {
        ctx.ir_gen->setPersonalityFunction(funcVal, ctx.ir_gen->getPersonalityFunction());
    }

    // Register Function Info
    CompilerContext::FunctionInfo funcInfo;
    funcInfo.func = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(funcVal));
    funcInfo.argNames = argNames;
    funcInfo.version = func->version;
    funcInfo.compiled_version = func->version;  // locked at compile time
    // Assign fptable slot — preserve any pre-existing index (e.g. from an alias entry)
    if (ctx.functions.count(mangledName) && ctx.functions[mangledName].fptable_index >= 0)
        funcInfo.fptable_index = ctx.functions[mangledName].fptable_index;
    else
        funcInfo.fptable_index = ctx.fptable_next_index++;
    funcInfo.sourceYaml = YAML::Dump(func->sourceNode);
    // Track user-defined arg + return types for sys.bundle (recurses into list<T>/map<K,V>)
    auto trackType = [&](const std::string& typeName) {
        std::set<std::string> found;
        ctx.collectUserTypes(typeName, found);
        funcInfo.referencedTypes.insert(found.begin(), found.end());
    };
    for (const auto& arg : func->args) trackType(arg.second);
    trackType(func->returnType);
    ctx.functions[func->name] = funcInfo;
    ctx.functions[mangledName] = funcInfo;

    // Header Only mode check
    if (ctx.headerOnlyMode) {
        LOG_DEBUG("HeaderOnly: Skipping body for " + mangledName);
        return; 
    }

    // Create Entry Block directly attached to the function (no dependency on current insert point)
    llvm::Function* llvmFunc = funcInfo.func;
    llvm::BasicBlock* llvmEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmFunc);
    
    // Save previous block and symbol table, set to new entry
    IRBlock* oldBlock = ctx.ir_gen->getInsertBlock();
    auto savedSymbols = ctx.symbol_table;
    ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(llvmEntryBB));

    // Setup Arguments in Symbol Table — reject shadowing of module-level vars
    bool shadowError = false;
    for (size_t i = 0; i < argNames.size(); ++i) {
        if (ctx.symbol_table.count(argNames[i])) {
            LOG_ERROR("Variable shadowing is not allowed: function argument '" + argNames[i]
                      + "' in '" + func->name + "' shadows an existing variable");
            shadowError = true;
        }
        IRValue* argAlloca = ctx.ir_gen->createAlloca(argTypes[i], argNames[i]);
        IRValue* argVal = ctx.ir_gen->getFunctionArg(funcVal, i);
        ctx.ir_gen->createStore(argVal, argAlloca);

        Symbol sym;
        sym.ir_alloca_inst = argAlloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = argTypes[i];
        sym.typeName = func->args[i].second;
        ctx.symbol_table[argNames[i]] = sym;
    }
    if (shadowError) {
        ctx.symbol_table = savedSymbols;
        ctx.ir_gen->setInsertPoint(oldBlock);
        return;
    }

    // ── Trace prologue (AIML_TRACE env var) ──────────────────────────────────
    // if (aiml_trace_active(fname)) { build args_str; aiml_trace(fname, "=>", args_str); }
    // Hot path (trace inactive) = one conditional branch, never taken.
    {
        llvm::PointerType* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type*        i32t = ctx.builder.getInt32Ty();

        llvm::FunctionCallee traceActiveFn = ctx.module.getOrInsertFunction(
            "aiml_trace_active",
            llvm::FunctionType::get(i32t, {i8p}, false));
        llvm::FunctionCallee traceFn = ctx.module.getOrInsertFunction(
            "aiml_trace",
            llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p}, false));

        llvm::Value* fnameStr = ctx.builder.CreateGlobalStringPtr(func->name, "tr_fname");
        llvm::Value* trActive = ctx.builder.CreateCall(traceActiveFn, {fnameStr}, "tr_active");
        llvm::Value* trCond   = ctx.builder.CreateICmpNE(
            trActive, llvm::ConstantInt::get(i32t, 0), "tr_cond");

        llvm::BasicBlock* traceBB = llvm::BasicBlock::Create(ctx.context, "tr_enter", llvmFunc);
        llvm::BasicBlock* bodyBB  = llvm::BasicBlock::Create(ctx.context, "tr_body",  llvmFunc);
        ctx.builder.CreateCondBr(trCond, traceBB, bodyBB);

        // ── trace_bb: build "name=val, ..." string and call aiml_trace ────────
        ctx.builder.SetInsertPoint(traceBB);

        llvm::Value* argsStr = ctx.builder.CreateGlobalStringPtr("", "tr_args");
        bool firstArg = true;
        for (size_t i = 0; i < argNames.size(); ++i) {
            const std::string& nm = argNames[i];
            std::string prefix = (firstArg ? "" : ", ") + nm + "=";
            firstArg = false;

            Symbol& sym    = ctx.symbol_table[nm];
            llvm::Value* allocaV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym.ir_alloca_inst));
            llvm::Type*  elemTy  = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(sym.ir_element_type));
            llvm::Value* loaded  = ctx.builder.CreateLoad(elemTy, allocaV, nm + "_tr");

            llvm::Value* valStr = nullptr;
            if (elemTy->isIntegerTy(64)) {
                valStr = StringUtils::int64ToString(ctx, loaded);
            } else if (elemTy->isIntegerTy(32)) {
                valStr = StringUtils::intToString(ctx, loaded);
            } else if (elemTy->isIntegerTy(16) || elemTy->isIntegerTy(8) || elemTy->isIntegerTy(1)) {
                valStr = StringUtils::intToString(ctx, ctx.builder.CreateZExt(loaded, i32t));
            } else if (elemTy->isFloatTy()) {
                valStr = StringUtils::floatToString(ctx, loaded);
            } else if (elemTy->isDoubleTy()) {
                valStr = StringUtils::doubleToString(ctx, loaded);
            } else if (elemTy->isPointerTy()) {
                valStr = loaded; // i8* string or opaque handle
            } else {
                valStr = ctx.builder.CreateGlobalStringPtr("<?>", "tr_unk");
            }

            llvm::Value* prefixV = ctx.builder.CreateGlobalStringPtr(prefix, "tr_pfx");
            argsStr = StringUtils::concat(ctx, argsStr, prefixV);
            argsStr = StringUtils::concat(ctx, argsStr, valStr);
        }

        llvm::Value* dirStr = ctx.builder.CreateGlobalStringPtr("=>", "tr_dir");
        ctx.builder.CreateCall(traceFn, {fnameStr, dirStr, argsStr});
        ctx.builder.CreateBr(bodyBB);

        // ── body_bb: actual function body follows ─────────────────────────────
        ctx.builder.SetInsertPoint(bodyBB);
    }

    // Compile Body (track referenced user types for sys.bundle)
    ctx.bundleTrackingTarget = mangledName;
    if (func->body) {
        visitBlockStmt(func->body.get());
    }
    ctx.bundleTrackingTarget.clear();

    // Check if the last block has a terminator (like ret)
    llvm::BasicBlock* currentBB = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
    if (!currentBB->getTerminator()) {
        llvm::Type* unwrappedRetTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(retTy));
        if (unwrappedRetTy->isVoidTy()) {
            ctx.ir_gen->createRet(nullptr);
        } else if (unwrappedRetTy->isIntegerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantInt::get(unwrappedRetTy, 0)));
        } else if (unwrappedRetTy->isPointerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(unwrappedRetTy))));
        } else if (unwrappedRetTy->isFloatingPointTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantFP::get(unwrappedRetTy, 0.0)));
        } else {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::UndefValue::get(unwrappedRetTy)));
        }
    }

    // Restore insertion point and symbol table (function-local vars don't leak)
    ctx.symbol_table = savedSymbols;
    ctx.ir_gen->setInsertPoint(oldBlock);
    ctx.currentFunctionName = "";
}
Symbol* Compiler::resolveVariable(const std::string& name) {
    if (ctx.currentScope) {
        CompilerContext::Scope* scope = ctx.currentScope;
        while (scope) {
            if (scope->variables.count(name)) return &scope->variables[name];
            scope = scope->parent;
        }
    }
    
    // Check namespace globals
    if (ctx.currentNs && ctx.currentNs->globals.count(name)) {
        return &ctx.currentNs->globals[name];
    }
    
    // Legacy fallback during transition
    if (ctx.symbol_table.count(name)) return &ctx.symbol_table[name];
    
    // Check FQN fallback (e.g. `math.pi` -> `my.math.pi`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.symbol_table.count(qualifiedName)) return &ctx.symbol_table[qualifiedName];
    
    return nullptr;
}
IRValue* Compiler::resolveEnum(const std::string& baseName, const std::string& propName) {
    // Check current namespace
    if (ctx.currentNs && ctx.currentNs->enums.count(baseName)) {
        auto& enumDef = ctx.currentNs->enums[baseName];
        if (enumDef.count(propName)) return ctx.ir_gen->getInt32(enumDef[propName]);
    }
    
    // Legacy fallback
    if (ctx.enums.count(baseName)) {
        auto& enumDef = ctx.enums[baseName];
        if (enumDef.count(propName)) return ctx.ir_gen->getInt32(enumDef[propName]);
    }
    
    LOG_ERROR("Undefined enum variant: " + propName + " on enum " + baseName);
    return nullptr;
}
IRValue* Compiler::resolveProperty(Symbol* baseSym, const std::string& propName) {
    std::string className = baseSym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
         className = className.substr(4, className.length() - 5);
    }
    // buffer<T> → "buffer" (all buffer variants share the same class registration)
    if (className.rfind("buffer<", 0) == 0 && className.back() == '>') {
        className = "buffer";
    }
    
    IRValue* baseAlloca = baseSym->ir_alloca_inst ? baseSym->ir_alloca_inst : ctx.ir_gen->wrap(baseSym->alloca_inst);
    IRType* baseElemTy = baseSym->ir_element_type ? baseSym->ir_element_type : ctx.ir_gen->wrapType(baseSym->element_type);
    
    if (className.find("list<") == 0) {
        if (propName == "len") return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    } else if (className.find("map<") == 0) {
        llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (propName == "len") return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    } else if (className == "string") {
        if (propName == "len") {
            llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
            if (!strlenFunc)
                strlenFunc = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getInt64Ty(),
                        {ctx.builder.getInt8Ty()->getPointerTo()}, false),
                    llvm::Function::ExternalLinkage, "strlen", ctx.module);
            llvm::Value* strVal = ctx.builder.CreateLoad(
                ctx.builder.getInt8Ty()->getPointerTo(),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
            llvm::Value* len64 = ctx.builder.CreateCall(strlenFunc, {strVal});
            return ctx.ir_gen->wrap(ctx.builder.CreateTrunc(len64, ctx.builder.getInt32Ty(), "str_len_i32"));
        }
    }

    // ── array<T:N[:M[:...]]> properties: .len, .data_ptr, .dim0, .dim1, ... ──
    if (className.rfind("array<", 0) == 0 && className.back() == '>') {
        std::string inner = className.substr(6, className.size() - 7);
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t sep = inner.find(':', pos);
            if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
            parts.push_back(inner.substr(pos, sep - pos));
            pos = sep + 1;
        }
        std::vector<uint64_t> dims;
        for (size_t i = 1; i < parts.size(); ++i) dims.push_back(std::stoull(parts[i]));
        uint64_t totalCount = 1;
        for (auto d : dims) totalCount *= d;

        if (propName == "len") {
            return ctx.ir_gen->wrap(ctx.builder.getInt32((uint32_t)totalCount));
        }
        if (propName == "data_ptr") {
            llvm::Type* elemTy = ctx.getLLVMType(parts[0]);
            llvm::Type* arrTy = llvm::ArrayType::get(elemTy, totalCount);
            llvm::Value* arrPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* gep = ctx.builder.CreateGEP(arrTy, arrPtr,
                {ctx.builder.getInt32(0), ctx.builder.getInt32(0)}, "arr_data_ptr");
            return ctx.ir_gen->wrap(ctx.builder.CreateBitCast(gep, ctx.builder.getInt8Ty()->getPointerTo()));
        }
        // .dim0, .dim1, ...
        if (propName.rfind("dim", 0) == 0 && propName.size() > 3) {
            size_t dimIdx = std::stoul(propName.substr(3));
            if (dimIdx < dims.size())
                return ctx.ir_gen->wrap(ctx.builder.getInt32((uint32_t)dims[dimIdx]));
            LOG_ERROR(ctx.fmtLoc() + "array property " + propName + " out of range");
            return ctx.ir_gen->getInt32(0);
        }
    }

    // Check classes (legacy + namespace)
    CompilerContext::ClassInfo* classInfo = nullptr;
    if (ctx.currentNs && ctx.currentNs->classes.count(className)) classInfo = &ctx.currentNs->classes[className];
    else if (ctx.classes.count(className)) classInfo = &ctx.classes[className];
    
    if (classInfo) {
        // Handle chained property access: "field.rest" or "field-idx"
        {
            size_t dotP  = propName.find('.');
            size_t dashP = propName.find('-');
            if (dotP != std::string::npos || dashP != std::string::npos) {
                bool dashFirst = (dashP != std::string::npos) &&
                                 (dotP == std::string::npos || dashP < dotP);
                size_t sepP       = dashFirst ? dashP : dotP;
                std::string fieldName = propName.substr(0, sepP);
                std::string rest      = propName.substr(sepP + 1);

                if (classInfo->members.count(fieldName)) {
                    auto& member = classInfo->members[fieldName];
                    IRType* structTy = ctx.ir_gen->wrapType(classInfo->type);
                    IRValue* structPtr = baseAlloca;
                    if (baseElemTy->isPointer())
                        structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "chain_base");
                    IRValue* fieldGep   = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "chain_gep");
                    IRType*  fieldElemTy = ctx.ir_gen->wrapType(member.type);

                    if (dashFirst) {
                        // List index: field-idx
                        if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                            LOG_ERROR("Dash-index on non-list field: " + fieldName + " (type: " + member.typeName + ")");
                            return nullptr;
                        }
                        std::string elemName = member.typeName.substr(5, member.typeName.length() - 6);
                        llvm::Type* elemType = ctx.getLLVMType(elemName);
                        llvm::Value* listAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep));

                        llvm::Value* idxVal = nullptr;
                        try {
                            idxVal = ctx.builder.getInt32(std::stoi(rest));
                        } catch (...) {
                            Symbol* idxSym = resolveVariable(rest);
                            if (idxSym) {
                                IRValue* idxA  = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                                IRType*  idxET = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                                idxVal = ctx.builder.CreateLoad(
                                    static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxET)),
                                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxA)));
                                if (idxVal->getType()->isIntegerTy(64))
                                    idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                            } else {
                                LOG_ERROR("Invalid list index: " + rest);
                                return ctx.ir_gen->getInt32(0);
                            }
                        }
                        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listAllocaVal, idxVal, elemType);
                        size_t dotInRest = rest.find('.');
                        if (dotInRest == std::string::npos) {
                            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "chain_elem"));
                        }

                        std::string afterIdx = rest.substr(dotInRest + 1);
                        Symbol tmpSym;
                        tmpSym.typeName = elemName;
                        tmpSym.ir_alloca_inst = ctx.ir_gen->wrap(elemPtr);
                        tmpSym.ir_element_type = ctx.ir_gen->wrapType(elemType);
                        return resolveProperty(&tmpSym, afterIdx);
                    } else {
                        // Dot chain: field.rest — recurse
                        Symbol tmpSym;
                        tmpSym.typeName         = member.typeName;
                        tmpSym.ir_alloca_inst   = fieldGep;
                        tmpSym.ir_element_type  = fieldElemTy;
                        return resolveProperty(&tmpSym, rest);
                    }
                }
            }
        }

        if (classInfo->members.count(propName)) {
            auto& member = classInfo->members[propName];
            IRType* structTy = ctx.ir_gen->wrapType(classInfo->type);
            IRValue* structPtr = baseAlloca;

            if (baseElemTy->isPointer()) {
                structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base");
            }

            IRValue* gep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "prop_ptr");
            // For list<T>/buffer<T> fields, return the GEP pointer directly.
            // visitDeclStmt/visitSetStmt expect a pointer to memcpy from, not a loaded struct value.
            // Loading a 16-byte list struct and passing it as memcpy src would use the size field (e.g. 5) as a pointer.
            if (member.typeName.rfind("list<", 0) == 0 || member.typeName.rfind("buffer<", 0) == 0) {
                return gep;
            }
            IRType* propTy = ctx.ir_gen->wrapType(member.type);
            return ctx.ir_gen->createLoad(propTy, gep, propName + "_val");
        }

        // Builtin property getter: dispatch builtinMethod with same name (zero-arg accessor).
        // If preLoadBase, load the handle ptr from alloca first (HTTP handles).
        if (classInfo->isBuiltin && classInfo->builtinMethods.count(propName)) {
            llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* baseForHandler = classInfo->preLoadBase
                ? ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), rawAlloca, "h_base_p")
                : rawAlloca;
            auto dummyEval = [](AIML::AST::Expr*) -> AIML::IRValue* { return nullptr; };
            return classInfo->builtinMethods.at(propName)(ctx, baseForHandler, nullptr, dummyEval);
        }
    }

    LOG_ERROR("Undefined property: " + propName + " on type " + className);
    return nullptr;
}

IRValue* Compiler::resolvePropertyWritePtr(Symbol* baseSym, const std::string& propName, std::string& outTypeName) {
    std::string className = baseSym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
        className = className.substr(4, className.length() - 5);
    }

    IRValue* baseAlloca = baseSym->ir_alloca_inst ? baseSym->ir_alloca_inst : ctx.ir_gen->wrap(baseSym->alloca_inst);
    IRType* baseElemTy  = baseSym->ir_element_type ? baseSym->ir_element_type : ctx.ir_gen->wrapType(baseSym->element_type);

    CompilerContext::ClassInfo* classInfo = nullptr;
    if (ctx.currentNs && ctx.currentNs->classes.count(className)) classInfo = &ctx.currentNs->classes[className];
    else if (ctx.classes.count(className)) classInfo = &ctx.classes[className];

    if (!classInfo) { LOG_DEBUG("resolvePropertyWritePtr: no classInfo for " + className); return nullptr; }
    LOG_DEBUG("resolvePropertyWritePtr: class=" + className + " members=" + std::to_string(classInfo->members.size()) + " prop=" + propName);

    // Handle chained access: "field.rest" or "field-idx[.rest]"
    {
        size_t dotP  = propName.find('.');
        size_t dashP = propName.find('-');
        if (dotP != std::string::npos || dashP != std::string::npos) {
            bool dashFirst = (dashP != std::string::npos) &&
                             (dotP == std::string::npos || dashP < dotP);
            size_t sepP       = dashFirst ? dashP : dotP;
            std::string fieldName = propName.substr(0, sepP);
            std::string rest      = propName.substr(sepP + 1);

            if (classInfo->members.count(fieldName)) {
                auto& member = classInfo->members[fieldName];

                if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
                    LOG_ERROR("Cannot access private member " + fieldName);
                    return nullptr;
                } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                    LOG_ERROR("Cannot access protected member " + fieldName);
                    return nullptr;
                }

                IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
                IRValue* structPtr = baseAlloca;
                if (baseElemTy->isPointer())
                    structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "chain_base_w");
                IRValue* fieldGep    = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "chain_gep_w");
                IRType*  fieldElemTy = ctx.ir_gen->wrapType(member.type);

                if (dashFirst) {
                    // List element: field-idx[.further]
                    if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                        LOG_ERROR("Dash-index on non-list field: " + fieldName);
                        return nullptr;
                    }
                    std::string elemName = member.typeName.substr(5, member.typeName.length() - 6);
                    llvm::Type* elemType = ctx.getLLVMType(elemName);
                    llvm::Value* listAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep));

                    // rest may be "idx" or "idx.further"
                    size_t dotInRest = rest.find('.');
                    std::string idxStr   = (dotInRest != std::string::npos) ? rest.substr(0, dotInRest) : rest;
                    std::string afterIdx = (dotInRest != std::string::npos) ? rest.substr(dotInRest + 1) : "";

                    llvm::Value* idxVal = nullptr;
                    try {
                        idxVal = ctx.builder.getInt32(std::stoi(idxStr));
                    } catch (...) {
                        Symbol* idxSym = resolveVariable(idxStr);
                        if (idxSym) {
                            IRValue* idxA  = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                            IRType*  idxET = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                            idxVal = ctx.builder.CreateLoad(
                                static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxET)),
                                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxA)));
                            if (idxVal->getType()->isIntegerTy(64))
                                idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                        } else {
                            LOG_ERROR("Invalid list index: " + idxStr);
                            return nullptr;
                        }
                    }
                    llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listAllocaVal, idxVal, elemType);

                    if (afterIdx.empty()) {
                        outTypeName = elemName;
                        return ctx.ir_gen->wrap(elemPtr);
                    } else {
                        Symbol tmpSym;
                        tmpSym.typeName        = elemName;
                        tmpSym.ir_alloca_inst  = ctx.ir_gen->wrap(elemPtr);
                        tmpSym.ir_element_type = ctx.ir_gen->wrapType(elemType);
                        return resolvePropertyWritePtr(&tmpSym, afterIdx, outTypeName);
                    }
                } else {
                    // Dot chain: recurse into next field
                    Symbol tmpSym;
                    tmpSym.typeName        = member.typeName;
                    tmpSym.ir_alloca_inst  = fieldGep;
                    tmpSym.ir_element_type = fieldElemTy;
                    return resolvePropertyWritePtr(&tmpSym, rest, outTypeName);
                }
            }
        }
    }

    // Direct (leaf) member
    if (classInfo->members.count(propName)) {
        auto& member = classInfo->members[propName];
        if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
            LOG_ERROR("Cannot access private member " + propName);
            return nullptr;
        } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
            LOG_ERROR("Cannot access protected member " + propName);
            return nullptr;
        }
        if (member.is_const) {
            LOG_ERROR("Cannot assign to const member " + propName);
            return nullptr;
        }

        IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
        IRValue* structPtr = baseAlloca;
        if (baseElemTy->isPointer())
            structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base_w");

        outTypeName = member.typeName;
        return ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "set_prop_ptr_w");
    }

    return nullptr;
}

CompilerContext::FunctionInfo* Compiler::resolveFunction(const std::string& name) {
    if (ctx.currentNs && ctx.currentNs->functions.count(name)) return &ctx.currentNs->functions[name];
    if (ctx.functions.count(name)) return &ctx.functions[name];
    
    // Check FQN fallback (e.g., calling `calc` inside `my.math` resolves to `my.math.calc`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.functions.count(qualifiedName)) return &ctx.functions[qualifiedName];
    
    return nullptr;
}
} // namespace AST
} // namespace AIML
