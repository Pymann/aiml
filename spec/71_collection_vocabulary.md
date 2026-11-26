# 71 — Collection vocabulary

Status: normative.  This chapter fixes the names of every operation on a collection or
collection-like built-in.  Where another chapter names an operation differently, this chapter
wins and the other chapter is wrong.

The rule behind it: **one name has exactly one meaning and exactly one spelling, across the whole
language.**  A reader — a person or a model — never has to work out from the receiver's type
which of two meanings a name has, and never has to choose between two spellings of the same
thing.  There are no aliases.  Every name this chapter retires is a compile error that names its
replacement (§8).

`cascade<T>` is outside this chapter: it keeps the `list<i32>` surface it has today (spec/57).

## 1. The matrix

Rows are operations, columns are types.  `·` = the type does not have the operation.

### 1.1 Queries — properties only

| Operation | Name | `list<T>` | `buffer<T>` | `buffer` | `array<T:N>` | `dyn_array<T>` | `string` | `map` | `hashmap` | `Kv` | `Store` | `VecStore` | `TimeSeries` | `tensor` | `Code` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| element count | `.len` | ✓ | ✓ | ✓ (bytes = elements) | ✓ | ✓ | ✓ (bytes) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| no elements | `.empty` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| bytes of the elements | `.byte_len` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ (= `.len`) | · | · | · | · | · | · | · | · |
| maximum element count | `.capacity` | ✓ settable | ✓ read-only | ✓ read-only | · | · | · | ✓ settable | ✓ settable | ✓ settable | · | · | · | · | · |

`sys.size(x)` is not a collection property: it is the size of the **type** of `x` in bytes, the
way C's `sizeof` is (spec/23).

A `chan<T>` has no `.capacity`: its buffer size from `init` (spec/26, spec/28) is a different
quantity — `0` there means *rendezvous*, not *unbounded* — and keeps its own name.  For a `list` it is the size of the list header, whatever the list
holds.

### 1.2 Keyed containers — `map<K:V>`, `hashmap<K:V>`, `Kv<K:V>`, `storage`

| Operation | Name | Result |
|---|---|---|
| write | `set(k, v)` | — |
| read | `get(k)` / `get(k, d)` | the value; if `k` is missing, `d` or the zero value (§3) |
| key present | `has(k)` | `i32` 0/1 |
| remove | `delete(k)` | `i32` 1 if `k` was there, else 0 |
| all keys | `keys()` | `list<K>`, sorted (strings in byte order); `map` only with `string` keys; not on `storage` |
| remove all | `clear()` | — |
| counter | `incr(k, d)` | `hashmap<K:integer>` only: `v += d`, a missing key counts as 0; returns the new value |

`storage` is a namespace, so its operations are functions: `storage.set(k, v)`,
`storage.get(k)` / `storage.get(k, d)`, `storage.has(k)`, `storage.delete(k)`, `storage.clear()`;
the session store has `storage.session_get(k)`, `storage.session_set(k, v)` and
`storage.session_delete(k)` (spec/23_2).  `storage` keys and values are `string`.

### 1.3 Sequences — `list<T>`, `buffer<T>`, `array<T:N>`, `dyn_array<T>`

| Operation | Name | `list<T>` | `buffer<T>` | `array<T:N>` | `dyn_array<T>` |
|---|---|---|---|---|---|
| read at position `i` | `{ at: [s, i] }` (expression) | ✓ | ✓ | ✓ | ✓ |
| write at position `i` | `idx: [s, i, v]` (statement) | ✓ | ✓ | ✓ | ✓ |
| append one element | `push(v)` | ✓ | ✓ | · | · |
| take the last element | `pop()` | ✓ | ✓ | · | · |
| insert at position `i` | `insert(i, v)` | ✓ | · | · | · |
| remove at position `i` | `remove(i)` | ✓ | · | · | · |
| append a whole sequence | `extend(s)` | ✓ | ✓ | · | · |
| value present | `contains(v)` | ✓ | · | · | · |
| position of a value | `index_of(v)` | ✓ | · | · | · |
| occurrences of a value | `count_of(v)` | ✓ | · | · | · |
| remove every occurrence of a value | `remove_value(v)` | ✓ | · | · | · |
| remove all | `clear()` | ✓ | ✓ | · | · |
| release early | `free()` | ✓ | · | · | ✓ |
| `sort()`, `sort_desc()`, `reverse()`, `shuffle()`, `slice(a, b)`, `join(sep)` | as spec/10 | ✓ | · | · | · |

`string` has `contains(s)` and `index_of(s)` with a substring as argument (spec/09).

### 1.4 Records — `Store<T>`, `VecStore`

| Operation | `Store<T>` | `VecStore` |
|---|---|---|
| write | `add(r)` → the new row's id | `set(k, vec)` |
| read | `get(id)` → `ref<T>`, `null` if missing | `search(q, k)` (spec/31) |
| remove | `delete(id)` → 0/1 | `delete(k)` → 0/1 |

### 1.5 Streams and resources

| Type | Operations |
|---|---|
| `chan<T>` | `send(v)`, `send_unique(v)`, `recv()`; `init` as spec/26 |
| `iterator<T>` | `next()` |
| `TimeSeries<T>` | `append(v)`, `at_time(t)` |
| `gl_buffer`, `gl_texture`, `gl_cubemap`, `gl_vao`, `gl_timer_query` | `free()` releases the GPU object early (spec/43_2 §5.5) |
| `yaml` documents | `yaml.len(node)`, `yaml.get(node, key)` (spec/60) |
| `tensor` | elements: `{ at: [t, i] }`, `idx: [t, i, v]` (flat index); `.len` = total element count; shape operations as spec/50 |

