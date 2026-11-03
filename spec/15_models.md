# Models & Schema Definition

## Model vs Class

| | Model | Class |
|--|-------|-------|
| **Purpose** | Data structure for serialization and validation | Type with behaviour (methods) |
| **Methods** | Not allowed — CompilationError if methods defined | Supported |
| **Instantiation** | `decl: [x, MyModel]` or assignment from map | `decl: [x, MyClass]` or `{ new: MyClass }` |
| **Map assignment** | Supported — runtime validates map against schema | Not supported |
| **Serialization** | Automatic via `sys.serialize` / `sys.deserialize` | Automatic via `sys.serialize` / `sys.deserialize` — same machinery as models |

Use `model` for data, `class` for data + behaviour.

> **Note on serialization (since 2026-05-22):** Classes are now auto-serializable
> just like models — the compiler emits an `__aiml_sernodes_<Name>` type-tree
> for every class registration, identical to the model path. `sys.serialize`,
> `sys.deserialize`, and `Kv<K:T>` (with `T` either a class or a model) work
> uniformly.  The only remaining structural difference is that classes carry
> methods + access modifiers; the wire format ignores both and serialises only
> the member layout.

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

## Field Aliases — speaking a foreign format

A field carries an optional **modifier map** in 4th position (the same slot that holds
`db_indexed`, `unique`, `level`).  The key `alias` gives the field a *second* name, used
wherever the field meets the outside world — today that is `sys.serialize` /
`sys.deserialize` and the flag names of `sys.arg_dump` (spec/23 F23-63).

```aiml
- model:
    name: ContainerState
    fields:
      - [ociVersion, string]
      - [ident,      string, "", { alias: "id" }]
      - [status,     string]
```

Serialising writes `"id"`, deserialising accepts `"id"`, and the aiml side keeps `ident`.

**Why this exists.**  Identifiers must be at least three characters long (a readability rule
that has earned its keep).  Foreign formats do not know that rule: the OCI runtime
specification names a field `id`, JSON APIs use `ts`, `os`, `cc`.  Without an alias such a
format cannot be modelled at all — the mapping by field name simply misses.  The alias keeps
the rule **and** the foreign contract, instead of weakening one for the other.

- **F15-9** An alias changes only the *external* name.  Inside aiml the field is always
  addressed by its real name; nothing reads `value.id`.
- **F15-10** An alias must be unique within its model, and must not collide with another
  field's real name.  Both would make deserialisation ambiguous.
- **F15-11** An alias is **not** key material for the pipeline step key, and changing one does
  not change a model's schema fingerprint — it is a name at the edge, not a shape.

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

## Heap Allocation and ptr<Model>

Models can also be allocated on the heap. Dot notation automatically dereferences
`ptr<Model>` — identical to classes:

```yaml
- decl: [conf, ptr<ServerConfig>, { new: ServerConfig }]

# Write — (*conf).host = "0.0.0.0"
- set: [conf.host, "0.0.0.0"]

# Read
- decl: [h, string]
- set: [h, conf.host]

- free: [conf]
```

Type aliases (e.g. `ServerConfig` as a name for `ptr<ServerConfig>`) can be defined for
frequently used handle types so that `ptr<...>` does not have to be written out
everywhere.

## Default Field Initialization

When a model is declared without an initializer (`- decl: [x, MyModel]`), every
field is initialized to a safe, immediately-usable default — you never observe an
uninitialized/null field:

- `list<T>` / `map<K:V>` → empty container (`.len == 0`, `.push` works at once).
- **`string` → the empty string `""`** (`.len == 0`). A string field is an `i8*`
  and `.len` lowers to `strlen(ptr)`; a null field would segfault on any access,
  so unset string fields default to `""` — mirroring the empty-container rule.
- numeric (`i32`/`f32`/…) / `bool` → `0`.
- a field with an explicit default (`- [name, type, default]`) → that default.
  For a `list<T>` field the only default is `[]` (the empty list it gets anyway);
  a non-empty list default is a compile error — fill the list in code (F15-40).
  This holds for **both** a model's `fields:` and a class's `members:`, and the
  rejection names file and line. The check lives in the parser (`ModelBuilder` /
  `ClassBuilder`), because the default never reaches code generation: the
  field-init routine builds an empty container for every list field and does not
  look at the default at all. So a missing check is not a mis-filled list but a
  silently **empty** one — which looks like a data problem, not a translation
  error. The class half of the check was absent until 2026-11-03; a filled
  default was accepted without a word and the field was empty at run time.
  Before 2026-10-29 a `[]` default silently ended the field list: every later
  field was missing. The check is anchored at `members:`/`fields:`, **not** at the
  shape of the entry: an interface signature
  `- [spur_fuer, list<i32>, [[programm, string]]]` has the same three-element form
  with a container type and a non-empty third element, but there that element is
  the argument list, not a default — a check matching on the shape alone breaks
  every interface with a container return type. Regressions:
  `tests/field_defaults/` (both halves, the nested case, plus the allowed `[]`,
  scalar defaults, and that interface boundary).

This holds for both stack decls and `{ new: Model }` heap allocations, and for
fields of nested models. Both paths route through the same field-init routine
(`initClassContainerMembers`), so `{ new: }` inits string/list/map/nested fields
identically to a stack `decl` (with `{ new: T, field: val }` overrides applied on
top). Regressions: `tests/test_model_string_field_default_init.aiml` (stack) and
`tests/test_model_string_default_heap_new.aiml` (`{ new: }` heap).
