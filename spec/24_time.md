Summary:
LLVM itself cannot sleep and has no clock.
LLVM only generates code. The actual time magic happens through:

Calling external functions

or through custom runtime wrappers

The elegant solution is almost always: declare wrapper functions and use them via call or invoke.

🛌 Using Sleep
Option 1: Call libc sleep directly

On Unix there is:

unsigned int sleep(unsigned int seconds);


In LLVM IR you simply declare:

FunctionType* sleepTy =
    FunctionType::get(builder.getInt32Ty(),
                      {builder.getInt32Ty()},
                      false);

Function* sleepFn =
    Function::Create(sleepTy,
                     Function::ExternalLinkage,
                     "sleep",
                     module);


And then:

builder.CreateCall(sleepFn, builder.getInt32(2));


That's it. The linker will resolve it via libc.

If you want to support exceptions, you can also use CreateInvoke.

⏱ Getting a Unix Timestamp

libc has:

time_t time(time_t* tloc);


Usually time_t is a long.

Declaration in LLVM:

FunctionType* timeTy =
    FunctionType::get(builder.getInt64Ty(),
                      {builder.getInt64Ty()->getPointerTo()},
                      false);

Function* timeFn =
    Function::Create(timeTy,
                     Function::ExternalLinkage,
                     "time",
                     module);


Call with nullptr:

Value* nullPtr =
    ConstantPointerNull::get(
        builder.getInt64Ty()->getPointerTo());

Value* ts = builder.CreateCall(timeFn, nullPtr);


ts now contains the Unix timestamp.

🎩 More Elegant: Custom Runtime Wrappers

I usually recommend a small runtime file:

extern "C" long my_timestamp() {
    return std::time(nullptr);
}

extern "C" void my_sleep(int sec) {
    sleep(sec);
}


Why?

You encapsulate platform details

You control types

You can change behavior later

Your IR stays clean

Then you only declare:

declare i64 @my_timestamp()
declare void @my_sleep(i32)


This is significantly more stable.

🧠 Why Wrappers Are Often Better

Calling libc directly is simple, but:

time_t can be 32 or 64 bits

sleep can be interrupted by signals

Some systems use nanosleep

Platform differences creep in

Wrappers isolate that.

Your LLVM code stays neutral and minimalist.

🚀 When You Use JIT

When you use ORC JIT, you must:

jit->getMainJITDylib().addGenerator(
    cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix()
    ))
);


This allows the JIT to find sleep or time in the current process.

🧩 Architecture Recommendation

For a custom language or VM:

[Your IR]
     ↓
[Runtime API Layer]
     ↓
[libc / syscalls]


LLVM remains the code generator.
The runtime is your operating system whisperer.
