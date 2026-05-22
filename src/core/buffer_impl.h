#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>

namespace BufferImpl {

    // Struct Layout: { i32 size, i32 capacity, i32 readIndex, i32 writeIndex, i8* data }
    // Indices: 0=size, 1=capacity, 2=read, 3=write, 4=data

    inline void alloc(CompilerContext& ctx, llvm::Value* bufPtr, llvm::Value* sizeVal) {
        // Initialize indices
        llvm::Value* zero = ctx.builder.getInt32(0);
        
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), bufPtr, 0)); // size
        ctx.builder.CreateStore(sizeVal, ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), bufPtr, 1)); // capacity
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), bufPtr, 2)); // read
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), bufPtr, 3)); // write
        
        // Malloc data — use target's size_t (i64 on x86_64, i32 on wasm32) so
        // the call signature matches the target libc. ZExtOrTrunc handles both
        // widths from sizeVal (i32).
        llvm::Type* sz = ctx.getSizeType();
        llvm::Function* mallocFunc = ctx.getFunction("malloc", ctx.builder.getInt8Ty()->getPointerTo(), {sz});
        llvm::Value* sizeArg = ctx.builder.CreateZExtOrTrunc(sizeVal, sz);
        llvm::Value* dataPtr = ctx.builder.CreateCall(mallocFunc, {sizeArg});

        ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), bufPtr, 4));
    }

    inline void write(CompilerContext& ctx, llvm::Value* bufPtr, llvm::Value* val, llvm::Type* type) {
        if (!bufPtr) {
            LOG_ERROR("Buffer pointer is null in write operation");
            return;
        }
        if (!val) {
            LOG_ERROR("Value is null in write operation for buffer");
            return;
        }
        
        llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
        
        // Check if it is a Class
        bool isClass = false;
        if (type->isStructTy()) {
             llvm::StructType* st = llvm::cast<llvm::StructType>(type);
             if (st->hasName() && ctx.classes.count(st->getName().str())) {
                  isClass = true;
                  // WRITE VERSION (u64)
                  uint64_t ver = ctx.classes[st->getName().str()].version;
                  
                  // We need to write u64. Recursive call to write? 
                  // or just duplicate primitive write logic for u64?
                  // Recursive is cleaner but beware infinite loop?
                  // write(ctx, bufPtr, const_u64, u64Ty) -> u64 is primitive, so no loop.
                  llvm::Value* verVal = ctx.builder.getInt64(ver);
                  write(ctx, bufPtr, verVal, ctx.builder.getInt64Ty());
             }
        }

        // Get Write Index
        llvm::Value* writeIdxPtr = ctx.builder.CreateStructGEP(bufType, bufPtr, 3);
        llvm::Value* writeIdx = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), writeIdxPtr);
        
        // Calculate Size of Type
        const llvm::DataLayout& dl = ctx.module.getDataLayout();
        uint64_t typeSize = dl.getTypeAllocSize(type);
        llvm::Value* typeSizeVal = ctx.builder.getInt32(typeSize);
        
        // Get Data Pointer
        llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, bufPtr, 4);
        llvm::Value* dataPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), dataPtrPtr);
        
        // Offset Logic
        llvm::Value* currentPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, writeIdx);
        
        // Bitcast Data Pointer to Type*
        llvm::Value* castPtr = ctx.builder.CreateBitCast(currentPtr, type->getPointerTo());
        
        // Store Value
        if (type->isStructTy()) {
             ctx.builder.CreateStore(val, castPtr);
        } else {
             ctx.builder.CreateStore(val, castPtr);
        }
        
        // Update Write Index
        llvm::Value* newWriteIdx = ctx.builder.CreateAdd(writeIdx, typeSizeVal);
        ctx.builder.CreateStore(newWriteIdx, writeIdxPtr);
        
        // Update Size if needed (max(size, writeIdx))
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(bufType, bufPtr, 0);
        llvm::Value* currentSize = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr);
        
        llvm::Value* isLarger = ctx.builder.CreateICmpSGT(newWriteIdx, currentSize);
        llvm::Value* maxSize = ctx.builder.CreateSelect(isLarger, newWriteIdx, currentSize);
        ctx.builder.CreateStore(maxSize, sizePtr);
    }
    
    inline llvm::Value* read(CompilerContext& ctx, llvm::Value* bufPtr, llvm::Type* type) {
        if (!bufPtr) {
            LOG_ERROR("Buffer pointer is null in read operation");
            return llvm::Constant::getNullValue(type);
        }
        
        // If Class, Consume Version Header first
        if (type->isStructTy()) {
             llvm::StructType* st = llvm::cast<llvm::StructType>(type);
             if (st->hasName() && ctx.classes.count(st->getName().str())) {
                  read(ctx, bufPtr, ctx.builder.getInt64Ty());
             }
        }

        llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
        
        // Get Read Index
        llvm::Value* readIdxPtr = ctx.builder.CreateStructGEP(bufType, bufPtr, 2);
        llvm::Value* readIdx = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), readIdxPtr);
        // ... (rest of read logic)
        
        // Get Data Pointer
        llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, bufPtr, 4);
        llvm::Value* dataPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), dataPtrPtr);
        
        // Offset
        llvm::Value* currentPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, readIdx);
        
        // Bitcast and Load
        llvm::Value* castPtr = ctx.builder.CreateBitCast(currentPtr, type->getPointerTo());
        
        llvm::Value* val = nullptr;
        if (type->isPointerTy()) {
             // For pointers (like strings), we stored the pointer value itself (i8*)
             // castPtr is i8** (pointer to pointer-type slot in buffer)
             // So loading from castPtr gives us the i8* value back.
             val = ctx.builder.CreateLoad(type, castPtr); 
        } else {
             val = ctx.builder.CreateLoad(type, castPtr);
        }
        
        // Update Read Index
        const llvm::DataLayout& dl = ctx.module.getDataLayout();
        uint64_t typeSize = dl.getTypeAllocSize(type);
        llvm::Value* newReadIdx = ctx.builder.CreateAdd(readIdx, ctx.builder.getInt32(typeSize));
        ctx.builder.CreateStore(newReadIdx, readIdxPtr);
        
        return val;
    }

    inline void seek(CompilerContext& ctx, llvm::Value* bufPtr, llvm::Value* pos, bool isRead) {
        llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
        int index = isRead ? 2 : 3;
        llvm::Value* idxPtr = ctx.builder.CreateStructGEP(bufType, bufPtr, index);
        ctx.builder.CreateStore(pos, idxPtr);
    }
    
    inline void reset(CompilerContext& ctx, llvm::Value* bufPtr) {
        llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
        llvm::Value* zero = ctx.builder.getInt32(0);
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(bufType, bufPtr, 0)); // size
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(bufType, bufPtr, 2)); // read
        ctx.builder.CreateStore(zero, ctx.builder.CreateStructGEP(bufType, bufPtr, 3)); // write
    }

    inline llvm::Value* read_versioned(CompilerContext& ctx, llvm::Value* bufPtr, llvm::Type* type, std::function<llvm::Value*(CompilerContext&, std::shared_ptr<AIML::AST::Expr>)> evaluator, llvm::Value* explicitVersion = nullptr) {
        if (!bufPtr) {
            LOG_ERROR("Buffer pointer is null in read_versioned operation");
            return llvm::Constant::getNullValue(type);
        }
        
        // Only supports Structs (Classes)
        if (!type->isStructTy()) {
             return read(ctx, bufPtr, type);
        }
        
        llvm::StructType* structTy = llvm::cast<llvm::StructType>(type);
        std::string className;
        if (structTy->hasName()) className = structTy->getName().str();
        
        if (className.empty() || !ctx.classes.count(className)) {
             return read(ctx, bufPtr, type);
        }
        
        // 1. Determine Data Version
        llvm::Value* dataVersion = explicitVersion;
        if (!dataVersion) {
            // Read from Buffer!
            dataVersion = read(ctx, bufPtr, ctx.builder.getInt64Ty());
        } else {
             // If explicit version provided, we STILL might need to skip the header in buffer if it exists?
             // Standard: "buffer.write" puts header.
             // If user says "read_versioned(Type, 1)", they imply data is v1.
             // Does that data HAVE a header? 
             // If written with `buffer.write`, yes.
             // If crafted manually, maybe not.
             // Let's assume standard flow: consume header anyway?
             // Or assume explicitVersion overrides header reading?
             // If I have logic relying on `read` internally, `read` consumes header.
             // We need `read_primitive` to bypass header check?
             // Current `read` checks `isStructTy`. `u64` is integer, so it won't consume header.
             // So `read(u64)` is safe.
             
             // If explicitVersion is given, we assume the user knows what they are doing.
             // BUT if the buffer HAS a header, we must skip it.
             // Let's look at use case:
             // Migration: read old buffer. Old buffer was written with `buffer.write`. So it HAS header.
             // So we should ALWAYS read the header to advance pointer?
             // But if we override it, we ignore the read value.
             llvm::Value* fileVer = read(ctx, bufPtr, ctx.builder.getInt64Ty());
             // if explicitVersion, use it. Else use fileVer.
             
             // Wait, if explicitVersion provided, maybe we shouldn't read?
             // "read_versioned(Type, ver)" usually implies "Treat data AS version X".
             // Maybe we should just stick to:
             // read_versioned(Type) -> Reads header, uses it.
             // read_versioned(Type, ver) -> Uses 'ver'. Does NOT read header? Or Reads and ignores?
             // If `write` blindly writes header, then `read` MUST blindly consume it to stay in sync.
             // So YES, always consume header for Classes.
        }
        
        auto& classInfo = ctx.classes[className];
        llvm::Value* alloca = ctx.createEntryAlloca(type, "read_obj");
        
        // Initialize with Defaults
        for(auto& [memName, member] : classInfo.members) {
             llvm::Value* initVal = nullptr;
             if (member.initValue != nullptr) {
                 initVal = evaluator(ctx, member.initValue);
             } else {
                 initVal = llvm::Constant::getNullValue(member.type);
             }
             if (initVal) {
                 llvm::Value* memPtr = ctx.builder.CreateStructGEP(type, alloca, member.index);
                 ctx.builder.CreateStore(initVal, memPtr);
             }
        }
        
        std::vector<const CompilerContext::StructMember*> sortedMembers;
        for(auto& [name, member] : classInfo.members) {
             sortedMembers.push_back(&member);
        }
        std::sort(sortedMembers.begin(), sortedMembers.end(), [](const auto* a, const auto* b){
             return a->index < b->index;
        });
        
        for (const auto* member : sortedMembers) {
             // Verify version
             llvm::Function* func = ctx.builder.GetInsertBlock()->getParent();
             llvm::BasicBlock* readBlock = llvm::BasicBlock::Create(ctx.context, "ver_read", func);
             llvm::BasicBlock* skipBlock = llvm::BasicBlock::Create(ctx.context, "ver_skip", func);
             
             llvm::Value* dataVer64 = dataVersion;
             if (dataVersion->getType()->isIntegerTy(32)) {
                  dataVer64 = ctx.builder.CreateZExt(dataVersion, ctx.builder.getInt64Ty(), "ver_ext");
             }
             
             llvm::Value* verReq = ctx.builder.getInt64(member->version);
             llvm::Value* cond = ctx.builder.CreateICmpUGE(dataVer64, verReq); 
             
             ctx.builder.CreateCondBr(cond, readBlock, skipBlock);
             
             ctx.builder.SetInsertPoint(readBlock);
             // RECURSIVE READ?
             // member->type could be a Struct.
             // If it is a struct, read(member->type) will consume ITS header.
             // This is correct behavior for nested objects!
             
             llvm::Value* val = read(ctx, bufPtr, member->type);
             if (!val) {
                 LOG_ERROR("Failed to read versioned value from buffer for member index: " + std::to_string(member->index));
                 val = llvm::Constant::getNullValue(member->type);
             }
             llvm::Value* memPtr = ctx.builder.CreateStructGEP(type, alloca, member->index);
             ctx.builder.CreateStore(val, memPtr);
             ctx.builder.CreateBr(skipBlock);
             
             ctx.builder.SetInsertPoint(skipBlock);
        }
        
        return ctx.builder.CreateLoad(type, alloca, "read_res");
    }

}
