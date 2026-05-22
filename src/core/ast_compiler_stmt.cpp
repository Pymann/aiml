#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "string_utils.h"
#include <algorithm>
#include <iostream>

namespace AIML {
namespace AST {

namespace {
void coerceOrStoreFieldDefault(CompilerContext& ctx,
                               llvm::Value* targetPtr,
                               const CompilerContext::StructMember& member,
                               llvm::Value* initVal) {
    if (!targetPtr || !initVal) return;

    llvm::Type* targetTy = member.type;
    llvm::Value* storeVal = initVal;

    if (member.typeName == "path" && storeVal->getType()->isPointerTy()) {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
        storeVal = ctx.builder.CreateCall(internFn, {storeVal}, "path_default_init");
    }

    bool copyByMemcpy =
        (member.typeName.find("buffer<") == 0) ||
        (member.typeName.find("list<") == 0) ||
        (member.typeName.find("map<") == 0) ||
        (targetTy && targetTy->isStructTy() && storeVal->getType()->isPointerTy());

    if (copyByMemcpy) {
        uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(targetTy);
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
        llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
        ctx.builder.CreateCall(memcpyFunc, {
            targetPtr,
            storeVal,
            ctx.getSizeConst(structSize)
        });
        return;
    }

    if (targetTy && targetTy != storeVal->getType()) {
        if (targetTy->isIntegerTy() && storeVal->getType()->isIntegerTy()) {
            unsigned dstBits = targetTy->getIntegerBitWidth();
            unsigned srcBits = storeVal->getType()->getIntegerBitWidth();
            if (dstBits < srcBits)
                storeVal = ctx.builder.CreateTrunc(storeVal, targetTy, "default_trunc");
            else if (srcBits == 1)
                storeVal = ctx.builder.CreateZExt(storeVal, targetTy, "default_zext_bool");
            else
                storeVal = ctx.builder.CreateSExt(storeVal, targetTy, "default_sext");
        } else if (targetTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy()) {
            storeVal = ctx.builder.CreateSIToFP(storeVal, targetTy, "default_int_to_fp");
        } else if (targetTy->isIntegerTy() && storeVal->getType()->isFloatingPointTy()) {
            storeVal = ctx.builder.CreateFPToSI(storeVal, targetTy, "default_fp_to_int");
        } else if (targetTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy()) {
            storeVal = ctx.builder.CreateFPCast(storeVal, targetTy, "default_fp_cast");
        }
    }

    ctx.builder.CreateStore(storeVal, targetPtr);
}
}

void Compiler::visitStatement(Stmt* stmt) {
    if (stmt->sourceLine > 0) ctx.currentLine = stmt->sourceLine;
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) visitDeclStmt(decl);
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) visitSetStmt(setStmt);
    else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) visitExprStmt(exprStmt);
    else if (auto retStmt = dynamic_cast<RetStmt*>(stmt)) visitRetStmt(retStmt);
    else if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt)) visitBlockStmt(blockStmt);
    else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) visitIfStmt(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) visitWhileStmt(whileStmt);
    else if (auto tryStmt = dynamic_cast<TryStmt*>(stmt)) visitTry(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStmt*>(stmt)) visitThrow(throwStmt);
    else if (auto idxStmt = dynamic_cast<IdxStmt*>(stmt)) visitIdxStmt(idxStmt);
}
void Compiler::visitDeclStmt(DeclStmt* decl) {
    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;

    // Reset Kv.get/get_copy tracking flags at statement entry — they leak from
    // earlier statements (e.g. a `set: [x, kv.get(k)]` that doesn't consume them).
    // If this decl's initial value evaluation contains a kv.get/get_copy call,
    // the flags will be set fresh by visitObjectMethodCall before we check them below.
    ctx.kvGetCalled = false;
    ctx.kvGetCopy = false;
    ctx.kvGetValueType.clear();

    // Check if this variable was pre-promoted to a GlobalVariable (non-library
    // top-level path). If so, skip alloca creation and only emit the initializer.
    bool isPrePromotedGlobal = false;
    IRValue* alloca = nullptr;
    llvm::Value* unwrappedAlloca = nullptr;

    if (ctx.symbol_table.count(decl->varName)) {
        auto& existing = ctx.symbol_table[decl->varName];
        if (existing.is_global && ctx.inTopLevelInit) {
            // Pre-promoted GlobalVariable — reuse storage, emit initializer in-order.
            isPrePromotedGlobal = true;
            alloca = existing.ir_alloca_inst;
            unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        } else {
            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Variable shadowing is not allowed: '" + decl->varName
                      + "' is already declared in an outer scope");
            return;
        }
    }

    if (!isPrePromotedGlobal) {
        alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
        // Zero-initialize the allocated memory
        unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
    }

    llvm::Type* unwrappedTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
    if (!isPrePromotedGlobal)
        ctx.builder.CreateStore(llvm::Constant::getNullValue(unwrappedTy), unwrappedAlloca);
    
    std::string tName = decl->typeName;
    if (tName.find("list<") == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(5, tName.length() - 6);
        llvm::Type* elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (elemTy) ListImpl::initList(ctx, unwrappedAlloca, elemTy, 4);
    } else if (tName.find("map<") == 0 && tName.back() == '>') {
        std::string inner = tName.substr(4, tName.length() - 5);
        size_t colon = inner.find(':');
        if (colon != std::string::npos) {
            std::string kName = inner.substr(0, colon);
            std::string vName = inner.substr(colon + 1);
            llvm::Type* kTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
            llvm::Type* vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
            if (kTy && vTy) MapImpl::initMap(ctx, unwrappedAlloca, kTy, vTy, 4);
        }
    } else if (tName.find("chan<") == 0 && tName.back() == '>') {
        // Auto-init with capacity=0 (true rendezvous). Use chan.init(N) for buffered.
        ChanImpl::init(ctx, unwrappedAlloca, ctx.builder.getInt64(0));
    } else if (tName.rfind("buffer<", 0) == 0 && tName.back() == '>') {
        // Auto-alloc when there is no initializer OR when the initializer is a
        // buffer literal [v1, v2, ...] (which expands to push calls, not a struct copy).
        bool isLiteral = decl->initialValue &&
                         dynamic_cast<BufferLiteralExpr*>(decl->initialValue.get());
        if (!decl->initialValue || isLiteral)
            BufferImpl::alloc(ctx, unwrappedAlloca, ctx.builder.getInt32(4096));
    } else if (tName == "ui_style" && !decl->initialValue) {
        // Initialize ui_style with current top-of-style-stack defaults
        auto* p    = llvm::PointerType::get(ctx.context, 0);
        auto* voidTy = ctx.builder.getVoidTy();
        ctx.builder.CreateCall(
            ctx.getFunction("aiml_ui_copy_current_style", voidTy, {p}),
            {unwrappedAlloca});
    }
    
    if (!isPrePromotedGlobal) {
        Symbol sym;
        sym.ir_alloca_inst = alloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = ty;
        sym.typeName = decl->typeName;
        sym.is_const = decl->is_const;
        sym.is_volatile = decl->is_volatile;
        ctx.symbol_table[decl->varName] = sym;
    }
    
    LOG_DEBUG("Compiled AST Decl: " + decl->varName + " of " + decl->typeName);

    if (!decl->initialValue) {
        auto classIt = ctx.classes.find(decl->typeName);
        if (classIt != ctx.classes.end() && !classIt->second.isBuiltin && classIt->second.type) {
            std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
            orderedMembers.reserve(classIt->second.members.size());
            for (const auto& [memberName, member] : classIt->second.members)
                orderedMembers.push_back({memberName, member});
            std::sort(orderedMembers.begin(), orderedMembers.end(),
                      [](const auto& a, const auto& b) { return a.second.index < b.second.index; });

            for (const auto& [memberName, member] : orderedMembers) {
                if (!member.initValue) continue;
                IRValue* fieldInitV = visitExpression(member.initValue.get());
                if (!fieldInitV) {
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Failed to evaluate default value for member '" +
                              memberName + "' of type '" + decl->typeName + "'");
                    continue;
                }
                llvm::Value* initVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV));
                if (!initVal) {
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Null LLVM value for default member '" +
                              memberName + "' of type '" + decl->typeName + "'");
                    continue;
                }
                llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classIt->second.type,
                                                                    unwrappedAlloca,
                                                                    member.index,
                                                                    memberName + "_default_gep");
                coerceOrStoreFieldDefault(ctx, fieldPtr, member, initVal);
            }
        }
    }
    
    if (decl->initialValue) {
        // Buffer literal: decl: [name, buffer<T>, [v1, v2, ...]]
        // Buffer is already alloc'd above; emit one write per element.
        if (auto* bufLit = dynamic_cast<BufferLiteralExpr*>(decl->initialValue.get())) {
            std::string elemName = tName.substr(7, tName.size() - 8); // strip "buffer<" + ">"
            llvm::Type* elemTy = ctx.getLLVMType(elemName);
            for (auto& elem : bufLit->elements) {
                IRValue* val = visitExpression(elem.get());
                if (!val) continue;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                if (elemTy && v->getType() != elemTy) {
                    if (elemTy->isFloatingPointTy() && v->getType()->isFloatingPointTy())
                        v = ctx.builder.CreateFPCast(v, elemTy, "fp_cast");
                    else if (elemTy->isIntegerTy() && v->getType()->isIntegerTy())
                        v = ctx.builder.CreateZExtOrTrunc(v, elemTy, "int_cast");
                }
                BufferImpl::write(ctx, unwrappedAlloca, v, elemTy ? elemTy : v->getType());
            }
            return; // done — skip normal expression init
        }

        ctx.deserializeTargetType = decl->typeName;
        IRValue* initVal = visitExpression(decl->initialValue.get());
        ctx.deserializeTargetType.clear();
        if (initVal) {
            // path: auto-intern so pointer identity == path equality
            if (decl->typeName == "path") {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(internFn, {raw}, "path_init"));
            }
            // String → int coercion: decl: [x, i32/i64, "${str_var}"]
            // Null-safe: if pointer is null (missing KV key etc.) → returns 0 instead of crashing atoi(NULL)
            if ((decl->typeName == "i32" || decl->typeName == "i64") && !decl->typeName.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                if (v->getType()->isPointerTy()) {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    bool is32 = (decl->typeName == "i32");
                    llvm::Type* intTy = is32 ? ctx.builder.getInt32Ty() : ctx.builder.getInt64Ty();
                    llvm::Value* zero = is32 ? ctx.builder.getInt32(0)
                                             : (llvm::Value*)ctx.builder.getInt64(0);
                    llvm::Value* isNull = ctx.builder.CreateIsNull(v, "str2i_null");
                    llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "str2i_null_bb", parent);
                    llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "str2i_ok",      parent);
                    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "str2i_merge",   parent);
                    ctx.builder.CreateCondBr(isNull, nullBB, okBB);
                    ctx.builder.SetInsertPoint(nullBB);
                    ctx.builder.CreateBr(mergeBB);
                    ctx.builder.SetInsertPoint(okBB);
                    llvm::Function* fn = is32 ? ctx.getFunction("atoi",  ctx.builder.getInt32Ty(), {i8p})
                                              : ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                    llvm::Value* callResult = ctx.builder.CreateCall(fn, {v}, is32 ? "str2i32_v" : "str2i64_v");
                    ctx.builder.CreateBr(mergeBB);
                    ctx.builder.SetInsertPoint(mergeBB);
                    llvm::PHINode* phi = ctx.builder.CreatePHI(intTy, 2, is32 ? "str2i32" : "str2i64");
                    phi->addIncoming(zero, nullBB);
                    phi->addIncoming(callResult, okBB);
                    initVal = ctx.ir_gen->wrap(phi);
                }
            }
            // buffer<u8>: handler returns a fresh alloca → memcpy struct content into target alloca
            if (decl->typeName.find("buffer<") == 0) {
                llvm::Type* bufTy = ctx.getLLVMType("buffer");
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(mcf, {
                    unwrappedAlloca,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal)),
                    ctx.getSizeConst(sz)
                });
            } else if (decl->typeName.find("list<") == 0) {
                llvm::Value* rawInitVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                if (rawInitVal->getType()->isPointerTy()) {
                    // Builtin handler returns alloca ptr → memcpy struct contents
                    llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
                    uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
                    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                    llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                    ctx.builder.CreateCall(mcf, {
                        unwrappedAlloca,
                        rawInitVal,
                        ctx.getSizeConst(sz)
                    });
                } else {
                    // User function returns list struct value → store directly
                    ctx.builder.CreateStore(rawInitVal, unwrappedAlloca);
                }
            } else {
                // For user-defined struct types (isBuiltin=false, real LLVM struct),
                // builtin handlers like next_event/next_rpc return an alloca pointer
                // to a temporary struct → memcpy into target alloca instead of store.
                // Only do this when: (1) target LLVM type is a struct, AND
                // (2) the initVal is a pointer type (i.e. an alloca ptr, not a struct value).
                llvm::Type* declTy = ctx.getLLVMType(decl->typeName);
                llvm::Value* initRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));

                // Enforce kv.get / kv.get_copy ↔ decl-type pairing for class
                // value types with heap fields.  See doc/issue/kv_get_mutate_set_same_key_crash.md.
                //
                //   decl: [p, T,      kv.get_copy(k)]  → OK (deep-copy below)
                //   decl: [p, ptr<T>, kv.get(k)]       → OK (zero-copy view)
                //   decl: [p, T,      kv.get(k)]       → ERROR
                //   decl: [p, ptr<T>, kv.get_copy(k)]  → ERROR
                //
                // ptr<T> / primitive-only structs are excluded from the check
                // because their lifetime model is unaffected.
                if (ctx.kvGetCalled) {
                    bool isPtrTarget = (decl->typeName.rfind("ptr<", 0) == 0 ||
                                        decl->typeName.rfind("ref<", 0) == 0)
                                       && decl->typeName.back() == '>';
                    std::string innerName = isPtrTarget
                        ? decl->typeName.substr(4, decl->typeName.size() - 5)
                        : decl->typeName;
                    auto ciIt2 = ctx.classes.find(innerName);
                    bool isHeapClass = ciIt2 != ctx.classes.end() && ciIt2->second.hasHeapFields;
                    if (isHeapClass) {
                        if (ctx.kvGetCopy && isPtrTarget) {
                            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                "kv.get_copy must be assigned to a value-type, not ptr<" + innerName + ">. "
                                "Deep-copy is wasted on a pointer view. "
                                "Use 'decl: [" + decl->varName + ", " + innerName + ", kv.get_copy(k)]' "
                                "or use 'kv.get' (without _copy) for a zero-copy view.");
                        } else if (!ctx.kvGetCopy && !isPtrTarget) {
                            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                "kv.get of struct '" + innerName + "' must be assigned to ptr<" + innerName + "> (zero-copy view), "
                                "not to a value-type. A shallow memcpy into a value-type shares heap "
                                "pointers with the kv decoded_cache and crashes on mutate-and-set-same-key. "
                                "Use 'decl: [" + decl->varName + ", ptr<" + innerName + ">, kv.get(k)]' "
                                "for a view, or 'decl: [" + decl->varName + ", " + innerName + ", kv.get_copy(k)]' "
                                "for an independent deep-copy.");
                        }
                    }
                    ctx.kvGetCalled = false;
                    ctx.kvGetValueType.clear();
                }

                // Interface upcast: decl: [s, ptr<Iface>, c] where c is a class
                // instance / ref<Class>.  The target is a fat pointer struct
                // {i8* obj, i8* vtbl}.  We need the SOURCE class name to look up
                // the __vtable_<Class>_<Iface> global.
                if (decl->typeName.rfind("ptr<", 0) == 0 && decl->typeName.back() == '>') {
                    std::string ifaceName = decl->typeName.substr(4, decl->typeName.size() - 5);
                    auto ifaceIt = ctx.interfaces.find(ifaceName);
                    if (ifaceIt != ctx.interfaces.end()) {
                        // Find source class from the init expression's symbol.
                        std::string srcClass;
                        if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                            if (Symbol* srcSym = resolveVariable(varExpr->name)) {
                                std::string srcType = srcSym->typeName;
                                if (srcType.rfind("ref<", 0) == 0 && srcType.back() == '>')
                                    srcClass = srcType.substr(4, srcType.size() - 5);
                                else if (srcType.rfind("ptr<", 0) == 0 && srcType.back() == '>')
                                    srcClass = srcType.substr(4, srcType.size() - 5);
                                else if (ctx.classes.count(srcType))
                                    srcClass = srcType;
                            }
                        }
                        if (!srcClass.empty() && ctx.classes.count(srcClass)) {
                            // initRaw should be the object pointer (i8*).  For
                            // ref<Class>/ptr<Class> srcs visitExpression returns
                            // the loaded pointer; that's already correct.  For a
                            // by-value class instance variable, take its alloca.
                            llvm::Value* objPtr = initRaw;
                            if (objPtr && !objPtr->getType()->isPointerTy()) {
                                if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                                    if (Symbol* srcSym = resolveVariable(varExpr->name)) {
                                        objPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                            srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                                   : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                                    }
                                }
                            }
                            std::string vtableName = "__vtable_" + srcClass + "_" + ifaceName;
                            llvm::GlobalVariable* vtbl = ctx.module.getNamedGlobal(vtableName);
                            if (vtbl) {
                                auto* fpTy = llvm::cast<llvm::StructType>(declTy);
                                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                                llvm::Value* objField = ctx.builder.CreateStructGEP(fpTy, unwrappedAlloca, 0, "fp_obj");
                                llvm::Value* vtblField = ctx.builder.CreateStructGEP(fpTy, unwrappedAlloca, 1, "fp_vtbl");
                                ctx.builder.CreateStore(ctx.builder.CreateBitCast(objPtr, i8p), objField);
                                ctx.builder.CreateStore(ctx.builder.CreateBitCast(vtbl, i8p), vtblField);
                                LOG_DEBUG("interface upcast: " + decl->varName + " = " + srcClass + " as ptr<" + ifaceName + ">");
                                return;
                            } else {
                                LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                    "class '" + srcClass + "' does not implement interface '" + ifaceName + "' (no " + vtableName + " global)");
                            }
                        }
                    }
                }

                if (declTy && declTy->isStructTy() && initRaw && initRaw->getType()->isPointerTy()) {
                    // ctx.kvGetCopy is set when the initialValue was `kv.get(k, {copy: true})`.
                    // In that case, do a deep-copy (encode-then-decode roundtrip) instead of
                    // a flat memcpy so the destination's heap allocations (strings, list
                    // buffers, etc.) are INDEPENDENT of the kv decoded_cache.  Otherwise
                    // mutate-and-set-same-key crashes — see doc/issue/kv_get_mutate_set_same_key_crash.md.
                    auto ciIt = ctx.classes.find(decl->typeName);
                    bool needsDeepCopy = ctx.kvGetCopy
                                         && ciIt != ctx.classes.end()
                                         && ciIt->second.hasHeapFields
                                         && ciIt->second.serNodesVar;
                    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                    if (needsDeepCopy) {
                        // Destination alloca is already zero-initialised (line 116).
                        // aiml_ser_deep_copy encodes src to a temp blob then decodes into dst,
                        // which malloc's fresh heap for every string/list/map/buffer field.
                        llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
                            ctx.builder.getVoidTy(),
                            {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                        ctx.builder.CreateCall(fn, {
                            unwrappedAlloca,
                            initRaw,
                            ciIt->second.serNodesVar,
                            ctx.builder.getInt32(ciIt->second.serNodesCount)
                        });
                    } else {
                        uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(declTy);
                        llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                        ctx.builder.CreateCall(mcf, {
                            unwrappedAlloca,
                            initRaw,
                            ctx.getSizeConst(sz)
                        });
                    }
                    ctx.kvGetCopy = false;  // reset; only applies to this decl
                } else {
                    // ptr<T>/ref<T> = struct_var: store the address of the struct, not the
                    // struct value itself (which would overflow the 8-byte pointer alloca).
                    llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                    bool isPtrTarget = (decl->typeName.rfind("ptr<", 0) == 0 ||
                                        decl->typeName.rfind("ref<", 0) == 0)
                                       && decl->typeName.back() == '>';
                    if (isPtrTarget && storeVal && storeVal->getType()->isStructTy()) {
                        // initVal is a loaded struct — we need the alloca (address) instead
                        if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                            Symbol* srcSym = resolveVariable(varExpr->name);
                            if (srcSym) {
                                llvm::Value* addrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                    srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                           : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                                initVal = ctx.ir_gen->wrap(addrVal);
                            }
                        }
                    }
                    // Numeric coercion (int↔int, int→float, float→int)
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                    if (sv) {
                        llvm::Type* targetTy = ctx.getLLVMType(decl->typeName);
                        if (targetTy && targetTy != sv->getType()) {
                            if (targetTy->isIntegerTy() && sv->getType()->isIntegerTy()) {
                                unsigned dstBits = targetTy->getIntegerBitWidth();
                                unsigned srcBits = sv->getType()->getIntegerBitWidth();
                                if (dstBits < srcBits)
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_narrow"));
                                else if (srcBits == 1)
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_bool"));
                                else
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_widen"));
                            } else if (targetTy->isFloatingPointTy() && sv->getType()->isIntegerTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(sv, targetTy, "int_to_fp"));
                            } else if (targetTy->isIntegerTy() && sv->getType()->isFloatingPointTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateFPToSI(sv, targetTy, "fp_to_int"));
                            } else if (targetTy->isFloatingPointTy() && sv->getType()->isFloatingPointTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateFPCast(sv, targetTy, "fp_cast"));
                            }
                        }
                    }
                    ctx.ir_gen->createStore(initVal, alloca);
                }
            }
        }
    }
}
void Compiler::visitSetStmt(SetStmt* setStmt) {
    // Reset Kv.get/get_copy flags so they don't leak into the next decl.
    // visitSetStmt does not currently use them — the kv.get pattern paired with
    // a `set:` (instead of decl-with-init) doesn't trigger the diagnostic, on
    // the principle that the set target already has a declared type and the
    // user has had an explicit decl: line earlier to declare it.
    ctx.kvGetCalled = false;
    ctx.kvGetCopy = false;
    ctx.kvGetValueType.clear();

    std::string targetStr = setStmt->target;
    if (targetStr.find("<.") == 0) {
        targetStr = "this." + targetStr.substr(2);
    }
    
    // Support dot-notation (obj.prop) and dash-notation (arr-index)
    size_t dotPos = targetStr.find('.');
    size_t dashPos = targetStr.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    
    std::string baseName = (sepPos != std::string::npos) ? targetStr.substr(0, sepPos) : targetStr;
    std::string propName = (sepPos != std::string::npos) ? targetStr.substr(sepPos + 1) : "";

    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "Undefined variable (set target): " + targetStr);
        return;
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (!baseAlloca) return;
    
    // Expose the target variable's type so db.open_kv / sys.deserialize can infer
    // the value type.  For plain variables (propName empty) use the variable's own
    // type.  For `this.field` assignments (class member writes), look up the
    // field's declared type so that e.g. set:[<.kv, {call:[db.open_kv,...]}] with
    // member type "Kv<string:i32>" produces the correct val_size.
    if (propName.empty()) {
        ctx.deserializeTargetType = sym->typeName;
    } else if (baseName == "this" && !propName.empty()) {
        // Resolve field type from enclosing class
        if (!ctx.currentClassName.empty()) {
            auto cit = ctx.classes.find(ctx.currentClassName);
            if (cit != ctx.classes.end()) {
                auto mit = cit->second.members.find(propName);
                if (mit != cit->second.members.end()) {
                    ctx.deserializeTargetType = mit->second.typeName;
                }
            }
        }
    }
    IRValue* val = visitExpression(setStmt->value.get());
    ctx.deserializeTargetType.clear();
    if (!val) {
        LOG_ERROR(ctx.fmtLoc() + "Failed to evaluate value expression for assignment to: " + targetStr);
        return;
    }
    
    if (propName.empty()) {
        if (sym->is_const) {
             LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
             return;
        }
        if (sym->typeName.find("list<") == 0) {
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (rawVal->getType()->isPointerTy()) {
                // Builtin handler / alloca ptr → memcpy struct contents
                llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    rawVal,
                    ctx.getSizeConst(structSize)
                });
            } else {
                // User function returns list struct value → store directly
                ctx.builder.CreateStore(rawVal, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
            }
            return;
        }
        if (sym->typeName.find("buffer<") == 0) {
            llvm::Type* bufTy = ctx.getLLVMType("buffer");
            if (bufTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.getSizeConst(structSize)
                });
                return;
            }
        }
        if (sym->typeName.find("map<") == 0) {
            llvm::Type* mapTy = ctx.getLLVMType(sym->typeName);
            if (mapTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(mapTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.getSizeConst(structSize)
                });
                return;
            }
        }
        // User struct/class assigned from deserialized i8* heap pointer → memcpy
        auto classIt = ctx.classes.find(sym->typeName);
        if (classIt != ctx.classes.end() && !classIt->second.isBuiltin && classIt->second.type) {
            llvm::StructType* structTy = classIt->second.type;
            uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            // Only memcpy if the value is a pointer (i8* from deserializer), not a struct value
            if (rawVal->getType()->isPointerTy()) {
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    rawVal,
                    ctx.getSizeConst(structSize)
                });
                return;
            }
        }
        // path type: auto-intern the value so pointer equality == path equality
        if (sym->typeName == "path") {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            llvm::Value* interned = ctx.builder.CreateCall(internFn, {rawVal}, "path_interned");
            val = ctx.ir_gen->wrap(interned);
        }
        // String → int coercion: set: [x, "${str_var}"] where x is i32/i64
        // Null-safe: if pointer is null → 0 instead of crashing atoi(NULL)
        if (sym->typeName == "i32" || sym->typeName == "i64") {
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (v->getType()->isPointerTy()) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                bool is32 = (sym->typeName == "i32");
                llvm::Type* intTy = is32 ? ctx.builder.getInt32Ty() : ctx.builder.getInt64Ty();
                llvm::Value* zero = is32 ? ctx.builder.getInt32(0)
                                         : (llvm::Value*)ctx.builder.getInt64(0);
                llvm::Value* isNull = ctx.builder.CreateIsNull(v, "str2i_null");
                llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "str2i_null_bb", parent);
                llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "str2i_ok",      parent);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "str2i_merge",   parent);
                ctx.builder.CreateCondBr(isNull, nullBB, okBB);
                ctx.builder.SetInsertPoint(nullBB);
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(okBB);
                llvm::Function* fn = is32 ? ctx.getFunction("atoi",  ctx.builder.getInt32Ty(), {i8p})
                                          : ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                llvm::Value* callResult = ctx.builder.CreateCall(fn, {v}, is32 ? "str2i32_v" : "str2i64_v");
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* phi = ctx.builder.CreatePHI(intTy, 2, is32 ? "str2i32" : "str2i64");
                phi->addIncoming(zero, nullBB);
                phi->addIncoming(callResult, okBB);
                val = ctx.ir_gen->wrap(phi);
            }
        }
        // Numeric coercion (int↔int, int→float, float→int)
        {
            llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (sv) {
                llvm::Type* targetTy = ctx.getLLVMType(sym->typeName);
                if (targetTy && targetTy != sv->getType()) {
                    if (targetTy->isIntegerTy() && sv->getType()->isIntegerTy()) {
                        unsigned dstBits = targetTy->getIntegerBitWidth();
                        unsigned srcBits = sv->getType()->getIntegerBitWidth();
                        if (dstBits < srcBits)
                            val = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_narrow"));
                        else if (srcBits == 1)
                            val = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_bool"));
                        else
                            val = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_widen"));
                    } else if (targetTy->isFloatingPointTy() && sv->getType()->isIntegerTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(sv, targetTy, "int_to_fp"));
                    } else if (targetTy->isIntegerTy() && sv->getType()->isFloatingPointTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateFPToSI(sv, targetTy, "fp_to_int"));
                    } else if (targetTy->isFloatingPointTy() && sv->getType()->isFloatingPointTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateFPCast(sv, targetTy, "fp_cast"));
                    }
                }
            }
        }
        ctx.ir_gen->createStore(val, baseAlloca);
    } else {
        std::string className = sym->typeName;
        if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
             className = className.substr(4, className.length() - 5);
        }
        
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash && (className.find("list<") == 0 || llvmBaseElemTy->isArrayTy())) {
            size_t dashInProp = propName.find('-');
            if (dashInProp != std::string::npos) {
                LOG_ERROR(ctx.fmtLoc() + "Slice assignment is not supported yet: " + targetStr);
                return;
            }
            
            llvm::Value* idxVal = nullptr;
            try {
                idxVal = ctx.builder.getInt32(std::stoi(propName));
            } catch(...) {
                Symbol* idxSym = resolveVariable(propName);
                if (idxSym) {
                    IRValue* idxAlloca = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                    IRType* idxElemTy = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                    idxVal = ctx.builder.CreateLoad(static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxElemTy)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxAlloca)));
                    if (idxVal->getType()->isIntegerTy(64)) idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                } else {
                    LOG_ERROR(ctx.fmtLoc() + "Invalid list index for set: " + propName);
                    return;
                }
            }
            
            llvm::Value* elemPtr = nullptr;
            if (llvmBaseElemTy->isArrayTy()) {
                elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), {ctx.builder.getInt32(0), idxVal});
            } else {
                // Resolve the inner element type (e.g. "list<i32>" → i32).
                // sym->element_type is the list *container* struct, not the element type.
                llvm::Type* innerElemTy = llvmBaseElemTy; // fallback
                if (className.find("list<") == 0 && className.back() == '>') {
                    std::string innerName = className.substr(5, className.length() - 6);
                    llvm::Type* resolved = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(innerName)));
                    if (resolved) innerElemTy = resolved;
                }
                elemPtr = ListImpl::getElementPtr(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), idxVal, innerElemTy);
            }
            ctx.builder.CreateStore(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), elemPtr);
            return;
        }
        
        // Try resolvePropertyWritePtr first — handles both shallow (a.b) and deep (a.b.c) chains.
        {
            Symbol tmpBaseSym;
            tmpBaseSym.typeName        = sym->typeName;
            tmpBaseSym.ir_alloca_inst  = baseAlloca;
            tmpBaseSym.ir_element_type = baseElemTy;
            std::string finalTypeName;
            IRValue* targetGep = resolvePropertyWritePtr(&tmpBaseSym, propName, finalTypeName);
            if (targetGep) {
                // Apply integer narrowing/widening for final member type
                if (!finalTypeName.empty()) {
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (sv && sv->getType()->isIntegerTy()) {
                        llvm::Type* targetTy = ctx.getLLVMType(finalTypeName);
                        if (targetTy && targetTy->isIntegerTy() && targetTy != sv->getType()) {
                            unsigned dstBits = targetTy->getIntegerBitWidth();
                            unsigned srcBits = sv->getType()->getIntegerBitWidth();
                            if (dstBits < srcBits)
                                val = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_member"));
                            else if (srcBits == 1)
                                val = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_member"));
                            else
                                val = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_member"));
                        }
                    }
                }
                ctx.ir_gen->createStore(val, targetGep);
                return;
            }
        }

        // Builtin property setter for handle types (e.g. resp.status, resp.body)
        if (!className.empty() && ctx.classes.count(className)) {
            auto& classInfo = ctx.classes[className];
            if (classInfo.isBuiltin && classInfo.builtinSetters.count(propName)) {
                llvm::Value* baseVal = ctx.builder.CreateLoad(
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "bs_load");
                classInfo.builtinSetters.at(propName)(ctx, baseVal, val);
                return;
            }
        }
        LOG_ERROR(ctx.fmtLoc() + "Undefined property for set: " + targetStr);
    }
}
void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    visitExpression(exprStmt->expr.get());
}
void Compiler::visitRetStmt(RetStmt* retStmt) {
    llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
    llvm::Type* retTy = currentFunc->getReturnType();

    // Void function: always emit ret void, evaluate expression only for side effects
    if (retTy->isVoidTy()) {
        if (retStmt->value) visitExpression(retStmt->value.get());
        ctx.ir_gen->createRet(nullptr);
    } else if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        if (val) {
            ctx.ir_gen->createRet(val);
        } else {
             LOG_ERROR(ctx.fmtLoc() + "Failed to evaluate return expression");
             ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
        }
    } else {
        LOG_ERROR(ctx.fmtLoc() + "Empty return statement in non-void function!");
        ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
    }
    // Move to a dead block so code emitted after return doesn't corrupt the ret block.
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "ret_dead", currentFunc);
    ctx.builder.SetInsertPoint(deadBB);
}
void Compiler::visitBlockStmt(BlockStmt* block) {
    auto savedSymbols = ctx.symbol_table;
    for (const auto& stmt : block->statements) {
        visitStatement(stmt.get());
    }
    ctx.symbol_table = savedSymbols;
}
void Compiler::visitIfStmt(IfStmt* ifStmt) {
    if (!ifStmt->condition) return;
    
    IRValue* condVal = visitExpression(ifStmt->condition.get());
    if (!condVal) return;
    
    // Ensure the condition is boolean (i1). In AIML, any non-zero is true.
    // For now assume strictly typed, or cast:
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0); // Assuming 32-bit compare for now
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "to_bool");
    }
    
    IRBlock* thenBB = ctx.ir_gen->createBasicBlock("if_then");
    IRBlock* mergeBB = nullptr;
    IRBlock* elseBB = nullptr;
    
    if (ifStmt->elseBranch) {
        elseBB = ctx.ir_gen->createBasicBlock("if_else");
    }
    mergeBB = ctx.ir_gen->createBasicBlock("if_merge");
    
    ctx.ir_gen->createCondBr(condVal, thenBB, ifStmt->elseBranch ? elseBB : mergeBB);
    
    // THEN branch
    ctx.ir_gen->setInsertPoint(thenBB);
    if (ifStmt->thenBranch) {
        visitBlockStmt(ifStmt->thenBranch.get());
    }
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(mergeBB);
    }

    // ELSE branch
    if (ifStmt->elseBranch) {
        ctx.ir_gen->setInsertPoint(elseBB);
        visitBlockStmt(ifStmt->elseBranch.get());
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(mergeBB);
    }

    // MERGE block
    ctx.ir_gen->setInsertPoint(mergeBB);
}
void Compiler::visitWhileStmt(WhileStmt* whileStmt) {
    if (!whileStmt->condition) return;
    
    IRBlock* loopBB = ctx.ir_gen->createBasicBlock("while_loop");
    IRBlock* bodyBB = ctx.ir_gen->createBasicBlock("while_body");
    IRBlock* endBB = ctx.ir_gen->createBasicBlock("while_end");
    
    // Jump to loop check
    ctx.ir_gen->createBr(loopBB);
    
    // Loop Check
    ctx.ir_gen->setInsertPoint(loopBB);
    IRValue* condVal = visitExpression(whileStmt->condition.get());
    if (!condVal) return;
    
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0);
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "while_cond_bool");
    }
    
    ctx.ir_gen->createCondBr(condVal, bodyBB, endBB);
    
    // Body Block
    ctx.ir_gen->setInsertPoint(bodyBB);
    if (whileStmt->body) {
        visitBlockStmt(whileStmt->body.get());
    }
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(loopBB);
    }
    
    // End Block
    ctx.ir_gen->setInsertPoint(endBB);
}
void Compiler::visitTry(TryStmt* tryStmt) {
    LOG_DEBUG("Compiling TryStmt. Has TryBlock: " + std::to_string(tryStmt->tryBlock != nullptr));
    if (!tryStmt->tryBlock) {
        LOG_ERROR(ctx.fmtLoc() + "AST Error: Try statement is missing a body. The try block will not execute.");
        return; 
    }

    llvm::Function* func = ctx.builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* tryBlock = llvm::BasicBlock::Create(ctx.context, "try_block", func);
    llvm::BasicBlock* catchBlock = llvm::BasicBlock::Create(ctx.context, "catch_block", func);
    llvm::BasicBlock* contBlock = llvm::BasicBlock::Create(ctx.context, "try_cont", func);

    ctx.builder.CreateBr(tryBlock);
    ctx.builder.SetInsertPoint(tryBlock);

    // Save previous catch block logically if we implement nested tries.
    // For now, assume top-level single try/catch handling via IRGen's context or just emit blocks.
    // To properly support invoke, any call inside tryBlock must be an invoke.
    // Since we don't know which call throws, we need to instruct ir_gen to use invoke instead of call!
    // We'll set a state in ctx to emit invokes to our catch block.
    
    llvm::BasicBlock* oldCatch = ctx.catchBlock;
    ctx.catchBlock = catchBlock;

    if (tryStmt->tryBlock) {
        visitBlockStmt(tryStmt->tryBlock.get());
    }
    
    // Jump to contBlock if we haven't returned/thrown
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateBr(contBlock);
    }

    // Restore catch block
    ctx.catchBlock = oldCatch;

    ctx.builder.SetInsertPoint(catchBlock);
    
    // Set Personality Function
    llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
    if (!parentFunc->hasPersonalityFn()) {
        llvm::Function* persFunc = ctx.module.getFunction("__gxx_personality_v0");
        if (!persFunc) {
            llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getInt32Ty(), true);
            persFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__gxx_personality_v0", ctx.module);
        }
        parentFunc->setPersonalityFn(persFunc);
    }

    // Emit LandingPad
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);
    llvm::StructType* landingPadTy = llvm::StructType::get(ctx.context, {ptrTy, ctx.builder.getInt32Ty()});
    llvm::LandingPadInst* lpad = ctx.builder.CreateLandingPad(landingPadTy, 1, "lpad");
    
    // We only catch C++ exceptions (or our own type) roughly.
    // We need typeinfo of __cxa_exceptio n or equivalent.
    // Null catches all in C++ LLVM IR usually: `catch i8* null`
    lpad->addClause(llvm::ConstantPointerNull::get(ptrTy));
    lpad->setCleanup(false); // We are catching it.
    
    // Extract exception pointer
    llvm::Value* excPtr = ctx.builder.CreateExtractValue(lpad, 0, "exc_ptr");

    // Begin Catch
    llvm::Function* beginCatchFunc = ctx.module.getFunction("__cxa_begin_catch");
    if (!beginCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
        beginCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_begin_catch", ctx.module);
    }
    
    llvm::Value* rawExc = ctx.builder.CreateCall(beginCatchFunc, {excPtr}, "raw_exc");
    
    // If catch type specified, cast to it. 
    // Wait, AIML dynamic typing or we cast to an Object base?
    // For MVP we just give them the pointer and let them cast, or assume it is an Exception object.
    
    if (tryStmt->catchBlock && !tryStmt->catchVar.empty()) {
        // Assume exception is a ptr to `catchType` struct
        llvm::Type* excType = ctx.getLLVMType(tryStmt->catchType);
        if (!excType) excType = ctx.builder.getInt8Ty(); // fallback
        
        // Since we allocated 8 bytes in __cxa_allocate_exception and stored the actual object ptr inside it,
        // we must load the object ptr from the returned envelope.
        llvm::Value* userExc = ctx.builder.CreateLoad(ptrTy, rawExc, "actual_exc_ptr");
        
        // Register variable in scope
        IRType* targetTy = ctx.ir_gen->wrapType(ptrTy); // Opaque pointer
        IRValue* alloca = ctx.ir_gen->createAlloca(targetTy, tryStmt->catchVar);
        ctx.builder.CreateStore(userExc, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca)));
        
        Symbol sym;
        sym.ir_alloca_inst = alloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = targetTy;
        sym.typeName = tryStmt->catchType;
        ctx.symbol_table[tryStmt->catchVar] = sym;
        
        visitBlockStmt(tryStmt->catchBlock.get());
    }

    // End Catch
    llvm::Function* endCatchFunc = ctx.module.getFunction("__cxa_end_catch");
    if (!endCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), false);
        endCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_end_catch", ctx.module);
    }
    
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateCall(endCatchFunc);
        ctx.builder.CreateBr(contBlock);
    } else {
        // If it terminates, we still need to end catch before returning/throwing?
        // Actually C++ requires invoking end_catch before returning from catch!
        // This is complex for MVP, we'll assume people don't branch directly out of catch without it manually, 
        // or we just put it right before.
    }

    ctx.builder.SetInsertPoint(contBlock);
}
void Compiler::visitThrow(ThrowStmt* throwStmt) {
    if (!throwStmt->value) return;
    
    IRValue* excVal = visitExpression(throwStmt->value.get());
    if (!excVal) return;

    llvm::Value* rawObj = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(excVal));
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocExcFunc = ctx.module.getFunction("__cxa_allocate_exception");
    if (!allocExcFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ctx.builder.getInt64Ty()}, false);
        allocExcFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_allocate_exception", ctx.module);
    }

    llvm::Function* throwFunc = ctx.module.getFunction("__cxa_throw");
    if (!throwFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), 
            {ptrTy, ptrTy, ptrTy}, false);
        throwFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_throw", ctx.module);
    }

    const llvm::DataLayout& dl = ctx.module.getDataLayout();
    uint64_t excSize = 8; // Pointer size for thrown object reference
    
    llvm::Value* excAlloc = ctx.builder.CreateCall(allocExcFunc, {ctx.builder.getInt64(excSize)}, "exc_alloc");
    
    // cast rawObj to i8* and store it in excAlloc? No, AIML uses objects. We can just store the pointer.
    // With Opaque Pointers, excAlloc is `ptr`. `rawObj` is `ptr`.
    ctx.builder.CreateStore(rawObj, excAlloc);

    llvm::Value* nullTypeInfo = llvm::ConstantPointerNull::get(ptrTy);
    llvm::Value* nullDtor = llvm::ConstantPointerNull::get(ptrTy);

    if (ctx.catchBlock) {
        llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachableBB = llvm::BasicBlock::Create(ctx.context, "throw_unreachable", currentFunc);
        ctx.builder.CreateInvoke(throwFunc, unreachableBB, ctx.catchBlock, {excAlloc, nullTypeInfo, nullDtor});
        ctx.builder.SetInsertPoint(unreachableBB);
        ctx.builder.CreateUnreachable();
        // Move insert point to a dead block so any subsequent statements in the same
        // scope don't append instructions after the unreachable terminator.
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "throw_dead", currentFunc);
        ctx.builder.SetInsertPoint(deadBB);
    } else {
        llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
        ctx.builder.CreateCall(throwFunc, {excAlloc, nullTypeInfo, nullDtor});
        ctx.builder.CreateUnreachable();
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "throw_dead", currentFunc);
        ctx.builder.SetInsertPoint(deadBB);
    }
}

