#pragma once
#include "context.h"
#include "../sys/registry.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include <vector>
#include <string>
#include <functional>
#include <yaml-cpp/yaml.h>
#include "../../logger.h"

class CallDispatcher {
public:
    using Evaluator = std::function<llvm::Value*(CompilerContext&, const YAML::Node&)>;

    static llvm::Value* dispatch(CompilerContext& ctx, const std::string& funcName, const std::vector<YAML::Node>& args, Evaluator evaluator, llvm::BasicBlock* exceptionBlock = nullptr) {
        // Method Call Check: obj.method
        size_t methodDotPos = funcName.rfind('.');
        if (methodDotPos != std::string::npos) {
            std::string objName = funcName.substr(0, methodDotPos);
            std::string methodName = funcName.substr(methodDotPos + 1);
            
            if (ctx.symbol_table.count(objName) == 0) {
                 LOG_ERROR("Undefined object for method call: " + objName);
                 return nullptr;
            }
            
            Symbol& sym = ctx.symbol_table[objName];
            std::string className;
            bool isPointer = false;
            
            if (sym.type->isStructTy()) {
                 // Stack allocated struct
                 llvm::StructType* st = llvm::cast<llvm::StructType>(sym.type);
                 if (st->hasName()) className = st->getName().str();
            } else if (sym.type->isPointerTy() && !sym.className.empty()) {
                 className = sym.className;
                 isPointer = true;
            }
            
            if (!className.empty() && ctx.classes.count(className)) {
                // ... (User Class Logic) ...
                // Mangle Name
                std::string mangledName = className + "_" + methodName;
                
                // Prepare 'this' pointer
                llvm::Value* thisPtr = sym.alloca_inst;
                if (isPointer) {
                     thisPtr = ctx.builder.CreateLoad(ctx.getLLVMType("ptr<" + className + ">"), sym.alloca_inst, "this_ptr");
                }
                
                // Prepare Args
                std::vector<llvm::Value*> llvmArgs;
                llvmArgs.push_back(thisPtr); // Inject 'this'
                
                // Skip args[0] (function name)
                for (size_t i = 1; i < args.size(); ++i) {
                    llvm::Value* argVal = evaluator(ctx, args[i]);
                    if (!argVal) return nullptr;
                    llvmArgs.push_back(argVal);
                }
                
                llvm::Function* func = nullptr;
                std::string currentClassLookup = className;
                
                // Walk up the inheritance chain
                while (true) {
                    std::string mangledName = currentClassLookup + "_" + methodName;
                    func = ctx.module.getFunction(mangledName);
                    
                    if (func) break; // Found it!
                    
                    // Check parent
                    if (ctx.classes.count(currentClassLookup) && !ctx.classes[currentClassLookup].parentName.empty()) {
                         currentClassLookup = ctx.classes[currentClassLookup].parentName;
                    } else {
                         break; // No parent or unknown class
                    }
                }

                if (!func) {
                    LOG_ERROR("Method '" + methodName + "' not found in class '" + className + "' or its parents.");
                    return nullptr;
                }
                
                // Cast 'this' pointer if necessary (if method belongs to parent)
                // Actually, if we use opaque pointers, no cast needed! 
                // But strictly for typed pointer logic (if we ever go back), we might need it.
                // For now, func expects ptr<Parent>, we have ptr<Child>.
                // In opaque ptr world, they are same.
                // In typed ptr world, bitcast.
                // Adding bitcast is safe.
                
                // Get Expected Argument Type for 'this' (Arg 0)
                if (func->arg_size() > 0) {
                    llvm::Type* expectedThisTy = func->getFunctionType()->getParamType(0);
                    if (thisPtr->getType() != expectedThisTy) {
                        thisPtr = ctx.builder.CreateBitCast(thisPtr, expectedThisTy);
                    }
                }
                
                if (exceptionBlock) {
                    llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "invoke_normal", ctx.builder.GetInsertBlock()->getParent());
                    llvm::Value* res = ctx.builder.CreateInvoke(func, normalBB, exceptionBlock, llvmArgs, "invoke_method");
                    ctx.builder.SetInsertPoint(normalBB);
                    return res;
                } else {
                    return ctx.builder.CreateCall(func, llvmArgs, "call_method");
                }
            }
            
            // --- LIST ---
            if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
                if (methodName == "push") {
                     if (args.size() > 1) {
                         llvm::Value* val = evaluator(ctx, args[1]);
                         ListImpl::push(ctx, sym.alloca_inst, val);
                     }
                     return nullptr; // Void
                } else if (methodName == "pop") {
                     return ListImpl::pop(ctx, sym.alloca_inst, sym.element_type);
                } else if (methodName == "get") {
                     if (args.size() > 1) {
                         llvm::Value* idx = evaluator(ctx, args[1]);
                         return ListImpl::get(ctx, sym.alloca_inst, idx, sym.element_type);
                     }
                     return nullptr;
                } else if (methodName == "insert") {
                     if (args.size() > 2) {
                         llvm::Value* idx = evaluator(ctx, args[1]);
                         llvm::Value* val = evaluator(ctx, args[2]);
                         ListImpl::insert(ctx, sym.alloca_inst, idx, val);
                     }
                     return nullptr;
                } else if (methodName == "remove") {
                     if (args.size() > 1) {
                         llvm::Value* idx = evaluator(ctx, args[1]);
                         ListImpl::remove(ctx, sym.alloca_inst, idx, sym.element_type);
                     }
                     return nullptr;
                } else if (methodName == "clear") {
                     ListImpl::clear(ctx, sym.alloca_inst);
                     return nullptr;
                } else if (methodName == "size") {
                     return ListImpl::getSize(ctx, sym.alloca_inst);
                } else if (methodName == "empty") {
                     llvm::Value* isEmpty = ListImpl::isEmpty(ctx, sym.alloca_inst);
                     return ctx.builder.CreateZExt(isEmpty, ctx.builder.getInt32Ty());
                }
            } 
            // --- MAP ---
            else if (sym.type->isStructTy() && sym.type->getStructName().startswith("map_")) {
                llvm::StructType* mapType = llvm::cast<llvm::StructType>(sym.type);
                if (methodName == "put") {
                    if (args.size() > 2) {
                        llvm::Value* key = evaluator(ctx, args[1]);
                        llvm::Value* val = evaluator(ctx, args[2]);
                        MapImpl::put(ctx, sym.alloca_inst, key, val, mapType);
                    }
                    return nullptr;
                } else if (methodName == "get") {
                    if (args.size() > 1) {
                        llvm::Value* key = evaluator(ctx, args[1]);
                        return MapImpl::get(ctx, sym.alloca_inst, key, sym.value_type, mapType);
                    }
                    return nullptr;
                } else if (methodName == "contains") {
                    if (args.size() > 1) {
                        llvm::Value* key = evaluator(ctx, args[1]);
                        // i1 result
                        llvm::Value* res = MapImpl::contains(ctx, sym.alloca_inst, key, mapType);
                        // Extend to i32 for expression compatibility (most things are i32)
                         return ctx.builder.CreateZExt(res, ctx.builder.getInt32Ty());
                    }
                    return nullptr;
                } else if (methodName == "remove") {
                     if (args.size() > 1) {
                         llvm::Value* key = evaluator(ctx, args[1]);
                         MapImpl::remove(ctx, sym.alloca_inst, key, sym.value_type, mapType);
                     }
                     return nullptr;
                } else if (methodName == "clear") {
                     // manually set size to 0
                     llvm::Value* sizePtr = ctx.builder.CreateStructGEP(mapType, sym.alloca_inst, 0);
                     ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);
                     return nullptr;
                } else if (methodName == "size") {
                     return MapImpl::getSize(ctx, sym.alloca_inst, mapType);
                }
            } 
            // --- BUFFER ---
            else if (sym.type->isStructTy() && sym.type->getStructName() == "buffer") {
                if (methodName == "alloc") {
                    if (args.size() > 1) {
                         llvm::Value* size = evaluator(ctx, args[1]);
                         BufferImpl::alloc(ctx, sym.alloca_inst, size);
                    }
                    return nullptr;
                } else if (methodName == "write") {
                    // write(val, type)
                    if (args.size() > 2) {
                         llvm::Value* val = evaluator(ctx, args[1]);
                         
                         // Type resolution from 2nd arg?
                         // args[2] is the TYPE NAME (e.g. i32, Header).
                         // But args[2] is a YAML::Node (Expression).
                         // If it's a bare word, we treat it as a type name?
                         // The evaluator evaluates VALUES. 
                         // We need a helper to resolve TYPE from AST Node if it's a type name.
                         // Or we use the type of 'val'?
                         // "call: [packet.write, h, Header]" -> Evaluator(h) gives Val. Evaluator(Header) -> Error? or Value(0)?
                         // We need to look at args[2] as a TYPE string.
                         
                         std::string typeName;
                         if (args[2].IsScalar()) typeName = args[2].as<std::string>();
                         
                         llvm::Type* writeType = nullptr;
                         if (!typeName.empty()) writeType = ctx.getLLVMType(typeName);
                         
                         if (!writeType) {
                              // Auto-deduce from value?
                              writeType = val->getType();
                         }
                         
                         BufferImpl::write(ctx, sym.alloca_inst, val, writeType);
                    }
                    return nullptr;
                } else if (methodName == "read_versioned") {
                     // read_versioned(type, [version])
                     if (args.size() > 1) {
                         std::string typeName;
                         if (args[1].IsScalar()) typeName = args[1].as<std::string>();
                         llvm::Type* readType = nullptr;
                         if (!typeName.empty()) readType = ctx.getLLVMType(typeName);
                         
                         if (readType) {
                             llvm::Value* verVal = nullptr;
                             if (args.size() > 2) {
                                 verVal = evaluator(ctx, args[2]);
                             }
                             return BufferImpl::read_versioned(ctx, sym.alloca_inst, readType, evaluator, verVal);
                         }
                     }
                     return nullptr;
                } else if (methodName == "read") {
                    // read(type)
                     if (args.size() > 1) {
                         std::string typeName;
                         if (args[1].IsScalar()) typeName = args[1].as<std::string>();
                         
                         llvm::Type* readType = nullptr;
                         if (!typeName.empty()) readType = ctx.getLLVMType(typeName);
                         
                         if (readType) {
                              return BufferImpl::read(ctx, sym.alloca_inst, readType);
                         } else {
                              LOG_ERROR("Unknown type for buffer.read: " + typeName);
                         }
                     }
                     return nullptr;
                } else if (methodName == "seek") {
                     if (args.size() > 1) {
                         llvm::Value* pos = evaluator(ctx, args[1]);
                         BufferImpl::seek(ctx, sym.alloca_inst, pos, false); // Write Seek
                         BufferImpl::seek(ctx, sym.alloca_inst, pos, true);  // Read Seek (Sync for now?)
                         // Maybe seek_read / seek_write separation later?
                         // User asked for "internal read/write cursor". Usually implies separate or same?
                         // buffer spec didn't specify separate seek.
                         // Let's assume seek moves BOTH or we add explicit params.
                         // For now, move both.
                     }
                     return nullptr;
                } else if (methodName == "reset") {
                     BufferImpl::reset(ctx, sym.alloca_inst);
                     return nullptr;
                }
            } else {
                LOG_ERROR("Method call on unknown class type or method not found: " + objName);
                return nullptr;
            }
        }

        // 1. Check System Intrinsics
        if (auto sysCall = SystemRegistry::instance().get(funcName)) {
            // SystemCalls take raw Nodes for now.
            // In future, they should take Evaluated Values to be consistent.
            return sysCall->compile(ctx, args);
        }

        // 2. [REMOVED DUPLICATE LOGIC]


        // 3. User Defined Function Call (or Externals)
        llvm::Function* func = ctx.module.getFunction(funcName);
        if (!func) {
             // Auto-declare libc functions if used
             if (funcName == "malloc") {
                 std::vector<llvm::Type*> args;
                 args.push_back(ctx.builder.getInt64Ty()); // size_t
                 llvm::FunctionType* ft = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), args, false);
                 func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "malloc", ctx.module);
             }
        }

        if (func) {
             std::vector<llvm::Value*> callArgs;
             // Verify arg count (args[0] is func name)
             if (func->arg_size() != args.size() - 1) {
                 LOG_ERROR("Function '" + funcName + "' expects " + std::to_string(func->arg_size()) + " arguments, got " + std::to_string(args.size() - 1));
                 return nullptr; 
             }
             
             for (size_t i = 1; i < args.size(); ++i) {
                 llvm::Value* val = evaluator(ctx, args[i]);
                 callArgs.push_back(val);
             }
             
             if (exceptionBlock) {
                 llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "invoke_normal", ctx.builder.GetInsertBlock()->getParent());
                 llvm::Value* res = ctx.builder.CreateInvoke(func, normalBB, exceptionBlock, callArgs);
                 ctx.builder.SetInsertPoint(normalBB);
                 return res;
             } else {
                 return ctx.builder.CreateCall(func, callArgs);
             }
        }

        LOG_ERROR("Function '" + funcName + "' not implemented or invalid format.");
        return nullptr;
    }
};
