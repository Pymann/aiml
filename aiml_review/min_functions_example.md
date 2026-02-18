#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

int main() {
    LLVMContext Context;
    Module *M = new Module("my_module", Context);
    IRBuilder<> Builder(Context);

    // int32 type
    Type *Int32Ty = Type::getInt32Ty(Context);

    // Function type: int(int, int)
    std::vector<Type*> Params(2, Int32Ty);
    FunctionType *FT = FunctionType::get(Int32Ty, Params, false);

    // Create function
    Function *F = Function::Create(
        FT,
        Function::ExternalLinkage,
        "add",
        M
    );

    // Name parameters
    auto Args = F->args().begin();
    Value *A = Args++;
    A->setName("a");
    Value *B = Args++;
    B->setName("b");

    // Create entry block
    BasicBlock *BB = BasicBlock::Create(Context, "entry", F);
    Builder.SetInsertPoint(BB);

    // a + b
    Value *Sum = Builder.CreateAdd(A, B, "sum");

    Builder.CreateRet(Sum);

    // Verify function
    verifyFunction(*F);

    // Print IR
    M->print(outs(), nullptr);

    delete M;
    return 0;
}
