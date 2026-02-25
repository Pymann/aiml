# String Expansions

String expansion allows embedding variable values or complex expressions directly into string literals using the `${...}` syntax. This feature is processed at **runtime** (or compile-time for constants, though current implementation leans runtime).

## 1. Basic Syntax

Strings can contain one or more expansion blocks.

```yaml
- decl: [name, string, "World"]
- print: ["Hello, ${name}!"]
# Output: Hello, World!
```

## 2. Scalar Expansion

If the content inside `${...}` is a scalar value (e.g., a variable name or literal), it is resolved and converted to a string.

- **Variables**: The value of the variable is retrieved.
- **Integers (i32, i64)**: Converted to string representation (e.g., `10` -> `"10"`).
- **Strings (ptr<i8>)**: Used directly.
- **Pointers**: Currently treated as C-strings if compatible, otherwise behavior is undefined (might print address or crash if not a valid string pointer).

```yaml
- decl: [count, i32, 42]
- print: ["The answer is ${count}."]
# Output: The answer is 42.
```

## 3. Expression Expansion

The content inside `${...}` can be a valid YAML object representing an expression. This allows function calls, math operations, and more to be evaluated *inline* and the result converted to a string.

### 3.1 Function Calls

You can call functions within a string. The return value of the function is converted to a string.

```yaml
- def:
    name: add
    args: [[a, i32], [b, i32]]
    return: i32
    body:
      - return: 30

- print: ["Result: ${ { call: [add, 10, 20] } }"]
# Output: Result: 30
```

### 3.2 Method Calls

Object methods (on lists, maps, or custom classes) can also be called.

```yaml
- decl: [my_list, list<i32>]
- call: [my_list.push, 100]

- print: ["List size: ${ { call: [my_list.size] } }"]
# Output: List size: 1
```

### 3.3 Math and Logic

Binary operations (like `+`, `-`, `*`, `/`) work as expected.

```yaml
- decl: [x, i32, 5]
- print: ["Calculated: ${ { +: [x, 5] } }"]
# Output: Calculated: 10
```

## 4. Implicit Type Conversion (String -> Int)

A unique feature of AIML string expansion used in declarations is the ability to implicitly convert the *resulting string* back to an integer type if the target variable requires it.

This is useful for initializing variables with complex string-based logic.

```yaml
# 1. Expansion happens: "10" + "20" -> "30" (as string)
# 2. Assignment detects target is i32.
# 3. Compiler inserts 'atoi("30")' to convert "30" -> 30 (int).
- decl: 
    name: result
    type: i32
    value: "${ { +: [10, 20] } }"
```

This acts as a bridge between string-based templating and typed logic.

## 5. Runtime Behavior

When the compiler encounters a string with expansions:

1.  **Scanning**: It scans the string for `${...}` blocks.
2.  **Evaluation**: 
    -   It extracts the content of the block.
    -   If it's YAML/JSON, it parses and evaluates it using the standard `Expression::evaluate` engine.
    -   The result (Type `llvm::Value*`) is converted to a standard C-string (`char*`).
3.  **Concatenation**: 
    -   The literal parts of the string and the evaluated parts are concatenated at runtime using `malloc`, `strcpy`, and `strcat`.
    -   Current implementation relies on `malloc` and does not automatically free these intermediate strings (leak warning).

## 6. Examples

### Complex Nested Logic

```yaml
- decl: [a, i32, 10]
- decl: [b, i32, 20]

# Math inside expansion
- print: ["Sum: ${ { +: [a, b] } }"]

# Call inside expansion
- print: ["Max: ${ { call: [math.max, a, b] } }"]

# Nested (conceptually supported via recursion in evaluator)
# "${ { +: [ { call: [f1] }, { call: [f2] } ] } }"
```

### Namespace Resolution

If evaluating a function in a namespace, ensure full qualification or correct scope resolution.

```yaml
- import: "lib.aiml"
# Calls 'my.lib.add' from 'lib.aiml'
- print: ["Lib Call: ${ { call: [my.lib.add, 1, 2] } }"]
```

