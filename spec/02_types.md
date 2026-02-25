# Type System

```yaml
# Implicit Typing (Inference from Literal)
- set: [i, 42]        # Inferred as i32
- set: [f, 3.14]      # Inferred as f64 (double)
- set: [s, "Hello"]   # Inferred as string (char*)
- set: [b, true]      # Inferred as bool (u8)

# Explicit Typing (Declaration)
# Syntax: decl: [name, type, (optional_value)]
- decl: [count, i32, 0]
- decl: [ratio, f64]   # Uninitialized (or default 0.0)
- decl: [name, string]

# Casting / Conversion
# Syntax: { cast: [value, target_type] }
- set: [x, { cast: [3.14, i32] }] # x is 3

# Type Safety
# - set: [i, "text"] -> Compiler Error (Type Mismatch)
# No implicit numeric promotion. Use cast: explicitly for all type conversions.

# Dynamic Typing — the 'any' type
#
# 'any' is a tagged variant type. It stores the value together with its runtime type tag.
# This allows heterogeneous containers (list<any>, map<string:any>) to preserve
# the original type of each stored element.
#
# Auto-unboxing:
# When a value is retrieved from a collection with 'any' value type,
# the runtime reads the type tag and returns the value as its REAL type.
# The receiving variable (via set:) inherits this real type.
#
# Example:
- decl: [m, map<string:any>]
- call: [m.put, "age", 30]         # stored as i32, tagged
- call: [m.put, "name", "Alice"]   # stored as string, tagged
- set: [age, { call: [m.get, "age"] }]    # age is i32 — auto-unboxed
- set: [name, { call: [m.get, "name"] }]  # name is string — auto-unboxed

# Comparisons work on auto-unboxed values:
- if:
    condition: { eq: [age, 30] }   # OK: age is i32
    then:
      - print: ["age is 30"]

# When declared explicitly as 'any', comparisons require explicit cast:
# because the static type is unknown at compile time.
- decl: [dynamic_val, any]
- set: [dynamic_val, 42]
# { eq: [dynamic_val, 42] } -> requires cast: first
- set: [typed_val, { cast: [dynamic_val, i32] }]
- if:
    condition: { eq: [typed_val, 42] }
    then:
      - print: ["match"]
```

## `any` Summary

| Context | Behaviour |
|---------|-----------|
| Stored in `list<any>` / `map<K:any>` | Tagged with runtime type; auto-unboxed on retrieval |
| Retrieved via `get()` / index access | Returns **real type** — `set:` infers the correct type |
| Explicit `decl: [x, any]` | Static type unknown; requires `cast:` for type-specific operations |
| Comparing `any` vs specific type directly | `TypeMismatchException` at runtime unless cast first |
