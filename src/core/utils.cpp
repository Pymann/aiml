#include "utils.h"
#include <iostream>

namespace Utils {

// --- internal helpers ---

static llvm::Value* resolveLValueMemberAccess(CompilerContext& ctx, const std::string& objName, const std::string& memberName, bool forWrite) {
    if (ctx.symbol_table.count(objName) == 0) return nullptr;
    
    Symbol& sym = ctx.symbol_table[objName];
    if (sym.type->isStructTy() && sym.is_const && forWrite) {
         LOG_ERROR("Cannot assign to member " + memberName + " of const variable " + objName);
         return nullptr;
    }

    llvm::Value* basePtr = sym.alloca_inst; 
    std::string className;
    bool isPointer = false;
    if (!sym.className.empty()) {
        className = sym.className;
        isPointer = sym.type->isPointerTy();
    } else if (sym.type->isStructTy()) {
         llvm::StructType* st = llvm::cast<llvm::StructType>(sym.type);
         if (st->hasName()) {
             className = st->getName().str();
             size_t dotPos = className.rfind('.');
             if (dotPos != std::string::npos && className.find_first_not_of("0123456789", dotPos + 1) == std::string::npos) {
                 className = className.substr(0, dotPos);
             }
         }
    }

    if (!className.empty() && ctx.classes.count(className)) {
        auto& classInfo = ctx.classes[className];
        if (classInfo.members.count(memberName)) {
             auto& member = classInfo.members[memberName];
             int index = member.index;
             
             if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
                 LOG_ERROR("Cannot access private member " + memberName);
                 return nullptr;
             } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                 LOG_ERROR("Cannot access protected member " + memberName);
                 return nullptr;
             }

             if (forWrite && member.is_const) {
                  LOG_ERROR("Cannot assign to const member " + memberName);
                  return nullptr; 
             }
             
             llvm::Value* structPtr = basePtr;
             if (isPointer) {
                 if (sym.type->isPointerTy() && sym.element_type == nullptr) {
                     // Exception payload proxy case
                     structPtr = ctx.builder.CreateBitCast(basePtr, llvm::PointerType::getUnqual(classInfo.type), objName + "_cast");
                 } else {
                     structPtr = ctx.builder.CreateLoad(ctx.getLLVMType("ptr<" + className + ">"), basePtr, objName + "_ptr");
                 }
             }
             return ctx.builder.CreateStructGEP(classInfo.type, structPtr, index, "member_addr");
        }
        LOG_ERROR("Member '" + memberName + "' not found in class '" + className + "' (total members: " + std::to_string(classInfo.members.size()) + ")");
        return nullptr;
    }
    LOG_ERROR("Cannot access member '" + memberName + "' on '" + objName + "' (sym.className='" + sym.className + "', resolved className='" + className + "')");
    return nullptr;
}

static llvm::Value* resolveLValueArrayAccess(CompilerContext& ctx, const std::string& varName, const std::string& indexStr, CompilerContext::Evaluator evaluator, bool forWrite) {
    if (ctx.symbol_table.count(varName) == 0) return nullptr;
    Symbol& sym = ctx.symbol_table[varName];
    
    if (forWrite && sym.is_const) {
         LOG_ERROR("Cannot modify element of const array/list");
         return nullptr;
    }
    
    size_t colonPos = indexStr.find(':');
    if (colonPos != std::string::npos) {
        if (forWrite) { LOG_ERROR("Slice assignment is not yet supported."); return nullptr; }
        
        std::string startStr = indexStr.substr(0, colonPos);
        std::string endStr = indexStr.substr(colonPos + 1);
        
        llvm::Value* startVal = startStr.empty() ? ctx.builder.getInt32(0) : resolveValue(ctx, YAML::Node(startStr), evaluator);
        llvm::Value* endVal = nullptr;
        
        if (endStr.empty()) {
            if (sym.type->isArrayTy()) endVal = ctx.builder.getInt32(sym.type->getArrayNumElements());
            else if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) endVal = ListImpl::getSize(ctx, sym.alloca_inst);
        } else {
            endVal = resolveValue(ctx, YAML::Node(endStr), evaluator);
        }
        
        if (sym.type->isArrayTy()) {
             LOG_ERROR("Slicing not supported for fixed arrays yet.");
             return nullptr;
        } else if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
             return ListImpl::slice(ctx, sym.alloca_inst, startVal, endVal, sym.element_type);
        }
    }
     
    if (sym.type->isArrayTy()) {
        llvm::Value* indexVal = resolveValue(ctx, YAML::Node(indexStr), evaluator); 
        std::vector<llvm::Value*> indices = {ctx.builder.getInt32(0), indexVal};
        return ctx.builder.CreateInBoundsGEP(sym.type, sym.alloca_inst, indices, varName + "_idx_" + indexStr);
    } else if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
         llvm::Value* indexVal = resolveValue(ctx, YAML::Node(indexStr), evaluator);
         return ListImpl::getElementPtr(ctx, sym.alloca_inst, indexVal, sym.element_type);
    }
    return nullptr;
}


