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
      - [id, string, { access: public }]       # SHA-256 hash of content
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

Plan: yyjson-basierte Serialisierung für AIML
Context
Die bestehende Serialisierung verwendet ein selbst gebautes YAML-ähnliches Format mit ~1000 Zeilen IR-Emission (StringBuilder, __aiml_get_field Scanner). Das Rad wurde neu erfunden. Wir ersetzen es vollständig durch yyjson — eine performante, einfache C-Bibliothek — mit einer klaren C-Wrapper-Schicht, die aus LLVM IR via ctx.getFunction() aufgerufen wird.

Wichtige Design-Entscheidungen
Serialisierungsformat
Primitiven (i8–u64, f32/f64, bool, string): unverändertes plain-String-Format ("42", "true", "hello")
Enums: plain Integer-String ("0" für Color.Red — identisch zu Primitiven, backward-kompatibel)
Compound-Typen (struct/class/model, list<T>, mapstring:T): JSON mit __type-Feld
Nested complex fields in Structs vollständig supported (rekursiv)

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
// → Oder direkt im Array: "items":[1,2] (wenn Typ bekannt ist)
Serialisierungsregeln
Typ	serialize	deserialize
any (irgendwo)	Compiler-Fehler	Compiler-Fehler
chan<T>, lock, thread_handle	Compiler-Fehler	Compiler-Fehler
ptr<T>	Auto-Deref → serialize als T	deserialize T → heap-malloc → ptr<T>
Primitiven/Enums	plain string	aus string parsen
struct/class/model	JSON object	JSON parsen → struct-alloca
list<T>	JSON object	JSON parsen → list-struct
map<string:T>	JSON object	JSON parsen → map-struct
Nested types in structs
Struct-Felder vom Typ class/model: als JSON sub-object (obj_begin/obj_end)
Struct-Felder vom Typ list<T>: als JSON array inline (arr_begin/arr_end)
Struct-Felder vom Typ mapstring:T: als JSON map inline (map_begin/map_end)
Rekursiv bis any (→ Fehler)
Architektur

AIML Code
  → visitSysSerializeCall (ast_compiler_call.cpp)
      → SerializeImpl::serialize/deserialize (serialize_impl.cpp)
          → ctx.getFunction("aiml_jw_*") / ctx.getFunction("aiml_jr_*")
              → aiml_jw/jr C-Wrapper (yyjson_wrapper.cpp, compiled into aiml binary)
                  → yyjson (yyjson.cpp, vendored)
Da -rdynamic im Linker aktiv ist, sind alle Symbole des aiml-Binary für JIT verfügbar.

Neue/geänderte Dateien
1. src/core/yyjson.h + src/core/yyjson.cpp (NEU — vendored)
Download von github.com/ibireme/yyjson Release 0.10.0
yyjson.c → yyjson.cpp (kompiliert unter C++ mit -w, da diese Flag bereits gesetzt ist)
Wird automatisch via src/**/*.cpp glob in exodus.json eingebunden
2. src/core/yyjson_wrapper.h (NEU)

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
void   aiml_jw_obj_begin(void* w, const char* k);   // für nested structs
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
3. src/core/yyjson_wrapper.cpp (NEU)
Implementierung des Wrappers:

aiml_jw_t struct: enthält yyjson_mut_doc*, Root-Node, Stack (max 32 Ebenen) für obj_begin/arr_begin
aiml_jr_t struct: enthält yyjson_doc* (nullable), yyjson_val* (current node)
aiml_jr_sub() gibt allokierten aiml_jr_t* mit doc=NULL zurück → aiml_jr_free darauf gibt nur die Struct frei, nicht das Doc
4. src/core/serialize_impl.cpp (KOMPLETT NEU — ~400 Zeilen, war ~1000)
Struktur:


// ── Hilfsmakros: Wrapper-Funktionen via ctx.getFunction() deklarieren ──────
static llvm::Function* getJwNew(CompilerContext& ctx);
static llvm::Function* getJwSetType(CompilerContext& ctx);
// ... je eine Getter-Funktion pro Wrapper-API-Funktion (lazy declaration)

// ── Serialize-Emitter ───────────────────────────────────────────────────────
static llvm::Function* emitSerializeStruct(ctx, typeName);
// IR: jw_new → jw_set_type → für jeden Feld: load + jw_set_* / arr_*/obj_* → jw_done

static llvm::Function* emitSerializeList(ctx, elemTypeName);
// IR: jw_new → jw_set_type → jw_arr_begin → loop: load+jw_arr_* → jw_arr_end → jw_done

