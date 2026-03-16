# Models & Schema Definition

## Model vs Class

| | Model | Class |
|--|-------|-------|
| **Purpose** | Data structure for serialization and validation | Type with behaviour (methods) |
| **Methods** | Not allowed — CompilationError if methods defined | Supported |
| **Instantiation** | `decl: [x, MyModel]` or assignment from map | `decl: [x, MyClass]` or `{ new: MyClass }` |
| **Map assignment** | Supported — runtime validates map against schema | Not supported |
| **Serialization** | Automatic via `sys.serialize` / `sys.deserialize` | Manual implementation required |

Use `model` for data, `class` for behaviour.

```yaml
# Models define the structure of dynamic data (like maps or JSON).
# Similar to Pydantic models: field names, types, defaults, and descriptions.

# Syntax:
# - model:
#     name: ModelName
#     description: "Optional model description"
#     version: 1
#     fields:
#       - [name, type]              # Required Field
#       - [name, type, default_val] # Optional Field with Default

# Example 1: Simple Config Model
- model:
    name: ServerConfig
    description: "Configuration for the HTTP server"
    fields:
      - [host, string]
      - [port, i32]
      - [ssl, bool, false]
    descriptions:
      host: "Hostname or IP address"
      port: "Listening port"
      ssl: "Enable SSL"

# Example 2: Nested Model
- model:
    name: DatabaseConfig
    fields:
      - [connection_string, string]
      - [pool_size, i32, 10]

- model:
    name: AppConfig
    fields:
      - [server, ServerConfig]
      - [db, DatabaseConfig]

# Usage in Code

# 1. Declaration & Instantiation
- decl: [conf, ServerConfig]
- set: [conf, { new: ServerConfig }]

# 2. Assignment from Map (Validation)
# The runtime validates the map against the schema.
- set: [conf, {host: "localhost", port: 8080}]

# 3. Serialization
- decl: [yaml_str, string]
- set: [yaml_str, { call: [sys.serialize, conf] }]
- decl: [restored, ServerConfig]
- set: [restored, { call: [sys.deserialize, yaml_str, ServerConfig] }]

# 4. Usage in Functions (Structural Typing)
# Functions accepting a Model type can be called with:
# A) An instance of that Model
# B) A Map structure matching the Model's fields

- def:
    name: start_server
    args: [[c, ServerConfig]]
    body: ...

- call: [start_server, conf]

# Call with mixed Map (Structural Typing)
- call: [start_server, {host: "10.0.0.5", port: 9090}]
```

## Model Inheritance (inherits:)

Models support single inheritance via `inherits:`. The parent must be a model
(not a class). Parent fields come first in memory, followed by the child's own
fields — identical layout to class `extends:`.

```yaml
- model:
    name: BaseCard
    fields:
      - [name,      string]
      - [card_type, string]
      - [mkp,       i32]

- model:
    name: CreatureCard
    inherits: BaseCard    # inherits name, card_type, mkp
    fields:
      - [lp,       i32]
      - [attack,   i32]
      - [defense,  i32]

# Memory layout of CreatureCard: [name, card_type, mkp, lp, attack, defense]

- decl: [c, CreatureCard]
- set:  [c.name,     "Dragon"]
- set:  [c.card_type, "creature"]
- set:  [c.mkp,      5]
- set:  [c.lp,       30]
- set:  [c.attack,   4]
- set:  [c.defense,  2]
```

Constraints:
- A model can only inherit from another model, not from a class.
- Only single inheritance (one `inherits:` parent).
- Duplicate field names between parent and child are a CompilationError.

## Heap-Allokation und ptr<Model>

Models können auch auf dem Heap alloziert werden. Punkt-Notation dereferenziert
`ptr<Model>` automatisch — identisch zu Klassen:

```yaml
- decl: [conf, ptr<ServerConfig>, { new: ServerConfig }]

# Schreiben — (*conf).host = "0.0.0.0"
- set: [conf.host, "0.0.0.0"]

# Lesen
- decl: [h, string]
- set: [h, conf.host]

- free: [conf]
```

Type-Aliases (z.B. `ServerConfig` als Name für `ptr<ServerConfig>`) können für
häufig verwendete Handle-Typen definiert werden, damit `ptr<...>` nicht
überall ausgeschrieben werden muss.
