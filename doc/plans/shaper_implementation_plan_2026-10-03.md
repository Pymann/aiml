# Shaper — requirements and implementation plan

As of: 2026-10-03. Planning state, no shaper implementation yet.

**Historical starting plan:** shaper and glbview have since been implemented.
The original gaps below are not a current list of remaining work.
See the [status comparison of 2026-10-04](shaper_open_topics_2026-10-04.md).

## 1. Goal and agreed scope

Shaper becomes a standalone AIML program. It reads a declarative
YAML model description and produces a GLB model including UV coordinates and
optionally a UV template for painting. Exodus can run the build;
shaper itself is not a Python/Exodus tool.

The input describes bodies, open surfaces and points, transformations,
materials, textures and nested geometry operations.

- `union`: implement first. Boolean union of closed bodies.
- `hull`: add after that. Form the convex hull of the input geometry;
  indentations and gaps can be bridged in the process.
- Open surfaces are valid without extrusion, for example textured
  leaf surfaces of a tree. Outside the bodies they are kept.
  Partly interior surfaces are cut; completely interior
  parts are dropped. Open surfaces do not remove volume from each other.
- Pure points do not determine an unambiguous non-convex surface. They need
  an explicit surface assignment or `hull`. Point cloud rendering is no
  substitute for this definition.
- All generated surfaces get UVs. Existing UVs can be kept;
  alternatively the result is unwrapped anew.
- Materials support in particular transparent leaf textures and
  double-sided display. Transparency in the image does not change the geometry.
- Difference and intersection are possible later extensions, not part
  of the first implementation.

## 2. YAML contract

A versioned format with `version`, `units`, `up_axis`, `materials`, `model`,
`uv` and `output`. The model tree consists of shapes and operations with children.
Names and syntax are a draft and are fixed before implementation.

```yaml
version: 1
units: meter
up_axis: y

materials:
  leaves:
    texture: textures/leaves.png
    alpha_mode: mask
    alpha_cutoff: 0.5
    double_sided: true

model:
  operation: union
  children:
    - type: box
      size: [0.3, 3, 0.3]
      position: [0, 1.5, 0]
    - type: surface
      points: [[-1, 2, 0], [1, 2, 0], [1, 3, 0], [-1, 3, 0]]
      uv: [[0, 0], [1, 0], [1, 1], [0, 1]]
      material: leaves

uv:
  mode: preserve_or_generate
  atlas_size: [2048, 2048]
  padding_px: 8

output:
  file: tree.glb
  uv_layout: tree_uv.png
```

To be defined: the origin of every shape (box: centre; pyramid: centre of the
base), handedness, angle unit, transformation order, outward orientation
of the surfaces and the texture coordinate convention. Resource paths are resolved relative to the
YAML file. Unknown keys, invalid indices, non-finite values
and invalid dimensions produce errors with the YAML path and source position.

For `union` additionally define deterministic rules for touching and coplanar
surfaces, including open surfaces exactly on a body boundary.
Explicit surfaces are at first simple planar polygons; holes and non-planar
inputs are only supported with a defined procedure, otherwise rejected.

## 3. AIML inventory

Present and checked against code as well as existing tests:

- YAML parsing and traversal including errors and source positions:
  `src/sys/yaml_runtime.cpp`, `tests/test_yaml_documents.aiml`.
- Models, containers, dynamic arrays and typed byte/number buffers.
- Vector/matrix/quaternion maths: `src/sys/math_runtime.h`.
- Binary reading: `src/sys/binary_io_runtime.cpp`, `tests/test_binary_io.aiml`.
- C FFI and buffer data pointers. A separate test in `/tmp` successfully wrote zero bytes and an `f32` value via
  `fwrite`; the bytes were checked.
- GLB/glTF import: `src/sys/gltf_runtime.cpp`, `tests/test_gltf.aiml`.
- OpenGL mesh creation present in the code; no interactive rendering test as part
  of this review. A `gl_mesh` does not replace an editable CPU mesh.

