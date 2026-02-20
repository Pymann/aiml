#include "call_dispatcher.h"
#include <iostream>

using Evaluator = CallDispatcher::Evaluator;

// --- Specialized Dispatchers ---

static llvm::Value* dispatchUserMethod(CompilerContext& ctx, const std::string& className, const std::string& methodName, Symbol& sym, bool isPointer, const std::vector<YAML::Node>& args, Evaluator evaluator, llvm::BasicBlock* exceptionBlock) {
    if (ctx.classes.count(className) == 0) return nullptr;
    
    std::string mangledName = className + "_" + methodName;
    
    llvm::Value* thisPtr = sym.alloca_inst;
    if (isPointer) {
         thisPtr = ctx.builder.CreateLoad(ctx.getLLVMType("ptr<" + className + ">"), sym.alloca_inst, "this_ptr");
    }
    
    std::vector<llvm::Value*> llvmArgs;
    llvmArgs.push_back(thisPtr); 
    
    for (size_t i = 1; i < args.size(); ++i) {
        llvm::Value* argVal = evaluator(ctx, args[i]);
        if (!argVal) return nullptr;
        llvmArgs.push_back(argVal);
    }
    
    llvm::Function* func = nullptr;
    std::string currentClassLookup = className;
    
    while (true) {
        std::string lookupName = currentClassLookup + "_" + methodName;
        func = ctx.module.getFunction(lookupName);
        if (func) break;
        if (ctx.classes.count(currentClassLookup) && !ctx.classes[currentClassLookup].parentName.empty()) {
             currentClassLookup = ctx.classes[currentClassLookup].parentName;
        } else break;
    }

    if (!func) {
        LOG_ERROR("Method  + methodName +  not found in class  + className +  or its parents.");
        return nullptr;
    }
    
    if (func->arg_size() > 0) {
        llvm::Type* expectedThisTy = func->getFunctionType()->getParamType(0);
        if (thisPtr->getType() != expectedThisTy && thisPtr->getType()->isPointerTy() && expectedThisTy->isPointerTy()) {
             thisPtr = ctx.builder.CreateBitCast(thisPtr, expectedThisTy);
             llvmArgs[0] = thisPtr;
        }
    }
    
    if (exceptionBlock) {
        llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "invoke_normal", ctx.builder.GetInsertBlock()->getParent());
        llvm::Value* res = ctx.builder.CreateInvoke(func, normalBB, exceptionBlock, llvmArgs, "invoke_method");
        ctx.builder.SetInsertPoint(normalBB);
        return res;
    }
    return ctx.builder.CreateCall(func, llvmArgs, "call_method");
}

static llvm::Value* dispatchListMethod(CompilerContext& ctx, const std::string& methodName, Symbol& sym, const std::vector<YAML::Node>& args, Evaluator evaluator) {
    if (methodName == "push") {
         if (args.size() > 1) {
             llvm::Value* val = nullptr;
             if (sym.element_type && sym.element_type->isPointerTy() && args[1].IsScalar()) {
                  std::string argName = args[1].as<std::string>();
                  if (ctx.symbol_table.count(argName) && ctx.symbol_table[argName].type->isStructTy()) val = ctx.symbol_table[argName].alloca_inst;
             }
             if (!val) val = evaluator(ctx, args[1]);
             ListImpl::push(ctx, sym.alloca_inst, val);
         }
         return nullptr;
    } else if (methodName == "pop") {
         return ListImpl::pop(ctx, sym.alloca_inst, sym.element_type);
    } else if (methodName == "get") {
         if (args.size() > 1) return ListImpl::get(ctx, sym.alloca_inst, evaluator(ctx, args[1]), sym.element_type);
         return nullptr;
    } else if (methodName == "insert") {
         if (args.size() > 2) {
             llvm::Value* idx = evaluator(ctx, args[1]);
             llvm::Value* val = nullptr;
             if (sym.element_type && sym.element_type->isPointerTy() && args[2].IsScalar()) {
                  std::string argName = args[2].as<std::string>();
                  if (ctx.symbol_table.count(argName) && ctx.symbol_table[argName].type->isStructTy()) val = ctx.symbol_table[argName].alloca_inst;
             }
             if (!val) val = evaluator(ctx, args[2]);
             ListImpl::insert(ctx, sym.alloca_inst, idx, val);
         }
         return nullptr;
    } else if (methodName == "remove") {
         if (args.size() > 1) ListImpl::remove(ctx, sym.alloca_inst, evaluator(ctx, args[1]), sym.element_type);
         return nullptr;
    } else if (methodName == "clear") {
         ListImpl::clear(ctx, sym.alloca_inst);
         return nullptr;
    } else if (methodName == "len") {
         return ListImpl::getSize(ctx, sym.alloca_inst);
    } else if (methodName == "empty") {
         return ctx.builder.CreateZExt(ListImpl::isEmpty(ctx, sym.alloca_inst), ctx.builder.getInt32Ty());
    }
    return nullptr;
}