static llvm::Function* emitSerializeMap(ctx, valTypeName);
// IR: jw_new → jw_set_type → loop: load key/val + jw_map_set_* → jw_done

// ── Deserialize-Emitter ─────────────────────────────────────────────────────
static llvm::Function* emitDeserializeStruct(ctx, typeName);
// IR: jr_parse → für jeden Feld: jr_* → store → jr_free → return heap-ptr

static llvm::Function* emitDeserializeList(ctx, elemTypeName);
// IR: jr_parse → jr_arr_len → malloc list → loop: jr_arr_* → store → jr_free → return

static llvm::Function* emitDeserializeMap(ctx, valTypeName);
// IR: jr_parse → jr_map_count → malloc map → loop: jr_map_key + jr_map_val_* → put → jr_free

// ── Öffentliche API ─────────────────────────────────────────────────────────
bool isSerializable(ctx, typeName);   // prüft rekursiv, any/chan/lock → false
llvm::Value* serialize(ctx, val, typeName);
llvm::Value* deserialize(ctx, yamlStr, typeName);
Key: alle emitSerialize/emitDeserialize nutzen saveIP/restoreIP und werden nur einmal emittiert (guard via ctx.module.getFunction(fnName)).**

Nested field dispatch in emitSerializeStruct:


for (auto& [fname, fmember] : sortedFields) {
    if (isClass(fmember.typeName)) {
        // GEP zum sub-struct, cast to i8*
        // jw_obj_begin(writer, fname)
        // emitSerializeStructFields(ctx, writer, subPtr, fmember.typeName)
        // jw_obj_end(writer)
    } else if (isList(fmember.typeName)) {
        // GEP zum list-field
        // jw_arr_begin(writer, fname)
        // emit loop over list elements
        // jw_arr_end(writer)
    } else {
        // load scalar → jw_set_*(writer, fname, val)
    }
}
→ Dafür brauchen wir eine interne emitSerializeStructFields(ctx, writer, structPtr, typeName) Helper-Funktion (kein IR-Funktion, nur C++-Helper der IR emittiert).

5. src/core/serialize_impl.h (UPDATE)
Kommentare aktualisieren: JSON-Format, ptr<T>-Semantik, any=Compiler-Fehler.

6. src/core/ast_compiler_call.cpp — visitSysSerializeCall (UPDATE)
serialize:


1. Resolve argSym + argType
2. any → LOG_ERROR
3. ptr<T>: strip ptr<>, lade Pointer aus alloca, serialize als T
4. !isSerializable → LOG_ERROR
5. struct/list/map: hole alloca, SerializeImpl::serialize(ctx, rawAlloca, argType)
6. enum/primitiv: evaluate expression, SerializeImpl::serialize(ctx, val, argType)
deserialize:


1. Parse optional type hint (2. Arg)
2. Kein Hint / "Code" → existierendes CodeImpl-Verhalten
3. ptr<T> im Hint: deserialize als T (gibt i8* heap zurück), direkt zurückgeben
4. any → LOG_ERROR
5. !isSerializable → LOG_ERROR
6. SerializeImpl::deserialize(ctx, src, typeName)
7. spec/29_serialize.md (UPDATE)
Neuen Abschnitt "5. sys.serialize / sys.deserialize" einfügen:

JSON-Format für compound types dokumentieren
ptr<T> Semantik
any → Compiler-Fehler
Vollständige Typ-Tabelle (supported/unsupported)
8. tests/test_serialize_ext.aiml (UPDATE)
Erwartete Outputs anpassen (JSON statt YAML-like)
Tests für ptr<T> hinzufügen
Tests für nested struct hinzufügen
Implementierungsreihenfolge
yyjson vendoren — yyjson.h + yyjson.cpp in src/core/ ablegen
yyjson_wrapper.h + yyjson_wrapper.cpp implementieren und testen (mit kurzem unit-test in C++)
serialize_impl.cpp komplett neu schreiben (Primitiven, Enums, Structs, Listen, Maps, nested fields)
visitSysSerializeCall updaten für ptr<T>, any-Check
Spec updaten
Tests updaten + build + bash run_all_tests.sh → 30/30 ✓
Verifikation
bash run_all_tests.sh → alle 30 Tests bestehen
Manuell: ./out/aiml -r tests/test_serialize_ext.aiml — Ausgabe zeigt JSON-Format
Manuell: ./out/aiml -r /tmp/test_ptr.aiml — ptr<T> round-trip
Manuell: ./out/aiml -r /tmp/test_nested.aiml — nested struct round-trip
