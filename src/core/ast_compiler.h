#pragma once
#include "ast_nodes.h"
#include "context.h"
#include "ir_generator.h"

namespace AIML {
namespace AST {

class Compiler {
public:
    Compiler(CompilerContext& ctx) : ctx(ctx) {}
    
    void compile(Program* prog);
    void registerTypes(Program* prog);
    // Header-only (spec/58 §11): register an imported file's classgroup
    // container/subclass TYPES and let the caller forward-declare their methods,
    // so importing units can reference the synthesized types cross-unit.  Merges
    // any `groupmethods:` present in `prog`, then synthesizes (which appends the
    // container/subclass ClassDecls to prog->declarations for the caller to visit
    // in header-only mode).  Method BODIES are emitted only in the defining unit.
    void synthesizeImportedGroups(Program* prog);
    void registerTypeDecl(Node* node);
    void visitFunction(FunctionDecl* func);
    void visitTopLevel(Node* node);

private:
    CompilerContext& ctx;
    // spec/61 §7.0 — deep-copy an inline container field (list/map/buffer of `typeName`) from
    // `srcPtr` into `dstPtr` (both are addresses of the inline container struct) so the
    // destination owns an INDEPENDENT heap.  Used when a container field is member-stored from a
    // borrowed/view source, so the destination model can keep its scope-drop without double-
    // freeing a buffer shared with the source's owner.  Zero-inits dst first (decode precondition;
    // any prior container in dst leaks — documented §4.4.5 deferral).  Returns false if TypeNodes
    // could not be built (caller then falls back to the shallow memcpy).
    bool emitContainerFieldDeepCopy(llvm::Value* dstPtr, llvm::Value* srcPtr,
                                    const std::string& typeName);
    // spec/61 §7.0 — release the heap a container field (list/map/buffer) currently owns before it
    // is overwritten, guarded against dst==src (self-assign). Used by the member-store MOVE path
    // (free the previous value before the shallow struct memcpy) so a repeated `set: [<.field, x]`
    // does not leak the prior container (the 9rooms take_gone_players / poll_board_state leaks).
    // Returns false if the type has no serialization descriptor (caller skips the free).
    bool emitContainerFieldFree(llvm::Value* dstPtr, llvm::Value* srcPtr,
                                const std::string& typeName);
    // Static check: hand-rolled string switches (enum/regex hints). Runs for
    // entry and imported (libraryMode) files. See spec/49 §16.
    void checkStringComparisons(Program* prog);
    // Static check (warning): pointer-indirection mismatch on call arguments.
    // Compares each variable-based argument's source pointer-depth against the
    // callee parameter's declared depth and warns on OVER-indirection (arg deeper
    // than param → callee derefs too few levels).  Free functions get the exact
    // compare; method/unresolvable calls fall back to the `&(already-ptr)`
    // heuristic.  LLVM opaque pointers hide the depth, so this is source-level.
    // Suppress per-function via `static_off: ["ptr_indirection"]`.
    void checkPtrIndirection(Program* prog);
    // Static check: a blocking primitive (send_and_await / sleep_ms / sleep)
    // reachable over the direct-call graph from an `engine.run` per-frame
    // callback stalls the render loop (freezes the WASM page via asyncify).
    // Warning; per-def opt-out `static_off: ["frame_blocking"]`. See spec/49 §20.
    void checkFrameBlocking(Program* prog);
    // Static check: engine.instantiate("<Name>") against a name with no
    // matching `- prefab:` declaration (silent runtime no-op → hard error).
    // Runs for entry and imported files. See spec/44_8_prefab_static.md.
    void checkPrefabUsage(Program* prog);
    void checkCascadeClasses(Program* prog);
    void checkCascadeClassSemantics(Program* prog);
    void synthesizeCascadeClasses(Program* prog);   // M3: emit each pool's render class
    void mergeGroupMethods(Program* prog);          // spec/58: fold external `groupmethods:` into groups
    void checkClassGroups(Program* prog);           // spec/58: validate group wiring
    void synthesizeClassGroups(Program* prog);      // spec/58: emit subclasses + container
    void visitExternBlock(ExternBlock* ext);
    