static llvm::Value* dispatchStringMethod(CompilerContext& ctx, const std::string& methodName, Symbol& sym) {
     if (methodName == "len") {
         llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
         if (!strlenFunc) {
             llvm::FunctionType* ft = llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false);
             strlenFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "strlen", ctx.module);
         }
         llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), sym.alloca_inst);
         llvm::Value* len64 = ctx.builder.CreateCall(strlenFunc, {strPtr});
         return ctx.builder.CreateTruncOrBitCast(len64, ctx.builder.getInt32Ty());
     }
     return nullptr;
}

static llvm::Value* dispatchMapMethod(CompilerContext& ctx, const std::string& methodName, Symbol& sym, const std::vector<YAML::Node>& args, Evaluator evaluator) {
    llvm::StructType* mapType = llvm::cast<llvm::StructType>(sym.type);
    if (methodName == "put") {
        if (args.size() > 2) {
            llvm::Value* key = evaluator(ctx, args[1]);
            llvm::Value* val = nullptr;
            if (sym.value_type && sym.value_type->isPointerTy() && args[2].IsScalar()) {
                  std::string argName = args[2].as<std::string>();
                  if (ctx.symbol_table.count(argName) && ctx.symbol_table[argName].type->isStructTy()) val = ctx.symbol_table[argName].alloca_inst;
            }
            if (!val) val = evaluator(ctx, args[2]);
            MapImpl::put(ctx, sym.alloca_inst, key, val, mapType);
        }
        return nullptr;
    } else if (methodName == "get") {
        if (args.size() > 1) return MapImpl::get(ctx, sym.alloca_inst, evaluator(ctx, args[1]), sym.value_type, mapType);
        return nullptr;
    } else if (methodName == "contains") {
        if (args.size() > 1) return ctx.builder.CreateZExt(MapImpl::contains(ctx, sym.alloca_inst, evaluator(ctx, args[1]), mapType), ctx.builder.getInt32Ty());
        return nullptr;
    } else if (methodName == "remove") {
         if (args.size() > 1) MapImpl::remove(ctx, sym.alloca_inst, evaluator(ctx, args[1]), sym.value_type, mapType);
         return nullptr;
    } else if (methodName == "clear") {
         llvm::Value* sizePtr = ctx.builder.CreateStructGEP(mapType, sym.alloca_inst, 0);
         ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);
         return nullptr;
    } else if (methodName == "len") {
         return MapImpl::getSize(ctx, sym.alloca_inst, mapType);
    }
    return nullptr;
}

static llvm::Value* dispatchBufferMethod(CompilerContext& ctx, const std::string& methodName, Symbol& sym, const std::vector<YAML::Node>& args, Evaluator evaluator) {
    if (methodName == "alloc") {
        if (args.size() > 1) BufferImpl::alloc(ctx, sym.alloca_inst, evaluator(ctx, args[1]));
        return nullptr;
    } else if (methodName == "write") {
        if (args.size() > 2) {
             llvm::Value* val = evaluator(ctx, args[1]);
             std::string typeName = args[2].IsScalar() ? args[2].as<std::string>() : "";
             llvm::Type* writeType = typeName.empty() ? val->getType() : ctx.getLLVMType(typeName);
             BufferImpl::write(ctx, sym.alloca_inst, val, writeType);
        }
        return nullptr;
    } else if (methodName == "read_versioned") {
         if (args.size() > 1) {
             std::string typeName = args[1].IsScalar() ? args[1].as<std::string>() : "";
             llvm::Type* readType = typeName.empty() ? nullptr : ctx.getLLVMType(typeName);
             if (readType) {
                 llvm::Value* verVal = args.size() > 2 ? evaluator(ctx, args[2]) : nullptr;
                 return BufferImpl::read_versioned(ctx, sym.alloca_inst, readType, evaluator, verVal);
             }
         }
         return nullptr;
    } else if (methodName == "read") {
         if (args.size() > 1) {
             std::string typeName = args[1].IsScalar() ? args[1].as<std::string>() : "";
             llvm::Type* readType = typeName.empty() ? nullptr : ctx.getLLVMType(typeName);
             if (readType) return BufferImpl::read(ctx, sym.alloca_inst, readType);
             else LOG_ERROR("Unknown type for buffer.read: " + typeName);
         }
         return nullptr;
    } else if (methodName == "seek") {
         if (args.size() > 1) {
             llvm::Value* pos = evaluator(ctx, args[1]);
             BufferImpl::seek(ctx, sym.alloca_inst, pos, false);
             BufferImpl::seek(ctx, sym.alloca_inst, pos, true); 
         }
         return nullptr;
    } else if (methodName == "reset") {
         BufferImpl::reset(ctx, sym.alloca_inst);
         return nullptr;
    }
    return nullptr;
}

