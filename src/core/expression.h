#pragma once
#include "context.h"
#include "utils.h"
#include "../../logger.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/IRBuilder.h>
#include "call_dispatcher.h"

namespace Expression {

inline llvm::Value* evaluate(CompilerContext& ctx, const YAML::Node& node) {
    LOG_DEBUG("Expression::evaluate Entry. Node Type: " + std::to_string(node.Type()));

    if (node.IsNull()) return ctx.builder.CreateGlobalStringPtr("NULL");

    // 1. Scalar checks (handled by resolveValue)
    if (node.IsScalar()) {
        try {
            return Utils::resolveValue(ctx, node);
        } catch (const std::exception& e) {
             LOG_ERROR("Expression Evaluate Scalar Error: " + std::string(e.what()));
             return ctx.builder.CreateGlobalStringPtr("SCALAR_ERROR");
        }
    }
    
    // 2. Map - It's an Operation or Call
    if (node.IsMap()) {
        if (node.size() == 0) return ctx.builder.CreateGlobalStringPtr("EMPTY_MAP");
        
        std::string op;
        try {
             // Debug: Log key type
             LOG_DEBUG("Expression KeyType: " + std::to_string(node.begin()->first.Type()));
             
             if (node.begin()->first.IsScalar()) {
                 op = node.begin()->first.as<std::string>();
                 LOG_DEBUG("Expression Op: " + op);
             } else {
                 LOG_ERROR("Map key is not a scalar string. Type: " + std::to_string(node.begin()->first.Type()));
                 return ctx.builder.CreateGlobalStringPtr("INVALID_KEY_TYPE");
             }
        } catch (const std::exception& e) {
             LOG_ERROR("Invalid Map Key conversion: " + std::string(e.what()));
             return ctx.builder.CreateGlobalStringPtr("INVALID_KEY_EXCEPTION");
        }
        
        const YAML::Node& args = node.begin()->second;
        LOG_DEBUG("Expression Op: [" + op + "], Args Type: " + std::to_string(args.Type()));
        
        // Check for 'new' operator (Heap Allocation)
        if (op == "new") {
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

                  // Call malloc: i8* malloc(i64)
                  llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
                  if (!mallocFunc) {
                       std::vector<llvm::Type*> mallocArgs;
                       mallocArgs.push_back(ctx.builder.getInt64Ty());
                       llvm::FunctionType* mallocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), mallocArgs, false);
                       mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", ctx.module);
                  }
                  
                  llvm::Value* rawPtr = ctx.builder.CreateCall(mallocFunc, sizeVal, "malloc_call");
                  
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
        }

        // Check for repeated '&' (Address-Of)
        // Syntax: { '&&': [varName] } -> Address of Address...
        if (op.find_first_not_of('&') == std::string::npos) {
            int depth = op.length();
            
            std::string varName;
            if (args.IsScalar()) varName = args.as<std::string>();
            else if (args.IsSequence() && args.size() > 0) varName = args[0].as<std::string>();
            
            if (!varName.empty()) {
                llvm::Value* currentVal = Utils::resolveLValue(ctx, varName); // First & (Address of var)
                
                if (currentVal) {
                    for (int i = 1; i < depth; ++i) {
                         llvm::AllocaInst* tempAlloca = ctx.builder.CreateAlloca(currentVal->getType());
                         ctx.builder.CreateStore(currentVal, tempAlloca);
                         currentVal = tempAlloca;
                    }
                    return currentVal;
                }
                LOG_ERROR("Could not take reference of: " + varName);
            }
            return ctx.builder.getInt32(0);
            
        } else if (op == "ref") {
             // Legacy ref (single depth)
             std::string varName;
             if (args.IsScalar()) varName = args.as<std::string>();
             else if (args.IsSequence() && args.size() > 0) varName = args[0].as<std::string>();
             
             if (!varName.empty()) {
                llvm::Value* ptr = Utils::resolveLValue(ctx, varName);
                if (ptr) return ptr;
                LOG_ERROR("Could not take reference of: " + varName);
             }
             return ctx.builder.getInt32(0);

        } else if (op.find_first_not_of('?') == std::string::npos) {
            // Dereference Operator
            int depth = op.length();
            
            llvm::Value* ptrVal = nullptr;
            if (args.IsSequence() && args.size() > 0) ptrVal = evaluate(ctx, args[0]);
            else ptrVal = evaluate(ctx, args);
            
            llvm::Value* currentVal = ptrVal;
            
            for(int i=0; i<depth; ++i) {
                if (currentVal && currentVal->getType()->isPointerTy()) {
                     llvm::Type* loadType = nullptr;
                     
                     if (i == 0 && args.IsSequence() && args[0].IsScalar()) {
                          std::string varName = args[0].as<std::string>();
                          if (ctx.symbol_table.count(varName)) {
                              Symbol& sym = ctx.symbol_table[varName];
                              if (i < depth - 1) {
                                  loadType = llvm::PointerType::get(ctx.context, 0);
                              } else {
                                  if (sym.element_type) loadType = sym.element_type;
                              }
                          }
                     }
                     
                     if (!loadType) {
                          if (i < depth - 1) loadType = llvm::PointerType::get(ctx.context, 0);
                          else loadType = ctx.builder.getInt32Ty(); // Fallback
                     }
                     
                     currentVal = ctx.builder.CreateLoad(loadType, currentVal, "deref_chain");
                } else {
                     LOG_ERROR("Dereference chain failed at depth " + std::to_string(i));
                     return ctx.builder.getInt32(0);
                }
            }
            return currentVal;
        }
        
