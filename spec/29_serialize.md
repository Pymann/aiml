# Serialization & Reflection (Code-as-Data)

## 1. Concept: Context Formation & Code Bundle
Serialization is not just converting an object to a string. 
For Agents, we need to serialize **Code + Context (Dependencies)**.

We introduce a `sys.bundle` operation that takes a **Selector** and produces a **Code** object.

## 2. Selection Syntax (Path-Based)
To select what to serialize, we use a concise path syntax similar to JSONPath but 
engaging with the AIML symbol table.

**Syntax:**
- `"func_name"` -> Selects function
- **Note:** This implies that functions, classes, and variables must have unique names within the same scope to avoid ambiguity.
- `"ClassName"` -> Selects class definition
- `"ModelName"` -> Selects model definition
- `"InterfaceName"` -> Selects interface definition
- `"namespace.symbol"` -> qualified access
- `"package.*"` -> Wildcard (all symbols in package) - *Future*

## 3. The `sys.bundle` Function
### 3.1 Usage
### 3.1 Usage
Usage: `- call: [sys.bundle, selector...]` -> Code

```aiml
# Definition of a task and its dependency
- class:
    name: Payload
    members: [[id, i32]]

- def:
    name: worker_task
    args: [[p, Payload]]
    body:
       - print: ["Working on ${p.id}"]

# Bundling for Remote Execution
- decl: [pkg, Code]
- set: [pkg, { call: [sys.bundle, "worker_task"] }]
```

### 3.2 The `Code` Type
A `Code` object represents a serialized code bundle.

**Reflected Definition:**
```aiml
- class:
    name: Code
    members:
      - [id, string, "", { access: public }]       # SHA-256 hash of content
      - [start_call, map]                      # AST Node to execute (e.g. { call: [func, args] })
      - [chunks, list<string>] # List of code segments (StringBuilder pattern)
    methods:
      - def:
          name: contains
          args: [[symbol_name, string]]
          return: bool
      - def:
          name: toString
          return: string
      - def:
          name: merge
          args: [[other, Code]]
          return: Code
```

### 3.3 Automatic Dependency Closure (Context Formation)
When `sys.bundle` is called on `"worker_task"`, the compiler analysis (as defined in `agents.md`) kicks in:
1.  Analyzes `worker_task` AST.
2.  Sees usage of `Payload` class.
3.  Automatically includes `Payload` class definition in the package.
4.  Resulting `pkg` contains BOTH the function and the class.

## 4. Object Serialization with Context
We can also bundle an *Instance* along with its *Type Definition*.

```aiml
- decl: [p, Payload]
- set: [p.id, 42]

# Serialize Instance + Class Def
# Option 2: Pass explicit flags if needed (future extension) or just the instance
- set: [pkg, { call: [sys.bundle, p] }]
```

## 5. sys.serialize / sys.deserialize

Data-value serialization (not code bundles). Implemented via **yyjson**.

### 5.1 API

```aiml
- set: [json_str, { call: [sys.serialize, value] }]
- set: [value,    { call: [sys.deserialize, json_str, "TypeName"] }]
```

### 5.2 Serialisation Format

| Type | Format | Example |
|------|--------|---------|
| `i8`–`u64`, `f32`, `f64` | Plain string | `"42"`, `"3.14"` |
| `bool` | Plain string | `"true"` / `"false"` |
| `string` | As-is | the string value |
| Enum | Plain integer string | `"0"` for `Color.Red` |
| struct / class / model | JSON object with `__type` | `{"__type":"Vec2","x":10,"y":20}` |
| `list<T>` | JSON object with `__type` | `{"__type":"list<i32>","values":[1,2,3]}` |
| `map<string:V>` | JSON object with `__type` | `{"__type":"map<string:i32>","entries":{"k":1}}` |
| Nested struct field | JSON sub-object inline | `{"__type":"Outer","pos":{"x":1,"y":2}}` |
| Nested list field | JSON array inline | `{"__type":"Wrapper","nums":[1,2,3]}` |
| Nested map field | JSON object inline | `{"__type":"Wrapper","scores":{"a":10}}` |

