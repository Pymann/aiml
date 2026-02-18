# Custom Database Plan: "AionDB" (Internal)

## Overview
A high-performance, embedded database implemented entirely in AIML, tailored for AI, Gaming, and Versioning. 
It avoids external dependencies (SQLite) and focuses on direct memory control, serialization, and native versioning support.

## Core Architecture

### 1. Storage Engine (Paged Memory)
Data is stored in fixed-size `Pages` (Buffers) to ensure cache locality and simplified serialization.

```aiml
- config:
   page_size: 4096 # 4KB

- class:
    name: PageHeader
    packed: true # No padding
    members:
      - [id, i32]
      - [count, i32]     # Number of items
      - [free_mask, u64] # Bitmask for valid slots (assuming max 64 items per page)

- class:
    name: Page<T>
    members:
      - [header, PageHeader]
      - [data, buffer] # allocated to page_size
```

### 2. Versioning ("Time Travel")
Every record implicitly supports versioning headers to allow querying the state of the DB at any logical timestamp (tick/frame).

```aiml
- class:
    name: RowHeader
    packed: true
    members:
      - [created_at, i64] # Global tick/timestamp
      - [deleted_at, i64] # If < 0, currently active. If > 0, deleted at this time.
```

### 3. Record Structure
User-defined tables are simply standard AIML classes that inherit or include the `RowHeader`.

```aiml
- class:
    name: Entity
    members:
      - [header, RowHeader]
      - [pos_x, f32]
      - [pos_y, f32]
      - [ai_state, i32]
```

## Features

### AI & Gaming Optimized
- **Zero-Copy Reads**: Cast buffer offsets directly to pointers `ptr<Entity>` using AIML's pointer arithmetic. This allows accessing millions of entities per frame without deserialization overhead.
- **ECS-Ready**: Data can be arranged in "Archetypes" (Component Columns) for cache efficiency.

### Query Engine
Instead of SQL text parsing, we use a fluent API constructing a query plan (Code-as-Data).

```aiml
# Find all Entities created after time 100
- decl: [query, Query]
- call: [query.from, "Entity"]
- call: [query.where, "header.created_at", ">", 100]
- decl: [results, list<ptr<Entity>>]
- set: [results, { call: [query.execute] }]
```

## Implementation Steps

1. **Memory Manager**: Implement `PageAllocator` using `buffer` and `ffi` (mmap) if needed, or simple heap allocation.
2. **Serializer**: Extend `serialize.aiml` concepts to write/read efficiently from specific buffer offsets.
3. **Index**: Implement a simple B-Tree or Hash Index in AIML (`map<i32:ptr>`) to lookup IDs.
4. **Transaction Log (WAL)**: For durability, append all operations to a sequential log buffer before applying to Pages.

## Usage Example

```aiml
- import: system.db.core

- def:
    name: game_loop
    args: []
    return: void
    body:
       - call: [db.begin_frame, { call: [time.now] }]
       
       # Create new version of entity
       - decl: [e, Entity]
       - set: [e.header.created_at, { call: [db.current_tick] }]
       - set: [e.pos_x, 10.0]
       
       # Persist (Zero-copy write to active page)
       - call: [db.insert, e]
       
       # Query
       - decl: [active_entities, list<ptr<Entity>>]
       - set: [active_entities, { call: [db.query_active_at, { call: [db.current_tick] }] }]
```
