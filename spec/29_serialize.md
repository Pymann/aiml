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

### 5.2 Addendum — maps are written with their content

- **F29-22** A `map<K:V>` is written **with its entries**, as a value of its own
  (row 109) as well as as a model field (row 112).  Until 2026-10-18 the
  implementation wrote **every** map as `{}`: the writer iterated over `capacity` and
  only let through slots whose `occupied` byte was set — but `MapImpl::put`
  never sets that byte.  An aiml map is **dense** (entries `0..size-1`;
  `get`, `len` and `remove` compute the same way, `remove` shifts down with `memmove`),
  and the serializer was the only reader of the `occupied` field.  The
  spec was right, the implementation was not; found on the OCI converter
  (`cosmos/doc/spec/110` §110.25), where `annotations` silently stayed empty.
  `tests/test_ser_alias_bare.aiml` pins down both cases.

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
- set:  [text, { call: [sys.serialize, value, plain] }]           # short form
- set:  [text, { call: [sys.serialize, nums, "list<i32>", plain] }]  # with a type name
```

- **F29-17** Both forms apply: the style may be the **second** argument or come after
  an explicit type name.  Until 2026-10-18 the implementation only accepted the long form
  and discarded the short one **silently** — no error, no effect, even with `pretty`.
  The distinction is not ambiguous: a `JsonStyle` is a variable of this type, a
  type name is a literal.

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

### 5.2c `skip_empty` — leaving out what is empty

An aiml model has no "not set": every field has a default value, and
`sys.serialize` writes every field.  Foreign formats, however, distinguish
**missing** from **empty**: the OCI image spec says of `variant` *SHOULD be omitted
if unknown*, and `"annotations": {}` or `"Entrypoint": []` are ballast there
that strict readers complain about.  `skip_empty` closes exactly this gap — without
bending the model (a `ptr<string>` just to be able to express "missing"
would be the worse answer).

```aiml
- decl: [lean, JsonStyle]
- set:  [lean.bare, 1]
- set:  [lean.skip_empty, 1]
- set:  [text, { call: [sys.serialize, img_cfg, lean] }]
```

- **F29-18** What is removed are **object members** whose value is an empty string,
  an empty array or an empty object — at every level.  **Not**
  removed are `0`, `false` and `null`: a zero is a value, not an absence.
  Whoever leaves out `0` changes data; whoever leaves out `""` only changes the notation.
- **F29-19** The check runs **bottom-up**: if an object becomes empty itself through
  the removal of its members, it is dropped as well.  The **root**
  always stays — an empty document would not be a document.
- **F29-20** **Array elements** are never removed, not even empty ones: an array
  carries its meaning in the order, and a removed entry shifts
  all following ones.
- **F29-21** `skip_empty` and `bare` are independent and can be combined; both
  work after writing on the finished document (like `pretty`), not in the
  write path.  Reading back remains possible: missing fields keep their
  default value (F29-16) — exactly the one that caused them to be left out.

### 5.2d Reading: defaults, `null`, errors

`sys.deserialize` stays **tolerant** (F29-15): a document that grows stays
readable, and an error does not abort reading — a value always results.
But tolerant does not mean **silent**.  A tool whose entire input is a
configuration file (exodus: `exodus.json`) must find out that `"jobs":
"viele"` was not a value and that `{"name": kaputt` is not a document.  Until
2026-10-29 it got only zeros in both cases.

- **F29-16 (refined)** A field that the document does not mention **or sets to
  `null`** receives its **declared default value**: `[jobs, i32, 4]`
  reads as 4, `[name, string, "app"]` as `"app"`, `[on, bool, true]` as
  `true`.  That applies at every level — in nested models and in the
  elements of a `list<Model>`.  Until 2026-10-29 the default value was
  ignored (0 or `""` was read), although F29-16 promised it.
  Default values are taken over if they are **literals** (number, boolean,
  string) — that is the ordinary case; an expression or an
  enum member as a default still reads as a null/empty value when the key is
  missing.
- **F29-23** If a **nested object** is missing entirely, it is still created
  completely: with the defaults of its fields, empty strings and empty
  lists/maps.  (Before, its strings and containers stayed null pointers —
  a `.len` on them crashed.)
- **F29-24** Numbers are **converted between integer and floating point**:
  an `f32` field reads `10` as 10.0, an `i32` field reads `3.0` as 3 (truncated
  towards zero).  Before, the "wrong" kind of number silently read 0.
  A `true`/`false` in a number field reads as 1/0.
- **F29-25** `sys.json_error() → string` says what the **last**
  `sys.deserialize` **of this thread** found wrong; `""` means clean.  Every
  call of `sys.deserialize` resets the message.  Reported are
  - a **syntax error** with line and column (1-based):
    `line 3 column 12: unexpected character`;
  - a value of the **wrong JSON kind** with its path:
    `deps[2].version: expected a string, got a number`,
    `jobs: expected a number, got a string`.  The value then receives the
    default value, as if it were missing.
  Several findings are listed line by line (at most 20, then `…`).  An unknown
  key is **not** an error (F29-16).
- **F29-26** The check belongs to the caller: whoever wants to be strict checks after
  reading.

  ```aiml
  - decl: [cfg, ProjectConfig, { call: [sys.deserialize, text, "ProjectConfig"] }]
  - decl: [problem, string, { call: [sys.json_error] }]
  - if: { condition: { gt: [problem.len, 0] }, then: [{ call: [log.error, "exodus.json: ${problem}"] }, { return: 1 }] }
  ```
- **F29-27** A `list<T>` may come as a **bare array**
  (`[1,2,3]`), a `map<string:V>` as a **bare object** (`{"a":1}`) —
  the way foreign tools write them.  The wrapper (`{"values":[…]}`,
  `{"entries":{…}}`, §5.2) is still read; an object counts as a wrapper if it
  carries the key `entries` or `__type`.  Writing is unchanged,
  with a wrapper (F29-13).
- **F29-28** The error text is **per thread**: two threads reading at the same time
  each see their own findings.

`tests/test_ser_defaults_errors.aiml` pins down F29-16 (refined) and
F29-23 to F29-28.

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

### 5.4.1 In-process handles carry a process stamp

`chan<T>` and `hashmap` are **handles**, not values: the wire form is the pointer
itself, valid only inside the process that wrote it. The encoder therefore writes
an 8-byte **process stamp** after the pointer — a value drawn once per process —
and the decoder compares it:

| Stamp | Decoder |
|---|---|
| equal | the handle is live: the pointer is adopted and retained (in-process handoff, `sys.clone`, pipelines) |
| different | the blob was written by **another process**: the field decodes as `null`, the decode error flag is set, and `[ser-ERROR] handle from another process` is printed |

A pointer read from a blob is **never** dereferenced before that comparison. A
stored blob (KV, file, network) outlives its process, so without the stamp a
later decode retains a dead address — observed as a SIGSEGV in a server reading
a months-old store.

The stamp does not make such a model storable: a handle that decodes as `null`
is a loss of data, and §5.4 stands — it only keeps the loss from becoming a
crash.

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
