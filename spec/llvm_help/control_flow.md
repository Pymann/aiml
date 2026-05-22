if and while in LLVM are just control flow with basic blocks + br + phi.

Here is the clean basic pattern in LLVM IR first, followed by typical error sources.

✅ 1️⃣ IF in LLVM (basic pattern)
Example in C:
if (a < b) {
    x = 1;
} else {
    x = 2;
}

Correct LLVM IR:
entry:
  %cond = icmp slt i32 %a, %b
  br i1 %cond, label %then, label %else

then:
  br label %merge

else:
  br label %merge

merge:
  %x = phi i32 [ 1, %then ], [ 2, %else ]
  ; you can use %x here

🔥 Important

Every block ends with br or ret

The merge block needs a phi

Every phi must have a value for all predecessor blocks

🧠 Alternative (simpler!)

If you don't need to return a value:

entry:
  %cond = icmp slt i32 %a, %b
  br i1 %cond, label %then, label %merge

then:
  call void @do_something()
  br label %merge

merge:
  ret void


No phi needed if no value is merged.

✅ 2️⃣ WHILE in LLVM (important!)
C code:
while (i < 10) {
    i = i + 1;
}

Correct LLVM IR pattern:
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

🔥 MOST IMPORTANT RULE FOR WHILE

The loop index must be a phi.

Without phi you get:

"Instruction does not dominate all uses"

or a broken SSA graph

🧨 Common errors
❌ 1. No branch at block end

Wrong:

then:
  %x = add i32 %a, 1


Every block needs:

br ...


or

ret ...


❌ 2. Phi is missing an incoming edge

Wrong:

%x = phi i32 [ 1, %then ]


If merge has 2 predecessors → needs 2 entries.

❌ 3. Definition in body, use in end block
body:
  %x = add i32 %i, 1
  br label %end

end:
  store i32 %x, ptr %p   ; ❌ dominance error


→ Solution: PHI or move the definition.

🏗 When working with IRBuilder (C++)
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

🧩 Debug tip

When you get something like:

Instruction does not dominate all uses


→ 90% of cases:

phi is missing

wrong block

value created in wrong scope
