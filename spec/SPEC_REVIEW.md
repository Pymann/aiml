# AIML Spec Review — Contradictions & Inconsistencies
> Generated 2026-02-25 | 40 files reviewed | 71 issues found

---

## Summary

| Category | Count | CRITICAL | MAJOR | MINOR |
|----------|-------|----------|-------|-------|
| Type System Contradictions | 3 | 2 | 1 | — |
| Syntax Inconsistencies | 5 | — | 4 | 1 |
| Undefined / Unspecified Behavior | 18 | 6 | 10 | 2 |
| YAML Parsing Conflicts | 3 | 2 | 1 | — |
| Duplicate / Copy-Paste Content | 13 | — | — | 13 |
| Missing Specs | 14 | 6 | 8 | — |
| Logic & Design Inconsistencies | 7 | 3 | 4 | — |
| Architectural Inconsistencies | 3 | — | 1 | 2 |
| Terminology Inconsistencies | 2 | — | 1 | 1 |
| Feature Interaction Problems | 3 | 2 | 1 | — |
| **TOTAL** | **71** | **21** | **41** | **9** |

---

## 1. TYPE SYSTEM CONTRADICTIONS

### 1.1 `int` vs `i32` — No Alias Defined
**Severity**: MAJOR
**Files**: `01_primitive_types.md`, `07_functions.md`
`01_primitive_types.md` defines `i32` as the signed 32-bit integer. `07_functions.md` uses `int` in examples (lines 9, 75). `int` is never defined as a type alias.
**Fix**: Add `int` as alias for `i32` in `01_primitive_types.md`, or replace all `int` with `i32`.

#Replace all int with i32, int should not be available

---

### 1.2 `bool` — `u8` vs `i1`
**Severity**: CRITICAL
**Files**: `01_primitive_types.md`, `02_types.md`
- `01_primitive_types.md` line 8: `bool` → LLVM `u8`
- `02_types.md` line 8: `set: [b, true]  # Inferred as bool (i1)`
**Fix**: Choose one. `i1` is LLVM-native; `u8` is C-compatible. Document the choice.

bool is always u8
---

### 1.3 `any` Type Validation Semantics Undefined
**Severity**: CRITICAL
**Files**: `02_types.md`, `13_cast.md`
`any` type is introduced but no spec defines what happens on runtime type mismatch, how validation errors are reported, or what exception is thrown.
**Fix**: Define explicit runtime validation semantics and error type.

comparison on anytype is always mismatch


## 2. SYNTAX INCONSISTENCIES

### 2.1 `.len` Property vs `call: [.size]` Method
**Severity**: MAJOR
**Files**: `10_collections.md`
- Line 21: `- set: [len, scores.len]` (property, no call)
- Line 58: `${ { call: [my_list.size] } }` (method call required)
Both `.len` and `.size` appear for the same concept with different call syntax.
**Fix**: Define whether collection sizes are properties or zero-arg methods and use one consistently.

#size operator is intrinsic methode, means call is required, returning count of bytes
#len is property of instance (string, map, list)

---

### 2.2 `print:` — String vs List Syntax
**Severity**: MAJOR
**Files**: `09_strings.md`, `09_1_string_expansions.md`
- `09_1_string_expansions.md` line 11: `- print: "Hello, ${name}!"`
- `09_strings.md` line 24: `- print: ["Hello ${name}, ..."]`
**Fix**: Decide: `print:` accepts either a bare string or a list. Document it.

# the bare-metal print for wrapping printf will be removed later, lets use list only
---

### 2.3 `??` Dereference Operator Ambiguity
**Severity**: MAJOR
**Files**: `12_pointers.md`
`?` for single dereference, `??` for double — but YAML may interpret `??` ambiguously.
**Fix**: Define unambiguous syntax or use alphanumeric keywords (`deref`).

?? is not a standard yaml operator, we can use it
---

### 2.4 `[op, args]` List vs `{ op: [args] }` Dict for Operations
**Severity**: MAJOR
**Files**: `04_math.md`
- Line 27: `- set: [my_list, [+, a, b]]` — "this is a list, NOT an operation"
- Line 42: `- set: [res, { *: [ { +: [a, b] }, 2 ] }]` — dict syntax for operation
No parser rule distinguishes these without context.
**Fix**: Mandate: operations MUST use `{ op: [...] }`. Lists MUST use `[...]`. Never mix.

