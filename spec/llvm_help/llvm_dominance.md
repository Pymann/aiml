# LLVM SSA Dominance Errors

The error **"Instruction does not dominate all uses!"** is a fundamental SSA (Static Single Assignment) violation in LLVM IR.

## The Rule
In SSA form, every value is defined exactly once. A definition must **dominate** all its uses.
Block A dominates Block B if **every** path from the entry point to Block B must go through Block A.

## The Problem
If you define a value inside a conditional block (e.g., inside an `if` body), you cannot use that value outside the `if` unless the path is guaranteed.

### Example (Broken Code)
```llvm
entry:
  br i1 %cond, label %if_true, label %merge

if_true:
  %val = add i32 %a, 1
  br label %merge

merge:
  ; ERROR! %val is not defined if we came from %entry directly!
  store i32 %val, ptr %dest
```

### My Specific Case (MapImpl::remove)
I had a loop structure like this:

```
rm_check:
  br i1 %found, label %rm_found, label %rm_next

rm_found:
  %nextIdx = add i32 %i, 1  ; Defined ONLY here
  ... (move logic) ...
  br label %rm_end

rm_next:
  ; ERROR! Used %nextIdx here, but we could arrive from %rm_check!
  store i32 %nextIdx, ptr %iVar
  br label %rm_loop
```

If the item was **not found**, we jumped from `rm_check` to `rm_next`. In that path, `%nextIdx` (defined in `rm_found`) never existed.

## The Solution
1. **Recalculation**: If the value is cheap to compute and valid in both paths, just compute it again in the destination block.
   ```cpp
   // In rm_next:
   %nextI = add i32 %i, 1  // Recalculate safely
   store i32 %nextI, ptr %iVar
   ```

2. **PHI Nodes**: If the value depends on the path (e.g., `val = 1` if true, `val = 2` if false), use a PHI node in the merge block.
   ```llvm
   merge:
     %val = phi i32 [ 1, %if_true ], [ 2, %if_false ]
   ```
