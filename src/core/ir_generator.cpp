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
        void* getRawPtr() override { return val; }
        IRType* getType() override; // Implemented below after LLVMGenerator / wrap
    };

    struct LLVMIRType : public IRType {
        llvm::Type* ty;
        LLVMIRType(llvm::Type* t) : ty(t) {}
        void* getRawPtr() override { return ty; }
        bool isPointer() override { return ty && ty->isPointerTy(); }
        bool isInteger() override { return ty && ty->isIntegerTy(); }
        bool isStruct() override { return ty && ty->isStructTy(); }
    };

    struct LLVMIRBlock : public IRBlock {
        llvm::BasicBlock* bb;
        LLVMIRBlock(llvm::BasicBlock* b) : bb(b) {}
        void* getRawPtr() override { return bb; }
    };

    class LLVMGenerator : public IRGenerator {
        CompilerContext& ctx;
    
        llvm::Value* unwrapLocal(IRValue* v) { return v ? static_cast<LLVMIRValue*>(v)->val : nullptr; }
        llvm::Type* unwrapLocal(IRType* t) { return t ? static_cast<LLVMIRType*>(t)->ty : nullptr; }
        llvm::BasicBlock* unwrapLocal(IRBlock* b) { return b ? static_cast<LLVMIRBlock*>(b)->bb : nullptr; }

        IRValue* wrapLocal(llvm::Value* v) { return v ? new LLVMIRValue(v) : nullptr; }
        IRType* wrapLocal(llvm::Type* t) { return t ? new LLVMIRType(t) : nullptr; }
        IRBlock* wrapLocal(llvm::BasicBlock* b) { return b ? new LLVMIRBlock(b) : nullptr; }
        
    public:
        // Migration Helpers Interface Implementation
        IRValue* wrap(void* llvmValue) override { return wrapLocal(static_cast<llvm::Value*>(llvmValue)); }
        IRType* wrapType(void* llvmType) override { return wrapLocal(static_cast<llvm::Type*>(llvmType)); }
        IRBlock* wrapBlock(void* llvmBlock) override { return wrapLocal(static_cast<llvm::BasicBlock*>(llvmBlock)); }
        void* unwrap(IRValue* val) override { return unwrapLocal(val); }
        void* unwrapType(IRType* type) override { return unwrapLocal(type); }
        void* unwrapBlock(IRBlock* block) override { return unwrapLocal(block); }

        LLVMGenerator(CompilerContext& c) : ctx(c) {}

        IRType* getInt32Ty() override { return wrapLocal(ctx.builder.getInt32Ty()); }
        IRType* getInt64Ty() override { return wrapLocal(ctx.builder.getInt64Ty()); }
        IRType* getInt8Ty() override { return wrapLocal(ctx.builder.getInt8Ty()); }
        IRType* getVoidTy() override { return wrapLocal(ctx.builder.getVoidTy()); }
        IRType* getPointerTy() override { return wrapLocal(llvm::PointerType::getUnqual(ctx.context)); }
        IRType* resolveType(const std::string& typeName) override { return wrapLocal(ctx.getLLVMType(typeName)); }

        IRValue* getInt32(int value) override { return wrapLocal(ctx.builder.getInt32(value)); }
        IRValue* getInt64(uint64_t value) override { return wrapLocal(ctx.builder.getInt64(value)); }
        IRValue* createGlobalStringPtr(const std::string& str) override { return wrapLocal(ctx.builder.CreateGlobalStringPtr(str)); }

        IRValue* createAdd(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateAdd(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createSub(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateSub(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createMul(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateMul(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createSDiv(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateSDiv(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createSRem(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateSRem(unwrapLocal(lhs), unwrapLocal(rhs), name)); }

        IRValue* createICmpEQ(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpEQ(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createICmpNE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpNE(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createICmpSLT(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpSLT(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createICmpSGT(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpSGT(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createICmpSLE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpSLE(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createICmpSGE(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateICmpSGE(unwrapLocal(lhs), unwrapLocal(rhs), name)); }

        IRValue* createAnd(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateAnd(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createOr(IRValue* lhs, IRValue* rhs, const std::string& name) override { return wrapLocal(ctx.builder.CreateOr(unwrapLocal(lhs), unwrapLocal(rhs), name)); }
        IRValue* createNot(IRValue* val, const std::string& name) override { return wrapLocal(ctx.builder.CreateNot(unwrapLocal(val), name)); }

        IRValue* createAlloca(IRType* type, const std::string& name) override { return wrapLocal(ctx.builder.CreateAlloca(unwrapLocal(type), nullptr, name)); }
        IRValue* createLoad(IRType* type, IRValue* ptr, const std::string& name) override { return wrapLocal(ctx.builder.CreateLoad(unwrapLocal(type), unwrapLocal(ptr), name)); }
        IRValue* createStore(IRValue* val, IRValue* ptr) override { return wrapLocal(ctx.builder.CreateStore(unwrapLocal(val), unwrapLocal(ptr))); }
        
        IRValue* createGEP(IRType* type, IRValue* ptr, const std::vector<IRValue*>& idxList, const std::string& name) override {
            std::vector<llvm::Value*> rawIdx;
            for (auto* i : idxList) rawIdx.push_back(unwrapLocal(i));
            return wrapLocal(ctx.builder.CreateInBoundsGEP(unwrapLocal(type), unwrapLocal(ptr), rawIdx, name));
        }
        IRValue* createStructGEP(IRType* type, IRValue* ptr, unsigned idx, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateStructGEP(unwrapLocal(type), unwrapLocal(ptr), idx, name));
        }

        IRValue* createFunction(const std::string& name, IRType* retTy, const std::vector<IRType*>& argTys) override {
            std::vector<llvm::Type*> rawArgTys;
            for (auto* t : argTys) rawArgTys.push_back(unwrapLocal(t));
            
            llvm::FunctionType* funcType = llvm::FunctionType::get(unwrapLocal(retTy), rawArgTys, false);
            llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, ctx.module);
            return wrapLocal(func);
        }

        IRValue* getFunctionArg(IRValue* func, unsigned idx) override {
            llvm::Function* F = llvm::cast<llvm::Function>(unwrapLocal(func));
            if (idx >= F->arg_size()) return nullptr;
            return wrapLocal(F->getArg(idx));
        }

        void setPersonalityFunction(IRValue* func, IRValue* persFn) override {
            llvm::Function* F = llvm::cast<llvm::Function>(unwrapLocal(func));
            F->setPersonalityFn(llvm::cast<llvm::Constant>(unwrapLocal(persFn)));
        }

        IRValue* createCall(const std::string& funcName, const std::vector<IRValue*>& args, const std::string& name) override {
            llvm::Function* func = ctx.module.getFunction(funcName);
            if (!func) return nullptr;
            std::vector<llvm::Value*> rawArgs;
            for (auto* a : args) rawArgs.push_back(unwrapLocal(a));
            return wrapLocal(ctx.builder.CreateCall(func, rawArgs, name));
        }

        IRValue* createCall(IRValue* func, const std::vector<IRValue*>& args, const std::string& name) override {
            // Function pointer call via llvm::FunctionCallee or casted value
            // Since this requires a type, we might need a richer signature in production
            return nullptr; // Stub for now
        }

        IRValue* createInvoke(const std::string& funcName, IRBlock* normalBlock, IRBlock* unwindBlock, const std::vector<IRValue*>& args, const std::string& name) override {
            llvm::Function* func = ctx.module.getFunction(funcName);
            if (!func) return nullptr;
            std::vector<llvm::Value*> rawArgs;
            for (auto* a : args) rawArgs.push_back(unwrapLocal(a));
            return wrapLocal(ctx.builder.CreateInvoke(func, unwrapLocal(normalBlock), unwrapLocal(unwindBlock), rawArgs, name));
        }

        void createBr(IRBlock* dest) override { ctx.builder.CreateBr(unwrapLocal(dest)); }
        void createCondBr(IRValue* cond, IRBlock* trueBlock, IRBlock* falseBlock) override {
            ctx.builder.CreateCondBr(unwrapLocal(cond), unwrapLocal(trueBlock), unwrapLocal(falseBlock));
        }
        void createRet(IRValue* val) override {
            if (val) ctx.builder.CreateRet(unwrapLocal(val));
            else ctx.builder.CreateRetVoid();
        }
        void createUnreachable() override { ctx.builder.CreateUnreachable(); }
        
        IRValue* createLandingPad(IRType* type, unsigned numClauses, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateLandingPad(unwrapLocal(type), numClauses, name));
        }
        void addClauseToLandingPad(IRValue* landingPad, IRValue* clauseVal) override {
            if (auto* lp = llvm::dyn_cast<llvm::LandingPadInst>(unwrapLocal(landingPad))) {
                lp->addClause(llvm::cast<llvm::Constant>(unwrapLocal(clauseVal)));
            }
        }
        void setCleanupToLandingPad(IRValue* landingPad, bool isCleanup) override {
            if (auto* lp = llvm::dyn_cast<llvm::LandingPadInst>(unwrapLocal(landingPad))) {
                lp->setCleanup(isCleanup);
            }
        }
        IRValue* createExtractValue(IRValue* agg, unsigned idx, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateExtractValue(unwrapLocal(agg), {idx}, name));
        }
        bool hasPersonalityFunction() override {
            return ctx.builder.GetInsertBlock()->getParent()->hasPersonalityFn();
        }
        void setPersonalityFunction(IRValue* persFn) override {
            ctx.builder.GetInsertBlock()->getParent()->setPersonalityFn(llvm::cast<llvm::Constant>(unwrapLocal(persFn)));
        }
        IRValue* getPersonalityFunction() override {
             return wrapLocal(ctx.getPersonalityFunction().getCallee());
        }

        IRValue* createBitCast(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateBitCast(unwrapLocal(val), unwrapLocal(destTy), name));
        }
        IRValue* createIntCast(IRValue* val, IRType* destTy, bool isSigned, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateIntCast(unwrapLocal(val), unwrapLocal(destTy), isSigned, name));
        }
        IRValue* createZExt(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateZExt(unwrapLocal(val), unwrapLocal(destTy), name));
        }
        IRValue* createSExt(IRValue* val, IRType* destTy, const std::string& name) override {
            return wrapLocal(ctx.builder.CreateSExt(unwrapLocal(val), unwrapLocal(destTy), name));
        }

        IRBlock* createBasicBlock(const std::string& name) override {
            llvm::Function* F = ctx.builder.GetInsertBlock()->getParent();
            return wrapLocal(llvm::BasicBlock::Create(ctx.context, name, F));
        }
        void setInsertPoint(IRBlock* block) override { ctx.builder.SetInsertPoint(unwrapLocal(block)); }
        IRBlock* getInsertBlock() override { return wrapLocal(ctx.builder.GetInsertBlock()); }
    };

    inline IRType* LLVMIRValue::getType() {
        return val ? new LLVMIRType(val->getType()) : nullptr;
    }

    std::unique_ptr<IRGenerator> createLLVMGenerator(CompilerContext& ctx) {
        return std::make_unique<LLVMGenerator>(ctx);
    }

} // namespace AIML
