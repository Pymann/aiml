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
            bool isVolatile = false;
            
            // Determine if the target is volatile
            if (varName.find('.') == std::string::npos && varName.find('-') == std::string::npos && ctx.symbol_table.count(varName)) {
                 isVolatile = ctx.symbol_table[varName].is_volatile;
            } else if (varName.find('.') != std::string::npos) {
                 size_t dotPos = varName.rfind('.');
                 std::string objName = varName.substr(0, dotPos);
                 std::string memName = varName.substr(dotPos + 1);
                 if (ctx.symbol_table.count(objName)) {
                      Symbol& sym = ctx.symbol_table[objName];
                      std::string clsName = sym.className;
                      if (!clsName.empty() && ctx.classes.count(clsName) && ctx.classes[clsName].members.count(memName)) {
                           isVolatile = ctx.classes[clsName].members[memName].is_volatile;
                      }
                 }
            } else if (varName.find('-') != std::string::npos) {
                 size_t hyphenPos = varName.find('-');
                 std::string arrName = varName.substr(0, hyphenPos);
                 if (ctx.symbol_table.count(arrName)) {
                     isVolatile = ctx.symbol_table[arrName].is_volatile;
                 }
            }
            
            llvm::StoreInst* store = ctx.builder.CreateStore(val, lvalue);
            if (isVolatile) store->setVolatile(true);
        } else {
            // Implicit declaration (Infer type from value) - Only for simple names
            if (varName.find('-') != std::string::npos || varName.find('.') != std::string::npos || ctx.symbol_table.count(varName)) {
                // Potential array or member access on non-existent array/object OR invalid operation
                // If it exists in symbol_table, resolveLValue returned nullptr because it's const or private!
                LOG_INFO("Assignment to '" + varName + "' failed (likely const, private, or invalid implicit).");
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
