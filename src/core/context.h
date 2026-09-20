#pragma once
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/GlobalVariable.h>
#include <map>
#include <string>
#include <cstdlib>   // std::getenv (modelRefcount env-read in ctor)
#include <memory>
#include <vector>
#include <functional>
#include <yaml-cpp/yaml.h>
#include "../../compiler.h" // For Symbol struct
#include <set>
#include "ir_generator.h"
#include "ownership_flow.h"   // spec/61 §4: OwnershipId / OwnershipPlan (LLVM-free)

namespace AIML {
    namespace AST {
        class Expr;
        class CallExpr;
        class GroupMethodsDecl;
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
        bool is_global = false;   // True for module-level GlobalVariable (not a local alloca)
        std::string lifetime = "scope"; // spec/61 ownership: scope|persistent|consumed.
                                        // Metadata for the lifetime pass; drop emission is a later step.
        bool is_byref_arg = false; // True for list<T>/map<K:V> args passed by reference:
                                   // alloca holds a pointer-to-struct instead of the struct itself.
                                   // Method dispatch must dereference once before reaching the list/map.
        bool is_param = false;     // spec/61 §7.1: a function/method parameter — a BORROW by
                                   // default. Storing a ptr/ref param into a member/container is a
                                   // borrow-escape (warned; will become an error under Migration A).
        AIML::AST::OwnershipId ownershipId = AIML::AST::NoOwnership;  // spec/61 §4: ownership-plan slot id
        // spec/61 §6.3: a value-model local declared from a sharing VIEW (decl:[x,Model,{at:..}] /
        // {obj.field} / a container accessor) — its inline list/map heap ALIASES the container
        // element. Cheap for read-only use (no copy at decl), but if it ESCAPES by being returned it
        // must be deep-copied so the caller does not double-free the aliased buffer with the
        // container's own drop (9rooms load_entry_at / creature-cost helpers → drive_ai_combat).
        bool is_view_alias = false;
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
            // spec/61 §3.1: per-parameter ownership qualifier, parallel to argNames.
            // "scope" (borrow, default) | "consumed" | "persistent". Read at the call site
            // to move a `consumed` argument out of the caller (drop the caller's ownership).
            std::vector<std::string> argLifetimes;
            AccessLevel access = AccessLevel::Public;
            uint64_t version = 1;             // Latest version (updated by visitTopLevel re-def)
            uint64_t compiled_version = 1;    // Version actually compiled into IR (set once in visitFunction)
            int fptable_index = -1;           // Index in __aiml_fptable / fptable_current (-1 = not in table)
            std::string sourceYaml;           // YAML dump of the function AST node (for sys.bundle)
            std::set<std::string> referencedTypes; // User-defined types referenced in the body
            bool deprecated = false;          // meta: [deprecated] → warn at each call site
            bool ownsReturn = false;          // spec/61 return-ownership: returns an owned heap box
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
            // classgroup sees-member: a wired COLLABORATOR pointer, not owned data.
            // Serialization skips it (spec/58 §9); also breaks the ser-schema
            // recursion of bidirectional sees cycles.
            bool is_collaborator = false;

            // AionDB secondary-index modifier (spec/45_db_indexed_fields.md).
            // Activated by 4th-position dict in model field: e.g.
            //   - [name, string, "", { db_indexed: true, unique: true }]
            // When set on a model used as Kv<string:Model>'s value type, the
            // compiler emits a sidecar Kv ("<main_name>__<field>") and hooks
            // set/delete to maintain it; auto-method findings_by_<field> /
            // finding_by_<field> resolves against the sidecar.
            bool db_indexed = false;
            std::string db_indexed_mode;   // "" | "lower"
            bool db_indexed_unique = false;
            bool db_indexed_skip_empty = false;
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

