# AIML Primitive Data Types (Explicit Sizes)

## Core Types

| Type Keyword | Description | LLVM Equivalent |
| :--- | :--- | :--- |
| `void` | No value (return type only) | `void` |
| `bool` | Boolean value | `u8` |
| `i32` | Signed 32-bit Integer (default for integer literals) | `i32` |
| `f64` | 64-bit Floating Point (default for float literals) | `double` |
| `f32` | 32-bit Floating Point | `float` |
| `string` | String literal / pointer | `i8*` (char*) |

## Extended Integer Types
| Type Keyword | Description | LLVM Equivalent |
| :--- | :--- | :--- |
| `i8` | 8-bit Integer (Byte/Char) | `i8` |
| `i16` | 16-bit Integer | `i16` |
| `i64` | 64-bit Integer (Long) | `i64` |
| `u8`, `u16`, ...| Unsigned variants | `iN` (interpreted as unsigned) |
| `i8_least`, ... | Least N-bit integer (exact width) | `iN` |
| `i8_fast`, ... | Fastest N-bit integer (platform dependent) | `i32`/`i64` |

## Pointer Types
| Syntax | Description | Example |
| :--- | :--- | :--- |
| `ptr<T>` | Typed Pointer to `T` | `ptr<i32>` |

## Pointer Operators
| Syntax | Description | YAML Example |
| :--- | :--- | :--- |
| `&` | Address-Of (Get pointer) | `{'&': [var]}` |
| `?` | Dereference (Read value) | `{'?': [ptr]}` |

## Fixed-Size Arrays
| Syntax | Description | Example |
| :--- | :--- | :--- |
| `type-size` | Stack-allocated array of `size` elements | `i32-4` (Array of 4 integers) |
| `name-index` | Access element at `index` | `arr-0` |
| `name-start-end` | Slice from `start` (inclusive) to `end` (exclusive) | `arr-0-2` |
| **Note** | We use hyphen syntax (`-`) instead of brackets `[]` because YAML flow collections reserve brackets. | 

> **⚠️ DEPRECATED — the `name-index` / `name-start-end` hyphen (`symbol-<index>`) notation is being eradicated.**
> The hyphen overloads subtraction and collides with identifiers, so element access and slicing move to
> explicit method calls:
> - **Index** `name-index` → **`{call: [name.at, index]}`** (canonical, §5.4 element access; `.get` is the
>   equivalent read verb and the most common in the corpus today).
> - **Slice** `name-start-end` → **`{call: [name.slice, start, end]}`** (`list.slice(start, end)`: start
>   inclusive, end exclusive, end optional → to len; a shallow copy into a fresh handle — spec/61 §5.4/§5.1,
>   move-only `ptr<T>` elements rejected). Element **set** by index → **`idx: [name, index, value]`**.
>
> New code MUST use the method forms. The hyphen forms remain parseable during the migration but are
> removed once the corpus and spec are converted.

## Inference Rules (for `set`)
- Integer literal (`42`) -> `i32`
- Float literal (`3.14`) -> `f64`
- String literal (`"hello"`) -> `string`
- High/Low literal (`true`, `false`) -> `bool`

## Collections
| Syntax | Description | Example |
| :--- | :--- | :--- |
| `list<T>` | Dynamic list of type `T` | `list<i32>` |
| `map<K:V>` | Key-Value Map. Note the colon `:` separator. | `map<string:i32>` |
