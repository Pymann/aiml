# Collections & Complex Types

```yaml
# 1. Fixed-Size Arrays (Primitive)
# Allocated on Stack (via 'alloca' array type).
# Out-of-bounds access raises IndexOutOfBoundsException.
# Syntax: type-size (e.g. i32-3). Hyphen syntax used because YAML reserves [].
- decl: [coords, i32-3]
- set: [coords-0, 10]

# 2. Dynamic List (Strongly Typed)
# Syntax: list<T>
# Out-of-bounds access raises IndexOutOfBoundsException.
- decl: [scores, list<i32>]

# Standard List Methods
- call: [scores.push, 100]       # Add to end
- call: [scores.pop]             # Remove from end (returns value)
- call: [scores.insert, 0, 50]   # Insert at index
- call: [scores.remove, 0]       # Remove at index; raises IndexOutOfBoundsException if out of range
- call: [scores.clear]           # Remove all
- set: [len, scores.len]         # Element count (property, i32)
- set: [is_empty, scores.empty]  # Check if empty (property, bool)

# Access
- set: [val, scores-0]           # Get element at index 0
- set: [subset, scores-1-3]      # Slice: elements at index 1 and 2
- set: [scores-0, 99]            # Set element at index 0

# Memory
- call: [scores.free]            # Explicit free (if no RAII)

# 3. Homogeneous Map
# Syntax: map<KeyType:ValueType>
# Note: The ':' inside '<>' is the type separator, not YAML key syntax.
- decl: [users, map<string:i32>]

- call: [users.put, "alice", 1]
- set: [val, { call: [users.get, "alice"] }]   # returns i32 or null pointer if missing
- set: [exists, { call: [users.has, "alice"] }]
- call: [users.remove, "alice"]                # raises MissingKeyError if key not found
- call: [users.clear]
- set: [len, users.len]

# Null check after get:
- if:
    condition: { eq: [val, null] }
    then:
      - print: ["Key not found"]

# 4. Heterogeneous Map — map<string:any>
# Values are stored as tagged variants: the runtime keeps the real type alongside the value.
# get() auto-unboxes: the returned value has the REAL runtime type.
# set: infers the correct type automatically.
# Comparisons work on the returned value because the real type is known at runtime.
- decl: [meta, map<string:any>]

- call: [meta.put, "count", 42]          # stored as i32 (tagged)
- call: [meta.put, "label", "hello"]     # stored as string (tagged)
- call: [meta.put, "ratio", 3.14]        # stored as f64 (tagged)

# Auto-unboxed retrieval — set: infers real type:
- set: [count, { call: [meta.get, "count"] }]  # count is i32
- set: [label, { call: [meta.get, "label"] }]  # label is string

# Comparisons work because the real type is returned:
- if:
    condition: { eq: [count, 42] }
    then:
      - print: ["count matches"]

# Null check still required for missing keys:
- set: [missing, { call: [meta.get, "nonexistent"] }]
- if:
    condition: { eq: [missing, null] }
    then:
      - print: ["Key not found"]
```

## Size vs Len

| Attribute | Syntax | Returns | Description |
|-----------|--------|---------|-------------|
| `.len` | `obj.len` (property) | `i32` | Element/character count |
| `.size` | `{ call: [obj.size] }` (method) | `u64` | Byte count in memory |

`.len` is available on `list`, `map`, and `string`.
`.size` is an intrinsic method available on all types.

## Built-in Generic Types

The generic type parameters (`list<T>`, `map<K:V>`, `ptr<T>`, `chan<T>`) are **built-in only**.
User-defined generic types or generic functions are not supported in AIML v1.
