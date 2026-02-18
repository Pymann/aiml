#pragma once
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <yaml-cpp/yaml.h>
#include "../../compiler.h" // For Symbol struct

    enum class AccessLevel { Public, Private, Protected };
    
    struct Symbol {
        llvm::Value* alloca_inst; // Address of the variable
        llvm::Type* type;         // Type of the variable (ptr if alloca)
        llvm::Type* element_type; // Type of the value stored (e.g. i32, Struct, or ptr)
        std::string className;    // Name of the class if this is an object or pointer to object
        llvm::Type* key_type = nullptr;     // For maps
        llvm::Type* value_type = nullptr;   // For maps
        bool is_const = false;
        bool is_volatile = false;
        AccessLevel access = AccessLevel::Public; // Default for vars
    };

    class CompilerContext {
    public:
        using Evaluator = std::function<llvm::Value*(CompilerContext&, const YAML::Node&)>;
        using CompilerCallback = std::function<void(CompilerContext&, const YAML::Node&)>;

        struct Params {
             CompilerCallback compileCallback;
        } params;

        // Compile Context State
        std::string currentClassName;

        struct FunctionInfo {
            llvm::Function* func;
            std::vector<std::string> argNames;
            AccessLevel access = AccessLevel::Public;
            uint64_t version = 0; // Version of the function, default 0
        };

        struct StructMember {
            int index;
            llvm::Type* type;
            std::string typeName;
            AccessLevel access = AccessLevel::Private; // Default Private
            bool is_const = false;
            uint64_t version = 1; // Version of the field (u64)
            // storing init value: simple literal or expression node?
            // For simplicity, let's store the raw YAML node if provided, so we can evaluate it during 'new'
            YAML::Node initValue; 
        };

        struct ClassInfo {
            llvm::StructType* type;
            std::string name;
            uint64_t version = 1; // Version of the class (u64)
            std::map<std::string, StructMember> members;
            std::map<std::string, FunctionInfo> methods;
        };

    CompilerContext(llvm::LLVMContext& ctx, llvm::Module& mod, llvm::IRBuilder<>& b)
        : context(ctx), module(mod), builder(b) {}

    // Original constructor was:
    // CompilerContext(llvm::LLVMContext& ctx, llvm::Module& mod, llvm::IRBuilder<>& b)
    //     : context(ctx), module(mod), builder(b) {}
    // The instruction implies a change to direct members and a default constructor.
    // However, to maintain functionality with the existing constructor, we'll add the new members.
    // If the intent was to replace the constructor, that would be a larger change.
    // For now, we'll assume the new members are added alongside the existing structure.

    llvm::LLVMContext& context;
    llvm::Module& module;
    llvm::IRBuilder<>& builder;

    // Symbol Table (Scope-aware in future)
    std::map<std::string, Symbol> symbol_table;
    std::map<std::string, FunctionInfo> functions;
    std::map<std::string, ClassInfo> classes; // Class Registry
    std::map<std::string, std::map<std::string, int>> enums; // Enum Registry

    // Helper: Get LLVM Type from string
    llvm::Type* getLLVMType(const std::string& typeName) {
        LOG_DEBUG("getLLVMType: '" + typeName + "'");

        // Check for Enums (Trace back to i32)
        if (enums.count(typeName)) {
             return llvm::Type::getInt32Ty(context);
        }

        // Check for List type: "list<T>"
        if (typeName.rfind("list<", 0) == 0 && typeName.back() == '>') {
            std::string innerType = typeName.substr(5, typeName.length() - 6);
            llvm::Type* elemType = getLLVMType(innerType);
            
            if (!elemType) return nullptr;

            std::string structName = "list_" + innerType; // Simple naming for now
            llvm::StructType* listType = llvm::StructType::getTypeByName(context, structName);
            
            if (!listType) {
                // Define struct { i32 size, i32 capacity, T* data }
                std::vector<llvm::Type*> members;
                members.push_back(llvm::Type::getInt32Ty(context)); // size
                members.push_back(llvm::Type::getInt32Ty(context)); // capacity
                members.push_back(llvm::PointerType::get(context, 0)); // Opaque pointer to data array
                
                listType = llvm::StructType::create(context, members, structName);
            }
            return listType;
        }

        // Check for User-Defined Classes
        if (classes.count(typeName)) {
             return classes[typeName].type;
        }

        // Check for Pointer type: "ptr<T>"
        if (typeName.rfind("ptr<", 0) == 0 && typeName.back() == '>') {
            std::string innerType = typeName.substr(4, typeName.length() - 5);
            llvm::Type* elemType = getLLVMType(innerType);
            if (!elemType) return nullptr;
            return llvm::PointerType::get(context, 0); // Opaque pointer
        }

        // Check for Map type: "map<K:V>"
        if (typeName.rfind("map<", 0) == 0 && typeName.back() == '>') {
             LOG_DEBUG("Parsing Map Type: '" + typeName + "'");
             // Split K and V
             std::string content = typeName.substr(4, typeName.length() - 5);
             size_t sepPos = content.find(':');
             
             if (sepPos != std::string::npos) {
                 std::string keyTypeStr = content.substr(0, sepPos);
                 std::string valTypeStr = content.substr(sepPos + 1);
                 
                 LOG_DEBUG("KeyType: '" + keyTypeStr + "', ValType: '" + valTypeStr + "'");
                 
                 // Trim spaces? Assuming no spaces for now or strict parsing
                 llvm::Type* keyType = getLLVMType(keyTypeStr);
                 llvm::Type* valType = getLLVMType(valTypeStr);
                 
                 if (!keyType || !valType) { 
                     LOG_ERROR("Failed to resolve key/val types for map");
                     return nullptr; 
                 }
                 
                  std::string structName = "map_" + keyTypeStr + "_" + valTypeStr;
                  llvm::StructType* mapType = llvm::StructType::getTypeByName(context, structName);
                  
                  if (!mapType) {
                      // Struct: { i32 size, i32 capacity, i32* occupied, K* keys, V* values }
                      std::vector<llvm::Type*> members;
                      members.push_back(llvm::Type::getInt32Ty(context)); // size
                      members.push_back(llvm::Type::getInt32Ty(context)); // capacity
                      members.push_back(llvm::PointerType::get(context, 0)); // occupied (i32*)
                      members.push_back(llvm::PointerType::get(context, 0)); // keys (K*)
                      members.push_back(llvm::PointerType::get(context, 0)); // values (V*)
                      
                      mapType = llvm::StructType::create(context, members, structName);
                  }
                  return mapType;
             } else {
                 LOG_ERROR("Separator ':' not found in map type content: " + content);
             }
        }

        // Check for array type: "type-size"
        size_t hyphenPos = typeName.rfind('-'); 
        
        if (hyphenPos != std::string::npos) {
            std::string baseTypeStr = typeName.substr(0, hyphenPos);
            std::string sizeStr = typeName.substr(hyphenPos + 1);
            
            // Check if sizeStr is numeric
            if (!sizeStr.empty() && std::all_of(sizeStr.begin(), sizeStr.end(), ::isdigit)) {
                llvm::Type* baseType = getLLVMType(baseTypeStr);
                uint64_t size = std::stoull(sizeStr);
                
                return llvm::ArrayType::get(baseType, size);
            }
        }

        if (typeName == "i32") return llvm::Type::getInt32Ty(context);
        if (typeName == "i64") return llvm::Type::getInt64Ty(context);
        if (typeName == "f32") return llvm::Type::getFloatTy(context);
        if (typeName == "f64") return llvm::Type::getDoubleTy(context);
        if (typeName == "bool") return llvm::Type::getInt8Ty(context);
        if (typeName == "void") return llvm::Type::getVoidTy(context);
        if (typeName == "string") return llvm::Type::getInt8Ty(context)->getPointerTo(); // char*
        
        // Unsigned aliases (LLVM types are sign-agnostic)
        if (typeName == "u8") return llvm::Type::getInt8Ty(context);
        if (typeName == "u16") return llvm::Type::getInt16Ty(context);
        if (typeName == "u32") return llvm::Type::getInt32Ty(context);
        if (typeName == "u64") return llvm::Type::getInt64Ty(context);
        
        if (typeName == "buffer") {
            llvm::StructType* bufferType = llvm::StructType::getTypeByName(context, "buffer");
            if (!bufferType) {
                 // Struct: { i32 size, i32 capacity, i32 readIndex, i32 writeIndex, i8* data }
                 std::vector<llvm::Type*> members;
                 members.push_back(llvm::Type::getInt32Ty(context)); // size (used bytes)
                 members.push_back(llvm::Type::getInt32Ty(context)); // capacity (alloc bytes)
                 members.push_back(llvm::Type::getInt32Ty(context)); // readIndex
                 members.push_back(llvm::Type::getInt32Ty(context)); // writeIndex
                 members.push_back(llvm::Type::getInt8Ty(context)->getPointerTo()); // data
                 
                 bufferType = llvm::StructType::create(context, members, "buffer");
            }
            return bufferType;
        }

        LOG_ERROR("Unknown Type: '" + typeName + "' -> defaulting to i32");
        return llvm::Type::getInt32Ty(context); // Default
    }

    // Helper to declare external C functions
    llvm::Function* getFunction(const std::string& name, llvm::Type* retTy, const std::vector<llvm::Type*>& args, bool varArgs = false) {
        llvm::Function* func = module.getFunction(name);
        if (!func) {
            llvm::FunctionType* funcTy = llvm::FunctionType::get(retTy, args, varArgs);
            func = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage, name, module);
        }
        return func;
    }
};