# yes only dict with one element for operations

---

### 2.5 `.get_id` Property vs Method
**Severity**: MINOR
**Files**: `26_concurrency.md`
Line 31: `- set: [id, t1.get_id]` — property access on what should be a method call.
**Fix**: Standardize: either `t1.id` (property) or `{ call: [t1.get_id] }` (method).

# no standard getter/setter should be generated, only property access is provided out of the box
---

## 3. UNDEFINED / UNSPECIFIED BEHAVIOR

### 3.1 `map.get()` on Missing Key
**Severity**: CRITICAL
**Files**: `10_collections.md`
Line 80 comment: `# Retrieve (Error if missing?) # Or returns pointer? (null if missing)` — three options listed, none chosen.
**Fix**: Define: throw `KeyNotFoundException` or return `null` (requires `any` return type).

# lets define the global null-pointer to compare with, its has generic pointer type and can be assigned to any pointer-type, it is string "null", get will return null-pointer if key is not found
---

### 3.2 Array Out-of-Bounds Behavior
**Severity**: CRITICAL
**Files**: `10_collections.md`, `33_security.md`
`33_security.md` says "bounds checking ON by default" but neither file specifies what exception is raised or what the behavior is.
**Fix**: "Out-of-bounds access raises `IndexOutOfBoundsException`."

#yes bounds checking is on by default, and raise IndexOutOfBoundsException
---

### 3.3 File I/O Error Handling
**Severity**: MAJOR
**Files**: `23_system.md`
Line 24: `# Check if open failed? (f == null)` — comment but no answer.
**Fix**: Define: `io.open` returns `null` on failure (with `null` check required), or throws `IOException`.

---

### 3.4 Buffer Overflow/Underflow Triggers
**Severity**: CRITICAL
**Files**: `11_buffer.md`, `22_exceptions.md`
`22_exceptions.md` defines `BufferOverflowException` / `BufferUnderflowException` but `11_buffer.md` never states when they are thrown.
**Fix**: "`write()` when `size + len > capacity` → `BufferOverflowException`. `read()` past written data → `BufferUnderflowException`."

# yes overflow is when size + len > capacity, underflow is when read past written data
---

### 3.5 Division by Zero
**Severity**: MAJOR
**Files**: `22_exceptions.md`, `04_math.md`
`DivisionByZeroException` is defined but `04_math.md` never mentions it.
**Fix**: Add note to `04_math.md`: "Integer division by zero raises `DivisionByZeroException`."

# yes division by zero raises DivisionByZeroException
---

### 3.6 Default Access Modifier — "Let's say private"
**Severity**: MAJOR
**Files**: `03_modifiers.md`
Line 17: `# Default is 'private' or 'public'? Let's say default is 'private'` — phrased as a live design decision, not a specification.
**Fix**: Commit to `private` by default; remove the hedging comment.

# yes default is private
---

### 3.7 Process `returncode: i32` vs `null` While Running
**Severity**: MAJOR
**Files**: `23_system.md`
Line 92: `returncode: i32  # Exit status (null if running)` — type is `i32` but comment says `null`.
**Fix**: Change type to `i32?` (optional/nullable) or use sentinel `-1`.

# yes returncode is i32?, on exception we should return 2
---

### 3.8 Thread Detach Resource Cleanup
**Severity**: MAJOR
**Files**: `26_concurrency.md`
`detach()` — "Resources cleaned up automatically on exit." What resources? Stack? TLS?
**Fix**: Specify exactly what is freed on thread exit after detach.

# I dont know enough about threads to answer this question
---

### 3.9 `join()` on Detached Thread
**Severity**: MAJOR
**Files**: `26_concurrency.md`
No specification of what happens if `join()` is called on an already-detached thread.
**Fix**: "Raises `InvalidThreadStateException`."

# yes 

---

### 3.10 `sys.*` Namespace — What Exactly Is Reserved?
**Severity**: CRITICAL
**Files**: `21_namespaces.md`
Line 5: "Namespaces with *sys* at start will be forbidden" — but no list of reserved symbols is given.
**Fix**: Provide the list of reserved `sys.*` symbols, or clarify: "the entire `sys` prefix is reserved."