### 5.2 Nachtrag — Maps werden mit Inhalt geschrieben

- **F29-22** Eine `map<K:V>` wird **mit ihren Einträgen** geschrieben, als eigener
  Wert (Zeile 109) wie als Modellfeld (Zeile 112).  Bis 18.10.2026 schrieb die
  Implementierung **jede** Map als `{}`: der Schreiber lief über `capacity` und
  liess nur Plätze durch, deren `occupied`-Byte gesetzt war — doch `MapImpl::put`
  setzt dieses Byte nie.  Eine aiml-Map ist **dicht** (Einträge `0..size-1`;
  `get`, `len` und `remove` rechnen genauso, `remove` schiebt per `memmove`
  nach), und der Serializer war der einzige Leser des `occupied`-Feldes.  Die
  Spec hatte recht, die Implementierung nicht; gefunden am OCI-Wandler
  (`cosmos/doc/spec/110` §110.25), wo `annotations` still leer blieb.
  `tests/test_ser_alias_bare.aiml` hält beide Fälle fest.

### 5.2a `JsonStyle` — shaping the output

`sys.serialize` takes an optional second argument: a `JsonStyle` that shapes the text **after**
the value has been written.  It was in the implementation long before it was written down;
this section closes that gap.

| Field | Type | Meaning |
|---|---|---|
| `pretty` | `i32` | 1 = indented, multi-line |
| `indent` | `string` | one indent step (default two spaces) |
| `newline` | `string` | line separator (default `\n`) |
| `bare` | `i32` | 1 = **drop every `__type` key** from the result |
| `skip_empty` | `i32` | 1 = **drop empty members** (`""`, `[]`, `{}`) from the result (§5.2c) |

```aiml
- decl: [plain, JsonStyle]
- set:  [plain.bare, 1]
- set:  [text, { call: [sys.serialize, value, plain] }]           # Kurzform
- set:  [text, { call: [sys.serialize, nums, "list<i32>", plain] }]  # mit Typnamen
```

- **F29-17** Beide Formen gelten: der Stil darf als **zweites** Argument stehen oder hinter
  einem ausdrücklichen Typnamen.  Bis 18.10.2026 nahm die Implementierung nur die lange Form
  an und verwarf die kurze **still** — kein Fehler, keine Wirkung, auch bei `pretty`.
  Mehrdeutig ist die Unterscheidung nicht: ein `JsonStyle` ist eine Variable dieses Typs, ein
  Typname ein Literal.

- **F29-12** `bare` removes `__type` **recursively**, at every nesting depth.  What remains is
  ordinary JSON that a foreign reader accepts.
- **F29-13** `bare` does **not** unwrap containers.  A top-level `list<T>` keeps its
  `{"values":[…]}` shape, because the wrapper carries the structure, not the type claim.  A
  list *inside* a model is an inline array anyway (§5.2) and is therefore unaffected.
- **F29-14** A bare document can be read back with `sys.deserialize` — the type is named by the
  caller, not by the document (§5.2b).

### 5.2b Reading foreign JSON

`sys.deserialize(text, "T")` names the type **in the call**.  A `__type` key in the document is
therefore not required, and a document that carries a *different* `__type` is not rejected:
the caller's claim wins.  Fields are matched by name — or by **alias** (`spec/15`), which is
what makes foreign formats readable whose keys break aiml's three-character rule.

- **F29-15** This tolerance is a **promise**, not an accident of the implementation.  cosmos
  depends on it to read OCI bundles (`cosmos/doc/spec/110` F110-13); `tests/test_ser_alias_bare.aiml`
  holds it.
