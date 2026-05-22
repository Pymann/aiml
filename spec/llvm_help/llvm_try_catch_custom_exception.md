Now it gets interesting 🔥
We're building a real LLVM IR try-catch block with:

invoke

landingpad

custom exception type

optional resume

And cleanly in the modern Itanium ABI style, as Clang does it on Linux.

Important upfront:

LLVM does not implement exceptions itself.
It uses the platform ABI. On Linux that is the Itanium C++ ABI with:

__cxa_allocate_exception

__cxa_throw

Personality function: __gxx_personality_v0

So we're building a small mini C++ exception system directly in IR.

🧠 Architecture
throw →
    __cxa_allocate_exception
    __cxa_throw
        ↓
invoke →
    landingpad →
        catch clause →
            handler
🎯 Example: Custom exception type

Suppose we want a custom exception:

struct MyException {
    int code;
};

In LLVM context we need:

struct type

typeinfo symbol

personality function

landingpad with catch clause

🏗️ Step 1: Set personality

Every function with exceptions needs:

Function* F = ...
F->setPersonalityFn(
    M->getOrInsertFunction(
        "__gxx_personality_v0",
        FunctionType::get(
            builder.getInt32Ty(), true)
    ).getCallee()
);

Without personality → no EH.

🏗️ Step 2: Throw a custom exception
Define the exception type
StructType* MyExType =
    StructType::create(context, "MyException");

MyExType->setBody(builder.getInt32Ty());
Get memory for the exception
FunctionCallee allocEx =
    M->getOrInsertFunction(
        "__cxa_allocate_exception",
        FunctionType::get(
            builder.getInt8PtrTy(),
            {builder.getInt64Ty()},
            false
        )
    );

Value* exSize =
    ConstantInt::get(builder.getInt64Ty(),
                     M->getDataLayout()
                      .getTypeAllocSize(MyExType));

Value* exMem =
    builder.CreateCall(allocEx, exSize);
Initialize the exception
Value* casted =
    builder.CreateBitCast(exMem,
                          MyExType->getPointerTo());

Value* codePtr =
    builder.CreateStructGEP(MyExType,
                            casted, 0);

builder.CreateStore(
    builder.getInt32(42),
    codePtr
);
Call throw
FunctionCallee throwFn =
    M->getOrInsertFunction(
        "__cxa_throw",
        FunctionType::get(
            builder.getVoidTy(),
            {
                builder.getInt8PtrTy(),
                builder.getInt8PtrTy(),
                builder.getInt8PtrTy()
            },
            false
        )
    );

Value* typeInfo =
    M->getOrInsertGlobal(
        "_ZTI11MyException",
        builder.getInt8PtrTy()
    );

builder.CreateCall(throwFn,
                   {exMem,
                    typeInfo,
                    ConstantPointerNull::get(
                        builder.getInt8PtrTy()
                    )});

builder.CreateUnreachable();

Now we have thrown.

🛑 Step 3: Try-catch block
invoke instead of call
builder.CreateInvoke(
    mayThrowFunc,
    normalBB,
    catchBB
);
Generate landingpad
builder.SetInsertPoint(catchBB);

LandingPadInst* lp =
    builder.CreateLandingPad(
        StructType::get(
            builder.getInt8PtrTy(),
            builder.getInt32Ty()
        ),
        1
    );

lp->setCleanup(false);
Add catch clause
lp->addClause(typeInfo);

This catches only MyException.

Handle the exception
Value* exObj =
    builder.CreateExtractValue(lp, 0);

// cast back
Value* myEx =
    builder.CreateBitCast(
        exObj,
        MyExType->getPointerTo()
    );

Value* code =
    builder.CreateLoad(
        builder.getInt32Ty(),
        builder.CreateStructGEP(MyExType,
                                myEx, 0)
    );

Now you have code.

🧹 Not handled?

If you don't want to catch:

builder.CreateResume(lp);

This corresponds to throw;.

📦 Minimal flow

Function with:

entry:
    invoke @may_throw
        to normal
        unwind catch

catch:
    %lp = landingpad {i8*, i32}
           catch typeinfo
    handler
🧠 Important notes

You need -fexceptions support

You need the correct personality

You must link against libstdc++

TypeInfo symbols must exist

In practice you let Clang compile a dummy exception once and inspect the IR.

🧩 Alternative: Custom VM exceptions

If you don't want the real C++ ABI, you can also:

Model exceptions as error codes

Use setjmp/longjmp

Build your own stack unwinder

Real C++ exceptions are heavy machinery.

🚀 If you want

I can build you:

a fully runnable LLVM C++ example

a minimalist custom exception without C++ ABI

or explain how ORC JIT handles EH

What do you want to go deeper on?
