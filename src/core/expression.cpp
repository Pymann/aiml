#include "expression.h"
#include <iostream>
#include <vector>
#include <string>
#include "call_dispatcher.h"

namespace Expression {

// --- Forward Declarations ---
static llvm::Value* evaluateScalar(CompilerContext& ctx, const YAML::Node& node);
static llvm::Value* evaluateMap(CompilerContext& ctx, const YAML::Node& node);
static llvm::Value* handleAddressOf(CompilerContext& ctx, const std::string& op, const YAML::Node& args);
static llvm::Value* handleDeref(CompilerContext& ctx, const std::string& op, const YAML::Node& args);

// --- Specialized Evaluator Classes / Namespaces ---

namespace MathEvaluator {
    static llvm::Value* handle(CompilerContext& ctx, const std::string& op, llvm::Value* lhs, llvm::Value* rhs) {
        if (op == "+") return ctx.builder.CreateAdd(lhs, rhs, "add_tmp");
        if (op == "-") return ctx.builder.CreateSub(lhs, rhs, "sub_tmp");
        if (op == "*") return ctx.builder.CreateMul(lhs, rhs, "mul_tmp");
        if (op == "/") return ctx.builder.CreateSDiv(lhs, rhs, "div_tmp");
        if (op == "%") return ctx.builder.CreateSRem(lhs, rhs, "mod_tmp");
        return ctx.builder.getInt32(0);
    }
}

namespace LogicEvaluator {
    static llvm::Value* handle(CompilerContext& ctx, const std::string& op, llvm::Value* lhs, llvm::Value* rhs, const YAML::Node& args) {
        if (op == "eq" || op == "neq") {
             bool useStrcmp = false;
             if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) useStrcmp = true; 

             if (useStrcmp) {
                 llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
                 if (!strcmpFunc) {
                     llvm::FunctionType* ft = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false);
                     strcmpFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "strcmp", ctx.module);
                 }
                 llvm::Value* cmpRes = ctx.builder.CreateCall(strcmpFunc, {lhs, rhs}, "strcmp_res");
                 llvm::Value* isZero = ctx.builder.CreateICmpEQ(cmpRes, ctx.builder.getInt32(0), "str_eq");
                 return (op == "eq") ? isZero : ctx.builder.CreateNot(isZero, "str_ne");
             }
        }

        if (op == "eq") return ctx.builder.CreateICmpEQ(lhs, rhs, "eq_tmp");
        if (op == "neq") return ctx.builder.CreateICmpNE(lhs, rhs, "ne_tmp");
        if (op == "lt") return ctx.builder.CreateICmpSLT(lhs, rhs, "lt_tmp");
        if (op == "gt") return ctx.builder.CreateICmpSGT(lhs, rhs, "gt_tmp");
        if (op == "le") return ctx.builder.CreateICmpSLE(lhs, rhs, "le_tmp");
        if (op == "ge") return ctx.builder.CreateICmpSGE(lhs, rhs, "ge_tmp");
        if (op == "and") return ctx.builder.CreateAnd(lhs, rhs, "and_tmp");
        if (op == "or") return ctx.builder.CreateOr(lhs, rhs, "or_tmp");

        return ctx.builder.getInt32(0);
    }
}

// --- Common Handlers ---

llvm::Value* handleNew(CompilerContext& ctx, const YAML::Node& args) {
    std::string className;
    if (args.IsScalar()) className = args.as<std::string>();
    else if (args.IsSequence() && args.size() > 0) className = args[0].as<std::string>();

    if (!className.empty() && ctx.classes.count(className)) {
        llvm::StructType* structTy = ctx.classes[className].type;
        uint64_t typeSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
        llvm::Value* sizeVal = ctx.builder.getInt64(typeSize);

        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
             callocFunc = llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
        }
          
        llvm::Value* rawPtr = ctx.builder.CreateCall(callocFunc, {ctx.builder.getInt64(1), sizeVal}, "calloc_call");
          
        auto& classInfo = ctx.classes[className];
        for (auto& [name, member] : classInfo.members) {
             if (member.initValue.IsDefined() && !member.initValue.IsNull()) {
                  llvm::Value* initVal = evaluate(ctx, member.initValue);
                  if (initVal) {
                       llvm::Value* memberPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtr, member.index);
                       ctx.builder.CreateStore(initVal, memberPtr);
                  }
             }
        }
        return rawPtr;
    }
    if (!className.empty()) LOG_ERROR("Unknown class for new: " + className);
    return ctx.builder.getInt32(0);
}

