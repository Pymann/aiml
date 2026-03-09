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

namespace AIML {
    namespace AST {
        class Expr;
        class CallExpr;
    }
}

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
            llvm::Function* func = nullptr;
            std::vector<std::string> argNames;
            AccessLevel access = AccessLevel::Public;
            uint64_t version = 1;             // Latest version (updated by visitTopLevel re-def)
            uint64_t compiled_version = 1;    // Version actually compiled into IR (set once in visitFunction)
            int fptable_index = -1;           // Index in __aiml_fptable / fptable_current (-1 = not in table)
            std::string sourceYaml;           // YAML dump of the function AST node (for sys.bundle)
            std::set<std::string> referencedTypes; // User-defined types referenced in the body
        };

        struct StructMember {
            int index;
            llvm::Type* type;
            std::string typeName;
            AccessLevel access = AccessLevel::Public; // Default Public
            bool is_const = false;
            bool is_volatile = false;
            uint64_t version = 1; // Version of the field (u64)
            std::shared_ptr<AIML::AST::Expr> initValue; 
        };

        // Builtin method handler: receives base alloca (raw ptr), full CallExpr for
        // lazy arg evaluation, and an evaluator callback.
        using MethodHandler = std::function<AIML::IRValue*(
            CompilerContext& ctx,
            llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call,
            std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr)>;

        // Builtin namespace function handler: args are eagerly pre-evaluated.
        using FuncHandler = std::function<AIML::IRValue*(
            CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args)>;

        struct ClassInfo {
            llvm::StructType* type = nullptr;
            std::string name;
            uint64_t version = 1; // Version of the class (u64)
            std::map<std::string, StructMember> members;
            std::map<std::string, FunctionInfo> methods;

            // Inheritance and Composition
            std::string parentName;
            std::vector<std::string> interfaces;
            std::vector<std::string> usedModels;
            std::vector<std::string> objectFilesToLink;

            // Mangled names of compiled methods — used by dfsCollect to include
            // types that appear in method bodies (tracked via bundleTrackingTarget).
            std::vector<std::string> methodNames;

            bool isInterface = false;
            bool isModel = false;
            std::string primaryKey;   // empty = slot-index (i32); else field name + type determines eid type

            // Builtin dispatch (set for intrinsic types like buffer, process)
            bool isBuiltin = false;
            // When true the dispatch layer loads the handle ptr from the alloca before
            // invoking the MethodHandler (so handlers receive the ptr, not the alloca).
            // Existing builtins (socket, ws, process, buffer) load internally → false.
            // HTTP handles set this to true.
            bool preLoadBase = false;
            std::map<std::string, MethodHandler> builtinMethods;

            // Setter handler for property writes on builtin handle types
            // Signature: (ctx, base_i8ptr, new_value) -> void
            using SetterHandler = std::function<void(
                CompilerContext& ctx,
                llvm::Value* base,
                AIML::IRValue* newVal)>;
            std::map<std::string, SetterHandler> builtinSetters;

            // Source preservation (for sys.bundle)
            std::string sourceYaml;                // YAML dump of the class AST node
            std::set<std::string> referencedTypes; // User-defined types in members/methods

            // Hot-Reload: size info for calloc padding
            uint64_t compiled_version = 1; // Version actually compiled into IR (set once, never updated by re-def)
            int packed_size = 0;   // Actual size of all declared fields (LLVM DataLayout)
            int padded_size = 0;   // Next power-of-2 of packed_size (allocation size for calloc)

            // Mirror: sync/rpc annotations (from model: sync:[...] rpc:[...])
            std::vector<std::string> syncFields;
            std::vector<std::string> rpcMethods;
        };

        class Scope {
        public:
            std::map<std::string, Symbol> variables;
            Scope* parent = nullptr;
            Scope() = default;
            Scope(Scope* p) : parent(p) {}
        };

        struct Namespace {
            std::map<std::string, FunctionInfo> functions;
            std::map<std::string, ClassInfo> classes;
            std::map<std::string, std::map<std::string, int>> enums;
            std::map<std::string, Symbol> globals;
            // Builtin function dispatch (sys, io, time)
            std::map<std::string, FuncHandler> builtinFuncs;
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
    llvm::BasicBlock* catchBlock = nullptr;
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
    std::map<std::string, std::string> enumSources;          // enum name → YAML source (for sys.bundle)
    std::map<std::string, std::string> interfaceSources;     // interface name → YAML source (for sys.bundle)

    // Namespace & Import State
    std::string currentNamespace;
    std::map<std::string, Namespace> namespaces;
    Scope* currentScope = nullptr;
    Namespace* currentNs = nullptr;
    
    std::set<std::string> loadedFiles; // Full paths to loaded files
    std::vector<std::string> objectFilesToLink;
    std::string compilerPath; // Path to the aiml executable
    std::function<void(const std::string&)> resolveImportCallback;
    
    bool jitMode = false;
    bool headerOnlyMode = false;

    // Hot-Reload: next free fptable slot index (incremented in visitFunction)
    int fptable_next_index = 0;

    // When non-empty, name of the function/class currently being compiled.
    // getLLVMType uses this to record transitive type dependencies for sys.bundle.
    std::string bundleTrackingTarget;

    // When non-empty, the type name of the current set-statement target variable.
    // Used by sys.deserialize to infer the type without an explicit second argument.
    std::string deserializeTargetType;

    // Set by visitCall before dispatching db namespace functions when a
    // DictLiteralExpr option bag is present (e.g. {persist: true}, {layout: "column"}).
    bool        dbOpenPersist      = false;
    bool        dbOpenColumnLayout = false;
    llvm::Value* dbOpenBasePathVal = nullptr;  // null = default "./aiondb/"

    // Set by visitCall for db.query option bags:
    // {where: fn_name}, {order_by: "field"}, {dir: "asc"/"desc"}, {limit: N}, {offset: N}
    std::string dbQueryStoreType;   // type of the first arg (e.g. "Store<Score>") for order_by field lookup
    std::string dbQueryWhere;       // predicate function name (empty = no filter)
    std::string dbQueryOrderBy;     // field name (empty = no sort)
    int32_t     dbQueryOrderDir = 0; // 0 = asc, 1 = desc
    int32_t     dbQueryLimit    = -1; // -1 = no limit
    int32_t     dbQueryOffset   = 0;

    // Recursively collects all user-defined type names from a type expression.
    // Handles list<T>, map<K,V> (and nesting thereof).  Only names found in
    // classes (non-builtin), enumSources, or interfaceSources are emitted.
    void collectUserTypes(const std::string& typeName, std::set<std::string>& out) const {
        if (typeName.empty()) return;
        // list<T>
        if (typeName.size() > 5 && typeName.rfind("list<", 0) == 0 && typeName.back() == '>') {
            collectUserTypes(typeName.substr(5, typeName.size() - 6), out);
            return;
        }
        // map<K:V>  (AIML uses colon as separator)
        if (typeName.size() > 4 && typeName.rfind("map<", 0) == 0 && typeName.back() == '>') {
            std::string inner = typeName.substr(4, typeName.size() - 5);
            size_t sep = inner.find(':');
            if (sep != std::string::npos) {
                collectUserTypes(inner.substr(0, sep), out);
                collectUserTypes(inner.substr(sep + 1), out);
            }
            return;
        }
        // Direct type — emit only if user-defined
        if (classes.count(typeName) && !classes.at(typeName).isBuiltin)
            out.insert(typeName);
        else if (enumSources.count(typeName))
            out.insert(typeName);
        else if (interfaceSources.count(typeName))
            out.insert(typeName);
    }

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

        // Check for User-Defined Classes (skip builtins — they have their own
        // type definition further below, even if type != nullptr for GEP purposes)
        if (classes.count(typeName) && classes[typeName].type != nullptr && !classes[typeName].isBuiltin) {
            if (!bundleTrackingTarget.empty() && functions.count(bundleTrackingTarget))
                functions[bundleTrackingTarget].referencedTypes.insert(typeName);
            return classes[typeName].type;
        }
        // Try resolving with current namespace (e.g. "Point" -> "my.lib.Point")
        std::string qualified = qualify(typeName);
        if (classes.count(qualified) && classes[qualified].type != nullptr) {
            if (!bundleTrackingTarget.empty() && functions.count(bundleTrackingTarget))
                functions[bundleTrackingTarget].referencedTypes.insert(qualified);
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

        if (typeName.rfind("ptr<", 0) == 0 && typeName.back() == '>') {
            return getLLVMType(typeName.substr(4, typeName.length() - 5))->getPointerTo();
        }
        if (typeName.rfind("ref<", 0) == 0 && typeName.back() == '>') {
            return getLLVMType(typeName.substr(4, typeName.length() - 5))->getPointerTo();
        }
        if (typeName == "ptr") return llvm::Type::getInt8Ty(context)->getPointerTo();
        
        if (typeName == "any") return llvm::Type::getInt8Ty(context)->getPointerTo();

        // ── Math value types ──────────────────────────────────────────────
        // These are real structs on the stack (not i8* pointers).
        // The LLVM StructType is created once and cached by name.
        {
            auto makeMathF4 = [&](const char* nm) -> llvm::Type* {
                llvm::StructType* t = llvm::StructType::getTypeByName(context, nm);
                if (!t) t = llvm::StructType::create(context,
                    {llvm::Type::getFloatTy(context), llvm::Type::getFloatTy(context),
                     llvm::Type::getFloatTy(context), llvm::Type::getFloatTy(context)}, nm);
                return t;
            };
            if (typeName == "vec2") {
                llvm::StructType* t = llvm::StructType::getTypeByName(context, "vec2");
                if (!t) t = llvm::StructType::create(context,
                    {llvm::Type::getFloatTy(context), llvm::Type::getFloatTy(context)}, "vec2");
                return t;
            }
            if (typeName == "vec3") return makeMathF4("vec3");
            if (typeName == "vec4") return makeMathF4("vec4");
            if (typeName == "quat") return makeMathF4("quat");
            if (typeName == "mat4") {
                llvm::StructType* t = llvm::StructType::getTypeByName(context, "mat4");
                if (!t) t = llvm::StructType::create(context,
                    {llvm::ArrayType::get(llvm::Type::getFloatTy(context), 16)}, "mat4");
                return t;
            }
            if (typeName == "ivec2") {
                llvm::StructType* t = llvm::StructType::getTypeByName(context, "ivec2");
                if (!t) t = llvm::StructType::create(context,
                    {llvm::Type::getInt32Ty(context), llvm::Type::getInt32Ty(context)}, "ivec2");
                return t;
            }
            if (typeName == "ivec3") {
                llvm::StructType* t = llvm::StructType::getTypeByName(context, "ivec3");
                if (!t) t = llvm::StructType::create(context,
                    {llvm::Type::getInt32Ty(context), llvm::Type::getInt32Ty(context),
                     llvm::Type::getInt32Ty(context), llvm::Type::getInt32Ty(context)}, "ivec3");
                return t;
            }
        }

        if (typeName == "i8" || typeName == "i8_least" || typeName == "i8_fast") return llvm::Type::getInt8Ty(context);
        if (typeName == "i16") return llvm::Type::getInt16Ty(context);
        if (typeName == "i32") return llvm::Type::getInt32Ty(context);
        if (typeName == "i64") return llvm::Type::getInt64Ty(context);
        if (typeName == "f32") return llvm::Type::getFloatTy(context);
        if (typeName == "f64") return llvm::Type::getDoubleTy(context);
        if (typeName == "bool") return llvm::Type::getInt8Ty(context);
        if (typeName == "void") return llvm::Type::getVoidTy(context);
        if (typeName == "string") return llvm::Type::getInt8Ty(context)->getPointerTo(); // char*
        if (typeName == "path")   return llvm::Type::getInt8Ty(context)->getPointerTo(); // interned char*

        // HTTP handle types — all opaque i8* pointers to heap-allocated C structs
        // (struct type for GEP is stored in ClassInfo.type, retrieved via ctx.classes)
        if (typeName == "url")                  return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "url_credential")       return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_server_handle")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_route_handle")    return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_request_handle")  return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_response_handle") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_client_handle")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "http_session_handle")  return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "fs_options")           return llvm::Type::getInt8Ty(context)->getPointerTo();
        
        // Unsigned aliases (LLVM types are sign-agnostic)
        if (typeName == "u8") return llvm::Type::getInt8Ty(context);
        if (typeName == "u16") return llvm::Type::getInt16Ty(context);
        if (typeName == "u32") return llvm::Type::getInt32Ty(context);
        if (typeName == "u64") return llvm::Type::getInt64Ty(context);
        
        // fn<params:ret> — function pointer stored as opaque i8*
        if (typeName.rfind("fn<", 0) == 0 && typeName.back() == '>')
            return llvm::Type::getInt8Ty(context)->getPointerTo();

        // buffer<T> — template param is ignored, all buffers share the same struct layout
        if (typeName.rfind("buffer<", 0) == 0 && typeName.back() == '>') {
            return getLLVMType("buffer");
        }
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
        if (typeName == "socket_handle") return llvm::Type::getInt8Ty(context)->getPointerTo(); // AimlSocket*
        if (typeName == "tls_socket_handle") return llvm::Type::getInt8Ty(context)->getPointerTo(); // AimlTlsSocket*
        if (typeName == "ws_handle") return llvm::Type::getInt8Ty(context)->getPointerTo();          // AimlWs*
        if (typeName == "any") return llvm::Type::getInt8Ty(context)->getPointerTo();         // void*

        // gltf handle types — all opaque i8* pointers
        if (typeName == "gltf_scene")     return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_node")      return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_mesh")      return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_primitive") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_material")  return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_animation") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_skin")      return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_texture")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gltf_image")     return llvm::Type::getInt8Ty(context)->getPointerTo();

        // gl handle types — all opaque i8* pointers
        if (typeName == "gl_buffer")       return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_vao")          return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_shader")       return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_texture")      return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_framebuffer")  return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_renderbuffer") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "gl_mesh")         return llvm::Type::getInt8Ty(context)->getPointerTo();

        // engine / input handle types
        if (typeName == "window_handle")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "input_event")     return llvm::Type::getInt8Ty(context)->getPointerTo();

        // audio handle types — all opaque i8* pointers
        if (typeName == "audio_sound") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "audio_group") return llvm::Type::getInt8Ty(context)->getPointerTo();

        // mirror handle types
        if (typeName == "mirror_session") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "mirror_peer")    return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "mirror_event") {
            // Real struct for field access: {i8*(type), i8*(peer), i32(net_id)}
            llvm::StructType* t = llvm::StructType::getTypeByName(context, "mirror_event");
            if (!t) {
                t = llvm::StructType::create(context,
                    {llvm::Type::getInt8Ty(context)->getPointerTo(),
                     llvm::Type::getInt8Ty(context)->getPointerTo(),
                     llvm::Type::getInt32Ty(context)},
                    "mirror_event");
            }
            return t;
        }
        if (typeName == "rpc_event") {
            // Real struct: {i8*(method), i32(net_id), i32(arg)}
            llvm::StructType* t = llvm::StructType::getTypeByName(context, "rpc_event");
            if (!t) {
                t = llvm::StructType::create(context,
                    {llvm::Type::getInt8Ty(context)->getPointerTo(),
                     llvm::Type::getInt32Ty(context),
                     llvm::Type::getInt32Ty(context)},
                    "rpc_event");
            }
            return t;
        }

        // ui types
        if (typeName == "ui_font") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_slider_style")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_button_style")   return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_label_style")    return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_progress_style") return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_panel_style")    return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "ui_style") {
            // AimlUIStyle: {i32×5, f32×6, f32(padding_x), i8*, f32×3, f32(_pad2)} — 17 fields, 72 bytes
            auto* i8p  = llvm::Type::getInt8Ty(context)->getPointerTo();
            auto* i32t = llvm::Type::getInt32Ty(context);
            auto* f32t = llvm::Type::getFloatTy(context);
            llvm::StructType* t = llvm::StructType::getTypeByName(context, "ui_style");
            if (!t) t = llvm::StructType::create(context,
                {i32t, i32t, i32t, i32t, i32t,         /* color..border_color  [0-4] */
                 f32t, f32t, f32t, f32t, f32t, f32t,    /* font_size..opacity   [5-10] */
                 f32t,                                   /* padding_x            [11] */
                 i8p,                                    /* font                 [12] */
                 f32t, f32t, f32t,                       /* padding_y,size_w,size_h [13-15] */
                 f32t},                                  /* _pad2                [16] */
                "ui_style");
            return t;
        }

        // thread_handle: pointer to { i64 tid, i8 joined, i8 detached }
        if (typeName == "thread_handle") {
            llvm::StructType* handleTy = llvm::StructType::getTypeByName(context, "thread_handle");
            if (!handleTy) {
                handleTy = llvm::StructType::create(context,
                    {llvm::Type::getInt64Ty(context),
                     llvm::Type::getInt8Ty(context),
                     llvm::Type::getInt8Ty(context)},
                    "thread_handle");
            }
            return handleTy->getPointerTo();
        }

        // lock: pointer to heap-allocated pthread_mutex_t (opaque i8*)
        if (typeName == "lock") return llvm::Type::getInt8Ty(context)->getPointerTo();

        // chan<T>: pointer to heap-allocated channel struct (opaque i8*)
        if (typeName.rfind("chan<", 0) == 0 && typeName.back() == '>')
            return llvm::Type::getInt8Ty(context)->getPointerTo();

        // db types: all opaque i8* pointers to heap-allocated C++ structs
        if (typeName.rfind("Store<", 0) == 0 && typeName.back() == '>')
            return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName.rfind("Kv<", 0) == 0 && typeName.back() == '>')
            return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName.rfind("TimeSeries<", 0) == 0 && typeName.back() == '>')
            return llvm::Type::getInt8Ty(context)->getPointerTo();
        if (typeName == "VecStore")
            return llvm::Type::getInt8Ty(context)->getPointerTo();

        // Code: pointer to heap-allocated __aiml_code_t (opaque i8*)
        if (typeName == "Code")
            return llvm::Type::getInt8Ty(context)->getPointerTo();

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