void Compiler::visitIdxStmt(IdxStmt* idx) {
    // Resolve the container variable
    Symbol* sym = resolveVariable(idx->container);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "idx: unknown variable '" + idx->container + "'");
        return;
    }

    // Evaluate indices
    std::vector<llvm::Value*> indexVals;
    for (auto& idxExpr : idx->indices) {
        IRValue* v = visitExpression(idxExpr.get());
        if (!v) return;
        llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
        if (raw->getType()->isIntegerTy(64))
            raw = ctx.builder.CreateTrunc(raw, ctx.builder.getInt32Ty());
        indexVals.push_back(raw);
    }

    // Evaluate the value to store
    IRValue* valIR = visitExpression(idx->value.get());
    if (!valIR) return;
    llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valIR));

    const std::string& tName = sym->typeName;

    // ── array<T:N[:M[:...]]> ──
    if (tName.rfind("array<", 0) == 0 && tName.back() == '>') {
        std::string inner = tName.substr(6, tName.size() - 7);
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t sep = inner.find(':', pos);
            if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
            parts.push_back(inner.substr(pos, sep - pos));
            pos = sep + 1;
        }
        llvm::Type* elemTy = ctx.getLLVMType(parts[0]);
        std::vector<uint64_t> dims;
        for (size_t i = 1; i < parts.size(); ++i)
            dims.push_back(std::stoull(parts[i]));

        // Compute flat index (row-major)
        llvm::Value* flatIdx = nullptr;
        if (indexVals.size() == 1) {
            flatIdx = indexVals[0];
        } else {
            flatIdx = indexVals[0];
            for (size_t i = 1; i < indexVals.size(); ++i) {
                uint64_t stride = 1;
                for (size_t j = i; j < dims.size(); ++j) stride *= dims[j];
                flatIdx = ctx.builder.CreateMul(flatIdx, ctx.builder.getInt32((uint32_t)stride));
                flatIdx = ctx.builder.CreateAdd(flatIdx, indexVals[i]);
            }
        }

        uint64_t totalCount = 1;
        for (auto d : dims) totalCount *= d;
        llvm::Type* arrTy = llvm::ArrayType::get(elemTy, totalCount);
        llvm::Value* arrPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* elemPtr = ctx.builder.CreateGEP(arrTy, arrPtr,
            {ctx.builder.getInt32(0), flatIdx}, "arr_idx_gep");

        // Type coercion for store value.
        // Arrays now support C-like pointer/value semantics:
        // - ptr<T> elements store raw addresses
        // - struct elements store the full struct value inline
        if (storeVal->getType() != elemTy) {
            if (elemTy->isPointerTy() && storeVal->getType()->isPointerTy()) {
                storeVal = ctx.builder.CreatePointerCast(storeVal, elemTy, "idx_pcast");
            } else if (elemTy->isStructTy() && storeVal->getType()->isPointerTy()) {
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
                llvm::Value* tmp = ctx.createEntryAlloca(elemTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    tmp,
                    storeVal,
                    ctx.getSizeConst(sz)
                });
                storeVal = ctx.builder.CreateLoad(elemTy, tmp, "idx_struct_from_ptr");
            } else if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy()) {
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy, "idx_i2f");
            } else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy()) {
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy, "idx_fcast");
            } else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy()) {
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy, "idx_icast");
            } else if (elemTy->isIntegerTy() && storeVal->getType()->isFloatingPointTy()) {
                storeVal = ctx.builder.CreateFPToSI(storeVal, elemTy, "idx_f2i");
            } else {
                LOG_ERROR(ctx.fmtLoc() + "idx: incompatible value for array element store");
                return;
            }
        }

        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── dyn_array<T> ──
    if (tName.rfind("dyn_array<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(10, tName.size() - 11);
        llvm::Type* elemTy   = ctx.getLLVMType(elemName);
        llvm::Type* i8p      = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* ptrAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* rawPtr  = ctx.builder.CreateLoad(i8p, ptrAlloca, "darr_base");
        llvm::Value* typedPtr = ctx.builder.CreatePointerCast(rawPtr, elemTy->getPointerTo(), "darr_typed");
        if (indexVals.empty()) { LOG_ERROR("idx: dyn_array requires index"); return; }
        llvm::Value* elemPtr = ctx.builder.CreateGEP(elemTy, typedPtr, indexVals[0], "darr_idx_gep");
        if (storeVal->getType() != elemTy) {
            if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy, "darr_i2f");
            else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy())
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy, "darr_fcast");
            else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy, "darr_icast");
        }
        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── list<T> ──
    if (tName.rfind("list<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(5, tName.size() - 6);
        llvm::Type* elemTy = ctx.getLLVMType(elemName);
        llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        if (indexVals.empty()) { LOG_ERROR("idx: list requires at least one index"); return; }
        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);

        if (storeVal->getType() != elemTy) {
            if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy);
            else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy())
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy);
            else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy);
        }

        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── tensor ──
    if (tName == "tensor") {
        llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* tensorVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), tPtr);
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        // Coerce value to f32
        if (storeVal->getType()->isIntegerTy())
            storeVal = ctx.builder.CreateSIToFP(storeVal, f32);
        else if (storeVal->getType()->isDoubleTy())
            storeVal = ctx.builder.CreateFPTrunc(storeVal, f32);
        llvm::Function* setFn = ctx.getFunction("aiml_tensor_set", ctx.builder.getVoidTy(), {i8p, i32, f32});
        ctx.builder.CreateCall(setFn, {tensorVal, indexVals[0], storeVal});
        return;
    }

    LOG_ERROR(ctx.fmtLoc() + "idx: unsupported container type '" + tName + "'");
}

} // namespace AST
} // namespace AIML
