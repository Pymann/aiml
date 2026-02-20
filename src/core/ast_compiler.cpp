#include "ast_compiler.h"
#include <iostream>

namespace AIML {
namespace AST {

void Compiler::compile(Program* prog) {
    if (!prog) return;
    for (const auto& decl : prog->declarations) {
        visitTopLevel(decl.get());
    }
}

void Compiler::visitTopLevel(Node* node) {
    if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        visitFunction(func);
    } else if (auto stmt = dynamic_cast<Stmt*>(node)) {
        visitStatement(stmt);
    }
}

void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = func->name;
    
    std::vector<IRType*> argTypes;
    std::vector<std::string> argNames;
    
    for (const auto& arg : func->args) {
        IRType* t = ctx.ir_gen->resolveType(arg.second);
        if (t) {
            argTypes.push_back(t);
            argNames.push_back(arg.first);
        }
    }
    
    IRType* retTy = nullptr;
    if (!func->returnType.empty()) retTy = ctx.ir_gen->resolveType(func->returnType);
    if (!retTy) retTy = ctx.ir_gen->resolveType("void");
    
    // Note: To fully decouple, we'd add `createFunction` to IRGenerator.
    // Since Phase 3 isn't 100% complete for functions, we mix abstraction.
    // For now, let's just create a prototype print statement to prove AST walk works.
    LOG_DEBUG("Compiling Function: " + mangledName + " via AST");
    
    // TODO: Create the actual function using IRGenerator (requires extending IRGenerator)
}

void Compiler::visitStatement(Stmt* stmt) {
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) visitDeclStmt(decl);
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) visitSetStmt(setStmt);
    else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) visitExprStmt(exprStmt);
    else if (auto retStmt = dynamic_cast<RetStmt*>(stmt)) visitRetStmt(retStmt);
}

void Compiler::visitDeclStmt(DeclStmt* decl) {
    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;
    
    IRValue* alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
    
    Symbol sym;
    sym.ir_alloca_inst = alloca;
    sym.ir_type = ctx.ir_gen->getPointerTy();
    sym.ir_element_type = ty;
    sym.typeName = decl->typeName;
    ctx.symbol_table[decl->varName] = sym;
    
    LOG_DEBUG("Compiled AST Decl: " + decl->varName + " of " + decl->typeName);
    
    if (decl->initialValue) {
        IRValue* initVal = visitExpression(decl->initialValue.get());
        if (initVal) {
            ctx.ir_gen->createStore(initVal, alloca);
        }
    }
}

void Compiler::visitSetStmt(SetStmt* setStmt) {
    auto it = ctx.symbol_table.find(setStmt->target);
    if (it == ctx.symbol_table.end()) {
        std::cerr << "Undefined variable: " << setStmt->target << "\n";
        return;
    }
    IRValue* alloca = it->second.ir_alloca_inst;
    if (!alloca) return;
    
    IRValue* val = visitExpression(setStmt->value.get());
    if (val) {
        ctx.ir_gen->createStore(val, alloca);
    }
}

void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    visitExpression(exprStmt->expr.get());
}

void Compiler::visitRetStmt(RetStmt* retStmt) {
    if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        if (val) ctx.ir_gen->createRet(val);
        else ctx.ir_gen->createRet(nullptr);
    } else {
        ctx.ir_gen->createRet(nullptr);
    }
}

IRValue* Compiler::visitExpression(Expr* expr) {
    if (auto lit = dynamic_cast<LiteralExpr*>(expr)) return visitLiteral(lit);
    if (auto var = dynamic_cast<VariableExpr*>(expr)) return visitVariable(var);
    if (auto call = dynamic_cast<CallExpr*>(expr)) return visitCall(call);
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) return visitBinary(bin);
    return nullptr;
}

IRValue* Compiler::visitLiteral(LiteralExpr* lit) {
    // Need to cast to LLVM via wrapper or extend IRGenerator Context
    // Simplification for now
    LOG_DEBUG("Visit Literal: " + lit->value);
    return nullptr; // Stub
}

IRValue* Compiler::visitVariable(VariableExpr* var) {
    auto it = ctx.symbol_table.find(var->name);
    if (it != ctx.symbol_table.end() && it->second.ir_alloca_inst) {
        return ctx.ir_gen->createLoad(it->second.ir_element_type, it->second.ir_alloca_inst);
    }
    return nullptr; // Stub
}

IRValue* Compiler::visitCall(CallExpr* call) {
    std::vector<IRValue*> argVals;
    for (auto& arg : call->args) {
        IRValue* val = visitExpression(arg.get());
        if (val) argVals.push_back(val);
    }
    // Stub
    return nullptr; 
}

IRValue* Compiler::visitBinary(BinaryExpr* bin) {
    IRValue* lhs = visitExpression(bin->lhs.get());
    IRValue* rhs = visitExpression(bin->rhs.get());
    if (!lhs || !rhs) return nullptr;
    
    if (bin->op == "add") return ctx.ir_gen->createAdd(lhs, rhs);
    // ... other ops
    return nullptr;
}

} // namespace AST
} // namespace AIML