- **F29-16** Fields the document does not mention keep their default value; keys the model does
  not know are ignored.  Neither is an error — a format that grows must stay readable.

### 5.2c `skip_empty` — Leeres weglassen

Ein aiml-Modell hat kein „nicht gesetzt": jedes Feld hat einen Vorgabewert, und
`sys.serialize` schreibt jedes Feld.  Fremde Formate unterscheiden aber
**fehlend** von **leer**: die OCI-Image-Spec sagt zu `variant` *SHOULD be omitted
if unknown*, und `"annotations": {}` oder `"Entrypoint": []` sind dort Ballast,
den strenge Leser bemängeln.  `skip_empty` schließt genau diese Lücke — ohne das
Modell zu verbiegen (ein `ptr<string>`, nur um „fehlend" ausdrücken zu können,
wäre die schlechtere Antwort).

```aiml
- decl: [lean, JsonStyle]
- set:  [lean.bare, 1]
- set:  [lean.skip_empty, 1]
- set:  [text, { call: [sys.serialize, img_cfg, lean] }]
```

- **F29-18** Entfernt werden **Objekt-Mitglieder**, deren Wert ein leerer String,
  ein leeres Array oder ein leeres Objekt ist — auf jeder Ebene.  **Nicht**
  entfernt werden `0`, `false` und `null`: eine Null ist ein Wert, kein Fehlen.
  Wer `0` weglässt, ändert Daten; wer `""` weglässt, ändert nur die Schreibweise.
- **F29-19** Die Prüfung läuft **von unten nach oben**: wird ein Objekt durch das
  Entfernen seiner Mitglieder selbst leer, fällt es ebenfalls weg.  Die **Wurzel**
  bleibt immer stehen — ein leeres Dokument wäre kein Dokument.
- **F29-20** **Array-Elemente** werden nie entfernt, auch leere nicht: ein Array
  trägt seine Bedeutung in der Reihenfolge, und ein entfernter Eintrag verschiebt
  alle folgenden.
- **F29-21** `skip_empty` und `bare` sind unabhängig und kombinierbar; beide
  arbeiten nach dem Schreiben auf dem fertigen Dokument (wie `pretty`), nicht im
  Schreibpfad.  Zurücklesen bleibt möglich: fehlende Felder behalten ihren
  Vorgabewert (F29-16) — genau den, der sie hat weglassen lassen.

### 5.3 ptr\<T\> Semantics

- **serialize**: auto-deref — loads the pointer and serialises the pointed-to value as `T`
- **deserialize**: deserialises `T` and returns the heap pointer directly (type remains `ptr<T>`)

### 5.4 Unsupported Types → Compiler Error

The following types produce a compile-time error when passed to `sys.serialize` or `sys.deserialize`:

| Type | Reason |
|------|--------|
| `any` | No concrete type information at compile time |
| `chan<T>` | OS resource handle, not serialisable |
| `lock` | OS resource handle, not serialisable |
| `thread_handle` | OS resource handle, not serialisable |
| `Code` | Use `sys.bundle` / `sys.exec` instead |
| `file_handle`, `process` | OS resources |

### 5.5 Examples

```aiml
- class:
    name: Vec2
    members:
      - [x, i32]
      - [y, i32]

# Struct round-trip
- decl: [v, Vec2]
- set: [v.x, 10]
- set: [v.y, 20]
- decl: [s, string]
- set: [s, { call: [sys.serialize, v] }]   # → {"__type":"Vec2","x":10,"y":20}

- decl: [v2, Vec2]
- set: [v2, { call: [sys.deserialize, s, "Vec2"] }]
- print: ["${v2.x}"]   # → 10

# List round-trip
- decl: [nums, list<i32>]
- call: [nums.push, 1]
- call: [nums.push, 2]
- decl: [js, string]
- set: [js, { call: [sys.serialize, nums] }]   # → {"__type":"list<i32>","values":[1,2]}

# Enum serialization (plain integer)
- enum:
    name: Color
    values: [Red, Green, Blue]
- decl: [c, Color, Color.Green]
- decl: [cs, string]
- set: [cs, { call: [sys.serialize, c] }]   # → "1"
```

