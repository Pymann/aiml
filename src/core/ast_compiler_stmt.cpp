#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "string_utils.h"
#include "expression.h"
#include <iostream>

namespace AIML {
namespace AST {

void Compiler::visitStatement(Stmt* stmt) {
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) visitDeclStmt(decl);
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) visitSetStmt(setStmt);
    else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) visitExprStmt(exprStmt);
    else if (auto retStmt = dynamic_cast<RetStmt*>(stmt)) visitRetStmt(retStmt);
    else if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt)) visitBlockStmt(blockStmt);
    else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) visitIfStmt(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) visitWhileStmt(whileStmt);
    else if (auto tryStmt = dynamic_cast<TryStmt*>(stmt)) visitTry(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStmt*>(stmt)) visitThrow(throwStmt);
}
void Compiler::visitDeclStmt(DeclStmt* decl) {
    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;
    
    IRValue* alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
    
    // Zero-initialize the allocated memory
    llvm::Type* unwrappedTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
    llvm::Value* unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
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
    
    Symbol sym;
    sym.ir_alloca_inst = alloca;
    sym.ir_type = ctx.ir_gen->getPointerTy();
    sym.ir_element_type = ty;
    sym.typeName = decl->typeName;
    sym.is_const = decl->is_const;
    sym.is_volatile = decl->is_volatile;
    ctx.symbol_table[decl->varName] = sym;
    
    LOG_DEBUG("Compiled AST Decl: " + decl->varName + " of " + decl->typeName);
    
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

        IRValue* initVal = visitExpression(decl->initialValue.get());
        if (initVal) {
            // path: auto-intern so pointer identity == path equality
            if (decl->typeName == "path") {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(internFn, {raw}, "path_init"));
            }
            // String → int coercion: decl: [x, i32/i64, "${str_var}"]
            if ((decl->typeName == "i32" || decl->typeName == "i64") && !decl->typeName.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                if (v->getType()->isPointerTy()) {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    if (decl->typeName == "i32") {
                        llvm::Function* fn = ctx.getFunction("atoi", ctx.builder.getInt32Ty(), {i8p});
                        initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}, "str2i32"));
                    } else {
                        llvm::Function* fn = ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                        initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}, "str2i64"));
                    }
                }
            }
            // buffer<u8>: handler returns a fresh alloca → memcpy struct content into target alloca
            if (decl->typeName.find("buffer<") == 0) {
                llvm::Type* bufTy = ctx.getLLVMType("buffer");
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
                ctx.builder.CreateCall(mcf, {
                    unwrappedAlloca,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal)),
                    ctx.builder.getInt64(sz)
                });
            } else if (decl->typeName.find("list<") == 0) {
                // list: handler returns a fresh alloca → memcpy struct into target alloca
                llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
                ctx.builder.CreateCall(mcf, {
                    unwrappedAlloca,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal)),
                    ctx.builder.getInt64(sz)
                });
            } else {
                ctx.ir_gen->createStore(initVal, alloca);
            }
        }
    }
}
void Compiler::visitSetStmt(SetStmt* setStmt) {
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
        LOG_ERROR("Undefined variable (set target): " + targetStr);
        return;
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (!baseAlloca) return;
    
    // Expose the target variable's type so sys.deserialize can infer it without an explicit arg.
    if (propName.empty()) ctx.deserializeTargetType = sym->typeName;
    IRValue* val = visitExpression(setStmt->value.get());
    ctx.deserializeTargetType.clear();
    if (!val) {
        LOG_ERROR("Failed to evaluate value expression for assignment to: " + targetStr);
        return;
    }
    
    if (propName.empty()) {
        if (sym->is_const) {
             LOG_ERROR("Cannot assign to const variable " + targetStr);
             return;
        }
        if (sym->typeName.find("list<") == 0) {
            llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
            uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
            ctx.builder.CreateCall(memcpyFunc, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                ctx.builder.getInt64(structSize)
            });
            return;
        }
        if (sym->typeName.find("buffer<") == 0) {
            llvm::Type* bufTy = ctx.getLLVMType("buffer");
            if (bufTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.builder.getInt64(structSize)
                });
                return;
            }
        }
        if (sym->typeName.find("map<") == 0) {
            llvm::Type* mapTy = ctx.getLLVMType(sym->typeName);
            if (mapTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(mapTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.builder.getInt64(structSize)
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
            llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            // Only memcpy if the value is a pointer (i8* from deserializer), not a struct value
            if (rawVal->getType()->isPointerTy()) {
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    rawVal,
                    ctx.builder.getInt64(structSize)
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
        if (sym->typeName == "i32" || sym->typeName == "i64") {
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (v->getType()->isPointerTy()) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (sym->typeName == "i32") {
                    llvm::Function* fn = ctx.getFunction("atoi", ctx.builder.getInt32Ty(), {i8p});
                    val = ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}, "str2i32"));
                } else {
                    llvm::Function* fn = ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                    val = ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}, "str2i64"));
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
                LOG_ERROR("Slice assignment is not supported yet: " + targetStr);
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
                    LOG_ERROR("Invalid list index for set: " + propName);
                    return;
                }
            }
            
            llvm::Value* elemPtr = nullptr;
            if (llvmBaseElemTy->isArrayTy()) {
                elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), {ctx.builder.getInt32(0), idxVal});
            } else {
                elemPtr = ListImpl::getElementPtr(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), idxVal, llvmBaseElemTy);
            }
            ctx.builder.CreateStore(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), elemPtr);
            return;
        }
        
        if (!className.empty() && ctx.classes.count(className)) {
            auto& classInfo = ctx.classes[className];
            if (classInfo.members.count(propName)) {
                auto& member = classInfo.members[propName];

                if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
                     LOG_ERROR("Cannot access private member " + propName);
                     return;
                } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                     LOG_ERROR("Cannot access protected member " + propName);
                     return;
                }
                if (member.is_const) {
                     LOG_ERROR("Cannot assign to const member " + propName);
                     return;
                }

                IRType* structTy = ctx.ir_gen->wrapType(classInfo.type);
                IRValue* structPtr = baseAlloca;

                if (baseElemTy->isPointer()) {
                     structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_ptr");
                }

                IRValue* gep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "set_prop_ptr");
                ctx.ir_gen->createStore(val, gep);
                return;
            }

            // Builtin property setter for handle types (e.g. resp.status, resp.body)
            if (classInfo.isBuiltin && classInfo.builtinSetters.count(propName)) {
                llvm::Value* baseVal = ctx.builder.CreateLoad(
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "bs_load");
                classInfo.builtinSetters.at(propName)(ctx, baseVal, val);
                return;
            }
        }
        LOG_ERROR("Undefined property for set: " + targetStr);
    }
}
void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    visitExpression(exprStmt->expr.get());
}
void Compiler::visitRetStmt(RetStmt* retStmt) {
    llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
    llvm::Type* retTy = currentFunc->getReturnType();
    
    if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        if (val) {
            ctx.ir_gen->createRet(val);
        } else {
             LOG_ERROR("Failed to evaluate return expression");
             if (!retTy->isVoidTy()) ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
             else ctx.ir_gen->createRet(nullptr);
        }
    } else {
        if (!retTy->isVoidTy()) {
             LOG_ERROR("Empty return statement in non-void function!");
             ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
        } else {
             ctx.ir_gen->createRet(nullptr);
        }
    }
}
void Compiler::visitBlockStmt(BlockStmt* block) {
    for (const auto& stmt : block->statements) {
        visitStatement(stmt.get());
    }
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
    ctx.ir_gen->createBr(mergeBB);
    
    // ELSE branch
    if (ifStmt->elseBranch) {
        ctx.ir_gen->setInsertPoint(elseBB);
        visitBlockStmt(ifStmt->elseBranch.get());
        ctx.ir_gen->createBr(mergeBB);
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
    ctx.ir_gen->createBr(loopBB);
    
    // End Block
    ctx.ir_gen->setInsertPoint(endBB);
}
void Compiler::visitTry(TryStmt* tryStmt) {
    LOG_DEBUG("Compiling TryStmt. Has TryBlock: " + std::to_string(tryStmt->tryBlock != nullptr));
    if (!tryStmt->tryBlock) {
        LOG_ERROR("AST Error: Try statement is missing a body. The try block will not execute.");
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
    } else {
        ctx.builder.CreateCall(throwFunc, {excAlloc, nullTypeInfo, nullDtor});
        ctx.builder.CreateUnreachable();
    }
}
} // namespace AST
} // namespace AIML
