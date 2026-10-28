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

### 3.1a Ein gepunkteter Name ohne Argumente (Nachtrag 25.10.2026)

Steht in einer Einsetzung ein Name MIT Punkt, der keine Variable ist, aber eine **Funktion ohne
Argumente** benennt, wird sie gerufen und ihr Wert eingesetzt:

```yaml
- namespace: prm
- def: { name: branch, return: string, body: [{ return: "main" }] }

# in einer anderen Datei / einem anderen Bundle:
- print: ["git checkout ${prm.branch}"]      # git checkout main
```

Das ist derselbe Vorgang wie `${ { call: [prm.branch] } }`, nur lesbar.  Der Anlass ist ein
Namensraum voller Konstanten (cosmos `doc/spec/106` §106.6: die Parameter eines Jobs), und dort
steht die Einsetzung mitten in einer Shell-Zeile — die Klammerform macht sie unleserlich.

Die Regeln sind eng gezogen, damit nichts Bestehendes sich ändert:

- **nur mit Punkt.**  Ein nackter Funktionsname bleibt ein **Funktionszeiger** (so werden
  Rückrufe übergeben, z. B. `pipe.run(beobachter)`).
- **nur ohne Argumente** und **nur mit Rückgabewert**.  Eine Funktion mit Parametern lässt sich
  so nicht rufen, und `void` hätte nichts einzusetzen.
- **eine Variable gewinnt immer.**  `${a.b}` ist weiter das Feld `b` der Variablen `a`, wenn es
  eine Variable `a` gibt; erst wenn keine da ist, wird nach der Funktion gesucht.
- Eine bloß **deklarierte** Funktion zählt mit: in einem Compiler-Kontext (spec/29_2 §14a) steht
  ein früheres Bundle nur noch als Deklaration im Modul.  Ohne diesen Zweig wurde `${prm.branch}`
  zum Funktionszeiger und als Zeichenkette gedruckt — gemessen als Datenmüll in einem Befehl.

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