## 6. Deserialization & Execution of Code Bundles
On the receiving end (for Code objects):

```aiml
### 6.1 Manual Loading (Global Context)
Loads definitions into the current environment.

```aiml
- call: [sys.load_bundle, pkg] 
- call: [worker_task, { new: Payload }]
```

### 6.2 Direct Execution (Sandbox)
Executes the code object's entry point in an isolated scope.

```aiml
# Executes 'start_call' found in the code object, but will before sys.load_bundle with pkg
- call: [sys.exec, pkg]
```
```

Plan: yyjson-based serialization for AIML
Context
The existing serialization uses a hand-built YAML-like format with ~1000 lines of IR emission (StringBuilder, __aiml_get_field scanner). The wheel was reinvented. We replace it entirely with yyjson — a fast, simple C library — with a clean C wrapper layer called from LLVM IR via ctx.getFunction().

Key Design Decisions
Serialization format
Primitives (i8–u64, f32/f64, bool, string): unchanged plain-string format ("42", "true", "hello")
Enums: plain integer string ("0" for Color.Red — identical to primitives, backward-compatible)
Compound types (struct/class/model, list<T>, map<string:T>): JSON with __type field
Nested complex fields in structs fully supported (recursive)

// struct/class:
{"__type":"Vec2","x":10,"y":20}

// list<i32>:
{"__type":"list<i32>","values":[1,2,3]}

// map<string:i32>:
{"__type":"map<string:i32>","entries":{"key1":100}}

// Nested struct field:
{"__type":"Container","pos":{"x":1,"y":2},"count":5}

// Nested list field in struct:
{"__type":"Wrapper","items":{"__type":"list<i32>","values":[1,2]}}
// → Or directly in array: "items":[1,2] (when type is known)
Serialization rules
Type	serialize	deserialize
any (anywhere)	Compiler error	Compiler error
chan<T>, lock, thread_handle	Compiler error	Compiler error
ptr<T>	Auto-deref → serialize as T	deserialize T → heap-malloc → ptr<T>
Primitives/Enums	plain string	parse from string
struct/class/model	JSON object	parse JSON → struct alloca
list<T>	JSON object	parse JSON → list struct
map<string:T>	JSON object	parse JSON → map struct
Nested types in structs
Struct fields of type class/model: as JSON sub-object (obj_begin/obj_end)
Struct fields of type list<T>: as JSON array inline (arr_begin/arr_end)
Struct fields of type map<string:T>: as JSON map inline (map_begin/map_end)
Recursive until any (→ error)
Architecture

AIML Code
  → visitSysSerializeCall (ast_compiler_call.cpp)
      → SerializeImpl::serialize/deserialize (serialize_impl.cpp)
          → ctx.getFunction("aiml_jw_*") / ctx.getFunction("aiml_jr_*")
              → aiml_jw/jr C wrapper (yyjson_wrapper.cpp, compiled into aiml binary)
                  → yyjson (yyjson.cpp, vendored)
Since -rdynamic is active in the linker, all symbols of the aiml binary are available for JIT.

New/changed files
1. src/core/yyjson.h + src/core/yyjson.cpp (NEW — vendored)
Download from github.com/ibireme/yyjson Release 0.10.0
yyjson.c → yyjson.cpp (compiled under C++ with -w, since this flag is already set)
Automatically included via src/**/*.cpp glob in exodus.json
2. src/core/yyjson_wrapper.h (NEW)