llvm::Value* resolveLValue(CompilerContext& ctx, const std::string& argStr, CompilerContext::Evaluator evaluator, bool forWrite) {
    // 1. Member Access
    size_t dotPos = argStr.rfind('.');
    if (dotPos != std::string::npos) {
        return resolveLValueMemberAccess(ctx, argStr.substr(0, dotPos), argStr.substr(dotPos + 1), forWrite);
    }

    // 2. Array Access
    size_t hyphenPos = argStr.rfind('-'); 
    if (hyphenPos != std::string::npos) {
        return resolveLValueArrayAccess(ctx, argStr.substr(0, hyphenPos), argStr.substr(hyphenPos + 1), evaluator, forWrite);
    }
    
    // 3. Simple variable
    if (ctx.symbol_table.count(argStr)) {
        Symbol& sym = ctx.symbol_table[argStr];
        if (forWrite && sym.is_const) {
             LOG_ERROR("Cannot assign to const variable '" + argStr + "'");
             return nullptr;
        }
        return sym.alloca_inst;
    }
    return nullptr;
}

static llvm::Value* resolvePropertyOrEnum(CompilerContext& ctx, const std::string& objName, const std::string& propName) {
     if (ctx.enums.count(objName)) {
         auto& enumValues = ctx.enums[objName];
         if (enumValues.count(propName)) return ctx.builder.getInt32(enumValues[propName]);
         LOG_ERROR("Enum '" + objName + "' has no value '" + propName + "'");
         return ctx.builder.getInt32(0);
     }

     if (ctx.symbol_table.count(objName)) {
         Symbol& sym = ctx.symbol_table[objName];
         if (sym.type->isStructTy() && sym.type->getStructName().startswith("list_")) {
             if (propName == "len") return ListImpl::getSize(ctx, sym.alloca_inst);
             else if (propName == "empty") return ctx.builder.CreateZExt(ListImpl::isEmpty(ctx, sym.alloca_inst), ctx.builder.getInt32Ty());
         }
         else if (sym.type->isStructTy() && sym.type->getStructName().startswith("map_")) {
              if (propName == "len") return MapImpl::getSize(ctx, sym.alloca_inst, llvm::cast<llvm::StructType>(sym.type));
         }
     }
     
     if (ctx.classes.count(objName) && propName == "version") {
         return ctx.builder.getInt64(ctx.classes[objName].version);
     }
     return nullptr;
}

