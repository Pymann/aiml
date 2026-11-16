# AIML Language Reference Guide

This document serves as a comprehensive reference for AIML's built-in data types, structural methods, operators, and standard namespaces.

## Table of Contents
1. [Data Types](#1-data-types)
    - [Core & Primitive Types](#11-core--primitive-types)
    - [Extended Integer Types](#12-extended-integer-types)
    - [Pointer Types](#13-pointer-types)
    - [Fixed-Size Arrays](#14-fixed-size-arrays)
    - [Collections](#15-collections)
    - [Math & Vector Types](#16-math--vector-types)
    - [Path Type](#17-path-type)
2. [Operators & Logic](#2-operators--logic)
    - [Unary Operators](#21-unary-operators)
    - [Binary Logic Operators](#22-binary-logic-operators)
    - [Comparison Operators](#23-comparison-operators)
    - [Pointer Operators](#24-pointer-operators)
3. [Built-in Methods & Properties](#3-built-in-methods--properties)
    - [String Methods (ptr<i8>)](#31-string-methods-ptri8)
    - [List Methods (list<T>)](#32-list-methods-listt)
    - [Map Methods (map<K:V>)](#33-map-methods-mapkv)
    - [Memory & Size Properties](#34-memory--size-properties)
4. [Namespaces & Standard Library](#4-namespaces--standard-library)
    - [System & Environment (`sys`)](#41-system--environment-sys)
    - [Time (`time`)](#42-time-time)
    - [I/O & File System (`io`)](#43-io--file-system-io)
    - [Logging (`log`)](#44-logging-log)
    - [Database & Storage (`db`)](#45-database--storage-db)
    - [Math & Linear Algebra (`math`)](#46-math--linear-algebra-math)
    - [OS & Bare-Metal Hardware](#47-os--bare-metal-hardware)
    - [Concurrency & Versioning](#48-concurrency--versioning)
    - [Network & HTTP (`http`, `ws`)](#49-network--http-http-ws)
    - [Game Engine (`engine`)](#410-game-engine-engine)
    - [Multiplayer (`mirror`)](#411-multiplayer-mirror)
5. [Language Syntax & Structures](#5-language-syntax--structures)
    - [Variables (`decl`, `set`)](#51-variables-decl-set)
    - [Control Flow (`if`, `while`)](#52-control-flow-if-while)
    - [Functions (`def`, `call`)](#53-functions-def-call)
    - [Object-Oriented (`class`, `model`, `interface`)](#54-object-oriented-class-model-interface)
    - [Modules (`namespace`, `import`)](#55-modules-namespace-import)

---

## 1. Data Types

### 1.1 Core & Primitive Types
| Type Keyword | Description | LLVM Equivalent |
| :--- | :--- | :--- |
| `void` | No value (return type only) | `void` |
| `bool` | Boolean value | `u8` |
| `i32` | Signed 32-bit Integer (default fallback) | `i32` |
| `f64` | 64-bit Floating Point (default fallback) | `double` |
| `f32` | 32-bit Floating Point | `float` |
| `string` | String literal / pointer (alias for `ptr<i8>`) | `i8*` |

*Note: Strings implicitly coerce to `i32` or `i64` by automatically calling `atoi`/`atoll` when assigned to integer types.*

### 1.2 Extended Integer Types
| Type Keyword | Description |
| :--- | :--- |
| `i8`, `i16`, `i64` | Explicit width signed integers |
| `u8`, `u16`, `u32`, `u64`| Explicit width unsigned integers |
| `i8_least`, `i8_fast` | Least/fastest width integers |

### 1.3 Pointer Types
- `ptr<T>`: Typed pointer to `T`. E.g., `ptr<i32>`.
- `func`: Function pointer / callable reference type. Used for callbacks and indirect invocation.

### 1.4 Fixed-Size Arrays
Stack-allocated, bounded arrays. See `spec/52_arrays.md` for the full reference.
- **Declaration:** `array<T:N>` (e.g., `"array<f32:64>"`, `"array<ptr<Node>:8>"`, `"array<Point:4>"`)
- **Element types:** primitive scalars (`i32`, `f32`, ...), `ptr<T>`, model/struct value types. Not: `string`, `list<T>`, `map`, `buffer`.
- **Read:** `{ at: [arr, i] }` — GEP + load, zero overhead.
- **Write:** `- idx: [arr, i, value]` statement.
- **Pointer slots** are null-initialized; **struct slots** are zero-initialized.
- **Multi-dimensional:** `"array<f32:4:4>"` — row-major flat layout.
- **Properties:** `.len` (total count), `.dim0`/`.dim1` (per-dimension size), `.data_ptr` (i8* for C interop).

### 1.5 Collections
Heap-allocated, dynamic collections.
- `list<T>`: Dynamic list of type `T`.
- `map<K:V>`: Key-value map. (e.g., `map<string:i32>`). `map<string:any>` supports heterogeneous boxed values.
- `buffer<T>`: Buffer slices for direct memory blocks.
- `Kv<K:V>`: Persistent key-value store handle from AionDB.
- `Store<T>`: Typed persistent entity store.
- `VecStore`: Vector similarity index for `list<f32>` embeddings.
- `TimeSeries<T>`: Append-only persistent event stream.

### 1.6 Math & Vector Types
Value-types located directly on the stack for linear algebra.
- `vec2`, `vec3`, `vec4`: Floating point vectors (16-byte aligned for `vec3`/`vec4`).
- `ivec2`, `ivec3`: Integer vectors.
- `quat`: Quaternion (x, y, z, w).
- `mat4`: 4x4 matrix (column-major).

### 1.7 Path Type
The `path` type is a 64-bit interned pointer used for normalized, immutable, and optimized file system paths. Path compares are O(1) pointer comparisons. Sub-types include `abspath`, `relpath`, `dirpath`, and `filepath`.

---

## 2. Operators & Logic

All control flow structures (`if`, `while`) use deterministic conditions with strict arity operators.

### 2.1 Unary Operators (Arity 1)
| Operator | Description |
| :--- | :--- |
| `!` | Logical NOT |
| `?` | Pointer dereference |
| `&` | Address-of |
| `-` | Negation / Unary minus |

### 2.2 Binary Logic Operators (Arity 2)
Logical chains with more than 2 elements must be explicitly nested.
| Operator | Description |
| :--- | :--- |
| `and` / `&&` | Logical AND |
| `or`  / `\|\|` | Logical OR |

### 2.3 Comparison Operators (Arity 2)
| Operator | Description |
| :--- | :--- |
| `eq` | Equal |
| `neq`| Not equal |
| `lt` | Less than |
| `le` | Less than or equal |
| `gt` | Greater than |
| `ge` | Greater than or equal |
| `peq` / `pneq`| Pointer equality/inequality |

### 2.4 Pointer Operators
| Syntax | Application | Action |
| :--- | :--- | :--- |
| `&` | `{ '&': [var] }` | Get the address of a variable (Pointer Creation). |
| `?` | `{ '?': [ptr] }` | Read the value behind the pointer (Dereference). |

---

## 3. Built-in Methods & Properties

### 3.1 String Methods (`ptr<i8>`)
Strings are null-terminated, immutable UTF-8 C-strings. Methods return new allocated strings.
- **`.len`** `(property)`: Returns logical character length (`i32`).
- **`[substr, start: i32] -> string`**: Returns substring from start index.
- **`[substr, start: i32, len: i32] -> string`**: Returns up to `len` bytes from start index.
- **`[starts_with, prefix: string] -> i32`**: Returns `1` if starting with prefix, `0` otherwise.
- **`[ends_with, suffix: string] -> i32`**: Returns `1` if ending with suffix, `0` otherwise.
- **`[index_of, needle: string] -> i32`**: Returns first byte offset, or `-1` if missing.
- **`[lower] -> string`**: Returns lowercased string (ASCII).
- **`[trim] -> string`**: Removes leading/trailing whitespace.
- **`[replace, old: string, new: string] -> string`**: Replaces occurrences.
- **`[split, delimiter: string] -> list<string>`**: Splits string at occurrences.

### 3.2 List Methods (`list<T>`)
- **`[push, val: T]`**: Appends a value.
- **`[pop] -> T`**: Removes and returns last value.
- **`[insert, index: i32, val: T]`**: Inserts a value at index.
- **`[remove, index: i32]`**: Removes value at index.
- **`[get, index: i32] -> T`**: Returns value at index. Often used as method call `lst.get(i)`.
- **`[clear]`**: Empties the list.
- **`[free]`**: Explicit memory release if no RAII.
- **`.empty`** `(property)`: Returns `bool`, `true` if empty.
- **`.len`** `(property)`: Returns element count `i32`.
- **`[contains, val: T] -> i32`**: `1`/`0`. `eq`-semantics (strings by content).
- **`[index_of, val: T] -> i32`**: First matching index, or `-1`.
- **`[count, val: T] -> i32`**: Number of matches.
- **`[remove_value, val: T] -> i32`**: Removes the FIRST match; `1`/`0`.
- **`[extend, other: list<T>]`**: Appends value-copies of `other` (same `T`; `self.extend(self)` ok).
- **`[reverse]`**: In-place reverse.
- **`[sort]` / `[sort_desc]`**: In-place stable insertion sort. Only int/enum/float/string.
- **`[shuffle]`**: In-place Fisher-Yates (uses `math.random_int`; non-deterministic).
- **Type rules:** `contains/index_of/count/remove_value` reject struct/model elements; `extend` rejects a different `list<U>`; unsupported types are compile errors with a source location.
- **Index syntax:** `list-0`, `list-idx` and `set: [list-idx, value]` are actively used for dynamic list access and mutation.

### 3.3 Map Methods (`map<K:V>`)
- **`[put, key: K, val: V]`**: Inserts or updates key.
- **`[get, key: K] -> ptr<V>`** (or `V` / `null`): Retrieves value. Auto-unboxes in `map<string:any>`.
- **`[remove, key: K]`**: Deletes a key. Throws if missing.
- **`[clear]`**: Empties the map.
- **`.len`** `(property)`: Returns key count `i32`.

### 3.4 Persistent Store Handles (`Kv`, `Store`, `VecStore`, `TimeSeries`)
- **`Kv<K:V>`**: Persistent AionDB map handle. Common API: `[set, key, val]`, `[get, key]`, `[has, key]`, `[delete, key]`, `[clear]`, `.len`.
- **`Store<T>`**: Typed entity store. Used with `db.open`, `insert`, `update`, `delete`, `get`, query-style helpers.
- **`VecStore`**: Embedding index for nearest-neighbor search. Common API: `insert`, `search`.
- **`TimeSeries<T>`**: Append-only timeline/event store.

### 3.5 Buffer Methods (`buffer`)
Binary memory slices with unified read/write cursor pointers.
- **`[alloc, size: i32]`**: Allocates `size` bytes.
- **`[write, value: any, type: Type]`**: Writes the value natively. Serializes primitives and deep-copies complex structs.
- **`[read, type: Type] -> any`**: Reconstructs/deserializes value from cursor.
- **`.size`** `(property)`: Amount of bytes currently written (`i32`).
- **`.capacity`** `(property)`: Max allocated size (`i32`).

### 3.6 Memory Size Methods
- **`.len`** `(property)`: Element count specific to collections (`list`, `map`, `string`, `Kv`).
- **`[size]`** `(intrinsic method)`: Usable across all generic types (`{call: [obj.size]}`). Represents the pure byte count residing in memory (`u64`). Also available via `[sys.size, Type]`.

### 3.7 Path Methods (`path`)
Paths are interned 64-bit pointers guaranteeing `O(1)` comparisons.
- **`[path.join, p1, p2, ...]`** / **`[p.join, suffix]`**: Normalizes and concatenates, generating a new interned path.
- **`[parent] -> path`**: Returns directory portion.
- **`[name] -> string`**: Returns full filename.
- **`[stem] -> string`**: Returns filename without extension.
- **`[extension] -> string`**: Returns the extension.
- **`[parts] -> list<string>`**: Returns all path fragments.
- **`[depth] -> i32`**: Number of components.
- **`[root] -> path`**: Returns platform root.
- **`[is_absolute] -> bool`**, **`[is_relative] -> bool`**, **`[is_empty] -> bool`**: State checks.
- **`[relative_to, base: path] -> path`**: Calculates relative offset.
- **`[normalize] -> path`**: Eliminates `.` / `..` resolving cleanly.
- **`[to_string] -> string`**, **`[to_os_string] -> string`**, **`[path.from_string, s: string] -> path`**: Explicit string bridges.
- **Environment Context**: **`[path.home] -> path`**, **`[path.temp_dir] -> path`**, **`[path.config_dir, name: string] -> path`**, **`[path.current_exe] -> path`**, **`[path.cwd] -> path`**.

---

## 4. Namespaces & Standard Library

### 4.1 System & Environment (`sys`)
- **`[sys.exit, code: i32]`**: Terminates the program cleanly.
- **`[sys.get_arg, index: i32] -> string`**: Retrieves CLI string argument.
- **`[sys.arg_dump, target: model|struct]`**: Parses `argv[1..]` into a named model/struct variable using normalized flag names such as `--foo-bar -> foo_bar`. Supports `bool` switches, every integer and real width, `string`, `list<string>` and a `positional` list; a field with a spec/15 alias is set by its alias (`--id`). Every non-bool flag needs a well-formed value; unknown flags and missing or malformed values terminate with exit code `1`. `-3`, `-Flies(x)` and `--5 + 1` are values, not flags (spec/23 F23-61 to F23-63).
- **`[sys.size, type: AnyClass] -> u64`**: Returns exact memory footprint of a class.
- **`[sys.type, object: any] -> Type`**: Returns the runtime class/model type object.
- **`sys.env`** `(property)`: Returns environment variables as `map<string, string>`.
- **`[sys.exe, args: list<string>, options: map] -> process`**: Spawns subprocess, returning an interactive handle allowing `.start()`, `.wait()`, `.kill()`.
- **`[sys.serialize, obj: any] -> string`**: Recursively serializes models, lists, and maps into a clean JSON string containing `__type` metadata. 
- **`[sys.deserialize, json: string, type_hint: string] -> any`**: Reconstructs JSON structures dynamically into strictly typed AIML models/structs.
- **`[sys.bundle, selector: any] -> Code`**: Bundles function names or live instances (plus their transitive dependencies) into a serialized AST payload capable of network reflection.
- **`[sys.load_bundle, pkg: Code]`**, **`[sys.exec, pkg: Code]`**: Loads network-received AST code directly into the global environment or immediately executes it natively.

### 4.2 Time (`time`)
- **`[time.now] -> i64`**: Returns Unix Epoch timestamp in ticks/ms.
- **`[time.sleep_ms, ms: i32]`**: Pauses thread execution.

### 4.3 I/O & File System (`io`)
Provides stream semantics over the internal `path` typings.
- **`[io.read, fd: file_handle, size: i32] -> string`**: Reads raw byte chunks up to size.
- **`[io.read_all, fd: file_handle] -> string`**: Dumps remaining handle into string.
- **`[io.write, fd: file_handle, content: string]`**: Appends content to descriptor.
- **`[io.read_file, p: path] -> string`**: Reads a whole file as string.
- **`[io.write_file, p: path, content: string|buffer<u8>] -> i32`**: Writes (truncates) a whole file; `0` = ok, `-1` = error. A `string` is written up to its terminating NUL; a `buffer<u8>` is written binary-safe with its exact length (embedded NUL bytes included).
- **`[io.open, p: path, mode: string] -> file_handle`**: Standard `fopen` abstraction.
- **`[io.close, fd: file_handle]`**: Standard `fclose` equivalent.
- **`[io.exists, p: path] -> bool`**, **`[io.is_dir, p: path] -> bool`**, **`[io.is_file, p: path] -> bool`**.
- **`[io.file_size, p: path] -> i64`**: Determines file size immediately.
- **`[io.list_dir, p: path] -> list<path>`**: Returns contained non-recursive entities as paths.
- **`[io.mkdir, p: path]`**, **`[io.mkdir_all, p: path]`**: Creates directory nodes.
- **`[io.rename, src: path, dest: path]`**: Renames file pathing natively.
- **`[io.append_from, dst: path, src: path] -> i64`**: Appends the bytes of file `src` to file `dst` (created if missing), streamed in 1 MiB blocks, binary-safe. Returns the bytes appended, `-1` on error (unreadable `src`, unwritable `dst`, or `dst` and `src` naming the same file). Used to grow upload files from spooled request bodies (`req.body_path`) without loading them into memory.
- **`[io.remove, p: path]`**, **`[io.remove_all, p: path]`**: Deletes entities or trees.
- **`[io.can_read, p: path] -> bool`**, **`[io.can_write, p: path] -> bool`**, **`[io.can_exec, p: path] -> bool`**: Returns POSIX permissions bounds.

### 4.4 Logging (`log`)
Always emits location strings `(<relative_source_path>:<line_number>)` implicitly.
- **`[log.verbose, msg: string]`**, **`[log.debug, msg: string]`**, **`[log.info, msg: string]`**: Core logs.
- **`[log.warn, msg: string]`**, **`[log.error, msg: string]`**: Non-halting alerts.
- **`[log.fatal, msg: string]`**: Logs message and immediately halts with `[sys.exit, 1]`.
- **`[log.deprecated, msg: string]`**, **`[log.system, msg: string]`**: By-passes thresholds entirely.
- **`[log.set_level, level: string]`**: Adjusts global mask (`"info"`, `"debug"`, etc.).
- **`[log.set_file, p: path]`**: Hooks logger directly into secondary file natively.

### 4.5 Database & Storage (`db` & AionDB)
The core embedded database API.
- **`[db.open, name: string, options: map] -> Store<T>`**: Opens/Builds an Entity table.
- **`[db.open_kv, name: string] -> Kv<K,V>`**: Opens persistent KV map. API: `[set, key, val]`, `[get, key]`, `[has, key] -> i32`, `[delete, key]`, `.len` (property), `[clear]`. *(Note: Uses `set` and `delete`, unlike `map`).*
- **`[db.open_timeseries, name: string] -> TimeSeries<T>`**: Opens append-only event log.
- **`[db.open_vec, name: string, {dims: i32}] -> VecStore`**: Initializes vector index. In current codebase there are also variants with an explicit dimensions argument plus options.
- **`[db.query, st: Store, ops: map] -> list<ptr<T>>`**: O(N)/O(logN) filter evaluator `{where: filter_fn, order_by: "prop", limit: X}` native array mapping.
- **`[db.run_system, sys_func: func, st1: Store, st2: Store, ...]`**: ECS Archetype evaluator running intersection pipelines directly on columnar memory via SIMD arrays.
- **`[db.begin]`**, **`[db.commit]`**, **`[db.rollback]`**: Acid transaction wrapping.
- **`[db.begin_frame, time: i64]`**, **`[db.end_frame]`**: Game-tick sync blocks assigning static timestamps to mutating clusters natively.
- **`[db.checkpoint]`**, **`[db.auto_checkpoint, n: i32]`**, **`[db.reload_kv, st: Kv]`**: Write-Ahead-Log synchronization flushing.

### 4.6 Math & Linear Algebra (`math`)
Uses scalar value types / SSE-NEON intrinsics.
- **`[math.vec2, x: f32, y: f32] -> vec2`**: (Same signature applicable to `vec3`, `vec4`, `ivec2`, `ivec3`)
- **`[math.quat_euler, x: f32, y: f32, z: f32] -> quat`**: Builds rotational quaternion from radians.
- **`[math.mat4_translate, v: vec3] -> mat4`**, **`[math.mat4_scale, v: vec3] -> mat4`**, **`[math.mat4_rotate, axis: vec3, angle: f32] -> mat4`**: Space matrices.
- **`[math.mat4_perspective, fov: f32, aspect: f32, near: f32, far: f32] -> mat4`**: Camera projection matrix.
- **`[math.dot, a: vecN, b: vecN] -> f32`**, **`[math.cross, a: vec3, b: vec3] -> vec3`**: Linear properties.
- **`[math.length, v: vecN] -> f32`**, **`[math.normalize, v: vecN] -> vecN`**, **`[math.distance, a: vecN, b: vecN] -> f32`**.
- **`[math.sin, r: f32] -> f32`**, **`[math.cos, r: f32] -> f32`**, **`[math.tan, r: f32] -> f32`**: Trig.
- **`[math.min, a: f32, b: f32] -> f32`** / **`[math.min, lst: list<f32>] -> f32`** / **`[math.min, ptr: ptr<i8>, len: i32] -> f32`**: Min/Max reductions. Overloaded recursions.
  
### 4.7 OS & Bare-Metal Hardware
APIs exclusive to 0-ring compilation models targeting hardware registers natively.
- **`[hw.mmio.read8/16/32/64, addr: u64] -> uN`**: Memory-mapped loads.
- **`[hw.mmio.write8/16/32/64, addr: u64, val: uN]`**: Memory-mapped stores.
- **`[hw.mmio.fence]`**: Synchronization memory barrier (`DMB`/`MFENCE`).
- **`[hw.port.in8/16/32, port: u16] -> uX`**, **`[hw.port.out8/16/32, port: u16, val: uX]`**: x86 specific IO port access bounds.
- **`[intr.set_handler, vec: u8, fn: func]`**: Attach ISR routing hooks.
- **`[intr.enable]`**, **`[intr.disable]`**: Sets/Clears interrupt flagging.
- **`[cpu.iret]`**: Rebounds contextual boundary off bare frames.
- **`[cpu.rdmsr, reg: u32] -> u64`**, **`[cpu.wrmsr, reg: u32, val: u64]`**: Register settings overrides.
- **`[mem.phys.alloc] -> u64`**: Resolves free initial boot RAM 4KB block paging bounds.
- **`[mem.virt.create_space] -> u64`**: Establishes unique TLB / MMU bounds mapping configurations.
- **`[mem.virt.map, pml4: u64, vaddr: u64, paddr: u64, perms: map]`**: Translates OS mapped ranges natively avoiding VM bottlenecks.
- **`[atomic.load, ptr: ptr<T>] -> T`**, **`[atomic.store, ptr: ptr<T>, val: T]`**, **`[atomic.compare_exchange, ptr: ptr<T>, expected: T, desired: T] -> bool`**: Spinlock bounds context handling.

### 4.8 Concurrency & Versioning
OS threading scaling linearly on multi-core variants.
- **`[spawn, function, args...] -> thread_handle`**: Spawns OS thread natively.
- **`[thread.join]`**, **`[thread.detach]`**: Synchronizes or abandons thread execution bounds (`t: thread_handle`).
- **`thread.id`** `(property)`: OS assigned unique ID.
- **`chan<T>`**: Buffered pipe queues explicitly designed for threading. (`[send, val]`, `[recv] -> T`).
- **`[sys.free_version, function_name: string, version_id: u64]`**: Memory reclaimer cleaning up trailing functions created after applying Hot-Reload trampolines.

### 4.9 Network & HTTP (`http`, `ws`)
Zero-dependency network primitives natively supporting secure environments (`https://`, `wss://`) transparently.
- **`[http.parse_url, s: string] -> url`**: Validated HTTP URL struct (`u.protocol`, `u.host`, `u.path`, `u.get_param`).
- **`[http.session] -> http_session_handle`**: Stateful headers, keep-alive loops, and automated Cookie-Jars.
- **`[http.get, u: url] -> http_response_handle`**, **`[http.post, u: url, body: string]`**: Fire-and-forget REST requests.
- **`[http.server, host: string, port: i32] -> http_server_handle`**: Server framework natively supporting named parameters (`/users/:id`), middleware (`srv.use()`), and fast routing.
- **`[http.server_tls, host: string, port: i32, cert: path, key: path]`**: Secured WSS/HTTPS variant bindings.
- **`[ws.connect, u: url] -> ws_handle`**: Starts WS/WSS stream bounds (`[send, str]`, `[recv] -> str`, `[close]`).
- **`[ws.server, host: string, port: i32] -> ws_server_handle`**: Stateful callback listening sockets (`.on_connect`, `.on_message`).

### 4.9a Identity (`base64`, `ldap`, `saml`) — spec/67
- **`[base64.encode, x: string|buffer<u8>] -> string`**, **`[base64.url_encode, x]`**: RFC 4648 standard / URL-safe (no padding).
- **`[base64.decode, s] -> buffer<u8>`**, **`[base64.url_decode, s]`**: binary-safe; empty buffer on invalid input. **`[base64.decode_str, s, url=0] -> string`**: null on invalid input.
- **`[ldap.open, url] -> ldap_handle`**; methods `set_timeout(ms)`, `start_tls() -> i32`, `bind(dn, pw) -> i32` (1 ok / 0 bad credentials — an empty password is always rejected / -1 error), `search_dns(base, filter) -> list<string>`, `attr_values(dn, attr) -> list<string>`, `error() -> string`, `close()` (explicit; not dropped at scope exit).
- **`[ldap.escape_filter, s]`** (RFC 4515), **`[ldap.escape_dn, s]`** (RFC 4514): mandatory for user input in filters/DNs.
- **`[saml.authn_request_url, idp_sso_url, sp_entity_id, acs_url, request_id, relay_state=""] -> string`**: HTTP-Redirect AuthnRequest (unsigned). **`[saml.new_request_id] -> string`**.
- **`[saml.verify_response, b64, idp_cert_pem, sp_entity_id, acs_url, expected_request_id] -> map<string:string>`**: keys `ok` ("1"/"0"), `error`, `name_id`, `name_id_format`, `issuer`, `session_index`, `request_id`, `not_on_or_after`, `signed`, `attr.<Name>` (multi-values joined by "\n"). Signature with the configured cert only, wrapping/comment/DTD/SHA-1 rejected, audience/destination/time/InResponseTo checked. **`[saml.set_clock_skew, seconds]`** (default 120).

### 4.10 Game Engine (`engine`)
Abstracts GLFW/Emscripten/Android into an unified cross-platform window loop automatically configuring the GL-Context. Replaces `extern: "C" glfw...`.
- **`[engine.create_window, w: i32, h: i32, title: string] -> window_handle`**: Root context initializer handling setup.
- **`[engine.run, win: window_handle, update: fn, render: fn]`**: Invokes the platform-correct game loop (blocking `while`-loop on Desktop, `Asyncify` pipeline on the Web).
- **`[engine.swap, win: window_handle]`**: Yields contexts and swaps framebuffers.
- **`[engine.dt] -> f32`**, **`[engine.fps] -> f32`**, **`[engine.target_fps, fps: i32]`**: Monolithic timing limiters.

### 4.11 Multiplayer (`mirror`)
Automatically synchronizes object states (`sync:` lists) and routes remote method calls (`rpc:`) transparently.
- **`[mirror.listen, u: string] -> mirror_session`**: Configures multi-client server (supports `tcp://0.0...` or `ws://0.0...`).
- **`[mirror.connect, u: string] -> mirror_session`**: Authenticates client peers into a matching host.
- **`[session.register, obj: any] -> i32`**: Shares a model across the network structure. The caller immediately becomes the Owner.
- **`[session.update]`**: Diffs tracked model memory segments and drops dirty delta sequences into peer streams.
- **`[session.poll_event] -> mirror_event`**: Loops through connection queues checking client activity.
- **`[session.poll_rpc] -> rpc_event`**: Safely consumes pending remote code invocations originating from authenticated peers.

#### Built-in Mirror Models
The mirror subsystem exposes two native core models for parsing interaction queues:
```yaml
- model:
    name: mirror_event
    fields:
      - [type, string]      # E.g. "peer_connected", "object_registered", "" (sentinel)
      - [peer, mirror_peer] # Connected peer handle
      - [net_id, i32]       # Affected synchronized object ID
```
```yaml
- model:
    name: rpc_event
    fields:
      - [method, string]    # Target method name or "" (sentinel)
      - [net_id, i32]       # Target synchronized object ID
      - [arg, i32]          # Legacy integer argument/discriminator
      - [arg_kind, i32]     # 0 = integer only, 1 = serialized model payload
      - [arg_blob, ptr]     # Blob pointer for `decode_rpc`
      - [arg_blob_len, i32] # Length of the blob
```

---

## 5. Language Syntax & Structures

AIML utilizes YAML for Abstract Syntax Tree representations. All statements are nodes inside a list, reducing parsing ambiguity.

### 5.1 Variables (`decl`, `set`)
- **`decl`**: Declares a variable in the current scope.
  ```yaml
  - decl: [count, i32]           # zero-initialized
  - decl: [name, string, "Bob"]  # with initial value
  # Heap-allocation using `new` (auto-dereferenced)
  - decl: [obj, ptr<Point>, { new: Point }]
  ```
- Modifiers like `const`, `volatile`, `extern` belong only in the **4th** `decl` slot: `decl: [x, i32, 0, { const: true }]`.
  A map in the 3rd slot is the initializer, not the modifier bag.
- **`set`**: Assigns/mutates an existing variable or member path. Type inference applies to literals natively.
  ```yaml
  - set: [count, 10]
  - set: [obj.x, 20]
  - set: [count, { +: [count, 1] }]  # Inline expression calculation
  ```

### 5.2 Control Flow (`if`, `while`)
Control structures execute block lists dynamically based on strictly boolean conditions.
A `bool` value (variable, member, call result) is a condition by itself, e.g.
`condition: ready`; comparisons and `not`/`&&`/`||` yield `bool`.
- **`if`**: Branching execution. `else` is optional. The `true` placeholder validates standard YAML map constraints in shorthand forms.
  ```yaml
  - if: [{ lt: [x, 10] }, true]
    then:
      - print: ["x is small"]
    else:
      - print: ["x is large"]
  ```

- **`while`**: Loops evaluating conditions before each iteration.
  ```yaml
  - while: [{ lt: [x, 10] }]
    body:
      - set: [x, { +: [x, 1] }]
  ```

- **Walking a collection**: there is no `for:` — the compiler rejects that key
  (`spec/06`). Index with `while`, or take an `iterator<T>` (`spec/54`).
  ```yaml
  - decl: [i, i32, 0]
  - while:
      condition: { lt: [i, my_list.len] }
      body:
        - decl: [item, string, { at: [my_list, i] }]
        - call: [log.info, item]
        - set:  [i, { +: [i, 1] }]
  ```

- **`try` / `catch`**: Structured exception handling exists and is used in tests.
  ```yaml
  - try:
      body:
        - throw: { new: MyException, message: "boom" }
      catch:
        - type: MyException
          var: e
          body:
            - print: ["caught ${e.message}"]
  ```

### 5.3 Functions (`def`, `call`)
- **`def`**: Defines local logic nodes. Optional `return:` defaults to `void`. Returns mandate explicit `return:` blocks.
  ```yaml
  - def:
      name: add
      args: [[a, i32], [b, i32, 0]]  # 'b' defaults to 0
      return: i32
      body:
        - return: [{ +: [a, b] }]
  ```
- **`call`**: Unrolls function invocation either as a statement or an expression.
  ```yaml
  - call: [my_function, arg1, arg2]
  - set:  [res, { call: [add, 5, 2] }]
  ```

- **Function pointers (`func`)**: AIML also supports callable references for callback-style APIs.
  This is a real language feature, but it appears less frequently in everyday AIML code than normal
  `def`/`call` usage.
  ```yaml
  - decl: [cb, func, my_handler]
  - if:
      condition: { neq: [cb, null] }
      then:
        - call: [cb]
  ```

### 5.4 Object-Oriented (`class`, `model`, `interface`)
- **`class`**: Stateful definition. Implements encapsulation (`access: public|private`) and behaviour (`methods`). Stack/Heap allocated.
  ```yaml
  - class:
      name: Player
      extends: Entity         # Single Inheritance
      implements: [Drawable]  # Interface Enforcement
      members:
        - [health, i32, 0, { access: public }]
      methods:
        - def:
            name: take_damage
            args: [[dmg, i32]]
            body:
              - set: [<.health, { -: [<.health, dmg] }] # '<' is 'this' ptr
  ```

- **`model`**: Behaviorless purely-data struct schema targeting rapid JSON/YAML automatic structural serialization (`sys.serialize` / `sys.deserialize`). Fails compilation if methods are attached. Can also auto-synchronize via multiplayer namespaces (`sync:` bounds).
  ```yaml
  - model:
      name: Config
      inherits: BaseConfig
      fields:
        - [host, string]
        - [port, i32, 8080] # default 8080
  ```
  `inherits` is used for models in current tests; `extends` is the class inheritance keyword.

- **`interface`**: Virtual contract specifying signatures classes must manifest, powering trait-boundaries on pointer dereferences.
  ```yaml
  - interface:
      name: Drawable
      methods:
        - [draw, [], void]   # [method_name, [arg_signatures], return_type]
  ```

### 5.5 Modules (`namespace`, `import`)
- **`namespace`**: Sets the absolute namespace prefix for the file avoiding global symbol collisions. Must pre-date all declarative statements.
  ```yaml
  - namespace: core.math
  ```

- **`import`**: Resolves dependencies transitively relative to the current file. Absolute paths are forbidden. Imported functions/classes must be addressed by their full Fully Qualified Name (FQN) post-import. Module-level globally scoped variables (`decl:` outside defs) persist once.
  ```yaml
  - import: "../net/socket.aiml"
  ```

### 5.6 Additional Syntax Forms
- **`enum`**: Named integer constants.
  ```yaml
  - enum:
      name: Color
      values:
        - Red
        - Green
        - Blue: 10
  ```
- **Field/index chains**: Member paths can mix dots and indexed segments, e.g. `obj.items-0.hp`, `player.hand-idx.cost`.
- **Method chains on fields**: Chained access such as `obj.deep.map.get("x")` is part of current practical AIML usage.

---

## 6. Built-in Exceptions

The current AIML spec/test corpus documents several runtime exception types that are raised by core language features or built-in libraries.

| Exception | Typical Trigger |
| :--- | :--- |
| `DivisionByZeroException` | Integer division by zero. |
| `NullPointerException` | Dereferencing a null pointer or calling through a null function pointer. |
| `IndexOutOfBoundsException` | Invalid list/array index access or removal. |
| `BufferOverflowException` | Writing past buffer capacity. |
| `BufferUnderflowException` | Reading past written buffer data. |
| `InvalidThreadStateException` | Illegal thread lifecycle operation such as invalid/double join state. |
| `TypeMismatchException` | Invalid runtime type comparison or incompatible boxed/dynamic type use. |

These exceptions are documented across the main spec, for example in collections, pointers, buffers, arithmetic, and concurrency chapters. User-defined exceptions can also be thrown and caught with `throw`, `try`, and `catch`.
