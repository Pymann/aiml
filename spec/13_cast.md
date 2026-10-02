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

## 3. Explicit Casting (Future)

While implicit casts handle common pointer scenarios, explicit casting will be required for value conversions (e.g., `f64` -> `int`, `any` -> `struct` copy).

```yaml
# Proposed Syntax
- set: [val, { cast: [any_val, MyStruct] }] # Copies data from pointer
```

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
