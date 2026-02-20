# String Expansion / Interpolation

```yaml
# Question: Runtime vs Compile Time?
# Answer:
# - If variables are dynamic (changed at runtime), we MUST do it at Runtime.
# - If variables are `const` or literals, we CAN optimize at Compile Time.
# -> We should implement **Runtime Expansion** as the default.

# Syntax Recommendation: "${var}"
- decl: [name, string]
- set: [name, "Alice"]
- decl: [age, i32]
- set: [age, 30]

# Runtime Expansion
# Compiler translates this to:
#   string_builder sb;
#   sb.append("Hello ");
#   sb.append(name);
#   sb.append(", you use ");
#   sb.append(std::to_string(age)); 
#   ...
- print: ["Hello ${name}, you are ${age} years old."]

# Complex Expressions inside?
# - print: ["Next year: ${ { add: [age, 1] } }"]
# Keeping it simple (variables only) first is easier.

# Compile Time Optimization (Const)
# If the compiler sees:
# - decl: [pi, f64, 3.1415] (const)
# - print: ["Pi is ${pi}"]
# It can replace it with literal "Pi is 3.1415" in the binary data section.
# String Length vs. Size
# 
# 1. Byte Count (sys.size)
# Returns the number of bytes in memory (UTF-8 encoded).
- decl: [byte_count, u64]
- set: [byte_count, { call: [sys.size, name] }]

# 2. Character Count (string.len)
# Returns the number of Unicode characters (code points).
# "A" -> 1 char, 1 byte
# "€" -> 1 char, 3 bytes
- decl: [char_count, i32]
- set: [char_count, { call: [name.len] }]
```
# - If variables are `const` or literals, we CAN optimize at Compile Time.
# -> We should implement **Runtime Expansion** as the default.

# Syntax Recommendation: "${var}"
- decl: [name, string]
- set: [name, "Alice"]
- decl: [age, i32]
- set: [age, 30]

# Runtime Expansion
# Compiler translates this to:
#   string_builder sb;
#   sb.append("Hello ");
#   sb.append(name);
#   sb.append(", you use ");
#   sb.append(std::to_string(age)); 
#   ...
- print: ["Hello ${name}, you are ${age} years old."]

# Complex Expressions inside
# Uses nested YAML expression syntax inside the string interpolation block.
# See `09_1_string_expansions.md` for full details.
- print: ["Next year: ${ { +: [age, 1] } }"]

# Compile Time Optimization (Const)
# If the compiler sees:
# - decl: [pi, f64, 3.1415] (const)
# - print: ["Pi is ${pi}"]
# It can replace it with literal "Pi is 3.1415" in the binary data section.
# String Length vs. Size
# 
# 1. Byte Count (sys.size)
# Returns the number of bytes in memory (UTF-8 encoded).
- decl: [byte_count, u64]
- set: [byte_count, { call: [sys.size, name] }]

# 2. Character Count (string.len)
# Returns the number of Unicode characters (code points).
# "A" -> 1 char, 1 byte
# "€" -> 1 char, 3 bytes
- decl: [char_count, i32]
- set: [char_count, { call: [name.len] }]
