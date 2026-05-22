The error

Instruction does not dominate all uses!
%153 = add i32 %148, 1
store i32 %153, ptr %i_rm, align 4


does not mean a memory error — it is an SSA form error in the control flow.

LLVM requires:

A definition must dominate all of its uses.

That means:
The variable %153 is used in a basic block that is not guaranteed to execute after the definition.

🔎 What does "dominate" mean?

Block A dominates block B if every path to block B passes through A.

Typical error:

br i1 %cond, label %then, label %else

then:
  %x = add i32 %a, 1
  br label %merge

else:
  br label %merge

merge:
  store i32 %x, ptr %ptr   ; ❌ ERROR


Problem:
%x only exists in the then block,
but is used in the merge block.

If else executes → %x does not exist.

LLVM then says:

Instruction does not dominate all uses!

💥 Solution: Use a PHI node

The correct version:

br i1 %cond, label %then, label %else

then:
  %x_then = add i32 %a, 1
  br label %merge

else:
  %x_else = add i32 %a, 0
  br label %merge

merge:
  %x = phi i32 [ %x_then, %then ], [ %x_else, %else ]
  store i32 %x, ptr %ptr

🎯 What most likely happens in your case

You probably have something like:

loop:
  br i1 %cond, label %body, label %end

body:
  %153 = add i32 %148, 1
  br label %loop

end:
  store i32 %153, ptr %i_rm   ; ❌


If the loop is never entered,
%153 does not exist → dominance error.

🛠 Concrete fix strategies
✅ Case 1: Value used only in the same block

Then leave the store in the same block.

✅ Case 2: Value used after an if

→ Insert a PHI node.

✅ Case 3: Loop counter

For loops you ALWAYS need PHI:

Correct loop pattern:

entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, 10
  br i1 %cond, label %loop, label %end

end:
  ret void

🧠 Why does this happen often with hand-built maps?

Because you:

Compute a counter inside an if

But store it outside

Or increment it inside a loop

But use it after the loop

LLVM is extremely strict about SSA.

🔍 How to find the location?

Print the IR with block names:

opt -verify test_map.ll


or

llvm-as test_map.ll


This often shows the exact location.

🚨 IMPORTANT

This is NOT a runtime error.
The module is invalid IR.
That is why lli says:

Bad module


LLVM does not even attempt to execute it.
