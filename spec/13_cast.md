# Casting and Type Conversion in AIML

AIML supports both explicit and implicit type conversions to facilitate flexible data handling and interoperability between static and dynamic types.

## 1. Implicit Reference Passing ("Address Of")

When a function parameter expects a **pointer** type (e.g., `ptr<T>`, `any`), 
but the provided argument is a **struct variable** (LValue) of type `T`, 
the compiler automatically passes the **address** of the variable.

This allows for "pass-by-reference" semantics without explicit 
address-of operators (`&` in C++).

### Example
```yaml
# Function expecting a pointer
- def:
    name: process_model
    args: [[m, ptr<Model>]]
    # ...

# Calling with a struct variable
- decl: [myModel, Model]
- call: [process_model, myModel] # Implicitly passes &myModel
```

## 2. Pointer Bitcasts

AIML automatically handles casting between compatible pointer types, primarily to support the generic `any` type.

*   **Any Pointer -> `any`**: Any pointer type (`ptr<T>`) can be passed to a function expecting `any`. The compiler injects a bitcast to the generic pointer type.
*   **`any` -> Any Pointer**: A value of type `any` can be passed to a function expecting `ptr<T>`. The compiler injects a bitcast from the generic pointer type to `ptr<T>`.

> [!WARNING]
> This is a raw pointer cast. It is the programmer's responsibility to ensure the `any` value actually points to a valid instance of the target type. Runtime validation is planned but not yet enforced.

## 3. Explicit Casting

Numeric value conversions use the `cast` operator; the complete matrix for `bool`/`i32`/`i64`/
`f32`/`f64` is normative in [`04_1_precedence.md` §7](04_1_precedence.md).

```yaml
- decl: [pixels, i32, 1280]
- decl: [ratio,  f32, { cast: [pixels, f32] }]   # int → float
- decl: [index,  i32, { cast: [ratio, i32] }]    # float → int, truncates toward zero
```

Casting `any` to a struct (copying through a pointer) is **not** implemented.

## 4. Heterogeneous Collections

`any` is not allowed as a container element type: `list<any>`, `map<K:any>`, `map<any:V>`,
`buffer<any>`, `chan<any>`, `array<any:N>`, `dyn_array<any>`, `iterator<any>` and `cascade<any>`
are compile errors (spec/02). An element's type class decides how it is retained and released
(spec/61 §2.1); an untyped element has none. Use an interface (spec/19 §2) or a common base model:

```yaml
- decl: [cards, list<ptr<BaseCard>>]      # or list<ptr<CardIface>> for an interface
```

## 5. Dereferencing (Accessing Values)

To access the value pointed to by a pointer (e.g. `any` cast to `ptr<T>`), use the dereference operator `?`:

```yaml
- decl: [val, i32]
- set: [val, { '?': [p] }] # Reads value from pointer p
```

## 6. Numeric Conversion on Assignment

**Target state (decided): there are no implicit casts.** Every numeric conversion is written out
with `cast` (§3). A value of the wrong numeric type is a compile error, not a silent conversion —
`f32` → `i32` loses information, `i64` → `f32` loses precision, and both are too easy to introduce
by accident to happen unannounced.

**Current state: implicit conversion still happens** at the sites below, and is kept for now
(decision 2026-10-09) because removing it changes every call site that relies on it. Treat it as
deviation, not as contract: new code writes the `cast` explicitly.

| Site | Implicit conversions performed |
|---|---|
| Function / method arguments (`Compiler::convertNumericArg`) | integer resize, integer→float, float↔float; a **real for an integer parameter is a compile error** (since 2026-10-29) |
| `return` value (`ast_compiler_stmt.cpp` ~4904) | integer→float |
| Default-value stores and member stores (`ast_compiler_stmt.cpp` ~158, ~2636, ~3571, ~3915, ~4207) | integer↔integer, `bool`→integer, integer→float, float↔float, **float→integer** |
| List / map / array literal elements (`ast_compiler_stmt.cpp` ~909, ~1197, ~1327, ~2286, ~2317) | integer resize, integer→float |
| `list.push` (`ast_compiler_call.cpp` ~2094) | integer resize, integer→float |
| Model / class field defaults (`ast_compiler.cpp` ~277) | integer resize, integer→float |
| `vec*`/`quat` scalar operand (`ast_compiler_expr.cpp` ~704) | integer→`f32`, `f64`→`f32` |
| Bitwise right operand (`ast_compiler_expr.cpp` ~771) | integer width match |

⚠️ **Contradiction to record.** The store path at `ast_compiler_stmt.cpp:169` converts
**float → integer** implicitly (`default_fp_to_int`). That is the one direction this chapter
previously declared non-implicit, and it is the most lossy of all of them. It is the first
candidate to remove when implicit conversion goes away.

Arithmetic operands are not covered by any of this: mixed-type operands are a compile error
(`04_1` §3), which is the rule the rest of the language is moving toward.