# No Namespaces with sys, time, io can be created
---

### 3.11 Exception Propagation — Unhandled Exceptions
**Severity**: CRITICAL
**Files**: `22_exceptions.md`
No specification of what happens when no `catch` matches or no `try` block exists.
**Fix**: "Unhandled exceptions propagate to caller. If unhandled at top-level, program calls `sys.exit(2)` with error message to stderr."

# yes unhandled exceptions propagate to caller. If unhandled at top-level, program calls sys.exit(2) with error message to stderr.

---

### 3.12 Variable Shadowing Rules
**Severity**: MAJOR
**Files**: all
No specification of whether inner-scope declarations may shadow outer-scope names.
**Fix**: Define explicitly: allowed (with warning?) or compilation error.

# is compilation error
---

### 3.13 Default Return Type for Functions Without `return:`
**Severity**: MAJOR
**Files**: `07_functions.md`
Function definitions sometimes omit `return:`. No default defined.
**Fix**: "Functions without explicit `return:` default to `void`."

# yes default return type is void
---

### 3.14 Module-Level Symbol Export Rules
**Severity**: MAJOR
**Files**: `21_namespaces.md`
No specification of which symbols are exported vs module-private.
**Fix**: "Symbols without explicit `public` access modifier are module-private."

# have not completely understood, symbols of namespace shall be public. is it related to classes?
---

### 3.15 Stack vs Heap Allocation Decision
**Severity**: MAJOR
**Files**: `16_classes.md`, `18_instantiation.md`
Comment: "p_stack is a 'Value' (internally a pointer to the stack slot)" — unclear semantics.
**Fix**: "Direct class declaration → stack (`alloca`). `{ new: ClassName }` → heap (`malloc`)."

# yes direct class declaration -> stack (alloca). { new: ClassName } -> heap (malloc).
---

### 3.16 Circular Import Handling
**Severity**: MAJOR
**Files**: `21_namespaces.md`
"Each file compiled only once" but circular dependencies (A imports B, B imports A) not addressed.
**Fix**: "Circular imports raise `CircularImportError` at compile time."

# each import is compiled only once, the __aiml_cache knows whats already compiled
# circular deps is allowed therefore.
---

### 3.17 Import Path Resolution (`../`)
**Severity**: MAJOR
**Files**: `21_namespaces.md`
Import paths said to be relative to CWD. No mention of `../` or absolute paths.
**Fix**: "Paths are relative to the importing file's directory. `../` is supported. Absolute paths are not."

# absolute paths in import-statements is forbidden
---

### 3.18 Generic Types — User-Defined?
**Severity**: CRITICAL
**Files**: `10_collections.md`, `15_models.md`
`list<T>`, `map<K:V>`, `ptr<T>`, `chan<T>` exist. No spec on whether users can define generic functions or types.
**Fix**: "Generics are currently built-in only. User-defined generics are not supported in v1."

# what do you mean by generic-type?

---

## 4. YAML PARSING CONFLICTS

### 4.1 `map<string:i32>` — Colon in Type Parameter
**Severity**: CRITICAL
**Files**: `01_primitive_types.md`, `37_parsing_and_jit_internals.md`
`map<string:i32>` uses `:` which is YAML's key-value separator. Acknowledged in `37_parsing_and_jit_internals.md` but resolution is vague.
**Fix**: Formalize the parsing rule in `37_parsing_and_jit_internals.md`: "type parameters are only parsed inside `<>` angle brackets; `:` inside `<>` is not YAML key syntax."

# yes formalize parsing rule

---

### 4.2 Nested Maps in String Interpolation
**Severity**: CRITICAL
**Files**: `09_1_string_expansions.md`
`"Result: ${ { call: [add, 10, 20] } }"` — nested `{ }` inside `${ }` inside a YAML string can confuse parsers.
**Fix**: Document exact parsing rule. Consider alternative syntax like `$[[ call: [add, 10, 20] ]]`.

# so far it did not confuse, we can keep as is.
---

