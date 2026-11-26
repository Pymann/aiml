# Function Syntax

```yaml
# Definition
# Syntax: def: { name: ..., args: [[name, type], ...], return: type, body: [...] }
# - If 'return:' is omitted, the return type defaults to void.
# - If 'version:' is omitted, it defaults to 0.
#   Explicit version: is only required for hot-reload tracking (see 38_hot_reload.md).
# - Declaring two variables with the same name in the same scope is a CompilationError.

- def:
    name: add_two
    version: 1
    args: [[a, i32], [b, i32]]
    return: i32
    body:
      - set: [res, { +: [a, b] }]
      - return: [res]

# Void Function (return: omitted -> defaults to void)
- def:
    name: greet
    args: []
    body:
      - print: ["Hello World"]

# Function Call (as Statement)
- call: [greet]

# A void call has NO VALUE, so it cannot initialise or be assigned to anything:
#   - decl: [text, string, { call: [greet] }]   # compile error
#   - set:  [text, { call: [greet] }]           # compile error
# The message names the call and the target and points at the source line.  Use the statement
# form above, and give the function a `return:` when a value is wanted.
#
# This matters more than an ordinary type error: before it was reported (2026-11-03), a void
# value stored into a typed slot produced IR whose type LLVM walked in circles —
# getPrefTypeAlignment recursed until the 8 MB stack was gone and the compiler died of SIGSEGV
# with NO output at all, so the author saw only "build failed" and no file or line.  The common
# way into it is a function or method that LOSES its `return:` while its callers keep reading a
# value (measured in cai: `fertig` changed to write through a `ref<T>` out-parameter instead).
#
# What is checked: a direct call to a function of this unit, and a method call whose receiver
# resolves here — `<.m`, `<.member.m`, `local.m` — including through `ref<T>`/`ptr<T>`/
# `shared<T>`. A namespace function (`io.open`, `sys.exe`) is not a method and is left alone.
# A receiver this step cannot resolve falls through unreported: a wrong rejection would stop a
# build that was fine, which is worse than a gap.  Regression tests: `tests/void_init/`.

# Function Call (as Expression)
- set: [sum, { call: [add_two, 5, 10] }]
- print: [sum]

# Keyword Arguments & Default Values
# 1. Definition: Add 3rd element to args list for default value.
#    args: [[name, type, default_value], ...]
- def:
    name: connect
    args: [[host, string], [port, i32, 8080], [timeout, f64, 5.0]]
    return: void
    body:
      - print: ["Connecting to ${host}:${port} with timeout ${timeout}"]

# 2. Call semantics:
#    a) Positional (trailing defaults may be omitted):
#    - call: [connect, "localhost", 9000]  # timeout uses default 5.0
#    - call: [connect, "localhost"]        # port = 8080, timeout = 5.0
#    b) Named (trailing map overrides specific defaults):
#    - call: [connect, "localhost", {timeout: 10.0}]  # port = 8080

# 3. Model Validation (Auto-Unboxing)
# When passing a map<string:any> to a function, the system validates fields
# against the signature and unboxes compatible types.
#
# - def: { name: config, args: [[timeout, f64], [debug, bool]] }
#
# - decl: [options, map<string:any>]
# - call: [options.set, "timeout", 10.0]
# - call: [options.set, "debug", true]
# - call: [config, options]
#
# Logic:
# 1. "timeout" found in map? -> Yes. Value is 10.0 (f64). Compatible? -> Yes. Unbox.
# 2. "debug" found in map? -> Yes. Value true (bool). Compatible? -> Yes. Unbox.
# 3. Type mismatch (e.g. "timeout": "fast") -> TypeMismatchException.
```
