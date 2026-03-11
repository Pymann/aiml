# Kapitel 45 — Heap-Serialization

> **Leitgedanke:** Wenn AIML-Daten das aktuelle Prozess-Memory verlassen — in eine WAL-Datei,
> über ein Netzwerk-Socket oder in einen Message-Channel — müssen Heap-Pointer durch
> Byte-Streams ersetzt werden. Diese Umwandlung geschieht vollautomatisch, transparent
> für den AIML-Code, und ist rekursiv für beliebig tief verschachtelte Typen.

---

## 1. Das Problem

Viele AIML-Typen enthalten **Heap-Pointer**: Adressen, die nur im aktuellen Prozess gültig sind.

```aiml
- class:
    name: Card
    members:
      - [name,     string]      # char* → Heap-Adresse
      - [mkp,      i32]         # reiner Wert, kein Pointer
      - [tags,     list<string>] # AimlList* → Heap-Adresse
      - [metadata, map<string:string>] # i8* → Heap-Adresse
```

Wenn ein `Card`-Struct über `memcpy` serialisiert wird (wie es das bisherige
`Kv<string:Card>` oder `mirror.register` intern tut), werden die rohen Pointer-Werte
gespeichert:

```
WAL-Datei: [..., 0x7fff12345678, 42, 0x7fff87654321, ...]
                 ↑ name-Pointer     ↑ tags-Pointer
```

Nach einem Neustart (oder in einem anderen Prozess) sind diese Adressen **ungültig**.
Das Ergebnis ist ein Segfault oder Datenmüll.

Betroffen sind alle Systeme die Structs über Prozessgrenzen oder Lebenszeit-Grenzen
bewegen:

| System | Grenze |
|--------|--------|
| `Kv<string:T>` mit Persist | WAL → Neustart |
| `mirror` Namespace | Prozess A → Prozess B (Netzwerk) |
| `chan<T>` (zukünftig) | Thread-Übergabe mit Ownership |

---

## 2. Lösung: Automatische Heap-Serialization

Der Compiler kennt für jeden Typ seine vollständige Feld-Struktur. Er emittiert
einen **Type Descriptor** als statisches Constant-Array im LLVM-Modul.
Die Laufzeit (Runtime) nutzt diesen Descriptor um:

- **Encode**: Struct → Byte-Blob (alle Heap-Pointer durch ihre Daten ersetzt)
- **Decode**: Byte-Blob → Struct (Heap-Speicher neu allokiert, Pointer wiederhergestellt)

Für den AIML-Code ändert sich **nichts**:

```aiml
# Identischer Code — ob mit oder ohne string-Felder:
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "cards", {persist: true, path: "./db/"}] }]
- call: [kv.set, "abc", my_card]     # → intern: encode(my_card)
- decl: [c, Card]
- set:  [c, { call: [kv.get, "abc"] }]   # → intern: decode(blob)
```

---

## 3. Der Type Descriptor

### 3.1 Design-Prinzip: Flaches Node-Array

Ein rekursiver C-Struct mit Pointern wäre schwierig als LLVM-Constant-Global zu
emittieren. Stattdessen wird ein **flaches Array von `AimlTypeNode`** verwendet,
wobei Kinder durch ihren Integer-Index referenziert werden.

```c
typedef enum {
    AIML_TD_I8       = 0,   // 1 byte
    AIML_TD_I16      = 1,   // 2 bytes
    AIML_TD_I32      = 2,   // 4 bytes
    AIML_TD_I64      = 3,   // 8 bytes
    AIML_TD_F32      = 4,   // 4 bytes
    AIML_TD_F64      = 5,   // 8 bytes
    AIML_TD_STRING   = 6,   // char*  → [i32 len][bytes]
    AIML_TD_STRUCT   = 7,   // param1=n_fields, param2=index_of_first_field_node
    AIML_TD_LIST     = 8,   // param1=child_index (Element-Typ)
    AIML_TD_MAP      = 9,   // param1=key_child, param2=value_child
    AIML_TD_BUFFER   = 10,  // param1=child_index (Element-Typ)
    AIML_TD_RAWBYTES = 11,  // param1=byte_count (für vec3, mat4, eingebettete value-Structs)
} AimlTDKind;

typedef struct {
    int32_t kind;     // AimlTDKind
    int32_t param1;   // Bedeutung abhängig von kind (siehe oben)
    int32_t param2;   // Bedeutung abhängig von kind (siehe oben)
    int32_t offset;   // Byte-Offset dieses Feldes im Parent-Struct
                      // (nur relevant für Felder innerhalb eines STRUCT)
} AimlTypeNode;
```