static llvm::Value* resolveLValueLoad(CompilerContext& ctx, const std::string& argStr, llvm::Value* lvalue) {
    if (ctx.symbol_table.count(argStr)) {
         llvm::LoadInst* load = ctx.builder.CreateLoad(ctx.symbol_table[argStr].type, lvalue, argStr + "_val");
         if (ctx.symbol_table[argStr].is_volatile) load->setVolatile(true);
         return load;
    }
    
    size_t hyphenPos = argStr.rfind('-');
    if (hyphenPos != std::string::npos) {
         std::string varName = argStr.substr(0, hyphenPos);
         if (ctx.symbol_table.count(varName)) {
             llvm::Type* type = ctx.symbol_table[varName].type;
             if (type->isArrayTy()) {
                 llvm::LoadInst* load = ctx.builder.CreateLoad(type->getArrayElementType(), lvalue, "arr_elem_load");
                 if (ctx.symbol_table[varName].is_volatile) load->setVolatile(true);
                 return load;
             } else if (type->isStructTy() && type->getStructName().startswith("list_")) {
                  llvm::LoadInst* load = ctx.builder.CreateLoad(ctx.symbol_table[varName].element_type, lvalue, "list_elem_load");
                  if (ctx.symbol_table[varName].is_volatile) load->setVolatile(true);
                  return load;
             }
         }
    }
    
    size_t dotPos = argStr.rfind('.');
    if (dotPos != std::string::npos) {
         std::string objName = argStr.substr(0, dotPos);
         std::string memberName = argStr.substr(dotPos + 1);
         
         if (ctx.symbol_table.count(objName)) {
             Symbol& sym = ctx.symbol_table[objName];
             std::string className;
             if (!sym.className.empty()) {
                 className = sym.className;
             } else if (sym.element_type && sym.element_type->isStructTy()) {
                 className = sym.element_type->getStructName().str();
                 size_t dotPos = className.rfind('.');
                 if (dotPos != std::string::npos && className.find_first_not_of("0123456789", dotPos + 1) == std::string::npos) {
                     className = className.substr(0, dotPos);
                 }
             }
             
             if (!className.empty() && ctx.classes.count(className)) {
                 auto& classInfo = ctx.classes[className];
                 if (classInfo.members.count(memberName)) {
                      llvm::LoadInst* load = ctx.builder.CreateLoad(classInfo.members[memberName].type, lvalue, memberName + "_val");
                      if (classInfo.members[memberName].is_volatile) load->setVolatile(true);
                      return load;
                 }
             }
         }
    }
    return nullptr;
}


llvm::Value* resolveValue(CompilerContext& ctx, const YAML::Node& argNode, CompilerContext::Evaluator evaluator) {
    if (argNode.IsMap()) {
         if (evaluator) return evaluator(ctx, argNode);
         LOG_ERROR("Cannot evaluate map without evaluator");
         return ctx.builder.getInt32(0);
    }

    std::string argStr = argNode.as<std::string>();
    
    // 1. Literal Integer
    try {
        size_t idx;
        int val = std::stoi(argStr, &idx, 0);
        if (idx == argStr.length()) return ctx.builder.getInt32(val);
    } catch (...) {}

    // 2. Property Access or Enum Reference
    size_t dotPos = argStr.rfind('.');
    if (dotPos != std::string::npos) {
         llvm::Value* prop = resolvePropertyOrEnum(ctx, argStr.substr(0, dotPos), argStr.substr(dotPos + 1));
         if (prop) return prop;
    }

    // 3. LValue Load
    llvm::Value* lvalue = resolveLValue(ctx, argStr, evaluator);
    if (!lvalue) {
        if (ctx.symbol_table.count(argStr) || argStr.find('.') != std::string::npos || argStr.find('-') != std::string::npos) {
             // It's a known identifier or complex expression but resolveLValue failed (likely access violation)
             return ctx.builder.getInt32(0);
        }
    }
    else {
        llvm::Value* loaded = resolveLValueLoad(ctx, argStr, lvalue);
        if (loaded) return loaded;
        LOG_ERROR("Could not determine type for load: " + argStr);
        return ctx.builder.getInt32(0);
    }
    
    // 4. String Literal Expansion
    if (argStr.find("${") != std::string::npos) {
        return StringUtils::expandExpression(ctx, argStr, evaluator); // evaluator or nullptr handled nicely now
    }
    
    return ctx.builder.CreateGlobalStringPtr(argStr);
}

} // namespace Utils
