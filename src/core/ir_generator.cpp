#include "ir_generator.h"
#include "context.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace AIML {

    // Concrete wrapper structures
    struct LLVMIRValue : public IRValue {
        llvm::Value* val;
        LLVMIRValue(llvm::Value* v) : val(v) {}
    };

    struct LLVMIRType : public IRType {
        llvm::Type* ty;
        LLVMIRType(llvm::Type* t) : ty(t) {}
    };

    struct LLVMIRBlock : public IRBlock {
        llvm::BasicBlock* bb;
        LLVMIRBlock(llvm::BasicBlock* b) : bb(b) {}
    };

    class LLVMGenerator : public IRGenerator {
        CompilerContext& ctx;
    
        llvm::Value* unwrap(IRValue* v) { return v ? static_cast<LLVMIRValue*>(v)->val : nullptr; }
        llvm::Type* unwrap(IRType* t) { return t ? static_cast<LLVMIRType*>(t)->ty : nullptr; }
        llvm::BasicBlock* unwrap(IRBlock* b) { return b ? static_cast<LLVMIRBlock*>(b)->bb : nullptr; }

        IRValue* wrap(llvm::Value* v) { return v ? new LLVMIRValue(v) : nullptr; }
        IRType* wrap(llvm::Type* t) { return t ? new LLVMIRType(t) : nullptr; }
        IRBlock* wrap(llvm::BasicBlock* b) { return b ? new LLVMIRBlock(b) : nullptr; }

    public:
        LLVMGenerator(CompilerContext& c) : ctx(c) {}

        IRType* getInt32Ty() override { return wrap(ctx.builder.getInt32Ty()); }
        IRType* getInt64Ty() override { return wrap(ctx.builder.getInt64Ty()); }
        IRType* getInt8Ty() override { return wrap(ctx.builder.getInt8Ty()); }
        IRType* getVoidTy() override { return wrap(ctx.builder.getVoidTy()); }
        IRType* getPointerTy() override { return wrap(llvm::PointerType::getUnqual(ctx.context)); }
        IRType* resolveType(const std::string& typeName) override { return wrap(ctx.getLLVMType(typeName)); }

        IRValue* getInt32(int value) override { return wrap(ctx.builder.getInt32(value)); }
        IRValue* getInt64(uint64_t value) override { return wrap(ctx.builder.getInt64(value)); }
        IRValue* createGlobalStringPtr(const std::string& str) override { return wrap(ctx.builder.CreateGlobalStringPtr(str)); }

        IRValue* createAdd(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateAdd(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createSub(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateSub(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createMul(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateMul(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createSDiv(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateSDiv(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createSRem(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateSRem(unwrap(lhs), unwrap(rhs), name)); }

        IRValue* createICmpEQ(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpEQ(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createICmpNE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpNE(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createICmpSLT(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpSLT(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createICmpSGT(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpSGT(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createICmpSLE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpSLE(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createICmpSGE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateICmpSGE(unwrap(lhs), unwrap(rhs), name)); }

        IRValue* createAnd(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateAnd(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createOr(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrap(ctx.builder.CreateOr(unwrap(lhs), unwrap(rhs), name)); }
        IRValue* createNot(IRValue* val, const std::string& name) override { return wrap(ctx.builder.CreateNot(unwrap(val), name)); }

        IRValue* createAlloca(IRType* type, const std::string& name) override { return wrap(ctx.builder.CreateAlloca(unwrap(type), nullptr, name)); }
        IRValue* createLoad(IRType* type, IRValue* ptr, const std::string& name) override { return wrap(ctx.builder.CreateLoad(unwrap(type), unwrap(ptr), name)); }
        IRValue* createStore(IRValue* val, IRValue* ptr) override { return wrap(ctx.builder.CreateStore(unwrap(val), unwrap(ptr))); }
        
        IRValue* createGEP(IRType* type, IRValue* ptr, const std::vector<IRValue*>& idxList, const std::string& name) override {
            std::vector<llvm::Value*> rawIdx;
            for (auto* i : idxList) rawIdx.push_back(unwrap(i));
            return wrap(ctx.builder.CreateInBoundsGEP(unwrap(type), unwrap(ptr), rawIdx, name));
        }
        IRValue* createStructGEP(IRType* type, IRValue* ptr, unsigned idx, const std::string& name) override {
            return wrap(ctx.builder.CreateStructGEP(unwrap(type), unwrap(ptr), idx, name));
        }

        IRValue* createCall(const std::string& funcName, const std::vector<IRValue*>& args, const std::string& name) override {
            llvm::Function* func = ctx.module.getFunction(funcName);
            if (!func) return nullptr;
            std::vector<llvm::Value*> rawArgs;
            for (auto* a : args) rawArgs.push_back(unwrap(a));
            return wrap(ctx.builder.CreateCall(func, rawArgs, name));
        }

        IRValue* createCall(IRValue* func, const std::vector<IRValue*>& args, const std::string& name) override {
            // Function pointer call via llvm::FunctionCallee or casted value
            // Since this requires a type, we might need a richer signature in production
            return nullptr; // Stub for now
        }

        void createBr(IRBlock* dest) override { ctx.builder.CreateBr(unwrap(dest)); }
        void createCondBr(IRValue* cond, IRBlock* trueBlock, IRBlock* falseBlock) override {
            ctx.builder.CreateCondBr(unwrap(cond), unwrap(trueBlock), unwrap(falseBlock));
        }
        void createRet(IRValue* val) override {
            if (val) ctx.builder.CreateRet(unwrap(val));
            else ctx.builder.CreateRetVoid();
        }

        IRValue* createBitCast(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrap(ctx.builder.CreateBitCast(unwrap(val), unwrap(destTy), name));
        }
        IRValue* createIntCast(IRValue* val, IRType* destTy, bool isSigned, const std::string& name) override {
            return wrap(ctx.builder.CreateIntCast(unwrap(val), unwrap(destTy), isSigned, name));
        }
        IRValue* createZExt(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrap(ctx.builder.CreateZExt(unwrap(val), unwrap(destTy), name));
        }
        IRValue* createSExt(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrap(ctx.builder.CreateSExt(unwrap(val), unwrap(destTy), name));
        }

        IRBlock* createBasicBlock(const std::string& name) override {
            llvm::Function* F = ctx.builder.GetInsertBlock()->getParent();
            return wrap(llvm::BasicBlock::Create(ctx.context, name, F));
        }
        void setInsertPoint(IRBlock* block) override { ctx.builder.SetInsertPoint(unwrap(block)); }
        IRBlock* getInsertBlock() override { return wrap(ctx.builder.GetInsertBlock()); }
    };

} // namespace AIML
