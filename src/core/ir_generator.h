#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace AIML {

    // Opaque wrappers to isolate LLVM from the rest of the compiler
    struct IRValue { virtual ~IRValue() = default; };
    struct IRType { virtual ~IRType() = default; };
    struct IRBlock { virtual ~IRBlock() = default; };

    class IRGenerator {
    public:
         virtual ~IRGenerator() = default;
         
         // --- Types ---
         virtual IRType* getInt32Ty() = 0;
         virtual IRType* getInt64Ty() = 0;
         virtual IRType* getInt8Ty() = 0;
         virtual IRType* getVoidTy() = 0;
         virtual IRType* getPointerTy() = 0;
         virtual IRType* resolveType(const std::string& typeName) = 0;

         // --- Constants ---
         virtual IRValue* getInt32(int value) = 0;
         virtual IRValue* getInt64(uint64_t value) = 0;
         virtual IRValue* createGlobalStringPtr(const std::string& str) = 0;

         // --- Instructions ---
         virtual IRValue* createAdd(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createSub(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createMul(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createSDiv(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createSRem(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;

         // Logic
         virtual IRValue* createICmpEQ(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createICmpNE(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createICmpSLT(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createICmpSGT(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createICmpSLE(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createICmpSGE(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;

         virtual IRValue* createAnd(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createOr(IRValue* lhs, IRValue* rhs, const std::string& name = "") = 0;
         virtual IRValue* createNot(IRValue* val, const std::string& name = "") = 0;

         // Memory
         virtual IRValue* createAlloca(IRType* type, const std::string& name = "") = 0;
         virtual IRValue* createLoad(IRType* type, IRValue* ptr, const std::string& name = "") = 0;
         virtual IRValue* createStore(IRValue* val, IRValue* ptr) = 0;
         virtual IRValue* createGEP(IRType* type, IRValue* ptr, const std::vector<IRValue*>& idxList, const std::string& name = "") = 0;
         virtual IRValue* createStructGEP(IRType* type, IRValue* ptr, unsigned idx, const std::string& name = "") = 0;

         // Control Flow
         virtual IRValue* createCall(const std::string& funcName, const std::vector<IRValue*>& args, const std::string& name = "") = 0;
         virtual IRValue* createCall(IRValue* func, const std::vector<IRValue*>& args, const std::string& name = "") = 0;
         virtual void createBr(IRBlock* dest) = 0;
         virtual void createCondBr(IRValue* cond, IRBlock* trueBlock, IRBlock* falseBlock) = 0;
         virtual void createRet(IRValue* val = nullptr) = 0;

         // Casting
         virtual IRValue* createBitCast(IRValue* val, IRType* destTy, const std::string& name = "") = 0;
         virtual IRValue* createIntCast(IRValue* val, IRType* destTy, bool isSigned, const std::string& name = "") = 0;
         virtual IRValue* createZExt(IRValue* val, IRType* destTy, const std::string& name = "") = 0;
         virtual IRValue* createSExt(IRValue* val, IRType* destTy, const std::string& name = "") = 0;

         // Blocks
         virtual IRBlock* createBasicBlock(const std::string& name) = 0;
         virtual void setInsertPoint(IRBlock* block) = 0;
         virtual IRBlock* getInsertBlock() = 0;
    };

} // namespace AIML