            // Heap serialization (spec 45): set during registerTypeDecl
            // when the struct has at least one heap field (string, list, map, buffer).
            // serNodesVar is the emitted @__aiml_sernodes_<Name> LLVM global.
            bool                  hasHeapFields  = false;
            llvm::GlobalVariable* serNodesVar    = nullptr;  // nullptr = not yet emitted
            int32_t               serNodesCount  = 0;
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
        // AIML v2: read AIML_MODEL_REFCOUNT HERE, at construction, so `modelRefcount` is set
        // consistently in EVERY context from birth — including the header-only import sub-compilers
        // (which share/copy a context and register types BEFORE compile() runs its own env-read).
        // Otherwise a header-only pass with modelRefcount=false lays out a model's value-model
        // fields INLINE while the main compile (modelRefcount=true) emits incref/decref on them as
        // %M* pointers → an incref-on-struct IR verify failure / crash (the reaction/trigger cluster:
        // PendingReaction.sub). The later reads in compile()/registerTypes stay (idempotent).
        if (const char* mr = std::getenv("AIML_MODEL_REFCOUNT")) {
            std::string s = mr;
            modelRefcount = !(s.empty() || s == "0" || s == "off" || s == "OFF" || s == "false" || s == "FALSE");
        }
        // spec/61 memory model (v3): gated by AIML_MEM_V3, read at construction (same reason as
        // modelRefcount above — every context, incl. header-only sub-compilers, must agree). Under
        // this gate the compiler emits the spec/61 target behavior incrementally (first: string
        // assignment = share/incref, §4). Default OFF → deployed/OFF path byte-identical.
        if (const char* mv = std::getenv("AIML_MEM_V3")) {
            std::string s = mv;
            memV3 = !(s.empty() || s == "0" || s == "off" || s == "OFF" || s == "false" || s == "FALSE");
        }
        // initialized lazily or explicitly later to avoid circular dependency here
        llvm::Type* i32 = llvm::Type::getInt32Ty(context);
        llvm::Type* p = llvm::PointerType::get(context, 0);
        llvm::StructType* jsTy = llvm::StructType::getTypeByName(context, "JsonStyle");
        if (!jsTy) jsTy = llvm::StructType::create(context, {i32, p, p}, "JsonStyle");
        ClassInfo jsInfo;
        jsInfo.name = "JsonStyle";
        jsInfo.type = jsTy;
        jsInfo.isBuiltin = false;
        StructMember prettyMember; prettyMember.index = 0; prettyMember.type = i32; prettyMember.typeName = "i32";
        StructMember indentMember; indentMember.index = 1; indentMember.type = p; indentMember.typeName = "string";
        StructMember newlineMember; newlineMember.index = 2; newlineMember.type = p; newlineMember.typeName = "string";
        jsInfo.members["pretty"] = prettyMember;
        jsInfo.members["indent"] = indentMember;
        jsInfo.members["newline"] = newlineMember;
        classes["JsonStyle"] = std::move(jsInfo);
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

    // Helper: target's size_t-equivalent int type. i64 on x86_64, i32 on wasm32.
    // Use for libc-call args/returns (strlen, malloc, memcpy size, …) so the
    // call signature matches the target's libc.
    llvm::Type*  getSizeType()              { return builder.getIntNTy(module.getDataLayout().getPointerSizeInBits()); }
    llvm::Value* getSizeConst(uint64_t v)   { return llvm::ConstantInt::get(getSizeType(), v); }

    // Helper: alloca in function entry block (prevents stack growth in loops)
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name = "") {
        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock& entry = fn->getEntryBlock();
        llvm::IRBuilder<> eb(&entry, entry.getFirstInsertionPt());
        return eb.CreateAlloca(ty, nullptr, name);
    }

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

    // spec/61 lifetime: ordered per-scope cleanup list. One frame per block; a
    // deep-copied owned value-local (independent heap) registers a drop here at decl,
    // and visitBlockStmt emits aiml_ser_free_decoded for the frame (reverse order) at
    // normal block exit.  (A2: normal-exit only; return/break cleanup is a later step.)
    struct ScopeDrop {
        llvm::Value*          addr = nullptr;        // alloca of the owned value-local
        llvm::GlobalVariable* serNodes = nullptr;    // its class's @__aiml_sernodes global
        int32_t               serNodesCount = 0;
        int                   kind = 0;              // 0 = model (aiml_ser_free_decoded),
                                                     // 1 = string (load char* → aiml_str_decref),
                                                     // 2 = list  (aiml_list_free(addr, elemKind, …)),
                                                     // 3 = map   (aiml_map_free(addr, keyKind, …)),
                                                     // 4 = owned new-box ptr<T> (load ptr →
                                                     //     aiml_ser_free_decoded(ptr) then free(ptr) shell),
                                                     // 5 = owned interface fat-pointer box (load {obj,vtable}
                                                     //     → call vtable's trailing __free_<Class> slot on obj)
        llvm::Type*           fatPtrType = nullptr;   // kind 5: AimlFatPtr_<Iface> struct type (GEP obj/vtable)
        int                   ifaceMethodCount = 0;   // kind 5: interface method count = free-slot index
        int                   elemKind = 0;          // kind 2: 0=scalar,1=string,2=model. kind 3: VALUE kind
        int64_t               elemSize = 0;          // kind 2 elemKind 2: elem stride. kind 3: VALUE stride
        int                   keyKind = 0;           // kind 3: key kind (0 = scalar, 1 = string)
        int64_t               keyStride = 0;         // kind 3: key element stride
        AIML::AST::OwnershipId ownershipId = AIML::AST::NoOwnership;  // spec/61 §4: owning slot (mayDrop-gated)
        // spec/61 §7 managed string local (kind 1): the slot invariant is "null | one owned RC ref".
        // Its drop is null-safe (load; decref; store null) and a committed move nulls the slot rather
        // than erasing the drop, so Live|Moved control-flow merges free exactly once.
        bool                  managedStringSlot = false;
        // spec/61 §5 Option B (memV3): kind 2 where the slot holds a POINTER to a refcounted
        // list handle (not an inline struct) → drop = aiml_list_handle_release (decref; free
        // data+block at rc 0) after loading the pointer from the slot.
        bool                  isListHandle = false;
        std::string           debugName;             // AIML_TRACE_SYMBOL: source var name for lifetime tracing
        std::string           debugType;             // AIML_TRACE_SYMBOL: declared type name (list<>/map<>/model)
    };
    std::vector<std::vector<ScopeDrop>> cleanupStack;
    std::map<llvm::Value*, std::string> traceDeclarationSites; // diagnostic-only source locations


