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

### 3.1a A dotted name without arguments (addendum 25.10.2026)

If an expansion contains a name WITH a dot that is not a variable but names a **function without
arguments**, it is called and its value is substituted:

```yaml
- namespace: prm
- def: { name: branch, return: string, body: [{ return: "main" }] }

# in another file / another bundle:
- print: ["git checkout ${prm.branch}"]      # git checkout main
```

This is the same operation as `${ { call: [prm.branch] } }`, only readable.  The motivation is a
namespace full of constants (cosmos `doc/spec/106` §106.6: a job's parameters), where the
expansion sits in the middle of a shell line — the braced form makes it unreadable.

The rules are drawn tightly so that nothing existing changes:

- **only with a dot.**  A bare function name remains a **function pointer** (that is how
  callbacks are passed, e.g. `pipe.run(observer)`).
- **only without arguments** and **only with a return value**.  A function with parameters
  cannot be called this way, and `void` would have nothing to substitute.
- **a variable always wins.**  `${a.b}` is still the field `b` of the variable `a` if a variable
  `a` exists; only when there is none is a function looked up.
- A merely **declared** function counts too: in a compiler context (spec/29_2 §14a) an earlier
  bundle is present in the module only as a declaration.  Without that branch `${prm.branch}`
  became a function pointer and was printed as a string — measured as garbage inside a command.

### 3.2 Method Calls

Object methods (on lists, maps, or custom classes) can also be called.

```yaml
- decl: [my_list, list<i32>]
- call: [my_list.push, 100]

- print: ["List size: ${my_list.len}"]
# Output: List size: 1
```

### 3.3 Math and Logic

Binary operations (like `+`, `-`, `*`, `/`) work as expected.  Symbolic operator
keys are quoted here — `+`, `-`, `/` may stand bare, `*` and `%` may not
(spec/04 "Quoting operator keys", in an expansion §3.5).

```yaml
- decl: [x, i32, 5]
- print: ["Calculated: ${ { +: [x, 5] } }"]
# Output: Calculated: 10
```

### 3.4 Quotation marks in an expansion — escaped only (corrected 2026-11-07)

An expansion lies **within** a YAML scalar.  A **bare** double
quotation mark in the expansion therefore ends this scalar, and the rest of the line
is read as YAML structure:

```yaml
# WRONG — the quotation mark before xyz closes the scalar
- print: ["name: ${ { call: [etikett, "xyz"] } }"]
```

What arrives is a structure error, phrased in YAML grammar and often far away from the
cause:

```
YAML parse error: yaml-cpp: error at line 12, column 46: end of sequence flow not found
```

The compiler attaches a hint to this error that names the lines with the pattern.

**Escaped, it works.**  `\"` is a valid escape in a double-quoted YAML scalar;
yaml-cpp passes the scalar on with the quotation mark, and the
expansion body is read correctly:

```yaml
- print: ["name: ${ { call: [etikett, \"xyz\"] } }"]   # name: [xyz]
- print: ["name: ${ { call: [<.label, \"x\"] } }"]     # name: [x]   (also as a member)
```

Until 2026-11-07 this section said the construct could **not be written at all**, and the counterexample
was itself escaped — i.e. exactly the form that works.  Both measured on 2026-11-07.
Whoever still does not want to escape the line evaluates the call beforehand:

```yaml
- decl: [shown, string, { call: [<.label, "x"] }]
- print: ["name: ${shown}"]
```

Single quotation marks do not help: the AIML parser reads the expansion body as
YAML, and there `'x'` is the same token as `"x"` — bare, it ends the outer scalar
just the same.

### 3.5 Operator keys in an expansion (2026-11-07)

For operators in an expansion the rule from spec/04 "Quoting operator keys"
applies unchanged: **every symbolic operator key is quoted**.  In an
expansion the quotation mark needs the escaped form from §3.4:

```yaml
- decl: [lauf, i32, 143]
- print: ["rest=${ { \"%\": [lauf, 40] } }"]   # rest=23
```

Until 2026-11-07 the bare key was **silent** here.  `${ { %: [lauf, 40] } }` is
not valid YAML (spec/04, table), `YAML::Load` throws, and `parseInterpolatedString`
fell back to "then it is just text": the string literally received
`${ %: [lauf, 40] }`.  The compiler reported nothing, the exit was 0.  Measured on
2026-11-06 on a measurement probe with 300,000 rows that stored this text as a field value —
all 300,000 rows with the same value, and the index over it had exactly one entry.

Since 2026-11-07 this is a hard error:

```
line 6: operator key '%' in an expansion must be quoted -- a bare '%' is a YAML indicator
character, so ${ { %: [lauf, 40] } } is not valid YAML and was silently left as text.
Write ${ { \"%\": [...] } } (spec/09_1 §3.5).
```

The limit of the rule: an expansion body that cannot be read as an expression and
contains **no** bare operator key still remains literal text.  That
is intended and measured — `"so schreibt man ${...} im Text"` is prose about expansions
and appears nine times in the corpus (12,024 .aiml files).

The check therefore hangs on **two** conditions: the body has already failed (throw
or no expression) **and** it carries a bare operator key.  The first condition
is the important one: code that compiles today never gets past this point, so the new
message cannot break it.  All cases are drivers in
`tests/_opkey/run.sh` (14 checks, runs along in the suite).

## 4. Implicit Type Conversion (String -> Int)

Whenever the result of a string assignment is a `ptr<i8>` (i.e., a string) but the target
variable is `i32` or `i64`, the compiler **automatically inserts `atoi` / `atoll`**.

This covers all forms of string assignment, not just complex expressions:

```yaml
# Expression expansion
- decl:
    name: result
    type: i32
    value: "${ { +: [10, 20] } }"
# Expansion produces "30" (string), atoi converts to 30 (int).

# Simple variable expansion
- decl: [s, string, "99"]
- decl: [n, i32, "${s}"]   # atoi called → n == 99

# set: statement
- decl: [m, i32]
- set:  [m, "${s}"]        # atoi called → m == 99

# i64 variant uses atoll
- decl: [big, i64]
- set:  [big, "${s}"]      # atoll called → big == 99
```

The coercion is triggered by the **target type**, not the expression form. Any string value
(literal, interpolated, or returned by a function) assigned to `i32`/`i64` goes through this
conversion automatically.

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

