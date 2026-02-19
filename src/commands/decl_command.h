#include "../core/expression.h"

class DeclCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: decl: [name, type, (optional)init_val]
        auto args = node["decl"];
        std::string varName = args[0].as<std::string>();
        std::string typeName = args[1].as<std::string>();
        
        llvm::Type* varType = ctx.getLLVMType(typeName);
        llvm::AllocaInst* alloca = ctx.builder.CreateAlloca(varType, nullptr, varName);
        
        // Initialization
        if (args.size() > 2) {
            llvm::Value* initVal = Expression::evaluate(ctx, args[2]);
            // TODO: Implicit cast if types mismatch?
            ctx.builder.CreateStore(initVal, alloca);
            LOG_DEBUG("Declared " + varName + " as " + typeName + " (initialized)");
        } else {
             // Default Initialization (Zero or List/Map Init)
             if (varType->isStructTy()) {
                 llvm::StringRef structName = varType->getStructName();
                 
                 if (structName.startswith("list_")) {
                     // List Init
                     llvm::Type* elemType = nullptr;
                     if (typeName.rfind("list<", 0) == 0) {
                          std::string inner = typeName.substr(5, typeName.length()-6);
                          elemType = ctx.getLLVMType(inner);
                     }
                     
                     if (elemType) {
                         ListImpl::initList(ctx, alloca, elemType);
                         LOG_DEBUG("Declared " + varName + " as " + typeName + " (list initialized)");
                     } else {
                         ctx.builder.CreateStore(llvm::Constant::getNullValue(varType), alloca);
                     }
                 } else if (structName.startswith("map_")) {
                     // Map Init
                     llvm::Type* keyType = nullptr;
                     llvm::Type* valType = nullptr;
                     
                     if (typeName.rfind("map<", 0) == 0) {
                         std::string content = typeName.substr(4, typeName.length() - 5);
                         size_t sepPos = content.find(':');
                         
                         if (sepPos != std::string::npos) {
                             std::string keyTypeStr = content.substr(0, sepPos);
                             std::string valTypeStr = content.substr(sepPos + 1);
                             
                             // Trim spaces
                             keyTypeStr.erase(0, keyTypeStr.find_first_not_of(' '));
                             keyTypeStr.erase(keyTypeStr.find_last_not_of(' ') + 1);
                             valTypeStr.erase(0, valTypeStr.find_first_not_of(' '));
                             valTypeStr.erase(valTypeStr.find_last_not_of(' ') + 1);

                             keyType = ctx.getLLVMType(keyTypeStr);
                             valType = nullptr;
                             if (valTypeStr == "any") {
                                  valType = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
                             } else {
                                  valType = ctx.getLLVMType(valTypeStr);
                             }
                         }
                     }
                     
                     if (keyType && valType) {
                         MapImpl::initMap(ctx, alloca, keyType, valType);
                         LOG_DEBUG("Declared " + varName + " as " + typeName + " (map initialized)");
                     } else {
                         ctx.builder.CreateStore(llvm::Constant::getNullValue(varType), alloca);
                     }
                 } else {
                     // Default Struct Zero Init
                     ctx.builder.CreateStore(llvm::Constant::getNullValue(varType), alloca);
                     
                     // Initialize Members with Default Values
                     if (ctx.classes.count(typeName)) {
                         auto& classInfo = ctx.classes[typeName];
                         for(auto& [memName, member] : classInfo.members) {
                             if (member.initValue.IsDefined() && !member.initValue.IsNull()) {
                                  // Evaluate Init Value
                                  // We need to use Expression::evaluate.
                                  llvm::Value* val = Expression::evaluate(ctx, member.initValue);
                                  if (val) {
                                      // GEP and Store
                                      llvm::Value* memPtr = ctx.builder.CreateStructGEP(classInfo.type, alloca, member.index);
                                      ctx.builder.CreateStore(val, memPtr);
                                      LOG_DEBUG("Initialized member '" + memName + "'");
                                  } else {
                                      LOG_ERROR("Failed to evaluate init value for member '" + memName + "'");
                                  }
                             } else {
                                 LOG_DEBUG("No init value for member '" + memName + "'");
                             }
                         }
                     }
                 }
             } else {
                // Default zero-init for non-struct types
                ctx.builder.CreateStore(llvm::Constant::getNullValue(varType), alloca);
                LOG_DEBUG("Declared " + varName + " as " + typeName + " (zero-initialized)");
             }
        }

        // Store in Symbol Table
        Symbol sym;
        sym.alloca_inst = alloca;
        sym.type = varType; // The actual type of the variable (e.g., i32, {list_i32}, or ptr)
        sym.element_type = varType; // Default to varType, override below if needed
        
        // Determine Class Name if applicable
        if (ctx.classes.count(typeName)) {
            sym.className = typeName;
        } else if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) && typeName.back() == '>') {
             std::string innerType;
             if (typeName.rfind("ptr<", 0) == 0) innerType = typeName.substr(4, typeName.length() - 5);
             else innerType = typeName.substr(4, typeName.length() - 5);
             
             if (ctx.classes.count(innerType)) {
                 sym.className = innerType;
             }
        } else if (typeName == "process") {
             sym.className = "process";
        }
        
        if (typeName.rfind("list<", 0) == 0) {
             std::string inner = typeName.substr(5, typeName.length()-6);
             sym.element_type = ctx.getLLVMType(inner);
        } else if (typeName.rfind("map<", 0) == 0) {
             std::string content = typeName.substr(4, typeName.length() - 5);
             size_t sepPos = content.find(':');
             
             if (sepPos != std::string::npos) {
                 std::string keyTypeStr = content.substr(0, sepPos);
                 std::string valTypeStr = content.substr(sepPos + 1);
                 sym.key_type = ctx.getLLVMType(keyTypeStr);
                 sym.value_type = ctx.getLLVMType(valTypeStr);
             }
        } else if (typeName.rfind("ptr<", 0) == 0) {
             std::string inner = typeName.substr(4, typeName.length()-5);
             sym.element_type = ctx.getLLVMType(inner);
        }
        
        // Parse Modifiers (4th arg)
        if (args.size() > 3 && args[3].IsMap()) {
             auto modifiers = args[3];
             if (modifiers["const"].IsScalar() && modifiers["const"].as<bool>()) sym.is_const = true;
             if (modifiers["volatile"].IsScalar() && modifiers["volatile"].as<bool>()) sym.is_volatile = true;
        }

        ctx.symbol_table[varName] = sym;
    }
};