    // spec/61 §3.1 (consumed parameters): visitFunction stages a `consumed` handle
    // parameter's callee-drop here, and the NEXT visitBlockStmt (the function body) drains
    // these into its own cleanup frame — so an un-consumed consumed-param is freed at
    // function-body exit (the linear-obligation safety net) unless a push/return/store
    // consumes it first (consumeLocalDrop removes it, exactly like a scope local).
    std::vector<ScopeDrop> pendingParamDrops;

    // spec/61 §4: the current function's ownership plan (borrowed, set by visitFunction via an
    // RAII guard). When present, visitBlockStmt gates each registered scope-drop through
    // mayDrop() — a drop fires only for a provably-owned, un-rebound slot. nullptr (e.g. module
    // init outside a function) → the pre-plan behaviour (emit as registered).
    const AIML::AST::OwnershipPlan* ownershipPlan = nullptr;

    // spec/61 render-temporaries: owned heap-string TEMPORARIES (interpolation results)
    // created while compiling a statement.  One frame per statement (visitStatement).  A
    // temp consumed by decl-init / push / set / return is removed (moved); the remainder
    // (e.g. borrowed call args like set_text, which copy synchronously) is decref'd at
    // statement end.  This reclaims the unnamed `"${…}"` temporaries that drive the
    // client render-`populate` leak, which decl-local drops cannot reach.
    std::vector<std::vector<llvm::Value*>> stmtTempStack;

