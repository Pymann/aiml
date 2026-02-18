if und while sind in LLVM nur Control-Flow mit Basic Blocks + br + phi.

Ich zeige dir erst das saubere Grundmuster in LLVM-IR und danach typische Fehlerquellen.

✅ 1️⃣ IF in LLVM (Grundmuster)
Beispiel in C:
if (a < b) {
    x = 1;
} else {
    x = 2;
}

Korrektes LLVM-IR:
entry:
  %cond = icmp slt i32 %a, %b
  br i1 %cond, label %then, label %else

then:
  br label %merge

else:
  br label %merge

merge:
  %x = phi i32 [ 1, %then ], [ 2, %else ]
  ; hier kannst du %x verwenden

🔥 Wichtig

Jeder Block endet mit br oder ret

Der merge-Block braucht eine phi

Jede phi muss für alle Vorgänger-Blöcke einen Wert haben

🧠 Alternative (einfacher!)

Wenn du KEINEN Wert zurückgeben musst:

entry:
  %cond = icmp slt i32 %a, %b
  br i1 %cond, label %then, label %merge

then:
  call void @do_something()
  br label %merge

merge:
  ret void


Keine phi nötig, wenn kein Wert gemerged wird.

✅ 2️⃣ WHILE in LLVM (wichtig!)
C-Code:
while (i < 10) {
    i = i + 1;
}

Korrektes LLVM-IR Muster:
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]

  %cond = icmp slt i32 %i, 10
  br i1 %cond, label %body, label %end

body:
  %next = add i32 %i, 1
  br label %loop

end:
  ret void

🔥 WICHTIGSTE REGEL BEI WHILE

Der Loop-Index muss eine phi sein.

Ohne phi bekommst du:

„Instruction does not dominate all uses“

oder kaputten SSA-Graph

🧨 Häufige Fehler
❌ 1. Kein Branch am Block-Ende

Falsch:

then:
  %x = add i32 %a, 1


Jeder Block braucht:

br ...


oder

ret ...

❌ 2. Phi fehlt ein Eingang

Falsch:

%x = phi i32 [ 1, %then ]


Wenn merge 2 Vorgänger hat → braucht 2 Einträge.

❌ 3. Definition im Body, Nutzung im End-Block
body:
  %x = add i32 %i, 1
  br label %end

end:
  store i32 %x, ptr %p   ; ❌ Dominance-Fehler


→ Lösung: PHI oder Definition verschieben.

🏗 Wenn du mit IRBuilder arbeitest (C++)
IF:
Value *cond = builder.CreateICmpSLT(a, b);

BasicBlock *thenBB = BasicBlock::Create(ctx, "then", function);
BasicBlock *elseBB = BasicBlock::Create(ctx, "else");
BasicBlock *mergeBB = BasicBlock::Create(ctx, "merge");

builder.CreateCondBr(cond, thenBB, elseBB);

builder.SetInsertPoint(thenBB);
builder.CreateBr(mergeBB);

function->insert(function->end(), elseBB);
builder.SetInsertPoint(elseBB);
builder.CreateBr(mergeBB);

function->insert(function->end(), mergeBB);
builder.SetInsertPoint(mergeBB);

PHINode *phi = builder.CreatePHI(Type::getInt32Ty(ctx), 2);
phi->addIncoming(valThen, thenBB);
phi->addIncoming(valElse, elseBB);

🧩 Debug-Tipp

Wenn wieder sowas kommt wie:

Instruction does not dominate all uses


→ 90% der Fälle:

phi fehlt

falscher Block

value in falschem Scope erzeugt