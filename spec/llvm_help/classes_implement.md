There is no official "class" in LLVM. LLVM only knows structures and functions.
But we can build ourselves a class like a small theatrical play from:

A struct → represents the data

Functions → are the methods

The first argument is always this

So: Welcome to object-oriented programming à la IR 🎭

🎯 Example: A class Point with method sum()

C++ reference:

class Point {
  int x;
  int y;

  int sum() {
    return x + y;
  }
};

🧱 Step 1: Create the struct (the "class")
LLVMContext Context;
Module M("MyModule", Context);
IRBuilder<> Builder(Context);

Type *Int32Ty = Type::getInt32Ty(Context);

// struct Point { i32, i32 }
StructType *PointTy = StructType::create(Context, "Point");
PointTy->setBody({Int32Ty, Int32Ty});


Our "class" is ready.
Internally this is simply:

%Point = type { i32, i32 }

🔧 Step 2: Define a method

A method is simply a function with Point* this.

In LLVM 18 with opaque pointers:

PointerType *PointPtrTy = PointerType::getUnqual(Context);

// int sum(Point* this)
FunctionType *SumTy =
    FunctionType::get(Int32Ty, {PointPtrTy}, false);

Function *SumFunc =
    Function::Create(SumTy,
                     Function::ExternalLinkage,
                     "Point_sum",
                     M);

🧠 Step 3: Build the method body
BasicBlock *Entry =
    BasicBlock::Create(Context, "entry", SumFunc);
Builder.SetInsertPoint(Entry);

// get this pointer
Value *This = SumFunc->getArg(0);
This->setName("this");

// load x and y
Value *XPtr = Builder.CreateStructGEP(PointTy, This, 0);
Value *YPtr = Builder.CreateStructGEP(PointTy, This, 1);

Value *X = Builder.CreateLoad(Int32Ty, XPtr);
Value *Y = Builder.CreateLoad(Int32Ty, YPtr);

// x + y
Value *Sum = Builder.CreateAdd(X, Y);

Builder.CreateRet(Sum);

verifyFunction(*SumFunc);

🧾 Resulting IR

It looks roughly like this:

%Point = type { i32, i32 }

define i32 @Point_sum(ptr %this) {
entry:
  %0 = getelementptr %Point, ptr %this, i32 0, i32 0
  %1 = load i32, ptr %0
  %2 = getelementptr %Point, ptr %this, i32 0, i32 1
  %3 = load i32, ptr %2
  %4 = add i32 %1, %3
  ret i32 %4
}


And voilà.
You have a class. Or rather… a structural illusion of one 😌

🪄 Optional: Build a "constructor"

A constructor is simply a function:

Point* Point_create(int x, int y)


That:

Allocates memory (CreateAlloca or malloc)

Sets fields (CreateStore)

Returns the pointer

LLVM doesn't care about class hierarchies.
Only about memory and functions. Everything else is sugar sprinkled by the frontend compiler 🍬

🎯 Important in LLVM 18

Note opaque pointers (ptr)

Use CreateStructGEP(Type*, Value*, index)

Always specify the type in CreateLoad
