#include "../core/expression.h"

class SetCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: set: [name, value_expression]
        auto args = node["set"];
        std::string varName = args[0].as<std::string>();
        
        // Use Expression Evaluator for Value
        llvm::Value* val = Expression::evaluate(ctx, args[1]);
        
        LOG_DEBUG("Set " + varName);

        // Try to resolve as L-Value (existing variable or array element)
        llvm::Value* lvalue = Utils::resolveLValue(ctx, varName, nullptr, true);
        
        if (lvalue) {
            // Assignment to existing storage
            // TODO: Type checking/casting?
            ctx.builder.CreateStore(val, lvalue);
        } else {
            // Implicit declaration (Infer type from value) - Only for simple names
            if (varName.find('-') != std::string::npos || varName.find('.') != std::string::npos) {
                // Potential array or member access on non-existent array/object OR invalid operation
                LOG_INFO("Cannot implicitly declare array element or member or invalid name: " + varName + ". Did assignment fail due to const/access?");
                return;
            }

            llvm::Type* varType = val->getType();
            llvm::AllocaInst* alloca = ctx.builder.CreateAlloca(varType, nullptr, varName);
            ctx.builder.CreateStore(val, alloca);
            
            Symbol sym;
            sym.alloca_inst = alloca;
            sym.type = varType;
            ctx.symbol_table[varName] = sym;
        }
    }
};
