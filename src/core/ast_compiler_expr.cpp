#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include "expression.h"
#include <iostream>

namespace AIML {
namespace AST {

IRValue* Compiler::visitExpression(Expr* expr) {
    if (auto lit = dynamic_cast<LiteralExpr*>(expr)) return visitLiteral(lit);
    if (auto var = dynamic_cast<VariableExpr*>(expr)) return visitVariable(var);
    if (auto call = dynamic_cast<CallExpr*>(expr)) return visitCall(call);
    if (auto newExpr = dynamic_cast<NewExpr*>(expr)) return visitNewExpr(newExpr);
    if (auto interp = dynamic_cast<StringInterpolationExpr*>(expr)) return visitStringInterpolation(interp);
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) return visitBinary(bin);
    if (auto unary = dynamic_cast<UnaryExpr*>(expr)) return visitUnary(unary);
    return nullptr;
}
IRValue* Compiler::visitStringInterpolation(StringInterpolationExpr* expr) {
    if (expr->parts.empty()) return ctx.ir_gen->createGlobalStringPtr("");
    
    llvm::Value* currentStr = nullptr;
    for (auto& part : expr->parts) {
        IRValue* irVal = visitExpression(part.get());
        if (!irVal) continue;
        
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(irVal));
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irVal->getType()));
        
        LOG_DEBUG("Interpolation Part Ty: " + std::to_string(ty->getTypeID()));
        if (ty->isIntegerTy()) LOG_DEBUG("  Bitwidth: " + std::to_string(ty->getIntegerBitWidth()));
        
        llvm::Value* strVal = val;
        // Basic type conversions to string
        if (ty->isIntegerTy(32)) {
            strVal = StringUtils::intToString(ctx, val);
        } else if (ty->isIntegerTy(64)) {
            strVal = StringUtils::int64ToString(ctx, val);
        } else if (ty->isIntegerTy(1)) {
            llvm::Value* trueStr = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
            strVal = ctx.builder.CreateSelect(val, trueStr, falseStr); // val is already i1
        }
        
        if (!currentStr) currentStr = strVal;
        else currentStr = StringUtils::concat(ctx, currentStr, strVal);
    }
    
    return ctx.ir_gen->wrap(currentStr);
}
IRValue* Compiler::visitNewExpr(NewExpr* newExpr) {
    std::string className = newExpr->typeName;
    if (!className.empty() && ctx.classes.count(className)) {
        llvm::StructType* structTy = ctx.classes[className].type;
        uint64_t typeSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
        
        // Ensure calloc exists
        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
             llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
        }
        
        std::vector<IRValue*> callArgs;
        // Assuming we add a way to get Int64. If not, Int32 will probably implicitly cast or we can use getInt32.
        // Actually `ast_compiler` can just use `ctx.ir_gen->wrap(ctx.builder.getInt64(...))` for literal bridging.
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(1)));
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(typeSize)));
        
        IRValue* rawPtr = ctx.ir_gen->createCall("calloc", callArgs, "calloc_call");
        
        // Auto-initialize defined members
        auto& classInfo = ctx.classes[className];
        for (auto& [name, member] : classInfo.members) {
             Expr* initExpr = nullptr;
             if (newExpr->initValues.count(name)) {
                 initExpr = newExpr->initValues[name].get();
             } else if (member.initValue != nullptr) {
                 initExpr = member.initValue.get();
             }
             
             if (initExpr != nullptr) {
                  IRValue* fieldInitV = visitExpression(initExpr);
                  if (fieldInitV) {
                      llvm::Value* initVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV));
                      if (initVal) {
                          llvm::Value* rawPtrCast = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPtr));
                          llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtrCast, member.index, name + "_gep");
                          ctx.builder.CreateStore(initVal, fieldPtr);
                      } else {
                          LOG_ERROR("Null LLVM value evaluated for initialization of member: " + name);
                      }
                  } else {
                      LOG_ERROR("Failed to compile AST expression for initialization of member: " + name);
                  }
             }
        }
        return rawPtr;
    }
    
    LOG_ERROR("Unknown class for new: " + className);
    return ctx.ir_gen->getInt32(0);
}
IRValue* Compiler::visitLiteral(LiteralExpr* lit) {
    if (lit->type == "i32" || lit->type == "int") {
        try {
            return ctx.ir_gen->getInt32(std::stoi(lit->value, nullptr, 0));
        } catch (...) {
            return ctx.ir_gen->getInt32(0);
        }
    } else if (lit->type == "string") {
        return ctx.ir_gen->createGlobalStringPtr(lit->value);
    } else if (lit->type == "bool") {
        return ctx.ir_gen->getInt32(lit->value == "true" ? 1 : 0);
    } else if (lit->type == "unknown") {
        // Strict parsing for unquoted scalars that are numbers or booleans
        if (lit->value == "true") return ctx.ir_gen->getInt32(1);
        if (lit->value == "false") return ctx.ir_gen->getInt32(0);
        
        try {
            size_t pos = 0;
            int num = std::stoi(lit->value, &pos, 0);
            return ctx.ir_gen->getInt32(num);
        } catch (...) {
            LOG_ERROR("Invalid unknown literal (expected bool or number): " + lit->value);
            return ctx.ir_gen->getInt32(0);
        }
    }
    
    // Fallback stub
    LOG_DEBUG("Visit Unhandled Literal Type: " + lit->type + " Value: " + lit->value);
    return ctx.ir_gen->getInt32(0);
}
IRValue* Compiler::visitVariable(VariableExpr* var) {
    std::string varName = var->name;
    if (varName.find("<.") == 0) {
        varName = "this." + varName.substr(2);
    }
    
    size_t dotPos = varName.find('.');
    size_t dashPos = varName.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    
    std::string baseName = (sepPos != std::string::npos) ? varName.substr(0, sepPos) : varName;
    std::string propName = (sepPos != std::string::npos) ? varName.substr(sepPos + 1) : "";

    // 1. Unambiguous Enum Check
    if (dotPos != std::string::npos) {
        if ((ctx.currentNs && ctx.currentNs->enums.count(baseName)) || ctx.enums.count(baseName)) {
             return resolveEnum(baseName, propName);
        }
    }

    // 2. Strict Variable Lookup
    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        if (baseName == "sys" && propName == "env") {
            return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        } else if (baseName == "io" && (propName == "stdout" || propName == "stderr" || propName == "stdin")) {
            int fd = (propName == "stdin") ? 0 : (propName == "stdout" ? 1 : 2);
            std::string fmode = (propName == "stdin") ? "r" : "w";
            llvm::Function* fdopenFunc = ctx.module.getFunction("fdopen");
            if (!fdopenFunc) {
                llvm::FunctionType* fdType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false);
                fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module);
            }
            llvm::Value* modeStr = ctx.builder.CreateGlobalStringPtr(fmode);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), modeStr}));
        } else if (ctx.getLLVMType(varName) || varName == "string" || varName == "i32" || varName == "i64" || varName == "u8" || varName == "f32" || varName == "f64") {
            return ctx.ir_gen->getInt32(0); 
        }

        LOG_ERROR("Undefined variable: " + baseName);
        return nullptr; 
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (propName.empty()) {
        return ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_val");
    } else {
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash) {
            llvm::Value* baseAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
            
            bool isDynamicList = sym->typeName.find("list<") == 0;
            bool isRawArray = llvmBaseElemTy->isArrayTy();
            
            if (isDynamicList || isRawArray) {
                llvm::Type* elemType = isRawArray ? llvmBaseElemTy->getArrayElementType() : nullptr;
                if (isDynamicList) {
                    std::string elemName = sym->typeName.substr(5, sym->typeName.length() - 6);
                    elemType = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                }

                size_t dashInProp = propName.find('-');
                if (dashInProp != std::string::npos) {
                    // Slicing: varname-start-end (e.g. lst-1-4, lst-2-, lst--2)
                    if (!isDynamicList) {
                        LOG_ERROR("Array Slicing not supported on raw arrays yet.");
                        return nullptr;
                    }

                    std::string startStr = propName.substr(0, dashInProp);
                    std::string endStr = propName.substr(dashInProp + 1);
                    
                    llvm::Value* startVal = startStr.empty() ? ctx.builder.getInt32(0) : ctx.builder.getInt32(std::stoi(startStr));
                    llvm::Value* endVal = nullptr;
                    if (endStr.empty()) endVal = ListImpl::getSize(ctx, baseAllocaVal);
                    else endVal = ctx.builder.getInt32(std::stoi(endStr));
                    
                    return ctx.ir_gen->wrap(ListImpl::slice(ctx, baseAllocaVal, startVal, endVal, elemType));
                } else {
                    // Single Element Read
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
                            LOG_ERROR("Invalid list index: " + propName);
                            return ctx.ir_gen->getInt32(0);
                        }
                    }
                    
                    llvm::Value* elemPtr = nullptr;
                    if (isRawArray) {
                        elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, baseAllocaVal, {ctx.builder.getInt32(0), idxVal});
                    } else {
                        elemPtr = ListImpl::getElementPtr(ctx, baseAllocaVal, idxVal, elemType);
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "lst_elem"));
                }
            }
        }
        return resolveProperty(sym, propName);
    }
    return ctx.ir_gen->getInt32(0); 
}
IRValue* Compiler::visitStringCompare(bool negate, llvm::Value* rawLhs, llvm::Value* rawRhs) {
    llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
    std::string suffix = negate ? "_ne" : "";
    llvm::BasicBlock* checkNullBB = llvm::BasicBlock::Create(ctx.context, "cmp_check_null" + suffix, parentFunc);
    llvm::BasicBlock* doStrcmpBB  = llvm::BasicBlock::Create(ctx.context, "cmp_do_strcmp"  + suffix, parentFunc);
    llvm::BasicBlock* mergeBB     = llvm::BasicBlock::Create(ctx.context, "cmp_merge"       + suffix, parentFunc);
    llvm::Value* isLhsNull = ctx.builder.CreateIsNull(rawLhs);
    llvm::Value* ptrEq     = ctx.builder.CreateICmpEQ(rawLhs, rawRhs);
    llvm::Value* anyNull   = ctx.builder.CreateOr(isLhsNull, ctx.builder.CreateIsNull(rawRhs));
    ctx.builder.CreateCondBr(anyNull, checkNullBB, doStrcmpBB);
    ctx.builder.SetInsertPoint(checkNullBB);
    ctx.builder.CreateBr(mergeBB);
    ctx.builder.SetInsertPoint(doStrcmpBB);
    llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
    if (!strcmpFunc) strcmpFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strcmp", ctx.module);
    llvm::Value* cmpRes    = ctx.builder.CreateCall(strcmpFunc, {rawLhs, rawRhs});
    llvm::Value* strResult = negate
        ? ctx.builder.CreateICmpNE(cmpRes, ctx.builder.getInt32(0))
        : ctx.builder.CreateICmpEQ(cmpRes, ctx.builder.getInt32(0));
    ctx.builder.CreateBr(mergeBB);
    ctx.builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, "strcmp_res" + suffix);
    llvm::Value* ptrResult = negate ? ctx.builder.CreateNot(ptrEq) : ptrEq;
    phi->addIncoming(ptrResult, checkNullBB);
    phi->addIncoming(strResult, doStrcmpBB);
    return ctx.ir_gen->wrap(phi);
}
IRValue* Compiler::visitBinary(BinaryExpr* bin) {
    IRValue* lhs = visitExpression(bin->lhs.get());
    IRValue* rhs = visitExpression(bin->rhs.get());
    if (!lhs || !rhs) return nullptr;
    
    llvm::Type* lTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(lhs->getType()));
    llvm::Type* rTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(rhs->getType()));
    
    if (lTy != rTy) {
        if (lTy->isIntegerTy() && rTy->isIntegerTy()) {
            unsigned lBits = lTy->getIntegerBitWidth();
            unsigned rBits = rTy->getIntegerBitWidth();
            if (lBits < rBits) {
                lhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
            } else {
                rhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
            }
        } else if (lTy->isPointerTy() && rTy->isIntegerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        } else if (rTy->isPointerTy() && lTy->isIntegerTy()) {
            lhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
        } else if (lTy->isPointerTy() && rTy->isPointerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        }
    }
    
    if (bin->op == "+") return ctx.ir_gen->createAdd(lhs, rhs);
    if (bin->op == "-") return ctx.ir_gen->createSub(lhs, rhs);
    if (bin->op == "*") return ctx.ir_gen->createMul(lhs, rhs);
    if (bin->op == "/") return ctx.ir_gen->createSDiv(lhs, rhs);
    if (bin->op == "%") return ctx.ir_gen->createSRem(lhs, rhs);
    
    if (bin->op == "eq") {
        if (lTy->isPointerTy() && rTy->isPointerTy()) {
            return visitStringCompare(false,
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)));
        }
                return ctx.ir_gen->createICmpEQ(lhs, rhs);
    }
    if (bin->op == "neq") {
        if (lTy->isPointerTy() && rTy->isPointerTy()) {
            return visitStringCompare(true,
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)));
        }
                return ctx.ir_gen->createICmpNE(lhs, rhs);
    }
    if (bin->op == "lt") return ctx.ir_gen->createICmpSLT(lhs, rhs);
    if (bin->op == "gt") return ctx.ir_gen->createICmpSGT(lhs, rhs);
    if (bin->op == "le") return ctx.ir_gen->createICmpSLE(lhs, rhs);
    if (bin->op == "ge") return ctx.ir_gen->createICmpSGE(lhs, rhs);
    
    if (bin->op == "and") return ctx.ir_gen->createAnd(lhs, rhs);
    if (bin->op == "or") return ctx.ir_gen->createOr(lhs, rhs);

    LOG_ERROR("Unknown binary operator: " + bin->op);
    return nullptr;
}
// Helper: strip one level of ptr<...> or ref<...> wrapper, returns inner type string
static std::string stripPtrWrapper(const std::string& typeName) {
    if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) && typeName.back() == '>')
        return typeName.substr(4, typeName.size() - 5);
    return "";
}

