# Versioned Classes (Schema Evolution)

## Motivation
Directly support database/schema migration in AIML classes. By tagging fields with a `version`, the compiler/runtime can handle backward compatibility when reading data.

## Syntax

Add a `version` qualifier to class members. The class itself can optional have a `current_version` (or inferred max).

```yaml
- class:
    name: User
    version: 2  # u64 Type
    members:
      # Version 1 Fields
      - [id, i32, { version: 1 }]
      - [username, string, { version: 1 }]
      
      # Version 2 Fields (Added later)
      - [email, string, { version: 2 }]
      
      # Future/Draft Fields
      - [bio, string, { version: 3 }]
```

## Semantics

### 1. Default Version
Fields without a `version` qualifier default to version 1.

### 2. Migration during Read (Deserialization)
When reading a class from a Buffer, we need to handle version mismatches.

**Scenario: Reading Old Data (v1) into New Class (v2)**
The Buffer contains valid data for `id` and `username`. It ends there.
The Class expects `email`.
Implementation:
- Provide the source data version to the read call.
- logic: `if (field.version <= data_version) read() else use_default()`.

```yaml
- decl: [u, User]
- decl: [dataVersion, i32]
# ... read version from header ...
- call: [buffer.read_versioned, u, User, dataVersion]
```

### 3. Migration during Write (Serialization)
When writing a class, the **Class Version (u64)** is automatically written first.
`buffer.write` -> `[u64 Version][Struct Data]`

## API Updates

- `buffer.write(val, Type)`: Prepends `Type.version` if Type is a class.
- `buffer.read(Type)`: Reads and consumes `u64` version (strict check or ignore?), then reads data.
- `buffer.read_versioned(Type)`: Reads `u64` version from buffer, then performs migration.
  - (Old) `buffer.read_versioned(Type, explicitVersion)`: Still supported for manual control?

## Buffer Migration (Upgrading Data)
To migrate a binary buffer:
1. Allocate a new buffer.
2. `buffer.read_versioned(Type)` will read the old version header and migrate in-memory.
3. `buffer.write(Val, Type)` will write the new version header and new data.

## Buffer Migration (Upgrading Data)
To migrate a binary buffer from Version N to Version M (current):
1. Allocate a new buffer.
2. Read the old buffer using `read_versioned(Type, N)`. This populates the Struct with old data and defaults for new fields.
3. Write the Struct to the new buffer.

```yaml
- decl: [oldBuf, buffer]
# ... load oldBuf (v1) ...

- decl: [newBuf, buffer]
- call: [newBuf.alloc, 1024]

- decl: [u, User]
# Read v1 data into current User struct (migrates in-memory)
- set: [u, {call: [oldBuf.read_versioned, User, 1]}]

# Write current User struct to new buffer (v2 format)
- call: [newBuf.write, u, User]
```
