Eine „Klasse“ in LLVM gibt es offiziell nicht. LLVM kennt nur Strukturen und Funktionen.
Aber wir können uns eine Klasse bauen wie ein kleines Theaterstück aus:

Ein struct → repräsentiert die Daten

Funktionen → sind die Methoden

Das erste Argument ist immer this

Also: Willkommen in Objektorientierung à la IR 🎭

🎯 Beispiel: Eine Klasse Point mit Methode sum()

C++-Vorbild:

class Point {
  int x;
  int y;

  int sum() {
    return x + y;
  }
};

🧱 Schritt 1: Struct anlegen (die "Klasse")
LLVMContext Context;
Module M("MyModule", Context);
IRBuilder<> Builder(Context);

Type *Int32Ty = Type::getInt32Ty(Context);

// struct Point { i32, i32 }
StructType *PointTy = StructType::create(Context, "Point");
PointTy->setBody({Int32Ty, Int32Ty});


Fertig ist unsere „Klasse“.
Intern ist das einfach:

%Point = type { i32, i32 }

🔧 Schritt 2: Methode definieren

Eine Methode ist einfach eine Funktion mit Point* this.

In LLVM 18 mit opaque pointers:

PointerType *PointPtrTy = PointerType::getUnqual(Context);

// int sum(Point* this)
FunctionType *SumTy =
    FunctionType::get(Int32Ty, {PointPtrTy}, false);

Function *SumFunc =
    Function::Create(SumTy,
                     Function::ExternalLinkage,
                     "Point_sum",
                     M);

🧠 Schritt 3: Methoden-Body bauen
BasicBlock *Entry =
    BasicBlock::Create(Context, "entry", SumFunc);
Builder.SetInsertPoint(Entry);

// this pointer holen
Value *This = SumFunc->getArg(0);
This->setName("this");

// x und y laden
Value *XPtr = Builder.CreateStructGEP(PointTy, This, 0);
Value *YPtr = Builder.CreateStructGEP(PointTy, This, 1);

Value *X = Builder.CreateLoad(Int32Ty, XPtr);
Value *Y = Builder.CreateLoad(Int32Ty, YPtr);

// x + y
Value *Sum = Builder.CreateAdd(X, Y);

Builder.CreateRet(Sum);

verifyFunction(*SumFunc);

🧾 Ergebnis-IR

Das sieht dann ungefähr so aus:

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


Und voilà.
Du hast eine Klasse. Also… eine strukturelle Illusion davon 😌

🪄 Optional: "Konstruktor" bauen

Ein Konstruktor ist einfach eine Funktion:

Point* Point_create(int x, int y)


Die:

Speicher allokiert (CreateAlloca oder malloc)

Felder setzt (CreateStore)

Pointer zurückgibt

LLVM interessiert sich nicht für Klassenhierarchien.
Nur für Speicher und Funktionen. Alles andere ist Zucker, den der Frontend-Compiler streut 🍬

🎯 Wichtig in LLVM 18

Opaque pointer beachten (ptr)

CreateStructGEP(Type*, Value*, index) verwenden

Immer Typ beim CreateLoad angeben