### 3.2 Encoding als flaches Array

Jeder Typ wird als zusammenhängende Node-Sequenz kodiert. Der Root-Node steht
immer an Index 0. Kinder folgen in Tiefensuche (DFS-Reihenfolge).

**Beispiel: `Card { name:string, mkp:i32, tags:list<string> }`**

```
Index | kind     | param1 | param2 | offset
------+----------+--------+--------+-------
  0   | STRUCT   |   3    |   1    |   0     ← Root: 3 Felder, erste ab Index 1
  1   | STRING   |   0    |   0    |   0     ← Feld "name" bei Offset 0
  2   | I32      |   0    |   0    |   8     ← Feld "mkp" bei Offset 8
  3   | LIST     |   4    |   0    |  16     ← Feld "tags" bei Offset 16, Elem-Typ ab Index 4
  4   | STRING   |   0    |   0    |   0     ← Element-Typ von tags
```

**Beispiel: `Stats { scores:list<list<i32>>, label:string }`**

```
Index | kind     | param1 | param2 | offset
------+----------+--------+--------+-------
  0   | STRUCT   |   2    |   1    |   0
  1   | LIST     |   3    |   0    |   0     ← Feld "scores", Elem-Typ ab Index 3
  2   | STRING   |   0    |   0    |   8     ← Feld "label"
  3   | LIST     |   4    |   0    |   0     ← äußere Liste → Elem ab Index 4
  4   | I32      |   0    |   0    |   0     ← innerstes Element
```

**Beispiel: `Config { data:map<string:list<i32>>, count:i64 }`**

```
Index | kind   | param1 | param2 | offset
------+--------+--------+--------+-------
  0   | STRUCT |   2    |   1    |   0
  1   | MAP    |   3    |   4    |   0     ← Feld "data", Key-Typ=3, Val-Typ=4
  2   | I64    |   0    |   0    |   8     ← Feld "count"
  3   | STRING |   0    |   0    |   0     ← Key-Typ von map
  4   | LIST   |   5    |   0    |   0     ← Value-Typ von map
  5   | I32    |   0    |   0    |   0     ← Element-Typ der Liste
```

### 3.3 Rekursion über Verschachtelung

Der Serializer und Deserializer arbeiten **rekursiv** über den Node-Array:

```
serialize(node_idx, data_ptr):
  node = nodes[node_idx]
  switch node.kind:
    STRUCT:
      for i in range(node.param1):
        field = nodes[node.param2 + i]
        serialize(node.param2 + i, data_ptr + field.offset)
    LIST:
      list = *(AimlList*)(data_ptr)
      write i32: list.len
      elem_size = size_of(nodes[node.param1])
      for i in range(list.len):
        serialize(node.param1, list.data + i * elem_size)
    STRING:
      str = *(char**)(data_ptr)
      len = strlen(str)
      write i32: len
      write bytes: str[0..len]
    I32:   write 4 bytes
    I64:   write 8 bytes
    ...
    MAP:
      m = *(AimlMap*)(data_ptr)
      write i32: m.count
      for each (k, v) in m:
        serialize(node.param1, &k)
        serialize(node.param2, &v)
    BUFFER:
      buf = *(AimlBuffer*)(data_ptr)
      write i32: buf.size
      write bytes: buf.data[0..buf.size]
    RAWBYTES:
      write node.param1 bytes raw
```

---

## 4. Binary Wire Format

