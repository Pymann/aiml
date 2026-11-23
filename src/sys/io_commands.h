#pragma once
#include "syscall.h"
#include "../core/utils.h"

using Utils::resolveValue;

// Helper to get FILE* type (i8*)
static llvm::Type* getFilePtrTy(CompilerContext& ctx) {
    return ctx.builder.getInt8Ty()->getPointerTo();
}

class IoOpen : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // io.open(path, mode) -> FILE* (i8*)
        if (args.size() < 3) return nullptr;

        llvm::Value* path = resolveValue(ctx, args[1]);
        llvm::Value* mode = resolveValue(ctx, args[2]);

        llvm::Function* fopenFunc = ctx.module().getFunction("fopen");
        if (!fopenFunc) {
            std::vector<llvm::Type*> fopenArgs = { ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo() };
            llvm::FunctionType* fopenType = llvm::FunctionType::get(getFilePtrTy(ctx), fopenArgs, false);
            fopenFunc = llvm::Function::Create(fopenType, llvm::Function::ExternalLinkage, "fopen", ctx.module());
        }

        return ctx.builder.CreateCall(fopenFunc, {path, mode});
    }
};

class IoClose : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // io.close(handle)
        if (args.size() < 2) return nullptr;

        llvm::Value* handle = resolveValue(ctx, args[1]);

        llvm::Function* fcloseFunc = ctx.module().getFunction("fclose");
        if (!fcloseFunc) {
            std::vector<llvm::Type*> fcloseArgs = { getFilePtrTy(ctx) };
            llvm::FunctionType* fcloseType = llvm::FunctionType::get(ctx.builder.getInt32Ty(), fcloseArgs, false);
            fcloseFunc = llvm::Function::Create(fcloseType, llvm::Function::ExternalLinkage, "fclose", ctx.module());
        }

        return ctx.builder.CreateCall(fcloseFunc, {handle});
    }
};

class IoWrite : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // io.write(handle, text)
        if (args.size() < 3) return nullptr;

        llvm::Value* handle = nullptr;
        
        // Check for "io.stdout" / "io.stderr" string literals
        // We use fdopen(1, "w") or fdopen(2, "w") for these to get a FILE*
        // This is a bit inefficient (allocates FILE struct each time) but simple.
        
        bool isSpecialHandle = false;
        int fd = -1;
        if (args[1].IsScalar()) {
            std::string s = args[1].as<std::string>();
            if (s == "io.stdout") { isSpecialHandle = true; fd = 1; }
            else if (s == "io.stderr") { isSpecialHandle = true; fd = 2; }
        }

        if (isSpecialHandle) {
             llvm::Function* fdopenFunc = ctx.module().getFunction("fdopen");
             if (!fdopenFunc) {
                 std::vector<llvm::Type*> fdArgs = { ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo() };
                 llvm::FunctionType* fdType = llvm::FunctionType::get(getFilePtrTy(ctx), fdArgs, false);
                 fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module());
             }
             llvm::Value* mode = ctx.builder.CreateGlobalStringPtr("w");
             handle = ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), mode});
        } else {
             handle = resolveValue(ctx, args[1]);
        }

        llvm::Value* text = resolveValue(ctx, args[2]);

        // Calculate strlen of text
        llvm::Function* strlenFunc = ctx.module().getFunction("strlen");
        if (!strlenFunc) {
            std::vector<llvm::Type*> strArgs = { ctx.builder.getInt8Ty()->getPointerTo() };
            llvm::FunctionType* strType = llvm::FunctionType::get(ctx.getSizeType(), strArgs, false);
            strlenFunc = llvm::Function::Create(strType, llvm::Function::ExternalLinkage, "strlen", ctx.module());
        }
        llvm::Value* len = ctx.builder.CreateCall(strlenFunc, {text});

        // fwrite(ptr, size, nmemb, stream)
        llvm::Function* fwriteFunc = ctx.module().getFunction("fwrite");
        if (!fwriteFunc) {
            std::vector<llvm::Type*> fwArgs = { 
                ctx.builder.getInt8Ty()->getPointerTo(), 
                ctx.getSizeType(),
                ctx.getSizeType(),
                getFilePtrTy(ctx)
            };
            llvm::FunctionType* fwType = llvm::FunctionType::get(ctx.getSizeType(), fwArgs, false);
            fwriteFunc = llvm::Function::Create(fwType, llvm::Function::ExternalLinkage, "fwrite", ctx.module());
        }

        return ctx.builder.CreateCall(fwriteFunc, {text, ctx.getSizeConst(1), len, handle});
    }
};

