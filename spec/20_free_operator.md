LLVM only knows:

Allocate memory

Free memory

And the fate in between

So we build free like a grown-up compiler.

🧱 Basic Idea

C code:

free(ptr);


LLVM IR:

declare void @free(ptr)

call void @free(ptr %something)


That's all there is to it. Really. No ritual, no black magic.

🛠 Step 1: Declare free (if not already present)

In LLVM 18 with opaque pointers:

LLVMContext Context;
Module M("MyModule", Context);
IRBuilder<> Builder(Context);

// void free(ptr)
FunctionType *FreeTy =
    FunctionType::get(Type::getVoidTy(Context),
                      {PointerType::getUnqual(Context)},
                      false);

Function *FreeFunc =
    M.getFunction("free");

if (!FreeFunc) {
    FreeFunc = Function::Create(
        FreeTy,
        Function::ExternalLinkage,
        "free",
        M
    );
}

🧨 Step 2: Call free

Assuming you have a pointer Value *ObjPtr.

Then:

Builder.CreateCall(FreeFunc, {ObjPtr});


Done.

LLVM emits:

call void @free(ptr %obj)

🧠 Example: Mini-Constructor + free
Allocate memory with malloc
// declare malloc
FunctionType *MallocTy =
    FunctionType::get(
        PointerType::getUnqual(Context),
        {Type::getInt64Ty(Context)},
        false);

Function *MallocFunc =
    Function::Create(
        MallocTy,
        Function::ExternalLinkage,
        "malloc",
        M);

// sizeof(Point)
DataLayout DL(&M);
uint64_t Size = DL.getTypeAllocSize(PointTy);

Value *SizeVal =
    ConstantInt::get(Type::getInt64Ty(Context), Size);

Value *RawMem =
    Builder.CreateCall(MallocFunc, {SizeVal});


Now you can cast it:

Value *PointPtr =
    Builder.CreateBitCast(RawMem,
                          PointerType::getUnqual(Context));


And later:

Builder.CreateCall(FreeFunc, {PointPtr});


Boom. Memory returned to the cosmos 🌌

⚠ Important in LLVM 18

Opaque pointer → always ptr

Specify type explicitly in CreateLoad

Use DataLayout for sizeof

Do not simulate delete unless you are implementing the C++ ABI

💀 Optional: Emulating C++ delete

If you want a real operator delete, you need:

Call the destructor

Then call free

That would be:

Builder.CreateCall(DestructorFunc, {ObjPtr});
Builder.CreateCall(FreeFunc, {ObjPtr});


LLVM itself does not enforce any order. That is frontend logic.

🎯 Summary

free in LLVM is:

A normal function

With a pointer

And zero magic

Memory does not die dramatically.
It just vanishes back into the heap mist 🌫️

Here is the debug checklist that helps in 90 percent of cases.

🔥 1️⃣ Wrong pointer is freed

The classic mistake.

You may only free exactly the pointer that malloc returned.

❌ Wrong:

Value *FieldPtr = Builder.CreateStructGEP(PointTy, ObjPtr, 0);
Builder.CreateCall(FreeFunc, {FieldPtr});


This crashes.
FieldPtr points into the object, not to the start.

✅ Correct:

Builder.CreateCall(FreeFunc, {ObjPtr});


When in doubt:
What came directly from malloc? Only that may be freed.

💣 2️⃣ Freeing stack memory

If you used CreateAlloca:

Value *StackObj = Builder.CreateAlloca(PointTy);


Then it lives on the stack.

You must NEVER free it.

alloca is automatically cleaned up when the function exits.

free(alloca_ptr) → guaranteed chaos.

🧬 3️⃣ Wrong type casts with opaque pointers

LLVM 18 is strict.

If you do something like:

Builder.CreateBitCast(RawMem, PointerType::getUnqual(Context));


That is fine.

But if you play around with old element-typed pointers or build inconsistent casts, you may end up with incorrect GEPs later.

Advice:

Always use DataLayout for sizeof

Always use CreateStructGEP(PointTy, Ptr, index)

No hand-crafted GEP offsets

🧨 4️⃣ Double Free

Happens faster than you think.

Example:

Builder.CreateCall(FreeFunc, {ObjPtr});
Builder.CreateCall(FreeFunc, {ObjPtr});


Or:

Freeing an object in multiple control flow paths

And both paths can be executed

Valgrind or ASan will catch this immediately.

🧪 5️⃣ Use After Free

You free correctly, but then continue to use the pointer.

LLVM IR does not prevent this.

Example:

Builder.CreateCall(FreeFunc, {ObjPtr});
Builder.CreateLoad(Int32Ty, ObjPtr);  // 💀


This may work sometimes.
And then it explodes spectacularly later.

🧱 6️⃣ Wrong malloc signature

In LLVM 18 malloc should be declared as:

i8* malloc(i64)


So:

FunctionType::get(
    PointerType::getUnqual(Context),
    {Type::getInt64Ty(Context)},
    false);


If you use i32 for the size instead, this can go wrong on 64-bit systems.

🧠 7️⃣ Missing DataLayout

If your Module has no DataLayout set:

M.setDataLayout(TargetMachine->createDataLayout());


Then getTypeAllocSize may return wrong values.

And then you allocate too little memory.

That is a very quiet, nasty bug.