Das erzeugte Blob hat folgende Struktur:

```
[i32 total_data_len]   ← Länge des Daten-Teils (ohne diese 4 Bytes selbst)
[field_data...]        ← rekursiv enkodierte Felder
```

Enkodierungsregeln pro Knoten-Typ:

```
STRING          →  [i32 len][len bytes]              (kein Null-Terminator)
I8 / U8         →  [1 byte]
I16 / U16       →  [2 bytes little-endian]
I32 / U32 / F32 →  [4 bytes little-endian]
I64 / U64 / F64 →  [8 bytes little-endian]
STRUCT          →  Felder in Deklarations-Reihenfolge, rekursiv
LIST            →  [i32 count] [element_0] [element_1] ...
MAP             →  [i32 count] ([key] [value]) × count
BUFFER          →  [i32 byte_count] [raw bytes]
RAWBYTES(N)     →  [N raw bytes]     (keine Länge — N ist im Descriptor fest)
```

Alle Multi-Byte-Integer: **little-endian**.

---

## 5. C-API: `ser_runtime.h`

```c
// Encode: erzeugt malloc'd Blob = [i32 total_len][data...]
// Aufrufer ist verantwortlich für free(blob).
char* aiml_ser_encode(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);

// Decode: füllt pre-allokierten out-Buffer.
// Strings werden strdup'd, Listen/Maps/Buffer werden malloc'd.
// Muss mit aiml_ser_free_decoded bereinigt werden.
void aiml_ser_decode(const char* blob, int32_t blob_len,
                     void* out,
                     const AimlTypeNode* nodes, int32_t n_nodes);

// Gibt alle Heap-Ressourcen frei, die aiml_ser_decode allokiert hat.
// Gibt nicht den out-Buffer selbst frei (er gehört dem Aufrufer).
void aiml_ser_free_decoded(void* obj, const AimlTypeNode* nodes, int32_t n_nodes);

// Hilfsfunktion: berechnet die Größe des enkodieren Blobs ohne zu allokieren.
int32_t aiml_ser_encoded_size(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);
```

---

## 6. Compiler-Integration

### 6.1 Descriptor-Emission in `registerTypeDecl`

Für jede `class:` oder `model:` mit mindestens einem Heap-Feld emittiert der Compiler
ein privates Constant-Global:

```llvm
@__aiml_sernodes_Card = private constant [5 x { i32, i32, i32, i32 }] [
  { i32 7, i32 3, i32 1, i32 0 },   ; STRUCT, 3 Felder, ab Index 1
  { i32 6, i32 0, i32 0, i32 0 },   ; STRING, offset=0
  { i32 2, i32 0, i32 0, i32 8 },   ; I32, offset=8
  { i32 8, i32 4, i32 0, i32 16 },  ; LIST, elem=4, offset=16
  { i32 6, i32 0, i32 0, i32 0 },   ; STRING (Element-Typ)
]
```

Gespeichert in `ClassInfo`:
```cpp
llvm::GlobalVariable* serNodesVar   = nullptr;  // @__aiml_sernodes_TypeName
int32_t               serNodesCount = 0;
bool                  hasHeapFields = false;
```

`hasHeapFields` ist `true` wenn mindestens ein Feld (oder ein transitiv verschachteltes
Feld) ein Heap-Pointer enthält: `string`, `list<T>`, `map<K:V>`, `buffer<T>`.

### 6.2 Feld-Typ → Node-Kind Mapping

| AIML Typ          | AimlTDKind    | Untertypen (param1/param2)        |
|-------------------|---------------|-----------------------------------|
| `string`          | TD_STRING     | —                                 |
| `i8`, `u8`        | TD_I8         | —                                 |
| `i16`, `u16`      | TD_I16        | —                                 |
| `i32`, `u32`      | TD_I32        | —                                 |
| `i64`, `u64`      | TD_I64        | —                                 |
| `f32`             | TD_F32        | —                                 |
| `f64`             | TD_F64        | —                                 |
| `list<T>`         | TD_LIST       | param1 = child_index(T)           |
| `map<K:V>`        | TD_MAP        | param1 = child(K), param2 = child(V) |
| `buffer<T>`       | TD_BUFFER     | param1 = child_index(T)           |
| `vec2/vec3/vec4`  | TD_RAWBYTES   | param1 = sizeof (8/12/16)         |
| `mat4`            | TD_RAWBYTES   | param1 = 64                       |
| User-class T      | TD_STRUCT     | param1=n, param2=first_field_idx  |

