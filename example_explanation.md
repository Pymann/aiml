# AIML Example Explanation

This document explains what the `test.aiml` example code does and how it is compiled into LLVM IR.

## Input: `test.aiml`

```yaml
- set: [answer, 42]
- print: [answer]
```

### Explanation
1.  **`set: [answer, 42]`**: This command declares a variable named `answer` and initializes it with the integer value `42`.
2.  **`print: [answer]`**: This command is intended to print the value of the variable `answer`. *Note: In the current prototype, this command prints a message to the compiler's stdout but does not yet generate runtime print instructions in the LLVM IR.*

## Output: `output.ll` (LLVM IR)

```llvm
; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"

define i32 @main() {
entry:
  %answer = alloca i32, align 4     ; Allocate memory on stack for 'answer' (4 bytes)
  store i32 42, ptr %answer, align 4 ; Store the value 42 into the allocated memory
  ret i32 0                         ; Return 0 from main (success)
}
```

### Explanation of Generated IR
-   **`define i32 @main() { ... }`**: Defines the `main` function, which is the entry point of the program. It returns a 32-bit integer (`i32`).
-   **`%answer = alloca i32, align 4`**: Corresponds to the `set` command. It allocates stack memory for a 32-bit integer and assigns the address to the temporary variable `%answer`.
-   **`store i32 42, ptr %answer, align 4`**: Stores the integer constant `42` into the memory location pointed to by `%answer`.
-   **`ret i32 0`**: Returns `0` to the operating system, indicating successful execution.

## Execution
When the compiler runs:
1.  It parses `test.aiml`.
2.  It encounters `set`, prints "Set answer to 42" to the console, and generates the `alloca` and `store` instructions.
3.  It encounters `print`, prints "Print command found" to the console.
4.  It finalizes the `main` function with a `ret` instruction.
5.  It writes the generated IR to `output.ll`.