// Write API
void*  aiml_jw_new();
void   aiml_jw_set_type(void* w, const char* t);
void   aiml_jw_set_i32(void* w, const char* k, int32_t v);
void   aiml_jw_set_i64(void* w, const char* k, int64_t v);
void   aiml_jw_set_f64(void* w, const char* k, double v);
void   aiml_jw_set_bool(void* w, const char* k, int v);
void   aiml_jw_set_str(void* w, const char* k, const char* v);
void   aiml_jw_arr_begin(void* w, const char* k);
void   aiml_jw_arr_i32(void* w, int32_t v);
void   aiml_jw_arr_i64(void* w, int64_t v);
void   aiml_jw_arr_f64(void* w, double v);
void   aiml_jw_arr_bool(void* w, int v);
void   aiml_jw_arr_str(void* w, const char* v);
void   aiml_jw_arr_end(void* w);
void   aiml_jw_obj_begin(void* w, const char* k);   // for nested structs
void   aiml_jw_obj_end(void* w);
char*  aiml_jw_done(void* w);                        // heap JSON string, caller frees

// Read API
void*       aiml_jr_parse(const char* json);
void*       aiml_jr_sub(void* ctx, const char* key); // sub-object reader (non-owning)
void        aiml_jr_free(void* ctx);
int32_t     aiml_jr_i32(void* ctx, const char* key);
int64_t     aiml_jr_i64(void* ctx, const char* key);
double      aiml_jr_f64(void* ctx, const char* key);
int         aiml_jr_bool(void* ctx, const char* key);
char*       aiml_jr_str(void* ctx, const char* key);    // heap copy
int         aiml_jr_arr_len(void* ctx, const char* key);
int32_t     aiml_jr_arr_i32(void* ctx, const char* key, int idx);
int64_t     aiml_jr_arr_i64(void* ctx, const char* key, int idx);
double      aiml_jr_arr_f64(void* ctx, const char* key, int idx);
int         aiml_jr_arr_bool(void* ctx, const char* key, int idx);
char*       aiml_jr_arr_str(void* ctx, const char* key, int idx);  // heap copy
int         aiml_jr_map_count(void* ctx, const char* key);
char*       aiml_jr_map_key(void* ctx, const char* key, int idx);  // heap copy
int32_t     aiml_jr_map_val_i32(void* ctx, const char* key, int idx);
int64_t     aiml_jr_map_val_i64(void* ctx, const char* key, int idx);
double      aiml_jr_map_val_f64(void* ctx, const char* key, int idx);
char*       aiml_jr_map_val_str(void* ctx, const char* key, int idx);  // heap copy
3. src/core/yyjson_wrapper.cpp (NEW)
Implementation of the wrapper:

aiml_jw_t struct: contains yyjson_mut_doc*, root node, stack (max 32 levels) for obj_begin/arr_begin
aiml_jr_t struct: contains yyjson_doc* (nullable), yyjson_val* (current node)
aiml_jr_sub() returns allocated aiml_jr_t* with doc=NULL → aiml_jr_free on it only frees the struct, not the doc
4. src/core/serialize_impl.cpp (COMPLETELY NEW — ~400 lines, was ~1000)
Structure:


// ── Helper macros: declare wrapper functions via ctx.getFunction() ──────────
static llvm::Function* getJwNew(CompilerContext& ctx);
static llvm::Function* getJwSetType(CompilerContext& ctx);
// ... one getter function per wrapper API function (lazy declaration)

// ── Serialize emitters ──────────────────────────────────────────────────────
static llvm::Function* emitSerializeStruct(ctx, typeName);
// IR: jw_new → jw_set_type → for each field: load + jw_set_* / arr_*/obj_* → jw_done

static llvm::Function* emitSerializeList(ctx, elemTypeName);
// IR: jw_new → jw_set_type → jw_arr_begin → loop: load+jw_arr_* → jw_arr_end → jw_done

static llvm::Function* emitSerializeMap(ctx, valTypeName);
// IR: jw_new → jw_set_type → loop: load key/val + jw_map_set_* → jw_done