The seven functional tests `test_yaml_documents`, `test_binary_io`, `test_buffer`,
`test_math`, `test_dyn_array`, `test_ffi`, `test_gltf` ran successfully with the
existing compiler. That is neither a complete language check nor a
leak proof. Some specifications still mark functions that are already implemented
as a proposal; code, spec and tests must be looked at together.

## 4. What has to be added to AIML

### 4.1 General runtime/library functions

1. **Binary file output:** a public length-based interface such as
   `io.write_bytes(handle, buffer)` with error/short-write handling.
   `io.open(path, "wb")` exists; `io.write`/`io.write_file` are text-oriented.
   FFI is a tested interim route, not a missing language capability.
2. **Binary packing:** explicit little-endian writing of integers and
   floating-point numbers as well as padding and size checking as reusable helpers.
   `buffer.write` currently stores native values; do not derive platform-independent
   endianness from the documentation alone. For GLB pack fields individually,
   do not take over AIML structs including alignment/padding unchecked.
3. **PNG output:** encode an RGBA buffer to PNG when the UV template is
   produced as a PNG. Image decoding and WebP encoding already exist,
   a corresponding PNG encoder was not found. An SVG template can
   at first be output as text; it does not replace the planned PNG output.
4. **Spec 61 integration of every extension:** register and check ownership contracts, release routes,
   temporary return values and error paths in the compiler/runtime.
   Newly created buffers must be real managed class 3 handles.
   No raw heap block that merely happens to have the same payload layout.

According to the current findings, no new language syntax is required for this.
Shared library helpers can be implemented in AIML; native runtime
functions make sense where file or image codec interfaces require them.

### 4.2 New AIML modules for shaper

This functionality is missing, but does not necessarily belong in the compiler as a builtin:

| Module | Task |
|---|---|
| `schema` | validate YAML, defaults, transformations, resource paths |
| `mesh` | CPU mesh, face corners, topology, materials, validation |
| `primitives` | boxes, pyramids, further bodies and explicit surfaces |
| `geometry` | cuts, classification, triangulation, tolerance rules |
| `boolean` | union of bodies and trimming of open surfaces |
| `hull` | convex hull; diagnose degenerate point sets |
| `uv` | projection, UV preservation, unwrapping and atlas layout |
| `normals` | normals, hard edges, optionally tangents for normal maps |
| `gltf_export` | GLB container, JSON, geometry, materials and textures |
| `uv_layout` | visible UV template as SVG/PNG |

Compute geometry internally with dedicated `f64` value types; the existing `vec3`/`mat4`
use `f32`. Robust side/cut decisions additionally need a
defined numerics strategy. `f64` alone does not guarantee robust booleans.

## 5. Mesh, UV and export rules

- Separate geometric points from face corners. Every face corner can have its own
  UVs and normals, even if several corners reference the same point.
- Represent topology at first via indices/IDs in flat containers.
  Bodies and open surfaces stay distinguishable.
- Cuts keep their origin and material ID. New intersection points interpolate
  the existing face corner attributes, in particular UVs; recompute or normalise normals
  where needed.
- UV preservation: the existing texture mapping is kept; deliberately overlapping
  leaf UVs are allowed. Missing UVs are generated.
- Re-unwrapping: form islands and pack them with a defined texel density, resolution and
  spacing. New islands must not overlap in the unambiguous paint atlas.
- Transferring existing textures to a new layout requires baking. That
  is a separate later expansion stage; without baking, no claim that a
  re-unwrap automatically keeps the previous appearance.
- GLB export: positions, normals, UVs, indices, material groups, textures,
  `alphaMode`, `alphaCutoff`, `doubleSided`, scene and nodes. Split vertices at UV/
  normal seams appropriately on export.
- Check GLB sizes and alignment; no silent integer overflows. Images
  can at first be embedded as existing PNG/JPEG files without
  re-encoding them. The PNG encoder is not a precondition for that.
