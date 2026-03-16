#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include "expression.h"
#include <iostream>
#include <cmath>

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
    if (auto spawn = dynamic_cast<SpawnExpr*>(expr)) return visitSpawn(spawn);
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
        // Type-dispatch: convert any non-string value to i8* for concatenation
        if (ty->isIntegerTy(64)) {
            strVal = StringUtils::int64ToString(ctx, val);
        } else if (ty->isIntegerTy(32)) {
            strVal = StringUtils::intToString(ctx, val);
        } else if (ty->isIntegerTy(16)) {
            strVal = StringUtils::intToString(ctx,
                ctx.builder.CreateZExt(val, ctx.builder.getInt32Ty()));
        } else if (ty->isIntegerTy(8)) {
            // bool (u8) — show as "true" / "false"
            llvm::Value* cond     = ctx.builder.CreateICmpNE(val, ctx.builder.getInt8(0));
            llvm::Value* trueStr  = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
            strVal = ctx.builder.CreateSelect(cond, trueStr, falseStr);
        } else if (ty->isIntegerTy(1)) {
            llvm::Value* trueStr  = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
            strVal = ctx.builder.CreateSelect(val, trueStr, falseStr);
        } else if (ty->isFloatTy()) {
            strVal = StringUtils::floatToString(ctx, val);
        } else if (ty->isDoubleTy()) {
            strVal = StringUtils::doubleToString(ctx, val);
        }
        
        if (!currentStr) currentStr = strVal;
        else currentStr = StringUtils::concat(ctx, currentStr, strVal);
    }
    
    return ctx.ir_gen->wrap(currentStr);
}
IRValue* Compiler::visitNewExpr(NewExpr* newExpr) {
    std::string className = newExpr->typeName;
    if (!className.empty() && ctx.classes.count(className)) {
        auto& classInfo = ctx.classes[className];
        llvm::StructType* structTy = classInfo.type;

        // Use padded_size for calloc so hot-reloaded versions with new append-only
        // fields fit without reallocation (new slots are zero-initialized by calloc).
        uint64_t allocSize = (classInfo.padded_size > 0)
            ? (uint64_t)classInfo.padded_size
            : ctx.module.getDataLayout().getTypeAllocSize(structTy);

        // Ensure calloc exists
        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
             llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
        }

        std::vector<IRValue*> callArgs;
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(1)));
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(allocSize)));
        
        IRValue* rawPtr = ctx.ir_gen->createCall("calloc", callArgs, "calloc_call");

        // Auto-initialize defined members
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
    } else if (lit->type == "null") {
        auto* i8p = static_cast<llvm::PointerType*>(llvm::PointerType::get(ctx.context, 0));
        return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
    } else if (lit->type == "unknown") {
        // Strict parsing for unquoted scalars that are numbers or booleans
        if (lit->value == "true") return ctx.ir_gen->getInt32(1);
        if (lit->value == "false") return ctx.ir_gen->getInt32(0);

        // Float literal if it contains '.', 'e', or 'E' — emit as f32
        // But NOT if the string starts with 0x/0X (hex literal with A-F digits)
        const std::string& sv = lit->value;
        bool isHex = sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X');
        bool looksFloat = !isHex && (sv.find('.') != std::string::npos ||
                          sv.find('e') != std::string::npos ||
                          sv.find('E') != std::string::npos);
        if (looksFloat) {
            try {
                float f = std::stof(sv);
                return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), (double)f));
            } catch (...) {}
        }

        try {
            size_t pos = 0;
            if (isHex) {
                // Hex literals may exceed signed int range — parse as unsigned
                unsigned long ul = std::stoul(sv, &pos, 16);
                return ctx.ir_gen->getInt32(static_cast<int32_t>(static_cast<uint32_t>(ul)));
            }
            int num = std::stoi(sv, &pos, 0);
            return ctx.ir_gen->getInt32(num);
        } catch (...) {
            LOG_ERROR("Invalid unknown literal (expected bool or number): " + sv);
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

    // 0. math namespace constants (compile-time f32 literals)
    if (baseName == "math" && !propName.empty()) {
        auto f = [&](double v) -> IRValue* {
            return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), v));
        };
        if (propName == "PI")  return f(M_PI);
        if (propName == "TAU") return f(2.0 * M_PI);
        if (propName == "E")   return f(M_E);
        if (propName == "INF") return f((double)INFINITY);
        if (propName == "NAN") return f((double)NAN);
    }

    // 0b. gl namespace constants (compile-time i32 literals — OpenGL enum values)
    if (baseName == "gl" && !propName.empty()) {
        auto i = [&](int32_t v) -> IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.getInt32(v));
        };
        if (propName == "TRIANGLES")              return i(0x0004);
        if (propName == "LINES")                  return i(0x0001);
        if (propName == "POINTS")                 return i(0x0000);
        if (propName == "TRIANGLE_STRIP")         return i(0x0005);
        if (propName == "REPEAT")                 return i(0x2901);
        if (propName == "CLAMP_TO_EDGE")          return i(0x812F);
        if (propName == "MIRRORED_REPEAT")        return i(0x8370);
        if (propName == "LINEAR")                 return i(0x2601);
        if (propName == "NEAREST")                return i(0x2600);
        if (propName == "LINEAR_MIPMAP_LINEAR")   return i(0x2703);
        if (propName == "NEAREST_MIPMAP_LINEAR")  return i(0x2702);
        if (propName == "LINEAR_MIPMAP_NEAREST")  return i(0x2701);
        if (propName == "NEAREST_MIPMAP_NEAREST") return i(0x2700);
    }

    // 1. Unambiguous Enum Check
    if (dotPos != std::string::npos) {
        if ((ctx.currentNs && ctx.currentNs->enums.count(baseName)) || ctx.enums.count(baseName)) {
             return resolveEnum(baseName, propName);
        }
    }

    // 2. Strict Variable Lookup
    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        // special built-in names
        if (baseName == "sys" && propName == "env") {
            return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        }
        if (baseName == "io" && (propName == "stdout" || propName == "stderr" || propName == "stdin")) {
            int fd = (propName == "stdin") ? 0 : (propName == "stdout" ? 1 : 2);
            std::string fmode = (propName == "stdin") ? "r" : "w";
            llvm::Function* fdopenFunc = ctx.module.getFunction("fdopen");
            if (!fdopenFunc) {
                llvm::FunctionType* fdType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false);
                fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module);
            }
            llvm::Value* modeStr = ctx.builder.CreateGlobalStringPtr(fmode);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), modeStr}));
        }

        // Check if varName is an AIML function used as a function pointer / callback.
        // Must come BEFORE getLLVMType so function names don't trigger "Unknown Type" errors.
        {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = nullptr;
            auto fit = ctx.functions.find(varName);
            if (fit != ctx.functions.end() && fit->second.func)
                fn = fit->second.func;
            if (!fn) fn = ctx.module.getFunction(varName);
            if (fn) {
                llvm::Value* fp = ctx.builder.CreateBitCast(fn, i8p, varName + "_fnptr");
                return ctx.ir_gen->wrap(fp);
            }
        }

        // Type-name used as an expression (e.g. type-cast syntax) — return 0 as placeholder
        if (ctx.getLLVMType(varName) || varName == "string" || varName == "i32" || varName == "i64" || varName == "u8" || varName == "f32" || varName == "f64")
            return ctx.ir_gen->getInt32(0);

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
    // ptrResult must be computed in the entry block (before any SetInsertPoint calls),
    // because PHI nodes must precede all non-PHI instructions in their basic block.
    llvm::Value* ptrResult = negate ? ctx.builder.CreateNot(ptrEq) : ptrEq;
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
    
    // ── Math struct operator overloading ──────────────────────────────────
    if (lTy->isStructTy()) {
        auto* structTy = static_cast<llvm::StructType*>(lTy);
        std::string sn = structTy->getName().str();
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));

        // Store helper
        auto store = [&](llvm::Value* val, llvm::Type* ty) -> llvm::Value* {
            llvm::AllocaInst* a = ctx.builder.CreateAlloca(ty); ctx.builder.CreateStore(val,a); return a;
        };
        auto callVoidFn = [&](const char* nm, std::vector<llvm::Type*> pts, std::vector<llvm::Value*> xs, llvm::Type* outTy) -> llvm::Value* {
            llvm::Function* fn = ctx.module.getFunction(nm);
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),llvm::Function::ExternalLinkage,nm,ctx.module);
            llvm::AllocaInst* out = ctx.builder.CreateAlloca(outTy);
            xs.push_back(out);
            ctx.builder.CreateCall(fn,xs);
            return ctx.builder.CreateLoad(outTy,out);
        };
        auto op = llvm::PointerType::get(ctx.context,0);

        bool isVec2=(sn=="vec2"), isVec3=(sn=="vec3"), isVec4=(sn=="vec4"), isQuat=(sn=="quat"), isMat4=(sn=="mat4");

        if (isVec2 || isVec3 || isVec4 || isQuat) {
            // Check if rhs is f32/scalar (for scale/div)
            bool rhsIsScalar = rTy->isFloatTy() || rTy->isDoubleTy() || rTy->isIntegerTy();
            if (rhsIsScalar) rv = rTy->isFloatTy() ? rv : rTy->isDoubleTy() ?
                ctx.builder.CreateFPTrunc(rv, ctx.builder.getFloatTy()) :
                ctx.builder.CreateSIToFP(rv, ctx.builder.getFloatTy());

            std::string pre = "aiml_math_" + sn + "_";
            if (bin->op == "+" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"add").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "-" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"sub").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "*" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"mul").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "*" &&  rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"scale").c_str(),{op,ctx.builder.getFloatTy(),op},{store(lv,structTy),rv},structTy));
            if (bin->op == "/" &&  rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"div").c_str(),{op,ctx.builder.getFloatTy(),op},{store(lv,structTy),rv},structTy));
            if (bin->op == "==") {
                llvm::Function* fn=ctx.module.getFunction((pre+"eq").c_str());
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(),{op,op},false),llvm::Function::ExternalLinkage,(pre+"eq").c_str(),ctx.module);
                llvm::Value* res=ctx.builder.CreateCall(fn,{store(lv,structTy),store(rv,structTy)});
                return ctx.ir_gen->wrap(ctx.builder.CreateICmpNE(res,ctx.builder.getInt32(0)));
            }
        }

        if (isMat4) {
            // mat4 * mat4
            if (rTy == structTy) {
                return ctx.ir_gen->wrap(callVoidFn("aiml_math_mat4_mul",{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            }
            // mat4 * vec4
            llvm::Type* vec4Ty = ctx.getLLVMType("vec4");
            if (rTy == vec4Ty) {
                return ctx.ir_gen->wrap(callVoidFn("aiml_math_mat4_mul_vec4",{op,op,op},{store(lv,structTy),store(rv,vec4Ty)},vec4Ty));
            }
        }
    }

    // Float arithmetic and comparison — must come before integer fallback
    if (lTy->isFloatTy() || lTy->isDoubleTy()) {
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        if (bin->op == "+" || bin->op == "add") return ctx.ir_gen->wrap(ctx.builder.CreateFAdd(lv, rv));
        if (bin->op == "-" || bin->op == "sub") return ctx.ir_gen->wrap(ctx.builder.CreateFSub(lv, rv));
        if (bin->op == "*" || bin->op == "mul") return ctx.ir_gen->wrap(ctx.builder.CreateFMul(lv, rv));
        if (bin->op == "/" || bin->op == "div") return ctx.ir_gen->wrap(ctx.builder.CreateFDiv(lv, rv));
        if (bin->op == "==" || bin->op == "eq")  return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOEQ(lv, rv));
        if (bin->op == "!=" || bin->op == "neq") return ctx.ir_gen->wrap(ctx.builder.CreateFCmpONE(lv, rv));
        if (bin->op == "<"  || bin->op == "lt")  return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOLT(lv, rv));
        if (bin->op == ">"  || bin->op == "gt")  return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOGT(lv, rv));
        if (bin->op == "<=" || bin->op == "le")  return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOLE(lv, rv));
        if (bin->op == ">=" || bin->op == "ge")  return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOGE(lv, rv));
    }

    if (bin->op == "+") return ctx.ir_gen->createAdd(lhs, rhs);
    if (bin->op == "-") return ctx.ir_gen->createSub(lhs, rhs);
    if (bin->op == "*") return ctx.ir_gen->createMul(lhs, rhs);
    if (bin->op == "/") return ctx.ir_gen->createSDiv(lhs, rhs);
    if (bin->op == "%") return ctx.ir_gen->createSRem(lhs, rhs);

    // `==` / `!=` — identity comparison (integer or pointer address equality).
    // Distinct from `eq`/`neq` which do string comparison for ptr types.
    if (bin->op == "==" || bin->op == "!=") {
        bool isEq = (bin->op == "==");
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        llvm::Value* cmp = isEq ? ctx.builder.CreateICmpEQ(lv, rv)
                                : ctx.builder.CreateICmpNE(lv, rv);
        return ctx.ir_gen->wrap(cmp);
    }

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
    if (bin->op == "lt" || bin->op == "<")  return ctx.ir_gen->createICmpSLT(lhs, rhs);
    if (bin->op == "gt" || bin->op == ">")  return ctx.ir_gen->createICmpSGT(lhs, rhs);
    if (bin->op == "le" || bin->op == "<=") return ctx.ir_gen->createICmpSLE(lhs, rhs);
    if (bin->op == "ge" || bin->op == ">=") return ctx.ir_gen->createICmpSGE(lhs, rhs);

    if (bin->op == "and" || bin->op == "&&") return ctx.ir_gen->createAnd(lhs, rhs);
    if (bin->op == "or"  || bin->op == "||") return ctx.ir_gen->createOr(lhs, rhs);

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

    // '-' — arithmetic negation (scalar or math struct)
    if (unary->op == "-") {
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(val->getType()));
        if (ty->isStructTy()) {
            auto* structTy = static_cast<llvm::StructType*>(ty);
            std::string sn = structTy->getName().str();
            std::string nm = "aiml_math_" + sn + "_neg";
            auto op2 = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module.getFunction(nm.c_str());
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op2,op2},false),
                llvm::Function::ExternalLinkage,nm.c_str(),ctx.module);
            llvm::AllocaInst* src = ctx.builder.CreateAlloca(structTy);
            ctx.builder.CreateStore(v, src);
            llvm::AllocaInst* out = ctx.builder.CreateAlloca(structTy);
            ctx.builder.CreateCall(fn,{src,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(structTy,out));
        }
        if (ty->isFloatTy() || ty->isDoubleTy())
            return ctx.ir_gen->wrap(ctx.builder.CreateFNeg(v, "fneg"));
        return ctx.ir_gen->wrap(ctx.builder.CreateNeg(v, "neg"));
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
IRValue* Compiler::visitSpawn(SpawnExpr* spawn) {
    // 1. Resolve the target function
    CompilerContext::FunctionInfo* fi = resolveFunction(spawn->functionName);
    if (!fi || !fi->func) {
        LOG_ERROR("spawn: unknown function: " + spawn->functionName);
        return nullptr;
    }
    llvm::Function* targetFn = fi->func;

    // 2. Evaluate arguments
    std::vector<llvm::Value*> argVals;
    for (auto& arg : spawn->args) {
        IRValue* v = visitExpression(arg.get());
        if (v) argVals.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)));
    }

    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();

    // 3. Build or reuse a trampoline: void*(void*) that unpacks args and calls targetFn
    std::string trampolineName = "__aiml_spawn_" + spawn->functionName;
    llvm::Function* trampoline = ctx.module.getFunction(trampolineName);
    if (!trampoline) {
        std::vector<llvm::Type*> argTypes;
        for (auto* v : argVals) argTypes.push_back(v->getType());
        llvm::StructType* argStructTy = argTypes.empty()
            ? nullptr : llvm::StructType::get(ctx.context, argTypes);

        llvm::FunctionType* trampolineTy = llvm::FunctionType::get(i8p, {i8p}, false);
        trampoline = llvm::Function::Create(trampolineTy,
            llvm::Function::InternalLinkage, trampolineName, ctx.module);

        llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
        llvm::BasicBlock* tBB = llvm::BasicBlock::Create(ctx.context, "entry", trampoline);
        ctx.builder.SetInsertPoint(tBB);

        llvm::Value* rawArgPtr = trampoline->arg_begin();
        std::vector<llvm::Value*> callArgs;
        if (argStructTy) {
            llvm::Value* argStructPtr = ctx.builder.CreateBitCast(
                rawArgPtr, argStructTy->getPointerTo(), "args");
            for (size_t i = 0; i < argTypes.size(); ++i) {
                llvm::Value* gep = ctx.builder.CreateStructGEP(argStructTy, argStructPtr, (unsigned)i);
                callArgs.push_back(ctx.builder.CreateLoad(argTypes[i], gep));
            }
            llvm::Function* freeFn = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(targetFn, callArgs);
            ctx.builder.CreateCall(freeFn, {rawArgPtr});
        } else {
            ctx.builder.CreateCall(targetFn, callArgs);
        }
        ctx.builder.CreateRet(llvm::ConstantPointerNull::get(
            static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.restoreIP(savedIP);
    }

    // 4. Malloc args struct and fill it
    llvm::Value* argsVoidPtr = llvm::ConstantPointerNull::get(
        static_cast<llvm::PointerType*>(i8p));
    if (!argVals.empty()) {
        std::vector<llvm::Type*> argTypes;
        for (auto* v : argVals) argTypes.push_back(v->getType());
        llvm::StructType* argStructTy = llvm::StructType::get(ctx.context, argTypes);
        llvm::DataLayout dl(&ctx.module);
        uint64_t argStructSize = dl.getTypeAllocSize(argStructTy);

        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* rawArgsPtr  = ctx.builder.CreateCall(mallocFn,
            {ctx.builder.getInt64(argStructSize)}, "args_raw");
        llvm::Value* argsPtr = ctx.builder.CreateBitCast(
            rawArgsPtr, argStructTy->getPointerTo(), "args_ptr");
        for (size_t i = 0; i < argVals.size(); ++i) {
            llvm::Value* gep = ctx.builder.CreateStructGEP(argStructTy, argsPtr, (unsigned)i);
            ctx.builder.CreateStore(argVals[i], gep);
        }
        argsVoidPtr = rawArgsPtr;
    }

    // 5. Malloc the thread_handle struct { i64 tid, i8 joined, i8 detached }
    llvm::StructType* handleTy = llvm::StructType::getTypeByName(ctx.context, "thread_handle");
    if (!handleTy) {
        LOG_ERROR("spawn: thread_handle type not found");
        return nullptr;
    }
    llvm::DataLayout dl(&ctx.module);
    llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
    llvm::Value* rawHandle = ctx.builder.CreateCall(mallocFn,
        {ctx.builder.getInt64(dl.getTypeAllocSize(handleTy))}, "handle_raw");
    llvm::Value* handlePtr = ctx.builder.CreateBitCast(rawHandle, handleTy->getPointerTo(), "handle_ptr");
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr"));
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr"));

    // 6. pthread_create(&handle->tid, NULL, trampoline, args)
    llvm::Type* i64p = ctx.builder.getInt64Ty()->getPointerTo();
    llvm::Value* tidGEP = ctx.builder.CreateBitCast(
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_gep"), i64p);
    llvm::Value* trampolinePtr = ctx.builder.CreateBitCast(trampoline, i8p);
    llvm::Function* pthreadCreate = ctx.getFunction("pthread_create",
        ctx.builder.getInt32Ty(), {i64p, i8p, i8p, i8p});
    ctx.builder.CreateCall(pthreadCreate, {
        tidGEP,
        llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
        trampolinePtr,
        argsVoidPtr
    });

    // 7. Return the handle pointer (stored into the thread_handle* alloca by visitSetStmt)
    return ctx.ir_gen->wrap(handlePtr);
}

} // namespace AST
} // namespace AIML