    // spec/61: master switch for automatic drop/decref EMISSION.  OFF by default —
    // the model/string scope-drops + render-temp decrefs are correct on the aiml test
    // suite but regressed 9rooms_v2 (login/deckset KV paths not covered by the suite).
    // Registration/consume logic stays active (harmless when nothing is emitted); flip
    // this ON only after server-connected 9rooms e2e validation.  See spec/61 §8.
    // 2026-09-10: DEFAULT OFF again — with the drops ON the 9rooms suite double-frees on real
    // aliasing (shallow container/model member-stores from borrows) in ~20% of tests, and the
    // Increment-C deep-copy changes aliasing semantics (logic regressions).  Compiler::compile()
    // sets this from AIML_LIFETIME_DROPS (unset ⇒ OFF; =1 ⇒ ON; the mem-sanitizer harness sets
    // =1).  Re-enable by default only once the FULL 9rooms suite passes with it on.
    bool lifetimeDropsEnabled = false;
    // AIML v2 reference-semantics for heap-field models (doc/aiml_v2_memory_model.md +
    // aiml_v2_impl_plan.md). When ON, a model with heap fields is a refcounted heap object
    // (aiml_model_alloc/incref/decref); locals/fields/elements hold %M*. DEFAULT OFF (env
    // AIML_MODEL_REFCOUNT) — the v1 inline-value-model path — until the phased representation
    // change (P1–P6) lands and the full 9rooms suite is green with it. INERT until then.
    bool modelRefcount = false;
    // spec/61 memory model (v3): gated by AIML_MEM_V3 (read in ctor). When ON, the compiler emits
    // the spec/61 target behavior, landed incrementally (first: string assignment = share/incref,
    // §4). DEFAULT OFF → the deployed/OFF path is byte-identical. See spec/61_memory_management.
    bool memV3 = false;
    // spec/61 §10 owning-cycle lint: run once per compile (set by the first visitFunction).
    bool ownCycleLintDone = false;
    std::map<std::string, FunctionInfo> functions;
    // spec/61 return-ownership: ORIGINAL (un-mangled) short names of functions/methods
    // declared `owns_return: true`. Captured at parse-order (before method-name mangling)
    // so a call site's dotted `functionName` (e.g. `<.cards.get_base`) can be matched by its
    // trailing identifier — mirrors the `.get_copy` suffix precedent in the ownership analyzer.
    std::set<std::string> ownsReturnFns;
    // spec/61: a producer short name → the set of declared RETURN TYPES of producers with that name.
    // A short name alone is ambiguous — a container builtin `.get` collides with user producer `get`
    // methods (e.g. deck/lobby/match service `get` → *Resp), which wrongly classified a borrowed
    // `decl [hnd, ptr<RpcHandler>, {call: handlers.get}]` as an owned interface box → double-free of
    // the registered handler on every dispatch. freshOwned/visitDeclStmt therefore require the
    // producer's return type to match the decl's owned type, not just the name.
    std::map<std::string, std::set<std::string>> ownsReturnFnTypes;
    // spec/61: does a producer named `shortName` return a value whose type matches an owned local of
    // declared type `declType`? Guards the return-ownership classification against short-name
    // collisions (container `.get`/`.at` builtins vs user producer methods).
    bool ownsReturnMatches(const std::string& shortName, const std::string& declType) const {
        if (ownsReturnFns.count(shortName) == 0) return false;
        auto it = ownsReturnFnTypes.find(shortName);
        if (it == ownsReturnFnTypes.end()) return false;
        return it->second.count(declType) > 0;
    }
    // spec/61 §3.2 Part 2: the subset of ownsReturnFns whose return TYPE is `string`. A producer
    // string result used as a TRANSIENT sub-expression (not bound to a local) must be decref'd at
    // statement end — registered as an owned stmt-temp at the call site (aiml_str_decref). Gated on
    // string specifically because that temp mechanism is string-only (a list/model producer temp
    // would be wrongly decref'd). Matched by the call's trailing identifier, like ownsReturnFns.
    std::set<std::string> stringProducerFns;
    std::map<std::string, ClassInfo> classes; // Class Registry
    std::map<std::string, std::map<std::string, int>> enums; // Enum Registry
    std::map<std::string, std::map<int, std::string>> enumLabels; // enum name → id → display label
    std::map<std::string, std::string> enumSources;          // enum name → YAML source (for sys.bundle)

    /// Unused-enumerator analysis (spec/49): every enum member declared in THIS
    /// module (qualified enum name → set of member names), the members actually
    /// referenced as `Enum.Member`, and each enum's declaration source location.
    /// Merged up the AIMLCompiler sub-compiler hierarchy (allEnumDeclMembers /
    /// allEnumUsedMembers) exactly like prefabDecls/prefabUses, so the top-level
    /// build warns on declared-but-never-referenced members.  Keyed identically
    /// to `enums` (qualified name) so decl and use sets line up.
    std::map<std::string, std::set<std::string>> enumDeclMembers;
    std::map<std::string, std::set<std::string>> enumUsedMembers;
    std::map<std::string, std::string>            enumDeclLoc;   // enum name → "file:line: "
    std::map<std::string, std::string> interfaceSources;     // interface name → YAML source (for sys.bundle)

    // Cascade Registry — a cascade is an algebraic sum type: it has multiple
    // named nodes; each node carries a distinct bytecode id and a payload
    // (list of typed fields).  Serializes as `list<i32>` = [id, f0, f1, ...]
    // with nested cascade-typed fields expanding recursively.
    struct CascadeFieldInfo {
        std::string name;         // auto-generated at parse-time if compact form
        std::string typeName;     // resolved lazily (may reference forward-decl'd type)
        std::string defaultValue; // literal or empty
    };
    struct CascadeNodeInfo {
        std::string name;
        int         id = -1;
        std::string label;
        std::vector<CascadeFieldInfo> fields;
    };
    struct CascadeInfo {
        std::string name;
        std::string opcode;   // optional enum typing element 0 (node id); "" = none
        std::vector<CascadeNodeInfo> nodes;
        // Convenience lookups derived at registration time:
        std::map<int, size_t>         byId;    // id → index into `nodes`
        std::map<std::string, size_t> byName;  // node-name → index
    };
    std::map<std::string, CascadeInfo> cascades;     // Cascade Registry
    std::map<std::string, std::string> cascadeSources; // cascade name → YAML source

    // Function-cascade method pool (spec/57).  M1a: pool declaration + wiring.
    struct CascadeClassInfo {
        std::string name;
        std::string boundName;   // the cascade or enum it renders
        bool        bindsEnum = false;  // false = cascade, true = enum
        std::string carrier;     // entry carrier type
        std::string taker;       // exit carrier type
        std::vector<std::string> members;  // rule-holding class names
    };
    std::map<std::string, CascadeClassInfo> cascadeClasses;   // CascadeClass Registry

