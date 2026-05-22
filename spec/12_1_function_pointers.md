# Function Pointers

Function pointers are first-class values in AIML. The type is written using `fn<>`.

## Syntax

```
fn<>                          # no parameters, no return value
fn<i32>                       # one parameter, no return value
fn<i32, string>               # two parameters, no return value
fn<:i32>                      # no parameters, returns i32
fn<i32:string>                # one parameter, returns string
fn<i32, string:bool>          # two parameters, returns bool
```

Parameters are positional and described only by their type.
The return type is specified with `:` after the last parameter.
Without `:` the return value is `void`.

## Declaration and Assignment

```aiml
- def:
    name: add
    args: [[a, i32], [b, i32]]
    return: i32
    body:
      - return: { +: [a, b] }

- decl: [fp, fn<i32, i32:i32>]
- set: [fp, add]

- decl: [result, i32]
- set: [result, { call: [fp, 3, 4] }]
# result == 7
```

## Calling

A function pointer is called with `call` — identical to direct function calls:

```aiml
- call: [fp, arg1, arg2]
- set: [x, { call: [fp, arg1] }]
```

## As Parameters

```aiml
- def:
    name: apply
    args: [[f, fn<i32:i32>], [x, i32]]
    return: i32
    body:
      - return: { call: [f, x] }

- def:
    name: double
    args: [[n, i32]]
    return: i32
    body:
      - return: { *: [n, 2] }

- decl: [r, i32]
- set: [r, { call: [apply, double, 5] }]
# r == 10
```

## In Collections

```aiml
- decl: [handlers, map<string:fn<http_request, http_response>>]
- call: [handlers.put, "home", handle_home]
- call: [handlers.put, "about", handle_about]

- decl: [h, fn<http_request, http_response>]
- set: [h, { call: [handlers.get, "home"] }]
- call: [h, req, resp]
```

## Null Check

An uninitialized function pointer is `null`.
Calling a `null` pointer raises a `NullPointerException`.

```aiml
- decl: [cb, fn<>]
# cb is null

- if: [{ !=: [cb, null] }, true]
  then:
    - call: [cb]
```

## Nested Generics

Types with their own type parameters work as parameters and return values:

```aiml
fn<list<i32>:i32>             # takes list<i32>, returns i32
fn<buffer<u8>>                # takes buffer<u8>, no return value
fn<map<string:string>:bool>   # takes map, returns bool — : after > is unambiguous
```

## Type Matching

Two function pointer types are identical if the parameter count, parameter types (in order),
and return type match. No implicit conversion of any kind.

```aiml
- def:
    name: greet
    args: [[name, string]]
    return: string
    body:
      - return: "Hello ${name}"

- decl: [f, fn<string:string>]
- set: [f, greet]  # OK

- decl: [g, fn<i32:string>]
- set: [g, greet]  # Error: parameter type does not match
```
