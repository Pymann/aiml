#pragma once
#include "context.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/Value.h>
#include <string>

#include "string_utils.h"
#include "list_impl.h"
#include "map_impl.h"

namespace Utils {

// Forward declaration
inline llvm::Value* resolveValue(CompilerContext& ctx, const YAML::Node& argNode, CompilerContext::Evaluator evaluator = nullptr);

inline llvm::Value* resolveLValue(CompilerContext& ctx, const std::string& argStr, CompilerContext::Evaluator evaluator = nullptr, bool forWrite = false) {
        // Check for Dot Notation (Member Access)
        size_t dotPos = argStr.rfind('.');
        if (dotPos != std::string::npos) {
            std::string objName = argStr.substr(0, dotPos);
            std::string memberName = argStr.substr(dotPos + 1);
            
            // Resolve Base Object
            if (ctx.symbol_table.count(objName) == 0) {
                 // Check if it is '<' (this)
                 if (objName == "<") {
                      // It should be in symbol table if we are in a method!
                 }
                 // Could be a string literal with a dot (e.g. "file.txt" or "Done.")
                 // Return null so resolveValue can handle it as string.
                 return nullptr;
            }
            
            Symbol& sym = ctx.symbol_table[objName];
            
            // Check Base Variable Constness (if writing to the struct itself? No, writing to member)
            // If the struct variable is const, are its members const? Yes, typically.
            // But if it's a pointer? ptr<T> const p -> pointer is const, data is not?
            // "ptr<T> p" -> p points to T.
            // If p is const (ptr<T> const p), we can't change p (the address), but we can change p.x.
            // Unless it is "const ptr<T>" vs "ptr<const T>".
            // Our "const: true" on decl applies to the variable.
            // Stack struct: binding IS the data. So const applies to data.
            // Pointer: binding IS the address. Const applies to address.
            
            // So:
            // Stack Struct: Check sym.is_const if forWrite.
            if (sym.type->isStructTy() && sym.is_const && forWrite) {
                 LOG_ERROR("Cannot assign to member '" + memberName + "' of const variable '" + objName + "'");
                 return nullptr;
            }
            // Pointer: sym.is_const only protects the pointer value, not the pointee data.
            // UNLESS we support const pointers to data (ptr<const T>). We don't yet.

            llvm::Value* basePtr = sym.alloca_inst; 
            
            std::string className;
            bool isPointer = false;

            // Determine if it's a direct struct or a pointer to struct
            if (sym.type->isStructTy()) {
                 // Stack allocated struct (Direct Access)
                 // sym.alloca_inst points to the struct data
                 llvm::StructType* st = llvm::cast<llvm::StructType>(sym.type);
                 if (st->hasName()) className = st->getName().str();
            } else if (sym.type->isPointerTy() && !sym.className.empty()) {
                 // It's a pointer to an object (heap or ref)
                 // sym.alloca_inst points to the pointer
                 className = sym.className;
                 isPointer = true;
            }

            if (!className.empty() && ctx.classes.count(className)) {
                auto& classInfo = ctx.classes[className];
                if (classInfo.members.count(memberName)) {
                     auto& member = classInfo.members[memberName];
                     int index = member.index;
                     
                     // 1. Check Access Control
                     if (member.access == AccessLevel::Private) {
                         if (ctx.currentClassName != className) {
                             LOG_ERROR("Cannot access private member '" + memberName + "' of class '" + className + "' from '" + ctx.currentClassName + "'");
                             return nullptr;
                         }
                     } else if (member.access == AccessLevel::Protected) {
                         // Allow if same class (inheritance not impl yet)
                         if (ctx.currentClassName != className) {
                             LOG_ERROR("Cannot access protected member '" + memberName + "'");
                             return nullptr;
                         }
                     }

                     // 2. Check Const Correctness
                     if (forWrite && member.is_const) {
                          LOG_ERROR("Cannot assign to const member '" + memberName + "' of class '" + className + "'");
                          return nullptr; 
                     } else {
                          LOG_DEBUG("Const Check Passed: Member " + memberName + " IsConst: " + (member.is_const ? "TRUE" : "FALSE") + " ForWrite: " + (forWrite ? "TRUE" : "FALSE"));
                     }
                     
                     llvm::Value* structPtr = basePtr;
                     if (isPointer) {
                         // Load the pointer to get the address of the struct
                         structPtr = ctx.builder.CreateLoad(ctx.getLLVMType("ptr<" + className + ">"), basePtr, objName + "_ptr");
                     }
                     
                     // GEP
                     // LOG_DEBUG("resolveLValue Member Access: " + objName + "." + memberName);
                     // LOG_DEBUG("  SymType: " + std::to_string(sym.type->getTypeID()) + ", IsPtr: " + std::to_string(isPointer));
                     return ctx.builder.CreateStructGEP(classInfo.type, structPtr, index, "member_addr");
                }
                LOG_ERROR("Member '" + memberName + "' not found in class '" + className + "'");
                return nullptr;
            }
            
            LOG_ERROR("Cannot access member '" + memberName + "' on " + objName + " (unknown class type)");
            return nullptr;
        }

        // Check for array access: name-index
    size_t hyphenPos = argStr.rfind('-'); 
    
    if (hyphenPos != std::string::npos) {
        std::string varName = argStr.substr(0, hyphenPos);
        std::string indexStr = argStr.substr(hyphenPos + 1);
        
        // Ensure it's not a negative number or subtraction (handled by expression parser, but here we expect variable)
        // Check if varName exists as an array or list
        if (ctx.symbol_table.count(varName)) {
            Symbol& sym = ctx.symbol_table[varName];
            
            if (forWrite && sym.is_const) {
                 LOG_ERROR("Cannot modify element of const array/list '" + varName + "'");
                 return nullptr;
            }
             
            if (sym.type->isArrayTy()) {
                llvm::Value* indexVal = resolveValue(ctx, YAML::Node(indexStr), evaluator); 
                
                std::vector<llvm::Value*> indices;
                indices.push_back(ctx.builder.getInt32(0)); // Dereference pointer to array
                indices.push_back(indexVal); // Index into array
                
                return ctx.builder.CreateInBoundsGEP(sym.type, sym.alloca_inst, indices, varName + "_idx_" + indexStr);
            } else if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
                 // Dynamic List Access
                 llvm::Value* indexVal = resolveValue(ctx, YAML::Node(indexStr), evaluator);
                 return ListImpl::getElementPtr(ctx, sym.alloca_inst, indexVal, sym.element_type);
            }
        }
    }
    