class IoRead : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // io.read(handle, bytes) -> string
        // If handle is io.stdin, use fdopen(0, "r")
        if (args.size() < 3) return nullptr;

        llvm::Value* handle = nullptr;
         bool isSpecialHandle = false;
        int fd = -1;
        if (args[1].IsScalar()) {
            std::string s = args[1].as<std::string>();
            if (s == "io.stdin") { isSpecialHandle = true; fd = 0; }
        }

        if (isSpecialHandle) {
             llvm::Function* fdopenFunc = ctx.module().getFunction("fdopen");
             if (!fdopenFunc) {
                 std::vector<llvm::Type*> fdArgs = { ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo() };
                 llvm::FunctionType* fdType = llvm::FunctionType::get(getFilePtrTy(ctx), fdArgs, false);
                 fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module());
             }
             llvm::Value* mode = ctx.builder.CreateGlobalStringPtr("r");
             handle = ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), mode});
        } else {
             handle = resolveValue(ctx, args[1]);
        }

        llvm::Value* size = resolveValue(ctx, args[2]);
        
        // Allocate buffer: malloc(size + 1)
        llvm::Function* mallocFunc = ctx.module().getFunction("malloc");
        if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.getSizeType()}, false);
            mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", ctx.module());
        }

        size = ctx.toSizeType(size);
        llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.getSizeConst(1));
        llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});

        // fread(buffer, 1, size, handle)
         llvm::Function* freadFunc = ctx.module().getFunction("fread");
        if (!freadFunc) {
            std::vector<llvm::Type*> frArgs = { 
                ctx.builder.getInt8Ty()->getPointerTo(), 
                ctx.getSizeType(),
                ctx.getSizeType(),
                getFilePtrTy(ctx)
            };
            llvm::FunctionType* frType = llvm::FunctionType::get(ctx.getSizeType(), frArgs, false);
            freadFunc = llvm::Function::Create(frType, llvm::Function::ExternalLinkage, "fread", ctx.module());
        }
        
        llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.getSizeConst(1), size, handle});
        
        // Null terminate: buffer[readBytes] = 0
        llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
        ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);

        return buffer;
    }
};

class IoReadAll : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // io.read_all(handle) -> string
        // Logic: fseek(END), ftell, rewind, malloc, fread.
        if (args.size() < 2) return nullptr;
        llvm::Value* handle = resolveValue(ctx, args[1]);

        llvm::Type* filePtrTy = getFilePtrTy(ctx);
        // fseek/ftell take `long`, malloc/fread `size_t` — both are
        // pointer-wide: i64 native, i32 on wasm32.
        llvm::Type* szTy = ctx.getSizeType();
        llvm::Type* i32Ty = ctx.builder.getInt32Ty();

        // fseek
        llvm::Function* fseekFunc = ctx.module().getFunction("fseek");
        if (!fseekFunc) {
             fseekFunc = llvm::Function::Create(llvm::FunctionType::get(i32Ty, {filePtrTy, szTy, i32Ty}, false), llvm::Function::ExternalLinkage, "fseek", ctx.module());
        }
        // ftell
        llvm::Function* ftellFunc = ctx.module().getFunction("ftell");
        if (!ftellFunc) {
             ftellFunc = llvm::Function::Create(llvm::FunctionType::get(szTy, {filePtrTy}, false), llvm::Function::ExternalLinkage, "ftell", ctx.module());
        }
        // rewind
        llvm::Function* rewindFunc = ctx.module().getFunction("rewind");
        if (!rewindFunc) {
             rewindFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {filePtrTy}, false), llvm::Function::ExternalLinkage, "rewind", ctx.module());
        }

        // fseek(f, 0, SEEK_END (2))
        ctx.builder.CreateCall(fseekFunc, {handle, ctx.getSizeConst(0), ctx.builder.getInt32(2)});
        
        // size = ftell(f)
        llvm::Value* size = ctx.builder.CreateCall(ftellFunc, {handle});
        
        // rewind(f)
        ctx.builder.CreateCall(rewindFunc, {handle});

        // malloc(size + 1)
        llvm::Function* mallocFunc = ctx.module().getFunction("malloc");
         if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {szTy}, false);
            mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", ctx.module());
        }
        llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.getSizeConst(1));
        llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});

        // fread(buffer, 1, size, f)
        llvm::Function* freadFunc = ctx.module().getFunction("fread");
         if (!freadFunc) {
            std::vector<llvm::Type*> frArgs = { ctx.builder.getInt8Ty()->getPointerTo(), szTy, szTy, filePtrTy };
            llvm::FunctionType* frType = llvm::FunctionType::get(szTy, frArgs, false);
            freadFunc = llvm::Function::Create(frType, llvm::Function::ExternalLinkage, "fread", ctx.module());
        }
        llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.getSizeConst(1), size, handle});
        
        // buffer[readBytes] = 0
         llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
        ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);

        return buffer;
    }
};
