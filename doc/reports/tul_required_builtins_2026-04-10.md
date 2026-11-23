# Missing AIML builtins for tul GenAI

Date: 2026-04-10

## Context

The `tul` GenAI implementation plan needs string and math operations
that currently do not exist as builtins in AIML. Without them every
single operation would have to be implemented manually as a character-by-character while loop,
which bloats the code needlessly.

## Current state: what already exists

String methods are hard-coded in `ast_compiler_call.cpp` from line 706:
- `string.substr(start)` and `string.substr(start, len)` — via strdup/strndup
- `string.starts_with(prefix)` — via strncmp
- `string.index_of(needle)` — via strstr
- `string.len` — as a property (strlen)

Math namespace in `builtin_registry.cpp` from line 2911:
- `math.sqrt` already exists (via `sqrtf`)
- `math.sin`, `math.cos`, `math.exp` etc. exist

## Missing string builtins

### 1. `string.lower() -> string`

Convert all ASCII upper-case letters to lower case.

Needed for: tokenizer normalisation. Every input must be
normalised before tokenizing.

Implementation: analogous to the existing string methods in `ast_compiler_call.cpp`:
- C runtime: `aiml_string_lower(const char*) -> char*` — `strdup` + `tolower` loop
- Compiler: new branch `methodName == "lower"` in the `className == "string"` block
- JIT: `AddSymbol("aiml_string_lower", ...)`

### 2. `string.split(delimiter) -> list<string>`

Split a string at a delimiter and return it as a `list<string>`.

Needed for: tokenizer split (whitespace), tag parsing, general text processing.
Without this builtin every tokenizer has to build a manual while loop with
`index_of` + `substr` — that is ~15 lines per call site.

Implementation: more complex than other string methods because of the `list<string>` return.
- C runtime: `aiml_string_split(const char* str, const char* delim, int32_t* out_count) -> char**`
  Returns an array of `char*` + the element count via an out pointer.
- Compiler: `initList` + fill loop, analogous to the `io.list_dir` pattern in `ast_compiler_call.cpp`
  (stack-alloc AimlList, call `aiml_string_split`, insert elements via `ListImpl::push`)
- Return: `list<string>` as a stack struct, memcpy into the target alloca (like `visitDeclStmt` for lists)
- JIT: `AddSymbol("aiml_string_split", ...)`

### 3. `string.trim() -> string`

Remove leading and trailing whitespace characters.

Needed for: normalisation before tokenizing, robust input processing.

Implementation:
- C runtime: `aiml_string_trim(const char*) -> char*` — skip leading whitespace,
  find the last non-whitespace, `strndup(start, len)`
- Compiler: new branch `methodName == "trim"` in the string block
- JIT: `AddSymbol("aiml_string_trim", ...)`

### 4. `string.replace(old, new) -> string`

Replace all occurrences of `old` by `new`.

Needed for: punctuation handling (e.g. removing or separating commas/full stops).

Implementation:
- C runtime: `aiml_string_replace(const char*, const char* old, const char* new) -> char*`
  Iteratively with `strstr` + `memcpy` into a growing buffer
- Compiler: new branch `methodName == "replace"` in the string block, 2 args
- JIT: `AddSymbol("aiml_string_replace", ...)`

## Priority

| Builtin | Priority | Reason |
|---------|-----------|-------|
| `string.split` | Critical | Without it tokenizing is extremely cumbersome |
| `string.lower` | Critical | Normalisation is mandatory |
| `string.trim` | High | Robust input processing |
| `string.replace` | Medium | Punctuation handling, can possibly be worked around in phase 1 |
| ~~`math.sqrt`~~ | ~~Exists~~ | Already registered as `sqrtf` in the math namespace |

## Implementation location

All new string methods:
- C functions: new file `src/sys/string_runtime.h` + `src/sys/string_runtime.cpp`
- Compiler branches: `src/core/ast_compiler_call.cpp` in the `className == "string"` block (from line 707)
- JIT symbols: `compiler.cpp` — `AddSymbol` for every new C function
- exodus.json: add `string_runtime.cpp` to the sources

## Implementation order

1. `string.lower` + `string.trim` (simplest, char* -> char*, no list handling)
2. `string.replace` (iterative buffer, medium difficulty)
3. `string.split` (list return, the most elaborate pattern)

## Effort

Estimated 2-3h for all 4 builtins including tests.
`string.split` is the most elaborate because of the list return.
