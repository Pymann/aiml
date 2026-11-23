# Pointer return repro: C++ error analysis

## Repro

File:
- `tests/test_return_types_ptr_repro.aiml`

Minimal content:
- top-level `decl g_i32`
- function `ret_ptr_i32() -> ptr<i32>`
- `return [&g_i32]`
- the caller reads the returned pointer

Run:
- `./out/aiml/aiml -r tests/test_return_types_ptr_repro.aiml`

Observation:
- no normal compiler error
- hard crash with `exit code -1`

`gdb` backtrace:
- SIGSEGV in LLVM `LiveVariables::HandleVirtRegUse`
- stack via `MCJIT::emitObject`, `MCJIT::generateCodeForModule`, `MCJIT::runFunction`
- so not a normal AIML runtime error, but broken or inconsistent generated LLVM IR / machine IR

## Strongest suspicion

The repro currently does **not** look like a general error "pointer returns never work", but like a specific error:

- `&g_i32` takes the address of a **top-level `decl`**
- in the normal non-library path top-level `decl`s are **not** created as real globals
- instead they end up as local allocas in `__aiml_init`
- functions compiled later, however, still see these symbols in `ctx.symbol_table`
- as a result function code can reference an alloca from **another function**

That produces invalid IR and fits the late LLVM crash in codegen very well.

## Relevant C++ places

### 1. A top-level `decl` is not created as a global in a normal program

In `src/core/ast_compiler.cpp` the safe path is only used in `libraryMode`:

- top-level `DeclStmt` -> `llvm::GlobalVariable`
- the comment there says so itself explicitly

In the normal path:
- `__aiml_init` is created
- after that every top-level node is simply visited with `visitTopLevel(...)`

Affected places:
- `src/core/ast_compiler.cpp`
  - `compile(...)`, library path with `GlobalVariable`
  - `compile(...)`, non-library path with `__aiml_init`

Consequence:
- a top-level `decl` in run/JIT mode lives like a local variable in `__aiml_init`
- not like a real module global

### 2. `visitDeclStmt(...)` creates allocas at the current insert point

In `src/core/ast_compiler_stmt.cpp`:
- `visitDeclStmt(...)` calls `ctx.ir_gen->createAlloca(...)`
- in the non-library case the current insert point for top-level code is exactly `__aiml_init`

Consequence:
- `g_i32` from the repro is created as a stack/entry alloca of `__aiml_init`

### 3. `visitFunction(...)` takes existing symbols over into function compilation

In `src/core/ast_compiler.cpp`:
- `visitFunction(...)` stores `auto savedSymbols = ctx.symbol_table`
- so top-level symbols stay visible in function compilation
- there is no protection there against a symbol coming from another function

Consequence:
- `ret_ptr_i32()` can resolve `g_i32` successfully
- although `g_i32` was physically allocated in `__aiml_init`

### 4. Unary `&` blindly returns the alloca for variables

In `src/core/ast_compiler_expr.cpp`:
- `visitUnary(...)`
- for `&x`, `sym->ir_alloca_inst` is returned directly

Important:
- there is no check there whether this alloca belongs to the current `llvm::Function`
- the address of the symbol is simply returned

Consequence:
- `&g_i32` in `ret_ptr_i32()` very probably returns an alloca from `__aiml_init`
- that is not a valid local value origin for the current function

### 5. `visitRetStmt(...)` itself does no additional validation

In `src/core/ast_compiler_stmt.cpp`:
- `visitRetStmt(...)` only evaluates the expression
- then `ctx.ir_gen->createRet(val)`

That is not the main error in itself, but:
- here the problematic pointer is simply emitted unchecked as the return value

## Why that plausibly explains the LLVM crash

If a function uses IR that effectively points to allocas / definitions from another function, the IR is semantically broken.

Typical consequences:
- verifier errors, if it is verified
- or later crashes in LLVM passes

Here we see exactly the latter:
- not a clean AIML compile error
- but a SIGSEGV deep in LLVM LiveVariables during MCJIT codegen

That fits **invalid cross-function IR** much better than a simple runtime bug.

## Why `test_return_types.aiml` still passes

The stable test currently only uses:
- `i32` return
- `string` return

These functions do not access top-level alloca addresses.

Therefore:
- the basic return path works
- the specific repro with `&g_i32` tips over

## Further possible effects

The error is probably bigger than just the `ptr<i32>` return:

- every function that accesses a top-level `decl` in the non-library path could be affected
- particularly critical:
  - `&global`
  - returning `ptr<T>`
  - passing such pointers on to other functions
  - possibly also direct loads from a top-level `decl`, if they come from a foreign function context

## Probably the most sensible fix directions

### Fix direction 1: materialise top-level `decl`s as real globals in the non-library path too

That is the most robust direction.

Instead of:
- top-level `decl` as local allocas in `__aiml_init`

better:
- top-level `decl` always as an `llvm::GlobalVariable`
- `__aiml_init` only does initialisation and builtin init

That would be conceptually consistent with the already existing library path.

### Fix direction 2: until then a hard protection rule in the compiler

If the larger rework is not done right away:

- `visitUnary('&')` should reject when the target symbol comes from another `llvm::Function`
- more generally: every use of a symbol with an alloca from a foreign function context should raise a compiler error

Then there would at least be:
- a clear AIML error
- no LLVM SIGSEGV

### Fix direction 3: enforce module verification before JIT

Very sensible independently of the actual fix:

- verify the generated LLVM module
- abort cleanly on a verifier error
- do not let LLVM passes crash first

## Short conclusion

The strongest current finding is:

- the `ptr<i32>` return itself is not necessarily the actual root error
- the repro very probably breaks because `&g_i32` points to a top-level `decl` that only exists as an alloca in `__aiml_init` in the non-library/JIT path
- this address is then used and returned from another function
- that very probably produces invalid LLVM IR and later leads to the crash in `LiveVariables`

The best next technical step would be:
- unify top-level `decl` storage between the library and non-library paths
- or at least hard-forbid the use of foreign-function allocas