IRValue* Compiler::visitUnary(UnaryExpr* unary) {
    // '&'  / '&&'  — address-of (N levels): return the alloca pointer of the variable
    if (unary->op.find_first_not_of('&') == std::string::npos) {
        auto* varExpr = dynamic_cast<VariableExpr*>(unary->operand.get());
        if (!varExpr) { LOG_ERROR("Unary '&': operand must be a variable"); return nullptr; }
        Symbol* sym = resolveVariable(varExpr->name);
        if (!sym) { LOG_ERROR("Unary '&': undefined variable: " + varExpr->name); return nullptr; }
        IRValue* alloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
        if (unary->op == "&") {
            return alloca; // pointer to x
        }
        // '&&x' → alloca of a pointer-to-x (temp)
        llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        llvm::AllocaInst* ptrToPtr = ctx.builder.CreateAlloca(rawAlloca->getType(), nullptr, "addr_of_addr");
        ctx.builder.CreateStore(rawAlloca, ptrToPtr);
        return ctx.ir_gen->wrap(ptrToPtr);
    }

    // '?', '??', '???' — dereference (N levels)
    if (unary->op.find_first_not_of('?') == std::string::npos) {
        int depth = (int)unary->op.size();

        // Resolve base symbol to get the type chain
        auto* varExpr = dynamic_cast<VariableExpr*>(unary->operand.get());
        Symbol* sym = varExpr ? resolveVariable(varExpr->name) : nullptr;

        // Get the current pointer value (evaluating the operand loads its value)
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));

        // Build the type chain from the symbol's typeName (e.g. "ptr<ptr<i32>>")
        std::string curTypeName = sym ? sym->typeName : "";
        for (int i = 0; i < depth; ++i) {
            std::string inner = stripPtrWrapper(curTypeName);
            llvm::Type* loadTy = nullptr;
            if (!inner.empty()) {
                loadTy = ctx.getLLVMType(inner);
                curTypeName = inner; // advance one level
            }
            if (!loadTy) {
                // Fallback: load as opaque pointer (handles string/any)
                loadTy = ctx.builder.getInt8Ty()->getPointerTo();
            }
            ptr = ctx.builder.CreateLoad(loadTy, ptr, "deref");
        }
        return ctx.ir_gen->wrap(ptr);
    }

    // '!' — logical not
    if (unary->op == "!") {
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        return ctx.ir_gen->wrap(ctx.builder.CreateNot(v, "not"));
    }

    LOG_ERROR("Unknown unary operator: " + unary->op);
    return nullptr;
}
} // namespace AST
} // namespace AIML