## 2. Properties, not methods

A query (§1.1) is a property: `l.len`, never `{ call: [l.len] }`.  Calling a query as a method is
a compile error (§8).  `.capacity` is set like a field: `set: [l.capacity, 100]`.

## 3. Missing keys: `get` never throws

`get(k)` on a key that is not there returns:

- `d`, if the call passes a default: `get(k, d)`; `d` has type `V`.
- otherwise the **zero value** of `V`: `0` for integers and enums, `0.0` for floats, `0` (false)
  for `bool`, and the **null pointer** for every value held through a pointer or handle —
  `string`, containers, `ptr<T>`, `shared<T>`, models.

`get` never raises an exception and never reads out of bounds.  To tell "missing" from "stored
the zero value", ask `has(k)` (or pass a default that cannot be stored).

What `get` hands back otherwise — a copy, an owning handle or a `ref<T>` borrow — is spec/61 §5.4.

## 4. Positions: out of range throws

`{ at: [s, i] }` and `idx: [s, i, v]` on any sequence of §1.3, and `insert`/`remove` on a `list`,
raise `IndexOutOfBoundsException` when `i` is outside `[0, len)` (`insert`: `[0, len]`).  The
message is `index <i> of <len>`.  There is no unchecked access; a sequence that needs raw speed
hands its data pointer to native code.

`pop()` on an empty sequence raises `IndexOutOfBoundsException` too.

## 5. Capacity

`.capacity` is the most elements a collection takes.  `0` means **unbounded**, and every
collection starts unbounded.

| Rule | |
|---|---|
| F71-1 | `set: [c.capacity, n]` with `n ≥ 0` sets the maximum on a `list`, `map`, `hashmap` or `Kv`. `n = 0` removes it. |
| F71-2 | An operation that would make `len` exceed a non-zero capacity raises `CapacityExceededException` and changes nothing: `push`, `insert`, `extend` (checked for the whole sequence before the first element), and `set(k, v)` with a **new** key.  Overwriting an existing key does not add an element and is always allowed. |
| F71-3 | Setting a capacity below the current `len` raises `CapacityExceededException` and keeps the old capacity. |
| F71-4 | `.capacity` of a `buffer` / `buffer<T>` is what `alloc` reserved, in elements (bytes for `buffer`, `bytes / sizeof(T)` for `buffer<T>`); it is read-only. |
| F71-5 | The capacity of a `Kv` belongs to the open handle and is not persisted: a reopened `Kv` starts unbounded. |
| F71-6 | Copying a collection (`clone`, a value copy of a `map`) copies its capacity. |

## 6. `dyn_array<T>` knows its length

`new_array: [T, n]` stores `n` with the array, so `.len`, `.empty` and `.byte_len` work and §4 can
check bounds.  The variable still holds the pointer to the first element, so passing `arr` to
native code passes the data as before.

## 7. Bytes and elements

`.len` always counts **elements**.  On a `buffer<f32>` with 4 floats, `.len` is 4 and `.byte_len`
is 16.  On an untyped `buffer` an element is a byte, so `.len` = `.byte_len`.  The same holds for a
`string`: its elements are the bytes of its UTF-8 encoding, because every string position
(`substr`, `index_of`) is a byte offset (spec/09).  `"grün"` has `.len` 5.  `alloc(n)` reserves
`n` **bytes** (spec/11).

## 8. Retired names

Each name in this table is a compile error whose message names the replacement.

| Retired | On | Replacement |
|---|---|---|
| `len()`, `empty()`, `byte_len()`, `capacity()` as calls | everything | the property (§2) |
| `size`, `length`, `size()` as an element count | `list`, `hashmap`, `buffer`, `tensor`, `Code` | `.len` |
| `count` | `list`, `buffer<T>` | `.len` for the element count, `count_of(v)` for occurrences |
| `get(i)` | `list`, `buffer<T>`, `tensor` | `{ at: [s, i] }` |
| `set(i, v)`, `put(i, v)`, `replace(i, v)` | `list`, `tensor` | `idx: [s, i, v]` |
| `put(k, v)` | `map`, `hashmap` | `set(k, v)` |
| `contains(k)` | `map`, `hashmap` | `has(k)` |
| `remove(k)` | `map`, `hashmap`, `storage` | `delete(k)` |
| `add(k, d)` | `hashmap` | `incr(k, d)` |
| `append(other)` | `buffer` | `extend(other)` |
| `insert(r)` | `Store` | `add(r)` |
| `insert(k, vec)` | `VecStore` | `set(k, vec)` |
| `at(t)` | `TimeSeries` | `at_time(t)` |
| `delete()` | GPU objects | `free()` |
| `yaml.size(node)` | `yaml` | `yaml.len(node)` |

## 9. Tests

`tests/test_collection_vocabulary.aiml` exercises every ✓ of §1; the driver
`tests/collection_retired/run.sh` compiles one snippet per row of §8 and checks that the error
names the replacement;
`tests/test_collection_capacity.aiml` covers §5, `tests/test_collection_get_default.aiml` §3,
`tests/test_collection_bounds.aiml` §4 and §6.
