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
    void registerTypeDecl(Node* node);
    void visitFunction(FunctionDecl* func);
    void visitTopLevel(Node* node);

private:
    CompilerContext& ctx;
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
    // Static check: engine.instantiate("<Name>") against a name with no
    // matching `- prefab:` declaration (silent runtime no-op → hard error).
    // Runs for entry and imported files. See spec/44_8_prefab_static.md.
    void checkPrefabUsage(Program* prog);
    void checkCascadeClasses(Program* prog);
    void checkCascadeClassSemantics(Program* prog);
    void synthesizeCascadeClasses(Program* prog);   // M3: emit each pool's render class
    void checkClassGroups(Program* prog);           // spec/58: validate group wiring
    void synthesizeClassGroups(Program* prog);      // spec/58: emit subclasses + container
    void visitExternBlock(ExternBlock* ext);
    
    void visitStatement(Stmt* stmt);
    void visitDeclStmt(DeclStmt* decl);
    void visitSetStmt(SetStmt* setStmt);
    void visitExprStmt(ExprStmt* exprStmt);
    void visitRetStmt(RetStmt* retStmt);
    void visitBlockStmt(BlockStmt* block);
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
