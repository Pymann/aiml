#include <cassert>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <yaml-cpp/yaml.h>

#include "../../src/core/context.h"

int mirror_delta_unit_tests();

int main() {
    llvm::LLVMContext llvm_ctx;
    llvm::Module mod("aiml_unit_tests", llvm_ctx);
    llvm::IRBuilder<> builder(llvm_ctx);
    CompilerContext ctx(llvm_ctx, mod, builder);

    ctx.currentSourceFile = "tests/example.aiml";
    ctx.currentFunctionName = "main";
    ctx.currentLine = 17;

    assert(ctx.fmtLoc() == "tests/example.aiml:17: ");

    YAML::Node root = YAML::Load("foo: 1\nbar: 2\n");
    assert(ctx.fmtLoc(root["bar"]) == "tests/example.aiml:2: ");

    ctx.currentLine = -1;
    assert(ctx.fmtLoc() == "tests/example.aiml (in main): ");

    assert(mirror_delta_unit_tests() == 0);
    return 0;
}
