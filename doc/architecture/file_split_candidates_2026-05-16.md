# AIML — File Split Candidates (2026-05-16)

## Goal

This note collects the currently conspicuous large source files in
`aiml/src/` and assesses which of them should sensibly be split into smaller
modules.

The focus is not on vendor files, but on AIML's own
files that have become too broad in terms of domain or structure.

## Short conclusion

The clearest split candidates currently are:

1. `src/sys/tensor_runtime.cpp`
2. `src/sys/ui_runtime.cpp`
3. `src/sys/db_runtime.cpp`
4. `src/sys/prefab_runtime.cpp`

An additional, but somewhat different candidate is:

5. `src/core/builtin_registry.cpp`

## Measured sizes

Relevant large non-vendor files:

- `src/core/builtin_registry.cpp` — `9069` lines
- `src/sys/tensor_runtime.cpp` — `2591` lines
- `src/sys/ui_runtime.cpp` — `2249` lines
- `src/sys/db_runtime.cpp` — `2166` lines
- `src/sys/prefab_runtime.cpp` — `1602` lines
- `src/core/ast_compiler.cpp` — `1560` lines
- `src/sys/http_runtime.cpp` — `1437` lines
- `src/sys/mirror_runtime.cpp` — `1221` lines
- `src/sys/input_runtime.cpp` — `1180` lines

Not counted as an architecture problem:

- `src/vendor/miniaudio.h`
- `src/vendor/cgltf.h`
- `src/core/yyjson.cpp`
- `src/core/yyjson.h`

Vendor or embedded third-party files are large by nature and
are not considered a split target here.

## 1. `src/sys/tensor_runtime.cpp`

### Why conspicuous

This file is not only large, but mixes several clearly separable
responsibilities:

- arena and tensor allocation
- basic tensor operations
- neural/math ops
- autograd / backward paths
- persistence / save / load / hashing
- KV-related helper logic

### Visible dividing lines

Early basic functions:

- arena / allocation
- tensor object creation
- shape and data access

Mathematical ops:

- `add`, `sub`, `mul`, `matmul`, `softmax`, `layer_norm`, ...

Autograd block:

- `backward_add`, `backward_matmul`, `backward_softmax`, ...

Persistence/helper block:

- `aiml_tensor_save`
- `aiml_tensor_load`
- `aiml_tensor_sha256`

### Recommendation

Split at least into:

- `tensor_arena.cpp`
- `tensor_ops.cpp`
- `tensor_autograd.cpp`
- `tensor_io.cpp`

That is currently the strongest split candidate.

## 2. `src/sys/ui_runtime.cpp`

### Why conspicuous

The file currently combines:

- GL/shader setup
- font baking
- grid and cell layout
- concrete widgets
- a complete style API with very many setters

So it is not only large, but also hard to navigate.

### Visible dividing lines

Close to render/setup:

- shader compilation
- font baking
- initialisation

Layout / Grid:

- `aiml_ui_begin_grid`
- `aiml_ui_begin_cell`
- `aiml_ui_set_gap`

Widget implementations:

- `aiml_ui_button`
- `aiml_ui_checkbox`
- `aiml_ui_slider`
- `aiml_ui_text_input`

Style API:

- `aiml_ui_button_style_set_*`
- `aiml_ui_slider_style_set_*`
- `aiml_ui_progress_style_set_*`

### Recommendation

Split at least into:

- `ui_render.cpp`
- `ui_layout.cpp`
- `ui_widgets.cpp`
- `ui_style.cpp`

## 3. `src/sys/db_runtime.cpp`

### Why conspicuous

The file contains several subsystems that are logically separate:

- typed store / column store
- mmap / grow / replay
- kv-store
- WAL logic
- decoded cache / LRU
- vector store

### Visible dividing lines

Store/column logic:

- `store_col_insert`
- `store_col_get`
- `store_col_delete`

KV logic:

- `kv_mmap_attach`
- `kv_wal_write_set`
- `kv_wal_replay`
- `kv_decoded_*`

Vector store logic:

- `vec_wal_write_insert`
- `vec_wal_replay`

### Recommendation

Split at least into:

- `db_store_runtime.cpp`
- `db_kv_runtime.cpp`
- `db_vec_runtime.cpp`
- optional `db_wal_runtime.cpp`

## 4. `src/sys/prefab_runtime.cpp`

### Why conspicuous

The file currently mixes:

- render path / shader / VAO / texture cache
- text wrapping and font handling
- JSON parsing
- prefab instantiation
- child lookup
- hit test / hover / focus / drag
- docking

Those are several natural modules.

### Visible dividing lines

Render and GPU part:

- `compile_shader_src`
- `create_prefab_shader`
- `flush_batch`
- `render_instance`

JSON/definition part:

- `parse_json_string`
- `parse_json_number`
- `parse_children_json`

Interaction/UI part:

- `aiml_prefab_hit_test`
- `aiml_prefab_update_hover`
- `aiml_prefab_update_drag`
- `aiml_prefab_child_clicked`

### Recommendation

Split at least into:

- `prefab_render.cpp`
- `prefab_parse.cpp`
- `prefab_instance.cpp`
- `prefab_interaction.cpp`

## 5. `src/core/builtin_registry.cpp`

### Why conspicuous

At `9069` lines this is the largest own non-vendor file.
However, it is structurally different from the runtime monoliths:

- at its core it is one large registry file
- the central entry point is `registerBuiltins(CompilerContext&)`

### Assessment

This is rather a case of:

- very large
- but possibly kept central on purpose

If it is maintained manually, a split by domain is still
worthwhile.

### Recommendation

If it is touched, rather split it like this:

- `builtin_registry_core.cpp`
- `builtin_registry_io.cpp`
- `builtin_registry_net.cpp`
- `builtin_registry_ui.cpp`
- `builtin_registry_tensor.cpp`
- `builtin_registry_db.cpp`

Then `registerBuiltins(...)` remains as the aggregator.

## Files that currently still look okay

These files are not small, but do not yet look as heavily
overloaded:

- `src/sys/http_runtime.cpp`
- `src/sys/mirror_runtime.cpp`
- `src/sys/input_runtime.cpp`
- `src/sys/gl_runtime.cpp`

They can still become candidates later, but are currently not
the most urgent.

## Prioritised order

If the split is to happen in stages, the sensible order would be:

1. `tensor_runtime.cpp`
2. `ui_runtime.cpp`
3. `db_runtime.cpp`
4. `prefab_runtime.cpp`
5. `builtin_registry.cpp`

## Preliminary conclusion

The current main construction site is not simply "large files" in
general, but above all the renewed growing together of several
domain-wise separate subsystems into single runtime files.

This is particularly clear in:

- Tensor
- UI
- DB
- Prefab

There a split is worthwhile not only for the file size, but above
all for:

- navigability
- testability
- build comprehensibility
- future backend extensions