static llvm::Value* dispatchProcessMethod(CompilerContext& ctx, const std::string& methodName, Symbol& sym) {
     llvm::Function* waitFunc = ctx.module.getFunction("process_wait");
     if (!waitFunc) waitFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_wait", ctx.module);
     
     llvm::Function* killFunc = ctx.module.getFunction("process_kill");
     if (!killFunc) killFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_kill", ctx.module);
     
     if (methodName == "wait") {
         return ctx.builder.CreateCall(waitFunc, {ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), sym.alloca_inst)});
     } else if (methodName == "kill") {
         ctx.builder.CreateCall(killFunc, {ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), sym.alloca_inst)});
         return nullptr;
     }
     return nullptr;
}


llvm::Value* CallDispatcher::dispatch(CompilerContext& ctx, const std::string& funcName, const std::vector<YAML::Node>& args, Evaluator evaluator, llvm::BasicBlock* exceptionBlock) {
    if (auto sysCall = SystemRegistry::instance().get(funcName)) {
        return sysCall->compile(ctx, args);
    }

    size_t methodDotPos = funcName.rfind('.');
    if (methodDotPos != std::string::npos) {
        std::string objName = funcName.substr(0, methodDotPos);
        std::string methodName = funcName.substr(methodDotPos + 1);
        
        if (ctx.symbol_table.count(objName) == 0) goto try_function_lookup;
        Symbol& sym = ctx.symbol_table[objName];
        
        std::string className;
        bool isPointer = false;
        
        if (sym.type->isStructTy()) {
             llvm::StructType* st = llvm::cast<llvm::StructType>(sym.type);
             if (st->hasName()) className = st->getName().str();
        } else if (sym.type->isPointerTy() && !sym.className.empty()) {
             className = sym.className;
             isPointer = true;
        }
        
        if (!className.empty() && ctx.classes.count(className)) return dispatchUserMethod(ctx, className, methodName, sym, isPointer, args, evaluator, exceptionBlock);
        
        if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) return dispatchListMethod(ctx, methodName, sym, args, evaluator);
        if (sym.typeName == "string") return dispatchStringMethod(ctx, methodName, sym);
        if (sym.type->isStructTy() && sym.type->getStructName().startswith("map_")) return dispatchMapMethod(ctx, methodName, sym, args, evaluator);
        if (sym.type->isStructTy() && sym.type->getStructName() == "buffer") return dispatchBufferMethod(ctx, methodName, sym, args, evaluator);
        if (sym.className == "process") return dispatchProcessMethod(ctx, methodName, sym);
        
        LOG_ERROR("Method call on unknown class type or method not found: " + objName);
        return nullptr;
    }

    try_function_lookup:
    llvm::Function* func = ctx.module.getFunction(funcName);
    if (!func) {
         if (funcName == "malloc") {
             func = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
         }
    }

    if (func) {
         std::vector<llvm::Value*> callArgs;
         for (size_t i = 1; i < args.size(); ++i) {
             llvm::Type* expectedTy = nullptr;
             if (i-1 < func->arg_size()) expectedTy = func->getFunctionType()->getParamType(i-1);

             llvm::Value* val = nullptr;
             if (expectedTy && expectedTy->isPointerTy() && args[i].IsScalar()) {
                  std::string argName = args[i].as<std::string>();
                  if (ctx.symbol_table.count(argName)) {
                      Symbol& s = ctx.symbol_table[argName];
                      if (s.type->isStructTy()) { val = s.alloca_inst; LOG_DEBUG("Implicit Reference Pass for " + argName); }
                  }
             }
             if (!val) val = evaluator(ctx, args[i]);
             if (expectedTy && val->getType() != expectedTy && val->getType()->isPointerTy() && expectedTy->isPointerTy()) {
                 val = ctx.builder.CreateBitCast(val, expectedTy);
             }
             callArgs.push_back(val);
         }
         
         if (exceptionBlock) {
             llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "invoke_normal", ctx.builder.GetInsertBlock()->getParent());
             llvm::Value* res = ctx.builder.CreateInvoke(func, normalBB, exceptionBlock, callArgs);
             ctx.builder.SetInsertPoint(normalBB);
             return res;
         }
         return ctx.builder.CreateCall(func, callArgs);
    }
    LOG_ERROR("Function " + funcName + " not implemented or invalid format.");
    return nullptr;
}