Nested user-classes werden inline als STRUCT-Knoten eingebettet (DFS).

### 6.3 Verwendung durch `db.open_kv`

Wenn der Value-Typ `hasHeapFields == true`:

```aiml
# AIML-Code (unverändert):
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "cards", {persist: true, path: "./db/"}] }]

# Emittiertes LLVM IR (vereinfacht):
%kv = call i8* @aiml_db_kv_open_typed_persist(
    i8* "cards",
    i64 sizeof(Card),
    i8* "./db/",
    i8* bitcast(@__aiml_sernodes_Card to i8*),
    i32 5   ← n_nodes
)
```

### 6.4 Verwendung durch `mirror.register`

```aiml
# AIML-Code (unverändert):
- call: [sess.register, my_player]

# Bei hasHeapFields == true → emittiert:
call void @aiml_mirror_session_register_typed(
    i8* sess,
    i8* player_ptr,
    i64 sizeof(Player),
    i8* bitcast(@__aiml_sernodes_Player to i8*),
    i32 n_nodes
)
```

---

## 7. Laufzeit-Integration

### 7.1 `Kv<string:T>` mit Heap-Feldern

`AimlKv` speichert den Descriptor:

```cpp
struct AimlKv {
    int64_t             val_size;
    const AimlTypeNode* ser_nodes  = nullptr;   // null → bisheriger Pfad
    int32_t             ser_n      = 0;
    // Decoded-Cache: deserialisierte Structs für stabilen kv_get-Pointer
    std::unordered_map<std::string, std::vector<uint8_t>> decoded_cache;
    // ...
};
```

**`kv_set(key, val)`**:
- `ser_nodes != null` → `aiml_ser_encode(val, nodes, n)` → blob im map speichern
- decoded_cache[key] invalidieren
- WAL: blob wie bisher als Byte-Sequenz schreiben (Format unverändert)

**`kv_get(key)`**:
- `ser_nodes != null` → blob aus map → prüfe decoded_cache → falls miss: `aiml_ser_decode` → in decoded_cache speichern → return pointer
- Pointer in decoded_cache bleibt stabil bis nächstes `kv_set` / `kv_delete`

**WAL-Replay**:
- Blobs werden wie rohe Bytes gelesen (Format bereits korrekt: `[val_bytes:u32][data]`)
- Decode passiert lazy beim ersten `kv_get`

### 7.2 `mirror` Netzwerk-Sync

`AimlMirrorRegistered` speichert den Descriptor:

```cpp
struct AimlMirrorRegistered {
    void*               obj_ptr   = nullptr;
    int64_t             obj_size  = 0;
    uint8_t*            shadow    = nullptr;
    int32_t             net_id    = 0;
    bool                is_owner  = false;
    float               sync_rate = 0.0f;
    bool                manual_sync = false;
    const AimlTypeNode* ser_nodes = nullptr;   // null → raw memcpy (bisheriges Verhalten)
    int32_t             ser_n     = 0;
};
```

**`send_delta_pkt`** (bei `ser_nodes != null`):
- `aiml_ser_encode(obj_ptr, nodes, n)` → blob
- Paket: `[header 6 bytes][i32 blob_len][blob_data]`
- Shadow-Update: `aiml_ser_encode` desselben Objekts → in shadow speichern

**`process_packet` PKT_DELTA Empfang** (bei `ser_nodes != null`):
- Blob aus Paket lesen: `[i32 blob_len][blob_data]`
- `aiml_ser_decode(blob_data, blob_len, obj_ptr, nodes, n)`

