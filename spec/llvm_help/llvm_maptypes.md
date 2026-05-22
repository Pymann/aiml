You:
How can I implement a map/dict type with LLVM?! I keep getting memory access errors

ChatGPT:

If you want to implement a map/dict type with LLVM and are getting memory access errors, the cause is almost always one of these:

    wrong types in getelementptr

    memory not allocated (only the type created, but no alloca/malloc)

    wrong pointer level (i8* vs i8**)

    struct layout doesn't match the accesses

    lifetime problem (stack object used outside its scope)

Fundamentally: LLVM has no built-in map type

LLVM IR only knows:

    primitive types (i32, i8, double, …)

    arrays

    structs

    pointers

You have to model a dict yourself as a data structure, e.g.:
Option 1: Hashmap with buckets

Example struct in LLVM IR:

%Entry = type { i8*, i32 }        ; key (string), value (int)
%Bucket = type { %Entry*, i32 }   ; array of entries + count
%Dict = type { %Bucket*, i32 }    ; buckets + bucket_count

🔥 Most common cause of segfaults
❌ Error 1: Struct type created, but no memory allocated

Just this is NOT enough:

StructType *dictType = StructType::create(context, "Dict");

You need memory:
Stack:

Value *dict = builder.CreateAlloca(dictType);

Heap:

Value *dict = builder.CreateCall(mallocFunc, size);

❌ Error 2: getelementptr used incorrectly

Typical crash:

builder.CreateStructGEP(dictType, dict, 1);

If dict is not a pointer to dictType → segfault.

Correct:

Value *bucketPtr = builder.CreateStructGEP(dictType, dict, 0);

Important:

    The first parameter must be exactly the struct type

    dict must be dictType*

❌ Error 3: Pointer level mismatch

If you have e.g.:

%Entry = type { i8*, i32 }

Then:

    %Entry* → pointer to Entry

    %Entry** → pointer to pointer to Entry

Many segfaults come from a missing load.

Wrong:

Value *entry = bucketPtr;   // bucketPtr is Entry**
builder.CreateStructGEP(entryType, entry, 1);  // CRASH

Correct:

Value *entry = builder.CreateLoad(entryType->getPointerTo(), bucketPtr);

Minimal working map variant (simple!)

If you only want string -> int, start with something very simple:
Simplified structure

%Pair = type { i8*, i32 }
%Dict = type { %Pair*, i32 }   ; array + size

Process:

    Allocate Dict

    Allocate array (malloc)

    Store pointer in struct

    On insert: calculate index → GEP on array → Store

Important point: declare malloc correctly

declare i8* @malloc(i64)

And then:

Value *size = ConstantInt::get(Type::getInt64Ty(context), 100);
Value *mem = builder.CreateCall(mallocFunc, size);
Value *typedMem = builder.CreateBitCast(mem, pairType->getPointerTo());

If you forget the bitcast → segfault.
🔍 Debugging tips
1️⃣ Print the LLVM IR!

module->print(llvm::errs(), nullptr);

Then check:

    Do pointer levels match?

    Is alloca/malloc really called before access?

2️⃣ Compile with AddressSanitizer

If you use JIT:

    with -fsanitize=address

3️⃣ Test with lli

lli test.ll

🧠 Recommendation

If you just want a working map:

👉 Implement the map in C++
👉 Compile it
👉 Call it from LLVM

Example:

extern "C" int dict_get(Dict*, const char*);

This saves you massive amounts of debugging.
If you want

Show me:

    your struct definition

    your insert code

    the LLVM IR

    the exact error message

Then I can tell you exactly where the memory error occurs.

LLVM memory errors are almost always a pointer level problem.