### 4.3 `[+, a, b]` — List with Operator Symbol
**Severity**: MAJOR
**Files**: `04_math.md`
`+` as first element of a YAML sequence is ambiguous — YAML may parse `+` as a special scalar.
**Fix**: Reserve operator syntax for `{ op: [...] }` dict form only. Remove list-with-operator examples.

# yes remove list-with-operator examples

---

## 5. DUPLICATE / COPY-PASTE CONTENT

The following files contain verbatim duplicates of their own content (entire second half is copy-pasted from the first):

| File | Duplicate lines |
|------|----------------|
| `02_types.md` | Lines 36–65 duplicate lines 3–35 |
| `03_modifiers.md` | Lines 46–85 duplicate lines 1–45 |
| `04_math.md` | Lines 30–53 duplicate lines 3–29 |
| `05_logic.md` | Lines 23–38 duplicate lines 1–22 |
| `06_loops.md` | Lines 17–24 duplicate lines 3–15 |
| `07_functions.md` | Lines 73–139 duplicate lines 4–72 |
| `08_entrypoint.md` | Lines 42–76 duplicate lines 3–41 |
| `09_strings.md` | Lines 49–91 duplicate lines 3–48 |
| `10_collections.md` | Lines 48–89 duplicate lines 4–47 |
| `11_buffer.md` | Lines 64–118 duplicate lines 8–62 |
| `12_pointers.md` | Lines 34–61 duplicate lines 3–33 |
| `14_enums.md` | Lines 33–55 duplicate lines 6–32 |
| `17_classes_examples.md` | Lines 63–118 duplicate lines 6–62 |

**Fix**: Remove duplicate halves from all 13 files.

# yes remove duplicate halves from all 13 files.

---

## 6. MISSING SPECS

### 6.1 Operator Precedence and Associativity
**Severity**: CRITICAL
No file defines precedence rules for `+`, `*`, `and`, `or`, comparisons, etc.
**Fix**: Add `40_operator_precedence.md` with explicit precedence table.

# we have defined in 04_1_precedence.md

---

### 6.2 String Escape Sequences
**Severity**: MAJOR
**Files**: `09_strings.md`
Strings are used throughout but valid escape sequences (`\n`, `\t`, `\x41`, etc.) are never listed.
**Fix**: Add to `09_strings.md`: "Supported escapes: `\n`, `\t`, `\r`, `\\`, `\"`, `\xHH`."

# yes supported escapes: \n, \t, \r, \\, \", \xHH.

---

### 6.3 Abstract Methods / Interface Enforcement
**Severity**: MAJOR
**Files**: `19_inheritance.md`
Interfaces are shown but no enforcement rule defined. Can a class partially implement an interface?
**Fix**: "All interface methods MUST be implemented or a `CompilationError` is raised."

# yes all interface methods must be implemented or a CompilationError is raised.

---

### 6.4 Operator Overloading Support
**Severity**: MAJOR
No file mentions whether `+`, `-`, etc. can be overloaded by user-defined classes.
**Fix**: "Operator overloading is not supported in AIML v1."

# yes operator overloading is not supported in AIML v1.
# you can start spec in 40_overloading.md

---

### 6.5 Memory Alignment and Padding
**Severity**: MAJOR
**Files**: `16_classes.md`
`packed: true` is mentioned but default alignment rules are never specified.
**Fix**: "Default alignment follows target platform rules. `packed: true` uses 1-byte alignment."

# default alignment follows target platform rules. packed: true uses 1-byte alignment.

---

### 6.6 Null Pointer Check Semantics
**Severity**: CRITICAL
**Files**: `33_security.md`, `12_pointers.md`
`33_security.md` says "Null pointer checks ON by default" but no spec says what exception is raised.
**Fix**: "Dereferencing null raises `NullPointerException`. Disable with `unsafe` block."

# dereferencing null raises NullPointerException. Disable with unsafe block.

---

### 6.7 Namespace FQN Collision Handling
**Severity**: MAJOR
**Files**: `21_namespaces.md`
Two imported files defining the same FQN — behavior undefined.
**Fix**: "Duplicate FQNs raise `NamespaceCollisionError` at compile time."

# duplicate fqns raise NamespaceCollisionError at compile time.

---