    /// ClassGroup Registry (spec/58) — synthesized container-class names.  On
    /// {new: <container>}, visitNewExpr calls the synthesized `<container>___ctor`
    /// to allocate every instance and back-patch the sees pointers.
    std::set<std::string> classGroupContainers;

    /// External `groupmethods:` blocks (spec/58 §1.1/§11) collected during type
    /// registration, awaiting merge into their target classgroup.  Cross-file:
    /// the block and its target group may live in different units, so the methods
    /// are recorded here (raw ptrs into an AST kept alive by `astKeepAlive`) and
    /// folded in by mergeGroupMethods once the target group is in scope.  Entries
    /// are removed as they are consumed.
    std::vector<AIML::AST::GroupMethodsDecl*> pendingGroupMethods;
    /// Keeps imported sub-file ASTs alive for the whole compilation so the raw
    /// pointers in `pendingGroupMethods` (and their FunctionDecls) stay valid.
    /// `shared_ptr<void>` with a typed deleter avoids needing the AST types
    /// complete in this header.
    std::vector<std::shared_ptr<void>> astKeepAlive;

    /// Prefab Registry — maps every `- prefab:` name declared locally or in a
    /// (transitively) imported module to the set of its declared child names.
    /// Populated during registerTypes so that checkPrefabUsage can statically
    /// verify engine.instantiate("<Name>") (Tier 1) and child-addressing method
    /// calls like inst.set_text("<child>", …) (Tier 2).  A prefab with no
    /// children still gets an (empty) entry, so `.count(name)` answers the
    /// Tier-1 "is this a known prefab?" question.  See spec/44_8_prefab_static.md.
    std::map<std::string, std::set<std::string>> prefabDecls;

    /// Prefab names instantiated (via engine.instantiate("<literal>")) anywhere
    /// in THIS module's bodies.  Collected by checkPrefabUsage; merged up the
    /// sub-compiler hierarchy (AIMLCompiler::allPrefabUses) so the top-level
    /// build can warn on declared-but-never-instantiated prefabs (Tier 3
    /// dead-prefab).  See spec/44_8_prefab_static.md.
    std::set<std::string> prefabUses;

    /// Prefab-report mode (`--prefab-report`): when set, checkPrefabUsage also
    /// emits tab-separated fact records into `prefabReportLines` (declarations,
    /// engine.instantiate sites, spawn/payload_spawn wiring, child accesses).
    /// These are merged up the sub-compiler chain and printed once at the top
    /// level instead of producing a binary.  See spec/44_8_prefab_static.md.
    bool reportPrefabs = false;
    std::vector<std::string> prefabReportLines;

    /// Alias — named alias for a primitive type carrying widget metadata.
    /// Used by cascade payloads for role names (VALUE, COUNT, SIDES, ...) that
    /// map to i32 but rendere as constrained numeric inputs.
    struct AliasInfo {
        std::string name;
        std::string typeName;      // underlying type
        std::string label;
        std::string minValue;
        std::string maxValue;
        std::string defaultValue;
    };
    std::map<std::string, AliasInfo> aliases;

    // Interface Registry — name → ordered method signatures.  Used for:
    //   1. resolving ptr<Interface> as a fat-pointer type (obj_ptr + vtbl_ptr)
    //   2. vtable slot lookup at method-call sites
    //   3. vtable generation for classes that implement interfaces
    struct InterfaceMethod {
        std::string name;
        std::string returnType;
        std::vector<std::pair<std::string, std::string>> args; // [argName, argType]
        std::vector<std::string> argLifetimes;                 // spec/61 §3.1, parallel to args
    };
    struct InterfaceInfo {
        std::string name;
        std::vector<InterfaceMethod> methods;        // ordered, slot = index
        llvm::StructType* fatPtrType = nullptr;      // {i8* obj, i8* vtbl}, cached
    };
    std::map<std::string, InterfaceInfo> interfaces;

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
    bool libraryMode = false;  // skip __aiml_init/fptable/schemas for import-only objects
    bool inTopLevelInit = false;  // true while emitting __aiml_init body (top-level stmts)

    // Lib init function names from imported files — called at start of __aiml_init
    std::vector<std::string> libInitFuncNames;
    // Set by library-mode compile — the init function name for this module
    std::string libInitFuncName;

