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
    void visitFunction(FunctionDecl* func);
    
private:
    CompilerContext& ctx;
    
    // Visit methods
    void registerTypeDecl(Node* node);
    void visitTopLevel(Node* node);
    
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
    
    IRValue* visitExpression(Expr* expr);
    IRValue* visitLiteral(LiteralExpr* lit);
    IRValue* visitVariable(VariableExpr* var);
    IRValue* visitCall(CallExpr* call);
    IRValue* visitObjectMethodCall(const std::string& baseName, const std::string& methodName, Symbol* sym, CallExpr* call);
    IRValue* visitSysNamespaceCall(const std::string& baseName, const std::string& methodName, const std::vector<IRValue*>& argVals);
    IRValue* visitSysSerializeCall(const std::string& method, CallExpr* call);
    IRValue* visitSysBundleCall(CallExpr* call);
    IRValue* visitSysExecCall(CallExpr* call);
    IRValue* visitDbRunSystem(CallExpr* call);
    IRValue* visitStringCompare(bool negate, llvm::Value* lhs, llvm::Value* rhs);
    IRValue* visitNewExpr(NewExpr* newExpr);
    IRValue* visitStringInterpolation(StringInterpolationExpr* expr);
    IRValue* visitBinary(BinaryExpr* bin);
    IRValue* visitUnary(UnaryExpr* unary);
    IRValue* visitSpawn(SpawnExpr* spawn);

    // Strict Symbol Resolution Helpers
    Symbol* resolveVariable(const std::string& name);
    IRValue* resolveEnum(const std::string& baseName, const std::string& propName);
    IRValue* resolveProperty(Symbol* baseSym, const std::string& propName);
    CompilerContext::FunctionInfo* resolveFunction(const std::string& name);
};

} // namespace AST
} // namespace AIML
