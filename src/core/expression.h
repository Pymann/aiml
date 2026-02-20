#pragma once
#include "context.h"
#include "utils.h"
#include "../../logger.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/IRBuilder.h>
#include "call_dispatcher.h"
#include "string_utils.h"

namespace Expression {

    using OpHandler = std::function<llvm::Value*(CompilerContext&, const YAML::Node&)>;

    // Forward Declaration of evaluate for recursive usage
    inline llvm::Value* evaluate(CompilerContext& ctx, const YAML::Node& node);

    // --- Handler Implementations ---

    inline llvm::Value* handleNew(CompilerContext& ctx, const YAML::Node& args) {
         std::string className;
         if (args.IsScalar()) className = args.as<std::string>();
         else if (args.IsSequence() && args.size() > 0) className = args[0].as<std::string>();

         if (!className.empty() && ctx.classes.count(className)) {
             llvm::StructType* structTy = ctx.classes[className].type;
             
             // Calculate Size using DataLayout (Compile Time)
             const llvm::DataLayout& dl = ctx.module.getDataLayout();
             uint64_t typeSize = dl.getTypeAllocSize(structTy);
             LOG_DEBUG("Allocating " + std::to_string(typeSize) + " bytes for class " + className);
             llvm::Value* sizeVal = ctx.builder.getInt64(typeSize);

              // Call calloc: i8* calloc(i64, i64)
              llvm::Function* callocFunc = ctx.module.getFunction("calloc");
              if (!callocFunc) {
                   std::vector<llvm::Type*> callocArgs = {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()};
                   llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), callocArgs, false);
                   callocFunc = llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
              }
              
              llvm::Value* rawPtr = ctx.builder.CreateCall(callocFunc, {ctx.builder.getInt64(1), sizeVal}, "calloc_call");
              