llvm::Value* handleCall(CompilerContext& ctx, const YAML::Node& args) {
    std::string funcName;
    std::vector<YAML::Node> callArgs;
     
    if (args.IsScalar()) {
        try { funcName = args.as<std::string>(); } catch (...) { return ctx.builder.CreateGlobalStringPtr("INVALID_FUNC_NAME"); }
    } else if (args.IsSequence()) {
        if (args.size() == 0) return ctx.builder.CreateGlobalStringPtr("EMPTY_CALL");
        try { funcName = args[0].as<std::string>(); } catch (...) { return ctx.builder.CreateGlobalStringPtr("INVALID_FUNC"); }
        for (const auto& arg : args) callArgs.push_back(arg);
    } else return ctx.builder.CreateGlobalStringPtr("INVALID_CALL_ARGS");

    llvm::Value* res = CallDispatcher::dispatch(ctx, funcName, callArgs, [](CompilerContext& c, const YAML::Node& n) { return evaluate(c, n); });
    return res ? res : ctx.builder.getInt32(0);
}

llvm::Value* handleBinary(CompilerContext& ctx, const YAML::Node& args, const std::string& op) {
    if (!args.IsSequence() || args.size() < 2) return ctx.builder.getInt32(0);
    llvm::Value* lhs = evaluate(ctx, args[0]);
    llvm::Value* rhs = evaluate(ctx, args[1]);

    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        return MathEvaluator::handle(ctx, op, lhs, rhs);
    } else {
        return LogicEvaluator::handle(ctx, op, lhs, rhs, args);
    }
}

llvm::Value* handleCast(CompilerContext& ctx, const YAML::Node& args) {
     if (args.IsSequence() && args.size() == 2) {
         llvm::Value* val = evaluate(ctx, args[0]);
         llvm::Type* targetType = ctx.getLLVMType(args[1].as<std::string>());
         if (val && targetType) {
              if (val->getType()->isPointerTy() && targetType->isPointerTy()) return ctx.builder.CreateBitCast(val, targetType, "cast_ptr");
              else if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) return ctx.builder.CreateIntCast(val, targetType, true, "cast_int");
              else return ctx.builder.CreateBitCast(val, targetType, "cast_fallback");
         }
     }
     return ctx.builder.getInt32(0);
}

// --- Main Evaluator Deconstruct ---

static llvm::Value* evaluateScalar(CompilerContext& ctx, const YAML::Node& node) {
    std::string val = node.as<std::string>();
    if (val.find("${") != std::string::npos) {
         return StringUtils::expandExpression(ctx, val, [](CompilerContext& c, const YAML::Node& n) { return evaluate(c, n); });
    }
    try {
        return Utils::resolveValue(ctx, node);
    } catch (const std::exception& e) {
         LOG_ERROR("Expression Evaluate Scalar Error: " + std::string(e.what()));
         return ctx.builder.CreateGlobalStringPtr("SCALAR_ERROR");
    }
}

static llvm::Value* handleAddressOf(CompilerContext& ctx, const std::string& op, const YAML::Node& args) {
    int depth = op.length();
    std::string varName;
    if (args.IsScalar()) varName = args.as<std::string>();
    else if (args.IsSequence() && args.size() > 0) varName = args[0].as<std::string>();
    
    if (!varName.empty()) {
        llvm::Value* currentVal = Utils::resolveLValue(ctx, varName);
        if (currentVal) {
            for (int i = 1; i < depth; ++i) {
                 llvm::AllocaInst* tempAlloca = ctx.builder.CreateAlloca(currentVal->getType());
                 ctx.builder.CreateStore(currentVal, tempAlloca);
                 currentVal = tempAlloca;
            }
            return currentVal;
        }
    }
    return ctx.builder.getInt32(0);
}