**Dirty-Detection** (`memcmp` in `update()`):
- Bleibt vorerst auf Raw-Bytes basierend (acceptable false-positives für Pointer-Änderungen)
- v2: Shadow als Blob speichern, Blob-Vergleich

---

## 8. Scope

### In scope (v1)
- Alle primitiven Typen: `i8`, `i16`, `i32`, `i64`, `f32`, `f64`, `string`
- `list<T>` für alle primitiven T + `string` + user-defined structs
- `map<string:string>`, `map<string:T>` (T primitiv oder struct)
- `buffer<u8>` (und `buffer<i8>`)
- Pure-value Structs als Felder (`vec2`, `vec3`, `vec4`, `mat4`) → RAWBYTES
- User-defined Structs als Felder (eingebettete STRUCT-Knoten)
- Beliebige Verschachtelungstiefe (`list<list<list<string>>>` usw.)

### Nicht in scope (v2)
- `Store<T>` Persistenz (nutzt mmap, andere Mechanismus)
- `chan<T>` Thread-Übergabe
- Mirror Dirty-Detection als Blob-Vergleich
- Zirkuläre Referenzen (explizit verboten — Compiler-Fehler)
- `ptr<T>` / `ref<T>` als Struct-Felder (pointer auf andere Objekte — nicht serialisierbar)

---

## 9. Beispiel: 9Rooms Backend

```aiml
- class:
    name: Card
    members:
      - [name,        string]
      - [card_type,   string]
      - [mkp,         i32]
      - [description, string]
      - [abilities,   string]
      - [lp,          i32]
      ...

# Emittierter Descriptor (vereinfacht):
# @__aiml_sernodes_Card = [
#   {STRUCT, 15, 1, 0},     ← 15 Felder, ab Index 1
#   {STRING,  0, 0, 0},     ← name
#   {STRING,  0, 0, 8},     ← card_type
#   {I32,     0, 0, 16},    ← mkp
#   {STRING,  0, 0, 20},    ← description
#   {STRING,  0, 0, 28},    ← abilities
#   {I32,     0, 0, 36},    ← lp
#   ...
# ]

# AIML-Code unverändert — Persistenz funktioniert korrekt über Neustarts:
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "9r_cards", {persist: true, path: "./aiondb/"}] }]
- call: [kv.set, card_id, my_card]   # serialisiert automatisch
```

---

## 10. Implementierungsreihenfolge

| # | Datei | Inhalt |
|---|-------|--------|
| 1 | `src/sys/ser_runtime.h` | `AimlTypeNode`, `AimlTDKind`, C-API |
| 2 | `src/sys/ser_runtime.cpp` | `aiml_ser_encode`, `aiml_ser_decode`, `aiml_ser_free_decoded` |
| 3 | `src/sys/db_runtime.h` | Neue Funktionen `aiml_db_kv_open_typed[_persist]` |
| 4 | `src/sys/db_runtime.cpp` | Typed-Kv + decoded_cache + WAL-Integration |
| 5 | `src/sys/mirror_runtime.h` | `aiml_mirror_session_register_typed`, `register_id_typed` |
| 6 | `src/sys/mirror_runtime.cpp` | Typed send/recv |
| 7 | `src/core/context.h` | `ClassInfo`: `serNodesVar`, `serNodesCount`, `hasHeapFields` |
| 8 | `src/core/ast_compiler.cpp` | Descriptor-Global in `registerTypeDecl` emittieren |
| 9 | `src/core/builtin_registry.cpp` | `db.open_kv` → typed Variante bei `hasHeapFields` |
| 10 | `src/core/ast_compiler_call.cpp` | `sess.register` → typed Variante bei `hasHeapFields` |
| 11 | `compiler.cpp` | `#include ser_runtime.h` + 5 neue `AddSymbol`-Calls |
| 12 | `tests/test_heap_ser.aiml` | Unit-Tests für alle Typen |
| 13 | `tests/test_mirror.aiml` | Erweitern: Phase mit string-Feldern |