        if (op == "call") {
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

        // Math Operations
        llvm::Value* lhs = nullptr;
        llvm::Value* rhs = nullptr;
        
        if (args.IsSequence() && args.size() >= 2) {
            lhs = evaluate(ctx, args[0]);
            rhs = evaluate(ctx, args[1]);
        }
        
        if (op == "eq" || op == "neq") {
            bool useStrcmp = false;
            
            // Check args to infer type
             if (args.IsSequence()) {
                 for(const auto& arg : args) {
                      if (arg.IsScalar()) {
                           std::string s = arg.as<std::string>();
                           
                           // 1. Check for String Literal (Not a symbol, assumes not a number if it resolved to pointer)
                           if (ctx.symbol_table.count(s) == 0 && s.find('.') == std::string::npos) {
                                // Likely a literal.
                                useStrcmp = true;
                           }
                           // 2. Check for Variable
                           else if (ctx.symbol_table.count(s)) {
                                Symbol& sym = ctx.symbol_table[s];
                                // In AIML, 'string' is i8* (PointerTy). 
                                // Objects are StructTy (on stack).
                                // So if it is a PointerTy, assume String for now.
                                if (sym.type->isPointerTy()) useStrcmp = true;
                           }
                           // 3. Check for Member Access
                           else if (s.find('.') != std::string::npos) {
                                size_t dotPos = s.rfind('.');
                                std::string obj = s.substr(0, dotPos);
                                std::string mem = s.substr(dotPos+1);
                                if (ctx.symbol_table.count(obj)) {
                                     Symbol& objSym = ctx.symbol_table[obj];
                                     std::string className;
                                     if (objSym.type->isStructTy()) {
                                          llvm::StructType* st = llvm::cast<llvm::StructType>(objSym.type);
                                          if (st->hasName()) className = st->getName().str();
                                     }
                                     if (!className.empty() && ctx.classes.count(className)) {
                                          auto& member = ctx.classes[className].members[mem];
                                          if (member.type->isPointerTy()) useStrcmp = true;
                                     }
                                }
                           }
                      }
                 }
             }

            // Check for String Comparison (i8*)
            // Only if inferred logic says so, AND types are pointers
            if (useStrcmp && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                
                // Declare strcmp
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
            
            if (op == "eq") return ctx.builder.CreateICmpEQ(lhs, rhs, "eq_tmp");
            else return ctx.builder.CreateICmpNE(lhs, rhs, "ne_tmp");
        } 
        
        if (op == "+") {
            return ctx.builder.CreateICmpSLT(lhs, rhs, "lt_tmp");
        } else if (op == "gt") {
            return ctx.builder.CreateICmpSGT(lhs, rhs, "gt_tmp");
        } else if (op == "le") {
            return ctx.builder.CreateICmpSLE(lhs, rhs, "le_tmp");
        } else if (op == "ge") {
            return ctx.builder.CreateICmpSGE(lhs, rhs, "ge_tmp");
        }
    }
    
    LOG_ERROR("Unknown expression type: " + std::to_string(node.Type())); // Better debug
    return ctx.builder.getInt32(0);
}

}