// ── Deserialize emitters ────────────────────────────────────────────────────
static llvm::Function* emitDeserializeStruct(ctx, typeName);
// IR: jr_parse → for each field: jr_* → store → jr_free → return heap-ptr

static llvm::Function* emitDeserializeList(ctx, elemTypeName);
// IR: jr_parse → jr_arr_len → malloc list → loop: jr_arr_* → store → jr_free → return

static llvm::Function* emitDeserializeMap(ctx, valTypeName);
// IR: jr_parse → jr_map_count → malloc map → loop: jr_map_key + jr_map_val_* → put → jr_free

// ── Public API ──────────────────────────────────────────────────────────────
bool isSerializable(ctx, typeName);   // checks recursively, any/chan/lock → false
llvm::Value* serialize(ctx, val, typeName);
llvm::Value* deserialize(ctx, yamlStr, typeName);
Key: all emitSerialize/emitDeserialize use saveIP/restoreIP and are emitted only once (guard via ctx.module.getFunction(fnName)).**

Nested field dispatch in emitSerializeStruct:


for (auto& [fname, fmember] : sortedFields) {
    if (isClass(fmember.typeName)) {
        // GEP to sub-struct, cast to i8*
        // jw_obj_begin(writer, fname)
        // emitSerializeStructFields(ctx, writer, subPtr, fmember.typeName)
        // jw_obj_end(writer)
    } else if (isList(fmember.typeName)) {
        // GEP to list field
        // jw_arr_begin(writer, fname)
        // emit loop over list elements
        // jw_arr_end(writer)
    } else {
        // load scalar → jw_set_*(writer, fname, val)
    }
}
→ For this we need an internal emitSerializeStructFields(ctx, writer, structPtr, typeName) helper function (not an IR function, just a C++ helper that emits IR).

5. src/core/serialize_impl.h (UPDATE)
Update comments: JSON format, ptr<T> semantics, any=compiler error.

6. src/core/ast_compiler_call.cpp — visitSysSerializeCall (UPDATE)
serialize:


1. Resolve argSym + argType
2. any → LOG_ERROR
3. ptr<T>: strip ptr<>, load pointer from alloca, serialize as T
4. !isSerializable → LOG_ERROR
5. struct/list/map: get alloca, SerializeImpl::serialize(ctx, rawAlloca, argType)
6. enum/primitive: evaluate expression, SerializeImpl::serialize(ctx, val, argType)
deserialize:


1. Parse optional type hint (2nd arg)
2. No hint / "Code" → existing CodeImpl behavior
3. ptr<T> in hint: deserialize as T (returns i8* heap), return directly
4. any → LOG_ERROR
5. !isSerializable → LOG_ERROR
6. SerializeImpl::deserialize(ctx, src, typeName)
7. spec/29_serialize.md (UPDATE)
Insert new section "5. sys.serialize / sys.deserialize":

Document JSON format for compound types
ptr<T> semantics
any → compiler error
Complete type table (supported/unsupported)
8. tests/test_serialize_ext.aiml (UPDATE)
Update expected outputs (JSON instead of YAML-like)
Add tests for ptr<T>
Add tests for nested struct
Implementation order
Vendor yyjson — place yyjson.h + yyjson.cpp in src/core/
Implement and test yyjson_wrapper.h + yyjson_wrapper.cpp (with short unit test in C++)
Completely rewrite serialize_impl.cpp (primitives, enums, structs, lists, maps, nested fields)
Update visitSysSerializeCall for ptr<T>, any check
Update spec
Update tests + build + bash run_all_tests.sh → 30/30 ✓
Verification
bash run_all_tests.sh → all 30 tests pass
Manual: ./out/aiml -r tests/test_serialize_ext.aiml — output shows JSON format
Manual: ./out/aiml -r /tmp/test_ptr.aiml — ptr<T> round-trip
Manual: ./out/aiml -r /tmp/test_nested.aiml — nested struct round-trip