    // Source location tracking for log.* calls (Spec 46)
    std::string currentSourceFile;   // Relative path of file being compiled
    std::string currentFunctionName; // Name of the function/method currently being compiled
    int         currentLine = -1;    // 1-based line of the statement being compiled (-1 = unknown)
    // Decorator list of the function currently being compiled (spec/55).
    // Set at visitFunction entry, restored at exit.  Empty vector = plain fn.
    std::vector<std::string> currentFunctionDecorators;
    // Return type name of the current function (e.g. "string", "i32") — the
    // return-wrap logic needs it to know whether e.g. `vocab` applies.
    std::string              currentFunctionReturnType;

    // Returns an AIML-source location prefix for diagnostic messages.
    // Format: "file:line: " or "file (in func): " or "file: " or "".
    // Prepend to LOG_ERROR messages; C++ file:line is appended by the macro.
    std::string fmtLoc() const {
        if (currentSourceFile.empty()) return "";
        std::string loc = currentSourceFile;
        if (currentLine > 0) {
            loc += ":" + std::to_string(currentLine);
        } else if (!currentFunctionName.empty()) {
            loc += " (in " + currentFunctionName + ")";
        }
        return loc + ": ";
    }

    std::string fmtLoc(const YAML::Node& node) const {
        if (currentSourceFile.empty()) return "";
        std::string loc = currentSourceFile;
        if (node.IsDefined()) {
            int ln = node.Mark().line;
            if (ln >= 0) {
                loc += ":" + std::to_string(ln + 1);
                return loc + ": ";
            }
        }
        // Fall back to currentLine from visitStatement
        if (currentLine > 0) {
            loc += ":" + std::to_string(currentLine);
            return loc + ": ";
        }
        if (!currentFunctionName.empty())
            loc += " (in " + currentFunctionName + ")";
        return loc + ": ";
    }

    // Lint: identifiers (variable/arg/field/function/class/interface names) must
    // be at least 3 characters.  Catches loop-counter style like `k`/`i`/`n` and
    // forces names that read at the call-site without context.  Returns true
    // when the name is valid; emits LOG_ERROR and returns false otherwise.
    // `kind` describes the role for the diagnostic (e.g. "variable", "argument").
    bool validateIdentifier(const std::string& name, const std::string& kind,
                            const YAML::Node& srcNode) const {
        if (name.size() >= 3) return true;
        // Empty names indicate a parser bug, reported elsewhere — don't double-log.
        if (name.empty()) return true;
        LOG_ERROR(fmtLoc(srcNode) + kind + " name '" + name +
                  "' is shorter than 3 characters — pick a descriptive name");
        return false;
    }

    // Prefab names and prefab child names must be at least 5 characters.  A
    // prefab and its children are addressed BY NAME across files (engine.
    // instantiate, spawn:, set_text/set_texture/child_clicked), so a terse name
    // like `bg` / `btn` reads as noise at every call-site and collides easily.
    // Stricter than validateIdentifier's 3-char floor because these names form
    // the UI's cross-file wiring vocabulary.  See spec/44_8_prefab_static.md.
    bool validatePrefabName(const std::string& name, const std::string& kind,
                            const YAML::Node& srcNode) const {
        if (name.size() >= 5) return true;
        if (name.empty()) return true;   // parser bug, reported elsewhere
        LOG_ERROR(fmtLoc(srcNode) + kind + " name '" + name +
                  "' is shorter than 5 characters — prefab and child names are "
                  "referenced by name across files; pick a descriptive name "
                  "(>=5 chars)");
        return false;
    }

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

    // Tracking for Kv.get / Kv.get_copy dispatch so visitDeclStmt can enforce
    // the correct decl-type ↔ method pairing:
    //   decl: [p, T,      kv.get_copy(k)]  → deep-copy at decl site
    //   decl: [p, ptr<T>, kv.get(k)]       → zero-copy view
    //   decl: [p, T,      kv.get(k)]       → ERROR (shallow memcpy with heap-shared pointers)
    //   decl: [p, ptr<T>, kv.get_copy(k)]  → ERROR (deep-copy wasted on a view)
    // See doc/issue/kv_get_mutate_set_same_key_crash.md.
    bool        kvGetCopy           = false;   // last dispatched Kv method was get_copy
    bool        kvGetCalled         = false;   // last dispatched Kv method was get or get_copy
    std::string kvGetValueType;                // V from Kv<K:V> for diagnostic messages