- Write the output to a temporary file first and take it over after success.
  Errors must not leave behind a seemingly finished, damaged target model.

## 6. Binding memory rules — SPEC 61

**Spec 61 is the central design and acceptance basis for shaper and all
AIML functions added for it.** It currently still carries the title DRAFT;
this plan claims neither its complete implementation nor an already
measured leak freedom. Deviations in the compiler state actually used
must be detected by probes and fixed before release.

Reference: [spec 61](../../spec/61_memory_management.md), in particular
§2.1, §5–5.7, §6.2, §7, §8, §9.1/9.3 and §10.

| Resource | Ownership contract for shaper |
|---|---|
| Numbers, points, triangle indices | Small value types; no heap owners of their own |
| `string`, `path` | Class 2; managed handles, no manual C `free` |
| `list`, `map`, `buffer` | Class 3; assignment shares memory via RC, no independent copy |
| `ptr<Mesh>` / other `ptr<T>` | One owner; passing/assignment as an ownership transfer, the source unusable afterwards |
| `ref<Mesh>` | Non-owning function access; must not outlive the owner |
| `shared<T>` | Only where shared ownership is necessary; avoid cycles |
| `dyn_array<T>` | Class 4, a single owner; move instead of alias, release at the owner drop |
| `yaml_doc` | Owns the parse tree; freed after the input has been taken over |
| `yaml_node` | Borrowed view; never store beyond the document lifetime |
| File/native codec handles | A unique owner with a defined close/destroy function |

Concrete obligations:

1. **One owner per result:** the parser returns its own model description;
   the evaluator returns its own mesh. Functions that only read or edit
   take `ref<Model>`/`ref<Mesh>`, not accidentally consuming `ptr` parameters.
   Container parameters follow §8: an existing RC handle, no `ref<list/map/buffer>`.
2. **No ownership cycles:** the mesh owns its data; edges, neighbours,
   parent references and material assignments use IDs/indices or time-bounded
   borrows. Unique `ptr` owners alone do not prevent cycles either.
3. **Handle aliasing explicitly:** `set` of a container does not create an
   independent mesh copy. For independent data use a checked `clone` or a targeted
   rebuild. Growth/removal can invalidate element borrows;
   do not keep references across such mutations.
4. **Bound scopes:** own and free cut lists, polygon fragments, UV working data and
   export buffers per operation/iteration where possible.
   No `persistent` as a workaround for a wrong lifetime.
5. **Free on all normal exits:** success, early `return`, loop abort
   and handled exception unwind must clean up correctly. An explicit early
   release must not lead to a second scope drop. Pass expected input and
   export errors back up to the CLI, do not end deep in the algorithm with
   `sys.exit` and thereby hide missing cleanup.
6. **Check temporary results:** nested producer calls,
   method chains and unused results must also be freed or taken over.
   On a throw during a later argument, temporary values already created
   must be cleaned up correctly.
7. **Document FFI contracts:** for every parameter/return value define borrowed,
   owned or consumed, including the matching allocator/deallocator.
   Use `buffer.ptr` only during the valid buffer lifetime and without reallocation.
   Native functions must not store borrowed pointers permanently.
8. **Errors during partial construction:** mesh parts, images and files already created
   must be freed even on a subsequent error. Secure native runtime code paths
   with RAII or completely checked cleanup paths.
9. **One thread at first:** no concurrent mutation of shared class 3 containers.
   Later parallelisation needs its own spec 61 §5.3 proof.

## 7. Implementation in checkable stages

### P0 — contracts and memory baseline

- Fix the YAML conventions and the CPU mesh model.
- Document ownership per data structure and public function.
- Record the actual compiler/runtime version and active lifetime settings;
  do not take old comments or feature gates over as a reliable status.
- Run relevant existing spec 61 probes. Especially YAML, container fields,
  nested containers, buffer, `dyn_array`, returns and call temporaries.
- Fix any reproduced AIML lifetime errors first.

### P1 — YAML to GLB without a boolean operation