    // Simple variable
    if (ctx.symbol_table.find(argStr) != ctx.symbol_table.end()) {
        Symbol& sym = ctx.symbol_table[argStr];
        if (forWrite && sym.is_const) {
             LOG_ERROR("Cannot assign to const variable '" + argStr + "'");
             return nullptr;
        }
        return sym.alloca_inst;
    }
    
    return nullptr;
}

inline llvm::Value* resolveValue(CompilerContext& ctx, const YAML::Node& argNode, CompilerContext::Evaluator evaluator) {
    // 0. Check for Map (Expression/Call)
    if (argNode.IsMap()) {
         if (evaluator) return evaluator(ctx, argNode);
         // Fallback if no evaluator?
         LOG_ERROR("Cannot evaluate map in resolveValue without evaluator");
         return ctx.builder.getInt32(0);
    }

    std::string argStr = argNode.as<std::string>();
    
    // 1. Try integer literal (Hex/Dec/Oct)
    try {
        size_t idx;
        int val = std::stoi(argStr, &idx, 0); // Base 0 = auto-detect
        if (idx == argStr.length()) {
            return ctx.builder.getInt32(val);
        }
    } catch (...) {}

    // 2. Property Access (R-Value only) or Enum Lookup
    size_t dotPos = argStr.rfind('.');
    if (dotPos != std::string::npos) {
         std::string objName = argStr.substr(0, dotPos);
         std::string propName = argStr.substr(dotPos + 1);
         
         // Check for Enum
         if (ctx.enums.count(objName)) {
             auto& enumValues = ctx.enums[objName];
             if (enumValues.count(propName)) {
                 int val = enumValues[propName];
                 return ctx.builder.getInt32(val);
             } else {
                 LOG_ERROR("Enum '" + objName + "' has no value '" + propName + "'");
                 return ctx.builder.getInt32(0);
             }
         }

         if (ctx.symbol_table.count(objName)) {
             Symbol& sym = ctx.symbol_table[objName];
             if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
                 if (propName == "size") {
                     return ListImpl::getSize(ctx, sym.alloca_inst);
                 } else if (propName == "empty") {
                      llvm::Value* isEmpty = ListImpl::isEmpty(ctx, sym.alloca_inst);
                      return ctx.builder.CreateZExt(isEmpty, ctx.builder.getInt32Ty());
                 }
             }
             // MAP: map.size
             else if (sym.type->isStructTy() && sym.type->getStructName().startswith("map_")) {
                  if (propName == "size") {
                      llvm::StructType* mapType = llvm::cast<llvm::StructType>(sym.type);
                      return MapImpl::getSize(ctx, sym.alloca_inst, mapType);
                  }
             }
         }
         
         // Check for Static Class Property (e.g. User.version)
         if (ctx.classes.count(objName)) {
             if (propName == "version") {
                 return ctx.builder.getInt64(ctx.classes[objName].version);
             }
         }
    }

    // 3. Variable/LValue lookup
    llvm::Value* lvalue = resolveLValue(ctx, argStr, evaluator);
    if (lvalue) {
        // Load the value from the address
        if (ctx.symbol_table.count(argStr)) {
             return ctx.builder.CreateLoad(ctx.symbol_table[argStr].type, lvalue, argStr + "_val");
        }
        
        // If it was an array access, we need to deduce the type.

        size_t hyphenPos = argStr.rfind('-');
        if (hyphenPos != std::string::npos) {
             std::string varName = argStr.substr(0, hyphenPos);
             if (ctx.symbol_table.count(varName)) {
                 llvm::Type* type = ctx.symbol_table[varName].type;
                 if (type->isArrayTy()) {
                     return ctx.builder.CreateLoad(type->getArrayElementType(), lvalue, "arr_elem_load");
                 } else if (type->isStructTy() && type->getStructName().startswith("list_")) {
                      // List load
                      return ctx.builder.CreateLoad(ctx.symbol_table[varName].element_type, lvalue, "list_elem_load");
                 }
             }
        }
        
        // 4. Dot Notation (Member Load)
        size_t dotPos = argStr.rfind('.');
        if (dotPos != std::string::npos) {
             std::string objName = argStr.substr(0, dotPos);
             std::string memberName = argStr.substr(dotPos + 1);
             
             if (ctx.symbol_table.count(objName)) {
                 Symbol& sym = ctx.symbol_table[objName];
                 std::string className;
                 // Determine Class Name
                 if (sym.element_type && sym.element_type->isStructTy()) {
                      llvm::StructType* st = llvm::cast<llvm::StructType>(sym.element_type);
                      if (st->hasName()) className = st->getName().str();
                 } else if (!sym.className.empty()) {
                      className = sym.className;
                 }
                 
                 if (!className.empty() && ctx.classes.count(className)) {
                     auto& classInfo = ctx.classes[className];
                     if (classInfo.members.count(memberName)) {
                          llvm::Type* memberType = classInfo.members[memberName].type;
                          return ctx.builder.CreateLoad(memberType, lvalue, memberName + "_val");
                     }
                 }
             }
        }
         
         // Fallback/Error
         LOG_ERROR("Could not determine type for load: " + argStr);
         return ctx.builder.getInt32(0);
    }
    
    // 3. String literal fallback with Expansion
    if (argStr.find("${") != std::string::npos) {
        if (evaluator) {
            return StringUtils::expandExpression(ctx, argStr, evaluator);
        } else {
             // Fallback if no evaluator (e.g. system calls? or just error?)
             // SystemCalls should use it too?
             // Use a dummy evaluator that only handles resolveValue for simple variables?
             // Or allow StringUtils to handle simple variables if evaluator throws.
             // We can pass a simple lambda for legacy support.
             // For now, pass nullptr and let StringUtils crash? No.
             // Updated StringUtils to check?
             // StringUtils currently assumes evaluator exists.
             // I'll update StringUtils next to be safe.
             // If no evaluator, we cannot expand expressions that require evaluation (like function calls).
             // However, StringUtils might handle simple variable lookups if we pass a simple resolver?
             // Or we just pass nullptr and let StringUtils fail gracefully or do partial expansion.
             // Given the crash/empty return, let's try to be safer.
             return StringUtils::expandExpression(ctx, argStr, nullptr);
        }
    }
    
    return ctx.builder.CreateGlobalStringPtr(argStr);
}

}
