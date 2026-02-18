#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/utils.h"
#include "../../logger.h"

class PrintCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: print: [var_name] or [literal] or [list of items]
        const YAML::Node& args = node["print"];
        
        std::vector<llvm::Value*> valuesToPrint;
        
        if (args.IsSequence()) {
            for (const auto& arg : args) {
                if (arg.IsScalar()) {
                    std::string valStr = arg.as<std::string>();
                    // Check for string expansion ${...}
                    if (valStr.find("${") != std::string::npos) {
                         llvm::Value* expanded = StringUtils::expandExpression(ctx, valStr, [](CompilerContext& c, const YAML::Node& n) {
                             return Expression::evaluate(c, n);
                         });
                         valuesToPrint.push_back(expanded);
                    } else {
                         valuesToPrint.push_back(Utils::resolveValue(ctx, arg, [](CompilerContext& c, const YAML::Node& n){
                              return Expression::evaluate(c, n);
                         }));
                    }
                } else {
                    valuesToPrint.push_back(Utils::resolveValue(ctx, arg, [](CompilerContext& c, const YAML::Node& n){
                         return Expression::evaluate(c, n);
                    }));
                }
            }
        } else {
             // Single arg
             if (args.IsScalar()) {
                 std::string valStr = args.as<std::string>();
                 if (valStr.find("${") != std::string::npos) {
                      llvm::Value* expanded = StringUtils::expandExpression(ctx, valStr, [](CompilerContext& c, const YAML::Node& n) {
                          return Expression::evaluate(c, n);
                      });
                      valuesToPrint.push_back(expanded);
                 } else {
                      valuesToPrint.push_back(Utils::resolveValue(ctx, args, [](CompilerContext& c, const YAML::Node& n){
                           return Expression::evaluate(c, n);
                      }));
                 }
             } else {
                 valuesToPrint.push_back(Utils::resolveValue(ctx, args, [](CompilerContext& c, const YAML::Node& n){
                      return Expression::evaluate(c, n);
                 })); // expression?
             }
        }

        llvm::FunctionCallee dprintfFunc = ctx.module.getOrInsertFunction(
        "dprintf", 
        llvm::FunctionType::get(
            ctx.builder.getInt32Ty(), 
            {ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, 
            true
        )
    );

    for (auto* val : valuesToPrint) {
        // Determine format string based on value type
        llvm::Value* formatStr = nullptr;
        if (val->getType()->isIntegerTy(32)) {
             formatStr = ctx.builder.CreateGlobalStringPtr("%d ", "fmt_int");
        } else if (val->getType()->isIntegerTy(8) || val->getType()->isIntegerTy(16)) {
             formatStr = ctx.builder.CreateGlobalStringPtr("%d ", "fmt_short");
        } else if (val->getType()->isPointerTy()) {
             formatStr = ctx.builder.CreateGlobalStringPtr("%s ", "fmt_str");
        } else if (val->getType()->isIntegerTy(64)) {
             formatStr = ctx.builder.CreateGlobalStringPtr("%ld ", "fmt_long");
        } else {
             formatStr = ctx.builder.CreateGlobalStringPtr("UnknownType ");
        }
        
        // dprintf(1, format, val)
        ctx.builder.CreateCall(dprintfFunc, {ctx.builder.getInt32(1), formatStr, val});
    }
    // Print newline at end
    // dprintf(1, "\n")
    ctx.builder.CreateCall(dprintfFunc, {ctx.builder.getInt32(1), ctx.builder.CreateGlobalStringPtr("\n")});

    // Remove fflush call as dprintf to fd 1 should be sufficient (or at least different)
    // llvm::FunctionCallee fflushFunc = ...
    }
};