- Add binary output and packing helpers; test leaks and write errors.
- Read/produce a box, a single open surface, explicit UVs and material data.
- Build a minimal GLB exporter; load the produced model back into AIML.
- Add a texture and a double-sided transparent leaf surface.
- Format validation and data comparison; re-import alone is not a complete
  GLB conformance check.

### P2 — boolean union and open surfaces

- First two overlapping boxes, then pyramids and further valid polyhedra.
- Develop triangulation, inside/outside tests and shared cut geometry.
- Keep exterior surfaces, remove interior fragments; keep material
  and UVs at new intersection points.
- Check disjoint, enclosed, identical, touching and coplanar cases;
  diagnose invalid or unsupported inputs.
- Valid closed bodies must stay closed; open leaf surfaces
  must not be wrongly treated as faulty bodies by the validation.

### P3 — UVs for all valid surfaces

- Analytic unwrapping of simple bodies and planar surface projection.
- Generic island formation and atlas layout for complex results.
- Generate normals and hard edges consistently.
- UV template first as SVG, then as PNG with an added encoder/rasteriser.
- Check UV preservation and re-unwrapping separately. Reject degenerate surfaces
  instead of exporting invalid UVs or NaN values.

### P4 — convex hull

- Integrate `hull` into the same model tree; support point sets and bodies.
- Diagnose duplicate, collinear and coplanar points as well as too few points.
- Give new hull surfaces UVs. Regulate the material assignment explicitly;
  newly created surfaces do not automatically have an unambiguous source texture.

### P5 — larger models and final acceptance

- Run several export jobs within one process, including
  repeated error cases, without growing unreleased job data.
- Measure large leaf sets, size limits, performance and peak usage.
- Bring together GLB format checking, re-import and visual checking.
- Plan baking of existing textures on re-unwrap as a separate follow-up stage.

## 8. Leak and safety acceptance per stage

**No stage counts as finished merely because of correct geometry or exit code 0.**

- Run native functional tests separately from memory checks.
- Valgrind Memcheck must cover both generated AIML code and the runtime;
  use the existing control probes and harness. Also check AOT as soon as
  the executable shaper build exists. Use ASan/LSan only with matching instrumentation
  as a supplement, not as an unproven replacement for the JIT check.
- No invalid reads/writes, use-after-free, double free or uninitialised
  accesses in the new paths.
- No new shaper-caused definite/indirect/possible leaks; resolve unclear reports.
  Assess still reachable memory blocks separately: after the end of a job
  no job-dependent growing population may remain. Compare runtime/JIT caches
  against an identical baseline and justify them, do not hide them across the board.
- Evaluate leak figures explicitly: `tests/run_memory_sanitizers.sh` uses
  `--errors-for-leak-kinds=none`; a green safety exit is not a leak proof.
- Repetition probes with different numbers of jobs in the same process;
  use native heap/RSS trends only as a supplement, not as the sole proof.
- Error probes: invalid YAML, missing texture, invalid mesh, abort after
  partial construction, unwritable output and a simulated short write.
- Cleanup probes: move return, alias rebind, early return, exception unwind,
  loop temporaries and an explicit early release followed by the scope end.
- Store memory reports, compiler state, run parameters and expected owners as
  acceptance artifacts. No release with an unexplained regression.

Relevant existing foundations:

- [Spec 61](../../spec/61_memory_management.md)
- [Memory sanitizer harness](../../tests/run_memory_sanitizers.sh)
- [Call temporary cleanup plan](spec61_call_temporary_cleanup_implementation_2026-10-02.md)
- `tests/lifetime/163_yaml_doc_scope_release.aiml`
- `tests/lifetime/141_buffer_handle_set_and_param.aiml`
- `tests/lifetime/21_dyn_array_class4.aiml`
- `tests/lifetime/call_temporaries/`

The existing plans serve as checking hints, not as a claim that their earlier
errors are still open in the current compiler. Reproduce before changes.
