# Entry Point Definition

```yaml
# 1. Implicit Entry Point (Script Mode)
# If no entrypoint is defined, top-level instructions are executed in order.
# This behaves like Python or JavaScript.

- print: ["Starting script..."]
- decl: [x, i32, 10]
- print: [x]

# 2. Explicit Entry Point (Application Mode)
# Useful for larger applications where you want specific initialization.
# Specified in the 'config' block.

- config:
    entrypoint: main

- def:
    name: main
    args: []
    return: i32
    body:
      - print: ["Hello from Main!"]
      # 'helper' is executed ONLY because 'main' calls it.
      - call: [helper]
      - return: 0

- def:
    name: helper
    args: []
    return: void
    body:
      - print: ["Helper called!"]
```

## Top-Level Execution Order

AIML guarantees **strict source-order** execution for top-level statements
in both modes:

- **Main file** (the file passed to `aiml`): top-level statements run in the
  order they appear inside a synthetic `__aiml_init` function that the
  compiler invokes from the C entry-point *before* it calls `main`.
- **Imported library file** (`- import: "lib.aiml"`): top-level statements
  run in source order inside `__aiml_lib_init_<file>`, which the main
  file's `__aiml_init` calls before any of its own top-level code.

```yaml
# This file's top-level statements run as:
# 1. db.set_path("./mydata/")      ← runs first
# 2. db.load_dictionaries()        ← runs second
# 3. decl 'ready' is set to 1      ← runs third (the init-call sees the prior state)
- call: [db.set_path, "./mydata/"]
- call: [db.load_dictionaries]
- decl: [ready, i32, { call: [check_state] }]
```

The same ordering rule applies inside an imported library — a `decl` with
a call-initializer is **not** hoisted before sibling imperative statements
of the same library.

### Container Globals are Initialised First

A `decl` of a container type (`list<T>`, `map<K:V>`, `chan<T>`, `buffer<T>`)
at the top level allocates its runtime storage in a prologue *before* any
user-code runs — analogous to C++ static-storage zero-init.  This lets
later top-level statements (whether in the same file or in a function
they call) read or mutate the container without seeing an uninitialised
state, regardless of where the `decl` appears in source.

```yaml
- def:
    name: mark
    args: [[v, i32]]
    body:
      - call: [order_log.push, v]  # safe: order_log already initialised

- call: [mark, 1]                  # uses order_log → OK
- decl: [order_log, list<i32>]     # declaration appears AFTER the use
```

This rule applies to library files (`__aiml_lib_init_*`) as well as the
main file (`__aiml_init`).

### Imperative Statements at the Top Level

By default, top-level imperative statements (`- call:`, `- set:`, `- if:`,
`- while:`, `- decl:` with a `call(...)` initializer) are allowed and run
in source order.  Setting the environment variable `AIML_STRICT_TOPLEVEL=1`
forbids them and forces all imperative code into a `main` function — see
[49_static_analysis.md §11.1](49_static_analysis.md).

### Forbidden: Void-Call Initializers

Storing a value of type `void` (the result of a call to a function with no
declared `return:`) into a typed location is a hard compile-error:

```yaml
- def:
    name: side_effect
    args: [[v, i32]]
    body: [- print: ["${v}"]]      # ← no return: → void

- decl: [_a, i32, { call: [side_effect, 1] }]  # ERROR
- set: [some_i32, { call: [side_effect, 2] }]  # ERROR
```

The error names the variable, type, and the void-returning function, and
suggests two fixes: add `return: <T>` to the callee, or detach the call
into its own `- call: [side_effect, ...]` statement.