    void visitStatement(Stmt* stmt);
    void visitDeclStmt(DeclStmt* decl);
    // spec/61 §5 Option B (memV3): declare a list<T>/cascade<X> value-local as a refcounted handle.
    void emitListHandleDecl(DeclStmt* decl, bool isCascade);
    // spec/61 §5 Option B (memV3): declare a map<K:V> value-local as a refcounted handle.
    void emitMapHandleDecl(DeclStmt* decl);
    void visitSetStmt(SetStmt* setStmt);
    void visitExprStmt(ExprStmt* exprStmt);
    void visitRetStmt(RetStmt* retStmt);
    void visitBlockStmt(BlockStmt* block);
    // spec/61 (A3): mark a local consumed by push/return/store as MOVED — drop its
    // pending scope-cleanup entry so it is not freed here (the target now owns it).
    // nullManagedStringSlot: for a MANAGED string local (spec/61 §7), true = null the slot at runtime
    // and keep its null-safe drop (post-value-capture move sites: push/put/consuming-arg); false =
    // erase the drop like the legacy path (pre-value-load sites: return — the moved value is captured
    // afterwards and the returned value's owner is exempted from cleanup, so erase is safe there).
    void consumeLocalDrop(Expr* argExpr, bool nullManagedStringSlot = true);
    // spec/61 render-temporaries: a heap-string temp value moved into a decl-local /
    // container / member / caller is removed from the pending statement-temp decref list.
    void consumeStringTemp(llvm::Value* v);
    void visitIfStmt(IfStmt* ifStmt);
    void visitWhileStmt(WhileStmt* whileStmt);
    void visitTry(TryStmt* tryStmt);
    void visitThrow(ThrowStmt* throwStmt);
    void visitIdxStmt(IdxStmt* idx);
    void visitStoreStmt(StoreStmt* st);

    IRValue* visitExpression(Expr* expr);
    IRValue* visitAtExpr(AtExpr* at);
    IRValue* visitLiteral(LiteralExpr* lit);
    IRValue* visitVariable(VariableExpr* var);
    IRValue* visitCall(CallExpr* call);
    IRValue* visitObjectMethodCall(const std::string& baseName, const std::string& methodName, Symbol* sym, CallExpr* call);
    IRValue* visitSysNamespaceCall(const std::string& baseName, const std::string& methodName, const std::vector<IRValue*>& argVals);
    IRValue* visitSysSerializeCall(const std::string& method, CallExpr* call);
    IRValue* visitSysCloneCall(CallExpr* call);
    IRValue* visitSysBundleCall(CallExpr* call);

    // Build a fat-pointer struct {obj_ptr, vtable_ptr} for interface upcasts.
    // Returns the loaded fat-pointer value, or nullptr if the upcast is invalid
    // (interface not registered, class does not implement the interface, etc.).
    //   srcValue       — raw object pointer of the concrete class
    //   srcTypeName    — declared type, e.g. "ptr<LoginHandler>"
    //   targetStructTy — the AimlFatPtr_<Iface> struct type
    llvm::Value* buildInterfaceUpcast(llvm::Value* srcValue,
                                      const std::string& srcTypeName,
                                      llvm::StructType* targetStructTy);

    // Recursively initialises container-typed members (list/map/chan/buffer)
    // of a class instance at `basePtr`.  Lets `- decl: [g, ClassName]` work
    // without `{ new: ClassName }` boilerplate — used by Phase-2 prologue and
    // by visitDeclStmt for local class-instance decls.
    void initClassContainerMembers(llvm::Value* basePtr,
                                   const std::string& className);
    IRValue* visitSysExecCall(CallExpr* call);
    IRValue* visitSysArgDumpCall(CallExpr* call);
    IRValue* visitDbRunSystem(CallExpr* call);
    IRValue* visitStringCompare(bool negate, llvm::Value* lhs, llvm::Value* rhs);
    IRValue* visitNewExpr(NewExpr* newExpr);
    IRValue* visitNewArrayExpr(NewArrayExpr* na);
    IRValue* visitStringInterpolation(StringInterpolationExpr* expr);
    IRValue* visitBinary(BinaryExpr* bin);
    IRValue* visitUnary(UnaryExpr* unary);
    IRValue* visitSpawn(SpawnExpr* spawn);

    // Strict Symbol Resolution Helpers
    Symbol* resolveVariable(const std::string& name);
    IRValue* resolveEnum(const std::string& baseName, const std::string& propName);
    IRValue* resolveProperty(Symbol* baseSym, const std::string& propName);
    // Like resolveProperty but returns the GEP pointer (for writes) instead of loading.
    // Sets outTypeName to the final member's type name for caller-side coercions.
    IRValue* resolvePropertyWritePtr(Symbol* baseSym, const std::string& propName, std::string& outTypeName);
    // Address (storage pointer) of an addressable lvalue expression: a plain
    // variable OR a dotted member-access (a.b.c).  Used for unary '&' and for
    // passing member-access as a ptr<T> argument.  outTypeName gets the type
    // name of the addressed slot.  Returns nullptr if not addressable.
    IRValue* emitLValueAddress(Expr* e, std::string& outTypeName);
    CompilerContext::FunctionInfo* resolveFunction(const std::string& name);
};

} // namespace AST
} // namespace AIML
