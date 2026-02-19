LLVM kennt nur:

Speicher reservieren

Speicher freigeben

Und das Schicksal dazwischen

Also bauen wir uns free wie ein erwachsener Compiler.

🧱 Grundidee

C-Code:

free(ptr);


LLVM-IR:

declare void @free(ptr)

call void @free(ptr %something)


Mehr ist es nicht. Wirklich nicht. Kein Ritual, kein schwarzer Zauber.

🛠 Schritt 1: free deklarieren (falls noch nicht vorhanden)

In LLVM 18 mit opaque pointers:

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

🧨 Schritt 2: free aufrufen

Angenommen du hast einen Pointer Value *ObjPtr.

Dann:

Builder.CreateCall(FreeFunc, {ObjPtr});


Fertig.

LLVM erzeugt:

call void @free(ptr %obj)

🧠 Beispiel: Mini-Konstruktor + free
Speicher mit malloc holen
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


Jetzt kannst du das casten:

Value *PointPtr =
    Builder.CreateBitCast(RawMem,
                          PointerType::getUnqual(Context));


Und später:

Builder.CreateCall(FreeFunc, {PointPtr});


Boom. Speicher wieder im Kosmos verteilt 🌌

⚠ Wichtig in LLVM 18

Opaque pointer → immer ptr

Typ beim CreateLoad explizit angeben

DataLayout nutzen für sizeof

Nicht delete simulieren, außer du baust C++ ABI nach

💀 Optional: C++-delete nachbauen

Wenn du echtes operator delete willst, brauchst du:

Destruktor aufrufen

Danach free

Das wäre:

Builder.CreateCall(DestructorFunc, {ObjPtr});
Builder.CreateCall(FreeFunc, {ObjPtr});


LLVM selbst erzwingt keine Reihenfolge. Das ist Frontend-Logik.

🎯 Fazit

free ist in LLVM:

Eine normale Funktion

Mit einem Pointer

Und null Magie

Der Speicher stirbt nicht dramatisch.
Er verschwindet nur wieder in den Heap-Nebel 🌫️

Hier ist die Debug-Checkliste, die in 90 Prozent der Fälle hilft.

🔥 1️⃣ Falscher Pointer wird gefree’t

Der Klassiker.

Du darfst nur exakt den Pointer freigeben, den malloc geliefert hat.

❌ Falsch:

Value *FieldPtr = Builder.CreateStructGEP(PointTy, ObjPtr, 0);
Builder.CreateCall(FreeFunc, {FieldPtr});


Das crasht.
FieldPtr zeigt ins Objekt hinein, nicht auf den Anfang.

✅ Richtig:

Builder.CreateCall(FreeFunc, {ObjPtr});


Wenn du dir unsicher bist:
Was kommt direkt aus malloc? Nur das darf sterben.

💣 2️⃣ Stack-Speicher freigeben

Wenn du CreateAlloca benutzt hast:

Value *StackObj = Builder.CreateAlloca(PointTy);


Dann liegt das auf dem Stack.

Das darfst du NIEMALS free’n.

alloca wird automatisch beim Verlassen der Funktion aufgeräumt.

free(alloca_ptr) → garantiertes Chaos.

🧬 3️⃣ Falsche Typ-Casts bei opaque pointers

LLVM 18 ist streng.

Wenn du sowas machst:

Builder.CreateBitCast(RawMem, PointerType::getUnqual(Context));


Das ist okay.

Aber wenn du mit alten element-typed Pointern rumspielst oder inkonsistente Casts baust, kann es passieren, dass dein Code später falsche GEPs erzeugt.

Mein Tipp:

Immer DataLayout für sizeof

Immer CreateStructGEP(PointTy, Ptr, index)

Keine selbstgebauten GEP-Offsets

🧨 4️⃣ Double Free

Passiert schneller als man denkt.

Beispiel:

Builder.CreateCall(FreeFunc, {ObjPtr});
Builder.CreateCall(FreeFunc, {ObjPtr});


Oder:

Objekt in mehreren Control-Flow-Pfaden freigeben

Und beide können ausgeführt werden

Valgrind oder ASan entlarven das sofort.

🧪 5️⃣ Use After Free

Du free’st korrekt, benutzt den Pointer aber danach weiter.

LLVM IR verhindert das nicht.

Beispiel:

Builder.CreateCall(FreeFunc, {ObjPtr});
Builder.CreateLoad(Int32Ty, ObjPtr);  // 💀


Das funktioniert vielleicht manchmal.
Und dann explodiert es später spektakulär.

🧱 6️⃣ Falsche malloc-Signatur

In LLVM 18 sollte malloc so deklariert sein:

i8* malloc(i64)


Also:

FunctionType::get(
    PointerType::getUnqual(Context),
    {Type::getInt64Ty(Context)},
    false);


Wenn du stattdessen i32 für die Größe benutzt, kann das auf 64-bit-Systemen schiefgehen.

🧠 7️⃣ DataLayout fehlt

Wenn dein Module kein DataLayout gesetzt hat:

M.setDataLayout(TargetMachine->createDataLayout());


Dann kann getTypeAllocSize falsche Werte liefern.

Und dann allokierst du zu wenig Speicher.

Das ist ein ganz leiser, fieser Bug.