static llvm::Value* handleDeref(CompilerContext& ctx, const std::string& op, const YAML::Node& args) {
    int depth = op.length();
    llvm::Value* ptrVal = nullptr;
    std::string varName;
    if (args.IsSequence() && args.size() > 0) {
         ptrVal = evaluate(ctx, args[0]);
         if (args[0].IsScalar()) varName = args[0].as<std::string>();
    } else {
         ptrVal = evaluate(ctx, args);
         if (args.IsScalar()) varName = args.as<std::string>();
    }
    
    llvm::Value* currentVal = ptrVal;
    llvm::Type* baseType = nullptr;
    
    if (!varName.empty() && ctx.symbol_table.count(varName)) {
         std::string tName = ctx.symbol_table[varName].typeName;
         while (tName.rfind("ptr<", 0) == 0 || tName.rfind("ref<", 0) == 0) tName = tName.substr(4, tName.length() - 5);
         baseType = ctx.getLLVMType(tName);
    }

    for(int i=0; i<depth; ++i) {
        if (currentVal && currentVal->getType()->isPointerTy()) {
             llvm::Type* loadType = baseType;
             if (!loadType) loadType = ctx.builder.getInt32Ty(); 
             if (i < depth - 1) loadType = ctx.builder.getInt8Ty()->getPointerTo(); 
             currentVal = ctx.builder.CreateLoad(loadType, currentVal, "deref_chain");
        } else return ctx.builder.getInt32(0);
    }
    return currentVal;
}

static llvm::Value* evaluateMap(CompilerContext& ctx, const YAML::Node& node) {
    if (node.size() == 0) return ctx.builder.CreateGlobalStringPtr("EMPTY_MAP");
    
    std::string op;
    try {
         if (node.begin()->first.IsScalar()) op = node.begin()->first.as<std::string>();
         else { LOG_ERROR("Map key invalid."); return ctx.builder.CreateGlobalStringPtr("INVALID_KEY"); }
    } catch (...) { return ctx.builder.CreateGlobalStringPtr("INVALID_KEY_EXCEPTION"); }
    
    const YAML::Node& args = node.begin()->second;

    if (op.find_first_not_of('&') == std::string::npos) return handleAddressOf(ctx, op, args);
    if (op.find_first_not_of('?') == std::string::npos) return handleDeref(ctx, op, args);
    
    if (op == "new") return handleNew(ctx, args);
    if (op == "call") return handleCall(ctx, args);
    if (op == "cast") return handleCast(ctx, args);
    if (op == "ref") {
         std::string varName;
         if (args.IsScalar()) varName = args.as<std::string>();
         else if (args.IsSequence() && args.size() > 0) varName = args[0].as<std::string>();
         if (!varName.empty()) {
            llvm::Value* ptr = Utils::resolveLValue(ctx, varName);
            if (ptr) return ptr;
         }
         return ctx.builder.getInt32(0);
    }

    // Binary Ops Default
    if (op == "eq" || op == "neq" || op == "lt" || op == "gt" || op == "le" || op == "ge" || 
        op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || op == "and" || op == "or") {
        return handleBinary(ctx, args, op);
    }

    LOG_ERROR("Unknown expression op: " + op);
    return ctx.builder.getInt32(0);
}

// --- Main Evaluate Entry ---

llvm::Value* evaluate(CompilerContext& ctx, const YAML::Node& node) {
    if (node.IsNull()) return ctx.builder.CreateGlobalStringPtr("NULL");
    if (node.IsScalar()) return evaluateScalar(ctx, node);
    if (node.IsMap()) return evaluateMap(ctx, node);
    return ctx.builder.getInt32(0);
}

std::map<std::string, OpHandler> registry = {}; // Kept for header compat if needed, otherwise can remove later

} // namespace Expression