    // Transient: the CallExpr currently being dispatched.  Set by visitCall
    // before invoking a builtinFunc lambda so the lambda can inspect literal
    // args (e.g. asset registry compile-time validation of gl.tex("stem")).
    // Reset after dispatch — do not hold across nested calls.
    AIML::AST::CallExpr* currentCall = nullptr;

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
        // cascade<X>  — the inner name is a top-level cascade decl
        if (typeName.size() > 8 && typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>') {
            std::string inner = typeName.substr(8, typeName.size() - 9);
            if (cascades.count(inner)) out.insert(inner);
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

    // AIML v2 (doc/aiml_v2_impl_plan.md): is `typeName` a model that uses reference semantics —
    // i.e. a refcounted heap object (aiml_model_alloc, %M* everywhere)? True only when the
    // AIML_MODEL_REFCOUNT flag is ON and the type is a user MODEL with heap fields. A scalar-only
    // model, a class, an interface, or the flag being OFF all return false → the v1 inline-value
    // path is used unchanged. This is THE single gate for every v2 model-representation branch, so
    // that OFF stays byte-for-byte the v1 compiler. `bare` strips a leading ptr<>/ref<> wrapper.
    bool isRefcountModel(const std::string& typeName) const {
        std::string bare = typeName;
        if ((bare.rfind("ptr<", 0) == 0 || bare.rfind("ref<", 0) == 0) && !bare.empty() && bare.back() == '>')
            bare = bare.substr(4, bare.size() - 5);
        // spec/61 §6.2: `shared<T>` is an opt-in refcounted co-owned handle. Under AIML_MEM_V3 it
        // reuses the (well-tested) v2 refcounted-object machinery — %M* alloc/incref/decref,
        // member auto-deref, list<%M*> elements — WITHOUT the global modelRefcount flag: only the
        // `shared<>`-wrapped type is a refcount model, the bare `T` stays a value model.
        bool sharedWrap = (bare.rfind("shared<", 0) == 0 && bare.back() == '>');
        if (sharedWrap) bare = bare.substr(7, bare.size() - 8);
        // Gate: the global v2 flag OR a memV3 shared<> wrapper enables refcount semantics.
        if (!modelRefcount && !(memV3 && sharedWrap)) return false;
        auto it = classes.find(bare);
        if (it == classes.end() || !it->second.isModel || it->second.isBuiltin) return false;
        // An explicit `shared<T>` opts into heap co-ownership regardless of whether T has
        // heap fields — the user asked for a refcounted %M* handle. The global modelRefcount
        // path keeps the v2 optimization (heap-field-less value models need no refcount).
        if (memV3 && sharedWrap) return true;
        return it->second.hasHeapFields;
    }

    // Helper: Get LLVM Type from string
    llvm::Type* getLLVMType(const std::string& typeName) {
        if (typeName == "" || typeName == "<.balance" || typeName == "${ { +: [a, b] } }") {
             LOG_ERROR(fmtLoc() + "CRITICAL ERROR: getLLVMType called with invalid typeName: '" + typeName + "'");
             throw std::runtime_error("INVALID TYPE NAME FOUND: " + typeName);
        }
        LOG_DEBUG("getLLVMType: '" + typeName + "'");

        // Check for Enums (Trace back to i32)
        if (enums.count(typeName)) {
             return llvm::Type::getInt32Ty(context);
        }

        // Aliases: resolve to the underlying type.  The widget metadata
        // (label/min/max/default) is only consulted at UI-render time.
        if (aliases.count(typeName)) {
             return getLLVMType(aliases.at(typeName).typeName);
        }

        // cascade<X>: the wire format (and initial in-memory representation)
        // matches the bytecode signature `[node_id, field0, field1, ...]`
        // recursively.  So cascade<X> re-uses the list<i32> LLVM layout.
        // Higher-level operations (typed field access, widget binding,
        // serialize helpers) go through the cascade name looked up in
        // `cascades[typeName-innerX]` at the call site.
        if (typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>') {
            return getLLVMType("list<i32>");
        }

        // Check for List type: "list<T>"
        if (typeName.rfind("list<", 0) == 0 && typeName.back() == '>') {
            std::string innerType = typeName.substr(5, typeName.length() - 6);
            // Note: the list struct layout is always { i32, i32, i8* } regardless of T.
            // The inner type is resolved lazily at element access time.  Do NOT call
            // getLLVMType(innerType) here — it would spuriously error on forward-declared
            // types during Phase-A sibling type registration.
            std::string structName = "list_" + innerType;
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
        // Skip builtins here too — they have explicit type definitions below.
        std::string qualified = qualify(typeName);
        if (classes.count(qualified) && classes[qualified].type != nullptr && !classes[qualified].isBuiltin) {
            if (!bundleTrackingTarget.empty() && functions.count(bundleTrackingTarget))
                functions[bundleTrackingTarget].referencedTypes.insert(qualified);
            return classes[qualified].type;
        }

        // Check for Pointer type: "ptr<T>" or "ref<T>"
        if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) && typeName.back() == '>') {
            std::string innerType;
            if (typeName.rfind("ptr<", 0) == 0) innerType = typeName.substr(4, typeName.length() - 5);
            else innerType = typeName.substr(4, typeName.length() - 5); // ref< is also 4 chars

            // ptr<Interface> = fat pointer { i8* obj, i8* vtbl }.  Resolved via
            // ctx.interfaces — the interface must be registered.  ref<Interface>
            // is NOT supported (use ptr<Interface> for polymorphism).
            auto ifaceIt = interfaces.find(innerType);
            if (ifaceIt != interfaces.end() && typeName.rfind("ptr<", 0) == 0) {
                if (!ifaceIt->second.fatPtrType) {
                    auto* i8p = llvm::PointerType::get(context, 0);
                    ifaceIt->second.fatPtrType = llvm::StructType::create(
                        context, {i8p, i8p}, "AimlFatPtr_" + innerType);
                }
                return ifaceIt->second.fatPtrType;
            }

            // Opaque pointer: the LLVM layout is independent of the pointee, so
            // resolve the inner type LAZILY (at deref / member-access), NEVER here.
            // Eagerly resolving it would (a) spuriously fail/mis-default a forward-
            // declared or cyclic class type to i32 (crashing a later member call),
            // exactly the reason list<T> above skips inner resolution — and this is
            // what lets a classgroup sees-member be typed as a sibling's not-yet-
            // synthesized subclass (spec/58, bidirectional sees cycles).
            return llvm::PointerType::get(context, 0); // Opaque pointer
        }

        // spec/61 §6.2: shared<T> is an 8-byte opaque handle to a refcounted %M* block (reuses
        // the v2 model-refcount runtime). Inner type resolved lazily at member access, like ptr<>.
        if (typeName.rfind("shared<", 0) == 0 && typeName.back() == '>') {
            return llvm::PointerType::get(context, 0);
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
                     LOG_ERROR(fmtLoc() + "Failed to resolve key/val types for map");
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
                 LOG_ERROR(fmtLoc() + "Separator ':' not found in map type content: " + content);
             }
        }