              // Initialize Default Values
              auto& classInfo = ctx.classes[className];
              for (auto& [name, member] : classInfo.members) {
                   if (member.initValue.IsDefined() && !member.initValue.IsNull()) {
                        // Evaluate Init Value
                        llvm::Value* initVal = evaluate(ctx, member.initValue);
                        if (initVal) {
                             llvm::Value* memberPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtr, member.index);
                             ctx.builder.CreateStore(initVal, memberPtr);
                        }
                   }
              }

              return rawPtr; // Opaque pointer 
         }
         if (!className.empty()) LOG_ERROR("Unknown class for new: " + className);
         return ctx.builder.getInt32(0);
    }

    inline llvm::Value* handleCall(CompilerContext& ctx, const YAML::Node& args) {
         std::string funcName;
         std::vector<YAML::Node> callArgs;
         
         if (args.IsScalar()) {
             try {
                 funcName = args.as<std::string>();
             } catch (const std::exception& e) {
                 return ctx.builder.CreateGlobalStringPtr("INVALID_FUNC_NAME");
             }
         } else if (args.IsSequence()) {
             if (args.size() == 0) return ctx.builder.CreateGlobalStringPtr("EMPTY_CALL");
             
             try {
                 funcName = args[0].as<std::string>();
             } catch (const std::exception& e) {
                  return ctx.builder.CreateGlobalStringPtr("INVALID_FUNC");
             }
             
             for (const auto& arg : args) callArgs.push_back(arg);
        } else {
             return ctx.builder.CreateGlobalStringPtr("INVALID_CALL_ARGS");
        }

         llvm::Value* res = CallDispatcher::dispatch(ctx, funcName, callArgs, [](CompilerContext& c, const YAML::Node& n) {
             return evaluate(c, n);
         });
         
         if (!res) {
             return ctx.builder.getInt32(0);
         }
         return res;
    }

    inline llvm::Value* handleBinary(CompilerContext& ctx, const YAML::Node& args, const std::string& op) {
        llvm::Value* lhs = nullptr;
        llvm::Value* rhs = nullptr;
        
        if (args.IsSequence() && args.size() >= 2) {
            lhs = evaluate(ctx, args[0]);
            rhs = evaluate(ctx, args[1]);
        } else {
             return ctx.builder.getInt32(0); // Error
        }

        // String Comparison Logic (moved from main evaluate)
        if (op == "eq" || op == "neq") {
            bool useStrcmp = false;
            // Infer type from args
             if (args.IsSequence()) {
                 for(const auto& arg : args) {
                      if (arg.IsScalar()) {
                           std::string s = arg.as<std::string>();
                           if (ctx.symbol_table.count(s) == 0 && s.find('.') == std::string::npos) {
                                useStrcmp = true; // Literal
                           } else if (ctx.symbol_table.count(s)) {
                                Symbol& sym = ctx.symbol_table[s];
                                if (sym.type->isPointerTy()) useStrcmp = true;
                           } else if (s.find('.') != std::string::npos) {
                                // Member access check omitted for brevity but logic remains same if needed
                                // Assuming simplest heuristic for now or re-use full logic
                                // Let's simplify: if LHS and RHS are pointers, use strcmp?
                                // That's risky for int*, but we only have string* as common pointer type in simple AIML.
                                // Let's keep the heuristic simple:
                           }
                      }
                 }
             }
             // Better Check:
             if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                  // Assume string for now in this context?
                  // Or assume strcmp requested?
                  // Creating strcmp if not exists
                  useStrcmp = true; 
             }

            if (useStrcmp && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
                if (!strcmpFunc) {
                    std::vector<llvm::Type*> args = {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()};
                    llvm::FunctionType* ft = llvm::FunctionType::get(ctx.builder.getInt32Ty(), args, false);
                    strcmpFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "strcmp", ctx.module);
                }
                llvm::Value* cmpRes = ctx.builder.CreateCall(strcmpFunc, {lhs, rhs}, "strcmp_res");
                llvm::Value* isZero = ctx.builder.CreateICmpEQ(cmpRes, ctx.builder.getInt32(0), "str_eq");
                
                if (op == "eq") return isZero;
                else return ctx.builder.CreateNot(isZero, "str_ne");
            }
        }

        if (op == "eq") return ctx.builder.CreateICmpEQ(lhs, rhs, "eq_tmp");
        if (op == "neq") return ctx.builder.CreateICmpNE(lhs, rhs, "ne_tmp");
        if (op == "lt") return ctx.builder.CreateICmpSLT(lhs, rhs, "lt_tmp");
        if (op == "gt") return ctx.builder.CreateICmpSGT(lhs, rhs, "gt_tmp");
        if (op == "le") return ctx.builder.CreateICmpSLE(lhs, rhs, "le_tmp");
        if (op == "ge") return ctx.builder.CreateICmpSGE(lhs, rhs, "ge_tmp");
        
        if (op == "+") return ctx.builder.CreateAdd(lhs, rhs, "add_tmp");
        if (op == "-") return ctx.builder.CreateSub(lhs, rhs, "sub_tmp");
        if (op == "*") return ctx.builder.CreateMul(lhs, rhs, "mul_tmp");
        if (op == "/") return ctx.builder.CreateSDiv(lhs, rhs, "div_tmp");
        if (op == "%") return ctx.builder.CreateSRem(lhs, rhs, "mod_tmp");
        if (op == "and") {
            // Simplified boolean AND - bitwise or boolean depending on type, assuming i1 mapped from bool
            return ctx.builder.CreateAnd(lhs, rhs, "and_tmp");
        }
        if (op == "or") {
            return ctx.builder.CreateOr(lhs, rhs, "or_tmp");
        }

        return ctx.builder.getInt32(0);
    }
    
    inline llvm::Value* handleCast(CompilerContext& ctx, const YAML::Node& args) {
         if (args.IsSequence() && args.size() == 2) {
             llvm::Value* val = evaluate(ctx, args[0]);
             std::string targetTypeName = args[1].as<std::string>();
             llvm::Type* targetType = ctx.getLLVMType(targetTypeName);
             if (val && targetType) {
                  // Perform simple bitcast or int cast for MVP
                  if (val->getType()->isPointerTy() && targetType->isPointerTy()) {
                       // Opaque pointers might not even need bits casts, but CreateBitCast is safe
                       return ctx.builder.CreateBitCast(val, targetType, "cast_ptr");
                  } else if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
                       return ctx.builder.CreateIntCast(val, targetType, true, "cast_int"); // Assume signed
                  } else {
                       // General fallback bitcast
                       return ctx.builder.CreateBitCast(val, targetType, "cast_fallback");
                  }
             }
         }
         return ctx.builder.getInt32(0);
    }

    // --- Registry ---
    static const std::map<std::string, OpHandler> registry = {
        {"new", handleNew},
        {"call", handleCall},
        {"eq", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "eq"); }},
        {"neq", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "neq"); }},
        {"lt", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "lt"); }},
        {"gt", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "gt"); }},
        {"le", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "le"); }},
        {"ge", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "ge"); }},
        {"+", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "+"); }},
        {"-", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "-"); }},
        {"*", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "*"); }},
        {"/", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "/"); }},
        {"%", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "%"); }},
        {"and", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "and"); }},
        {"or", [](CompilerContext& c, const YAML::Node& n){ return handleBinary(c, n, "or"); }},
        {"cast", handleCast}
        // Legacy 'ref' could go here, but address-of '&' is dynamic
        // {"ref", handleRef} 
    };

    // --- Main Evaluate ---

    inline llvm::Value* evaluate(CompilerContext& ctx, const YAML::Node& node) {
        LOG_DEBUG("Expression::evaluate Entry. Node Type: " + std::to_string(node.Type()));

        if (node.IsNull()) return ctx.builder.CreateGlobalStringPtr("NULL");

        // 1. Scalar
        if (node.IsScalar()) {
            std::string val = node.as<std::string>();
            if (val.find("${") != std::string::npos) {
                 // Pass self as evaluator for recursive expansion
                 return StringUtils::expandExpression(ctx, val, [](CompilerContext& c, const YAML::Node& n) {
                     return evaluate(c, n);
                 });
            }
            try {
                return Utils::resolveValue(ctx, node);
            } catch (const std::exception& e) {
                 LOG_ERROR("Expression Evaluate Scalar Error: " + std::string(e.what()));
                 return ctx.builder.CreateGlobalStringPtr("SCALAR_ERROR");
            }
        }
        
        // 2. Map Operations
        if (node.IsMap()) {
            if (node.size() == 0) return ctx.builder.CreateGlobalStringPtr("EMPTY_MAP");
            
            std::string op;
            try {
                 if (node.begin()->first.IsScalar()) {
                     op = node.begin()->first.as<std::string>();
                     LOG_DEBUG("Expression Op: " + op);
                 } else {
                     LOG_ERROR("Map key invalid.");
                     return ctx.builder.CreateGlobalStringPtr("INVALID_KEY");
                 }
            } catch (const std::exception& e) {
                 return ctx.builder.CreateGlobalStringPtr("INVALID_KEY_EXCEPTION");
            }
            
            const YAML::Node& args = node.begin()->second;

            // 3. Fallback: Dynamic Operators (Address-Of / Deref)
            if (op.find_first_not_of('&') == std::string::npos) {
                // Address-Of Logic (Inline for now or extract)
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
            
            if (op.find_first_not_of('?') == std::string::npos) {
                // Deref Logic
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
                
                // Extract base type by parsing the original type string
                if (!varName.empty() && ctx.symbol_table.count(varName)) {
                     std::string tName = ctx.symbol_table[varName].typeName;
                     // Strip out all 'ptr<' and '>'
                     while (tName.rfind("ptr<", 0) == 0 || tName.rfind("ref<", 0) == 0) {
                          tName = tName.substr(4, tName.length() - 5);
                     }
                     baseType = ctx.getLLVMType(tName);
                }

                for(int i=0; i<depth; ++i) {
                    if (currentVal && currentVal->getType()->isPointerTy()) {
                         llvm::Type* loadType = baseType;
                         if (!loadType) loadType = ctx.builder.getInt32Ty(); // Default fallback
                         
                         // If we deref multiple times, intermediates are pointers
                         if (i < depth - 1) {
                              loadType = ctx.builder.getInt8Ty()->getPointerTo(); // Opaque pointer
                         }
                         
                         currentVal = ctx.builder.CreateLoad(loadType, currentVal, "deref_chain");
                    } else {
                         return ctx.builder.getInt32(0);
                    }
                }
                return currentVal;
            }

            // 4. Registry Lookup
            auto it = registry.find(op);
            if (it != registry.end()) {
                return it->second(ctx, args);
            }
            
            if (op == "ref") { // Handle legacy ref if not in map
                 std::string varName;
                 if (args.IsScalar()) varName = args.as<std::string>();
                 else if (args.IsSequence() && args.size() > 0) varName = args[0].as<std::string>();
                 if (!varName.empty()) {
                    llvm::Value* ptr = Utils::resolveLValue(ctx, varName);
                    if (ptr) return ptr;
                 }
                 return ctx.builder.getInt32(0);
            }

            LOG_ERROR("Unknown expression op: " + op);
        }
        
        return ctx.builder.getInt32(0);
    }
}
