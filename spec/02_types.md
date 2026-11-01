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
#
# `set` takes EXACTLY two elements: [target, value].  A third element is a compile
# error — it would otherwise be swallowed without a trace.  The usual cause is an
# index write, which is its own statement:
# - set: [nums, 0, 99]            -> Compiler Error (use idx:)
# - idx: [nums, 0, 99]            # write 99 at index 0 (spec/10)
#
# A container target does not take a scalar.  Assigning one used to write the scalar
# into the container slot and corrupt it (garbage `.len`, then SIGSEGV):
# - set: [nums, 99]               -> Compiler Error (nums is list<i32>)
# - call: [nums.push, 99]         # one element (spec/10)
# - set: [nums, [1, 2, 3]]        # the whole contents (spec/10)
# Only unambiguous cases are rejected: a literal, or a variable of a known scalar
# type.  A call or an interpolation is left alone rather than risk forbidding
# something correct.

# Dynamic Typing — the 'any' type
#
# 'any' is a single untyped value (a generic pointer / tagged scalar). It is NOT allowed as an
# element type: list<any>, map<K:any>, map<any:V>, buffer<any>, chan<any>, array<any:N>,
# dyn_array<any>, iterator<any> and cascade<any> are compile errors. A container's element class
# drives its release (spec/61 §2.1); an untyped element has none. Heterogeneous collections use
# an interface (spec/19 §2, list<ptr<Iface>>) or a common base model (list<ptr<Base>>).
#
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
| As a container element type (`list<any>`, `map<K:any>`, `buffer<any>`, `chan<any>`, `array<any:N>`, …) | **Compile error** — use an interface (spec/19 §2) or a base model |
| Explicit `decl: [x, any]` | Static type unknown; requires `cast:` for type-specific operations |
| Comparing `any` vs specific type directly | `TypeMismatchException` at runtime unless cast first |