        // Check for dyn_array<T> — heap-allocated, runtime-sized → stored as i8*
        if (typeName.rfind("dyn_array<", 0) == 0 && typeName.back() == '>') {
            return llvm::Type::getInt8Ty(context)->getPointerTo();
        }

        // Check for fixed-size array type: "array<T:N>" or "array<T:N:M>" etc.
        if (typeName.rfind("array<", 0) == 0 && typeName.back() == '>') {
            std::string inner = typeName.substr(6, typeName.size() - 7); // strip "array<" and ">"
            // Split on ':'  →  e.g. "f32:64" or "f32:4:4" or "i32:3:4:5"
            std::vector<std::string> parts;
            size_t pos = 0;
            while (pos < inner.size()) {
                size_t sep = inner.find(':', pos);
                if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
                parts.push_back(inner.substr(pos, sep - pos));
                pos = sep + 1;
            }
            if (parts.size() >= 2) {
                llvm::Type* elemType = getLLVMType(parts[0]);
                uint64_t totalCount = 1;
                for (size_t i = 1; i < parts.size(); ++i)
                    totalCount *= std::stoull(parts[i]);
                return llvm::ArrayType::get(elemType, totalCount);
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

        // iterator<T> — opaque heap pointer to AimlIterator
        if (typeName.rfind("iterator<", 0) == 0 && typeName.back() == '>')
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
        if (typeName == "audio_handle") return llvm::Type::getInt8Ty(context)->getPointerTo();       // V1 sentinel (asset registry)
        if (typeName == "prefab_instance") return llvm::Type::getInt8Ty(context)->getPointerTo();  // AimlPrefabInstance*
        if (typeName == "connector") return llvm::Type::getInt8Ty(context)->getPointerTo();      // AimlConnector*
        if (typeName == "tensor") return llvm::Type::getInt8Ty(context)->getPointerTo();       // AimlTensor*
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

        if (typeName == "JsonStyle") {
            llvm::StructType* t = llvm::StructType::getTypeByName(context, "JsonStyle");
            if (!t) t = llvm::StructType::create(context, {llvm::Type::getInt32Ty(context), llvm::PointerType::get(context, 0), llvm::PointerType::get(context, 0)}, "JsonStyle");
            return t;
        }

        // Code: pointer to heap-allocated __aiml_code_t (opaque i8*)
        if (typeName == "Code")
            return llvm::Type::getInt8Ty(context)->getPointerTo();

        LOG_ERROR(fmtLoc() + "Unknown Type: '" + typeName + "' -> defaulting to i32");
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
