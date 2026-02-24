Jetzt wird’s interessant 🔥
Wir bauen einen echten LLVM-IR Try-Catch Block mit:

invoke

landingpad

eigener Exception-Type

optionalem resume

Und zwar sauber im modernen Itanium-ABI-Stil, wie Clang es unter Linux macht.

Wichtig vorweg:

LLVM implementiert Exceptions nicht selbst.
Es nutzt das Plattform-ABI. Unter Linux ist das das Itanium C++ ABI mit:

__cxa_allocate_exception

__cxa_throw

Personality Function: __gxx_personality_v0

Wir bauen also ein kleines Mini-C++-Exception-System direkt in IR.

🧠 Architektur
throw →
    __cxa_allocate_exception
    __cxa_throw
        ↓
invoke →
    landingpad →
        catch clause →
            handler
🎯 Beispiel: Eigene Exception-Type

Angenommen wir wollen eine eigene Exception:

struct MyException {
    int code;
};

Im LLVM-Kontext brauchen wir:

struct type

typeinfo symbol

personality function

landingpad mit catch clause

🏗️ Schritt 1: Personality setzen

Jede Funktion mit Exceptions braucht:

Function* F = ...
F->setPersonalityFn(
    M->getOrInsertFunction(
        "__gxx_personality_v0",
        FunctionType::get(
            builder.getInt32Ty(), true)
    ).getCallee()
);

Ohne Personality → kein EH.

🏗️ Schritt 2: Eigene Exception werfen
Exception-Type definieren
StructType* MyExType =
    StructType::create(context, "MyException");

MyExType->setBody(builder.getInt32Ty());
Speicher für Exception holen
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
Exception initialisieren
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
Throw aufrufen
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

Jetzt haben wir geworfen.

🛑 Schritt 3: Try-Catch Block
invoke statt call
builder.CreateInvoke(
    mayThrowFunc,
    normalBB,
    catchBB
);
landingpad erzeugen
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
Catch-Clause hinzufügen
lp->addClause(typeInfo);

Damit wird nur MyException gefangen.

Exception behandeln
Value* exObj =
    builder.CreateExtractValue(lp, 0);

// cast zurück
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

Jetzt hast du code.

🧹 Nicht behandelt?

Wenn du nicht catchen willst:

builder.CreateResume(lp);

Das entspricht throw;.

📦 Minimaler Ablauf

Funktion mit:

entry:
    invoke @may_throw
        to normal
        unwind catch

catch:
    %lp = landingpad {i8*, i32}
           catch typeinfo
    handler
🧠 Wichtige Hinweise

Du brauchst -fexceptions Support

Du brauchst die richtige Personality

Du musst gegen libstdc++ linken

TypeInfo-Symbole müssen existieren

In der Praxis lässt man Clang einmal eine Dummy-Exception kompilieren und schaut sich die IR an.

🧩 Alternative: Eigene VM-Exceptions

Wenn du keine echte C++-ABI willst, kannst du auch:

Exceptions als Error-Codes modellieren

setjmp/longjmp verwenden

eigene Stack-Unwinder bauen

Echte C++-Exceptions sind heavy machinery.

🚀 Wenn du willst

Ich kann dir:

ein vollständig lauffähiges LLVM C++ Beispiel bauen

eine minimalistische Custom-Exception ohne C++ ABI

oder erklären, wie ORC JIT mit EH umgeht

Was willst du vertiefen?