### 6.8 `Model` vs `Class` — Semantic Distinction
**Severity**: MAJOR
**Files**: `15_models.md`, `16_classes.md`
Models and classes look nearly identical syntactically. The difference (data vs behavior) is implied but never stated.
**Fix**: "Models are data-only types designed for serialization/validation. Classes may have methods. Models cannot have methods."

# models are data-only types designed for serialization/validation. Classes may have methods. Models cannot have methods.

---

### 6.9 `extends` vs `models` — Inheritance vs Composition
**Severity**: MAJOR
**Files**: `19_inheritance.md`, `17_classes_examples.md`
- `extends: Shape` for inheritance
- `models: [Entity]` for mixin
Semantics of `models:` mixin never formally defined.
**Fix**: Clarify: "`extends:` = single inheritance. `models:` = structural composition (field injection, no method inheritance)."

# extends: = single inheritance. models: = structural composition (field injection, no method inheritance).

---

### 6.10 `version:` on Functions — Semantics
**Severity**: MAJOR
**Files**: `07_functions.md`, `38_hot_reload.md`
`version: 1` appears on functions. Does every function need a version? Default value?
**Fix**: "Function version defaults to `0`. Explicit `version:` is required only for hot-reload tracking."

# function version defaults to 0. Explicit version: is required only for hot-reload tracking.

---

### 6.11 Named Argument / Default Parameter Call Semantics
**Severity**: MAJOR
**Files**: `07_functions.md`
Default parameter syntax defined but call semantics not. Can arguments be omitted? In what order?
**Fix**: "Trailing default parameters may be omitted. Named parameters must be passed as a trailing map."

# trailing default parameters may be omitted. Named parameters must be passed as a trailing map.

---

### 6.12 Agent Context Auto-Injection — Which Symbols?
**Severity**: CRITICAL
**Files**: `29_agents.md`
"Compiler automatically generates hidden `__ctx` parameter" — no specification of which globals are injected.
**Fix**: "All globals referenced in the function body are injected. Mark with `@context` to enable explicit injection."

# reason why we refactored to direct yaml streaming and AST creation is, that we would like to conclude th context automatically.

---

### 6.13 Hot Reload + In-Flight Exception Interaction
**Severity**: CRITICAL
**Files**: `38_hot_reload.md`, `22_exceptions.md`
If a function is hot-reloaded while an exception from that function is being handled, behavior undefined.
**Fix**: "Exceptions complete in the version that raised them. Hot reload takes effect only for new calls."

---

### 6.14 Unsafe Code Serialization for Agents
**Severity**: CRITICAL
**Files**: `29_agents.md`, `33_security.md`
Whether `unsafe` blocks can be bundled and sent to remote agents is unspecified.
**Fix**: "`unsafe` blocks cannot be serialized. Attempting to bundle them raises a compile-time error."

#sry but there is no reason not to serialize them.

---

## 7. CRITICAL ISSUES — RESOLUTION PRIORITY

| # | Issue | Files |
|---|-------|-------|
| 1 | `bool` = `u8` vs `i1` | `01_primitive_types.md`, `02_types.md` |
| 2 | `map.get()` on missing key — undefined | `10_collections.md` |
| 3 | Exception propagation — unhandled case | `22_exceptions.md` |
| 4 | Buffer overflow/underflow triggers | `11_buffer.md`, `22_exceptions.md` |
| 5 | Null pointer check exception type | `33_security.md`, `12_pointers.md` |
| 6 | `any` type validation semantics | `02_types.md`, `13_cast.md` |
| 7 | `sys.*` reserved names — no list | `21_namespaces.md` |
| 8 | Generic types — user-defined? | `10_collections.md` |
| 9 | Agent `__ctx` injection — which symbols | `29_agents.md` |
| 10 | Hot reload + exception interaction | `38_hot_reload.md`, `22_exceptions.md` |
| 11 | Unsafe code serialization | `29_agents.md`, `33_security.md` |
| 12 | `map<K:V>` YAML parsing rule | `01_primitive_types.md` |
| 13 | Nested `${ { ... } }` parsing | `09_1_string_expansions.md` |
| 14 | Circular import resolution | `21_namespaces.md` |
| 15 | Operator precedence — no table | *(missing file)* |
