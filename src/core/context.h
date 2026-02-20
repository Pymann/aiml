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
#include <set>
#include "ir_generator.h"

    enum class AccessLevel { Public, Private, Protected };
    
    struct Symbol {
        // --- Legacy LLVM pointers (being phased out) ---
        llvm::Value* alloca_inst = nullptr; // Address of the variable
        llvm::Type* type = nullptr;         // Type of the variable (ptr if alloca)
        llvm::Type* element_type = nullptr; // Type of the value stored (e.g. i32, Struct, or ptr)
        llvm::Type* key_type = nullptr;     // For maps
        llvm::Type* value_type = nullptr;   // For maps

        // --- New IR Abstraction pointers ---
        AIML::IRValue* ir_alloca_inst = nullptr;
        AIML::IRType* ir_type = nullptr;
        AIML::IRType* ir_element_type = nullptr;
        AIML::IRType* ir_key_type = nullptr;
        AIML::IRType* ir_value_type = nullptr;

        std::string typeName;     // Original AIML type string
        std::string className;    // Name of the class if this is an object or pointer to object
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

        struct CompilerConfig {
            std::string entryPointName = "main";
            size_t stackSize = 1048576;
            size_t heapSize = 16777216;
            std::string target = "";
            std::string version = "1.0";
        };

        CompilerConfig config;

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
            AccessLevel access = AccessLevel::Public; // Default Public
            bool is_const = false;
            bool is_volatile = false;
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
            
            // Inheritance and Composition
            std::string parentName;
            std::vector<std::string> interfaces;
            std::vector<std::string> usedModels;
            std::vector<std::string> objectFilesToLink; // New member for ClassInfo
            
            bool isInterface = false;
            bool isModel = false; 
        };



    CompilerContext(llvm::LLVMContext& ctx, llvm::Module& mod, llvm::IRBuilder<>& b)
        : context(ctx), module(mod), builder(b) {
        // initialized lazily or explicitly later to avoid circular dependency here
    }

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
    std::unique_ptr<AIML::IRGenerator> ir_gen;

    // Exception Handling State
    llvm::BasicBlock* currentExceptionBlock = nullptr;
    AIML::IRBlock* ir_exception_block = nullptr;

    // Helper: Get or declare __gxx_personality_v0
    llvm::FunctionCallee getPersonalityFunction() {
        llvm::FunctionType* persType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context), true // varargs (i32 (...))
        );
        return module.getOrInsertFunction("__gxx_personality_v0", persType);
    }

    // Symbol Table (Scope-aware in future)
    std::map<std::string, Symbol> symbol_table;
    std::map<std::string, FunctionInfo> functions;
    std::map<std::string, ClassInfo> classes; // Class Registry
    std::map<std::string, std::map<std::string, int>> enums; // Enum Registry

    // Namespace & Import State
    std::string currentNamespace;
    std::set<std::string> loadedFiles; // Full paths to loaded files
    std::vector<std::string> objectFilesToLink;
    std::string compilerPath; // Path to the aiml executable
    bool jitMode = false;
    bool headerOnlyMode = false;

    // Helper: Qualify name with current namespace
    std::string qualify(const std::string& name) {
        if (currentNamespace.empty()) return name;
        return currentNamespace + "." + name;
    }

    // Helper: Get LLVM Type from string
    llvm::Type* getLLVMType(const std::string& typeName) {
        if (typeName == "" || typeName == "<.balance" || typeName == "${ { +: [a, b] } }") {
             LOG_ERROR("CRITICAL ERROR: getLLVMType called with invalid typeName: '" + typeName + "'");
             throw std::runtime_error("INVALID TYPE NAME FOUND: " + typeName);
        }
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
        // Try resolving with current namespace (e.g. "Point" -> "my.lib.Point")
        std::string qualified = qualify(typeName);
        if (classes.count(qualified)) {
             return classes[qualified].type;
        }

        // Check for Pointer type: "ptr<T>" or "ref<T>"
        if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) && typeName.back() == '>') {
            std::string innerType;
            if (typeName.rfind("ptr<", 0) == 0) innerType = typeName.substr(4, typeName.length() - 5);
            else innerType = typeName.substr(4, typeName.length() - 5); // ref< is also 4 chars
            
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

                 // Trim spaces
                 keyTypeStr.erase(0, keyTypeStr.find_first_not_of(' '));
                 keyTypeStr.erase(keyTypeStr.find_last_not_of(' ') + 1);
                 valTypeStr.erase(0, valTypeStr.find_first_not_of(' '));
                 valTypeStr.erase(valTypeStr.find_last_not_of(' ') + 1);
                 
                 LOG_DEBUG("KeyType: '" + keyTypeStr + "', ValType: '" + valTypeStr + "'");
                 
                 // Trim spaces? Assuming no spaces for now or strict parsing
                 llvm::Type* keyType = getLLVMType(keyTypeStr);
                 llvm::Type* valType = nullptr;
                 if (valTypeStr == "any") {
                      valType = llvm::Type::getInt8Ty(context)->getPointerTo();
                 } else {
                      valType = getLLVMType(valTypeStr);
                 }
                 
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

        if (typeName == "any") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "i8" || typeName == "i8_least" || typeName == "i8_fast") return llvm::Type::getInt8Ty(context);
        if (typeName == "i16") return llvm::Type::getInt16Ty(context);
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

        if (typeName == "file_handle") return llvm::Type::getInt8Ty(context)->getPointerTo(); // FILE*
        if (typeName == "process") return llvm::Type::getInt8Ty(context)->getPointerTo();     // ProcessHandle*
        if (typeName == "any") return llvm::Type::getInt8Ty(context)->getPointerTo();         // void*

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
