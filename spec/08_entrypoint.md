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
