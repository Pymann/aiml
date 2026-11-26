# Chapter 43.1 — `gltf` Namespace (3D Asset Loader)

> **Design principle:** glTF 2.0 is the standard interchange format for 3D assets in games.
> The `gltf` namespace loads `.glb`/`.gltf` files and provides structured access to
> meshes, materials, animations, and the scene hierarchy.
> Internally implemented via **cgltf** (single-header C library, MIT license).

---

## 1. Types

| Type | LLVM | Description |
|------|------|-------------|
| `gltf_scene` | `i8*` | Loaded scene — root container for everything |
| `gltf_node` | `i8*` | Node in the scene hierarchy (transform + children) |
| `gltf_mesh` | `i8*` | Mesh (contains one or more primitives) |
| `gltf_primitive` | `i8*` | Submesh: vertex buffer + index buffer + material |
| `gltf_material` | `i8*` | PBR material (color, metallic, roughness, textures) |
| `gltf_animation` | `i8*` | Animation clip (keyframes for nodes) |
| `gltf_skin` | `i8*` | Skeleton for skinning/skeletal animation |
| `gltf_texture` | `i8*` | Texture reference (URI or embedded buffer) |
| `gltf_image` | `i8*` | Raw image data (RGBA, width, height) |

---

## 2. Loading

```aiml
# Load .glb (binary, everything in one file)
- decl: [p, path, "/assets/character.glb"]
- decl: [scene, gltf_scene, { call: [gltf.load, p] }]

# Load .gltf (JSON + external files)
- decl: [p2, path, "/assets/level.gltf"]
- decl: [scene2, gltf_scene, { call: [gltf.load, p2] }]

# Error checking
- decl: [ok, i8, { call: [scene.valid] }]
- if: [{ eq: [ok, 0] }, true]
  then:
    - decl: [err, string, { call: [scene.error] }]
    - print: ["glTF load error: ${err}"]
```

---

## 3. Scene Hierarchy

```aiml
# All root nodes of the first scene
- decl: [nodes, list<gltf_node>, { call: [scene.nodes] }]
- decl: [i, i32, 0]
- while: [{ <: [i, nodes.len] }]
  body:
    - decl: [node, gltf_node]
    - set: [node, { at: [nodes, i] }]
    - decl: [name, string, { call: [node.name] }]
    - print: ["node: ${name}"]
    - set: [i, { +: [i, 1] }]

# All meshes directly
- decl: [meshes, list<gltf_mesh>, { call: [scene.meshes] }]
```

---

## 4. Mesh Access

```aiml
- decl: [mesh, gltf_mesh, { at: [meshes, 0] }]
- decl: [name, string, { call: [mesh.name] }]
- decl: [prim_count, i32, { call: [mesh.primitive_count] }]
- print: ["mesh '${name}' has ${prim_count} primitives"]

# Primitive (submesh)
- decl: [prim, gltf_primitive, { call: [mesh.primitive, 0] }]

# Vertex data as buffer<f32>
- decl: [positions, buffer<f32>, { call: [prim.positions] }]   # xyz per vertex
- decl: [normals,   buffer<f32>, { call: [prim.normals] }]     # xyz per vertex
- decl: [uvs,       buffer<f32>, { call: [prim.uvs, 0] }]      # uv per vertex (texcoord 0)
- decl: [tangents,  buffer<f32>, { call: [prim.tangents] }]    # xyzw per vertex

# Index buffer as buffer<u32>
- decl: [indices, buffer<u32>, { call: [prim.indices] }]

# Vertex count
- decl: [vc, i32, { call: [prim.vertex_count] }]
- decl: [ic, i32, { call: [prim.index_count] }]
- print: ["${vc} vertices, ${ic} indices"]
```

---

## 5. Material

Texture-info and material accessors for the studio renderer (`index`,
`normal_scale`, `occlusion_strength`, `emissive_strength`, `texture_coord`,
`texture_has_transform`, `gltf_texture.image_index`, scene `extensions_used`,
`extensions_required`, `extras`) are specified in [43.5 §3](43_5_stylized_rendering.md).

```aiml
- decl: [mat, gltf_material, { call: [prim.material] }]
- decl: [mat_name, string, { call: [mat.name] }]

# PBR base values
- decl: [base_r,    f32, { call: [mat.base_color_r] }]
- decl: [base_g,    f32, { call: [mat.base_color_g] }]
- decl: [base_b,    f32, { call: [mat.base_color_b] }]
- decl: [base_a,    f32, { call: [mat.base_color_a] }]
- decl: [metallic,  f32, { call: [mat.metallic] }]
- decl: [roughness, f32, { call: [mat.roughness] }]
- decl: [emissive_r, f32, { call: [mat.emissive_r] }]
- decl: [emissive_g, f32, { call: [mat.emissive_g] }]
- decl: [emissive_b, f32, { call: [mat.emissive_b] }]

# Textures (null if not present)
- decl: [albedo_tex,   gltf_texture, { call: [mat.base_color_texture] }]
- decl: [normal_tex,   gltf_texture, { call: [mat.normal_texture] }]
- decl: [metal_tex,    gltf_texture, { call: [mat.metallic_roughness_texture] }]
- decl: [emissive_tex, gltf_texture, { call: [mat.emissive_texture] }]
- decl: [occlusion_tex,gltf_texture, { call: [mat.occlusion_texture] }]

# Alpha mode: "OPAQUE", "MASK", "BLEND"
- decl: [alpha, string, { call: [mat.alpha_mode] }]
- decl: [alpha_cutoff, f32, { call: [mat.alpha_cutoff] }]
- decl: [double_sided, i8, { call: [mat.double_sided] }]

# Application data: the material's glTF `extras` as JSON text ("" when absent).
# JSON is YAML, so it can be read with yaml.parse (spec/60).
- decl: [extras, string, { call: [mat.extras] }]
```

All string getters of the glTF classes (`name`, `error`, `alpha_mode`, `wrap_s`, …, `extras`)
return an **owned** `string` (spec/61 §7): a copy that stays valid after `scene.free`.

---

## 6. Textures and Images

```aiml
- decl: [img, gltf_image, { call: [albedo_tex.image] }]

# Image data as RGBA buffer (decoded)
- decl: [pixels, buffer<u8>, { call: [img.pixels_rgba] }]
- decl: [w, i32, { call: [img.width] }]
- decl: [h, i32, { call: [img.height] }]
- print: ["texture: ${w}x${h}"]

# Sampler parameters
- decl: [wrap_s, string, { call: [albedo_tex.wrap_s] }]   # "REPEAT", "CLAMP_TO_EDGE", "MIRRORED_REPEAT"
- decl: [wrap_t, string, { call: [albedo_tex.wrap_t] }]
- decl: [min_filter, string, { call: [albedo_tex.min_filter] }]  # "LINEAR", "NEAREST", "LINEAR_MIPMAP_LINEAR" ...
- decl: [mag_filter, string, { call: [albedo_tex.mag_filter] }]
```

Wraps default to `"REPEAT"` (glTF default) when the texture has no sampler. A filter
that the file leaves undefined (no sampler, or no `minFilter`/`magFilter`) reads as
`""`: glTF leaves that choice to the viewer.

`tex.image()` returns an owned `gltf_image` handle (spec/61 Class 4): each call creates one, it is
released at owner drop (scope exit, unless moved by return / push / member store) and is valid only
while its scene is loaded.

---

## 7. Node Transform

```aiml
- decl: [node, gltf_node, { at: [nodes, 0] }]

# TRS components (local space)
- decl: [tx, f32, { call: [node.translation_x] }]
- decl: [ty, f32, { call: [node.translation_y] }]
- decl: [tz, f32, { call: [node.translation_z] }]

- decl: [rx, f32, { call: [node.rotation_x] }]   # Quaternion
- decl: [ry, f32, { call: [node.rotation_y] }]
- decl: [rz, f32, { call: [node.rotation_z] }]
- decl: [rw, f32, { call: [node.rotation_w] }]

- decl: [sx, f32, { call: [node.scale_x] }]
- decl: [sy, f32, { call: [node.scale_y] }]
- decl: [sz, f32, { call: [node.scale_z] }]

# World transform matrix (column-major f32[16], for direct upload to OpenGL/Vulkan)
- decl: [matrix, buffer<f32>, { call: [node.world_matrix] }]

# Local transform matrix (column-major f32[16]): the node's `matrix`, else T·R·S
- decl: [local, buffer<f32>, { call: [node.local_matrix] }]

# Children
- decl: [children, list<gltf_node>, { call: [node.children] }]
- decl: [mesh, gltf_mesh, { call: [node.mesh] }]    # null if no mesh
```

---

## 8. Animation

```aiml
# All animation clips
- decl: [anims, list<gltf_animation>, { call: [scene.animations] }]
- decl: [anim, gltf_animation, { at: [anims, 0] }]

- decl: [anim_name, string, { call: [anim.name] }]
- decl: [duration,  f32,    { call: [anim.duration] }]   # seconds
- print: ["animation '${anim_name}' ${duration}s"]

# Sample: node transform at a specific point in time (in seconds)
# Returns interpolated TRS values for the node
- decl: [tx, f32, { call: [anim.sample_translation_x, node, 0.5] }]
- decl: [ty, f32, { call: [anim.sample_translation_y, node, 0.5] }]
- decl: [tz, f32, { call: [anim.sample_translation_z, node, 0.5] }]
- decl: [rx, f32, { call: [anim.sample_rotation_x, node, 0.5] }]
- decl: [ry, f32, { call: [anim.sample_rotation_y, node, 0.5] }]
- decl: [rz, f32, { call: [anim.sample_rotation_z, node, 0.5] }]
- decl: [rw, f32, { call: [anim.sample_rotation_w, node, 0.5] }]

# The node's local matrix at time t (column-major f32[16]): animated TRS
# properties replaced by the clip, the others kept (glTF channel semantics)
- decl: [local_t, buffer<f32>, { call: [anim.sample_local_matrix, node, 0.5] }]

# Or: all transforms at once as a matrix buffer (for GPU upload)
# Returns list<buffer<f32>> — one buffer<f32>[16] per node
- decl: [pose, list<buffer<f32>>, { call: [anim.sample_pose, 0.5] }]
```

Sampling rules:

- `t` is clamped to the channel's key range (no wrap-around; a viewer loops by taking
  `t mod duration`).
- `LINEAR` interpolation: translation component-wise; rotation by spherical linear
  interpolation along the shorter arc, result normalised. `STEP` holds the earlier key.
- A property the clip does not animate for that node yields the node's own value
  (`translation_*` / `rotation_*` from the node; identity when the node has neither).
- A node with `matrix` is not animatable (glTF); `sample_local_matrix` returns its matrix.
- `sample_local_matrix` / `local_matrix` / `world_matrix` return a fresh owned buffer
  (Class 4, spec/61), released at scope end.

---

## 9. Skinning

```aiml
- decl: [skin, gltf_skin, { call: [node.skin] }]   # null if no skinning

- decl: [joint_count, i32, { call: [skin.joint_count] }]
- print: ["joints: ${joint_count}"]

# Inverse bind matrices as buffer<f32> (joint_count * 16 f32 values, for GPU)
- decl: [ibm, buffer<f32>, { call: [skin.inverse_bind_matrices] }]

# Joint nodes (bones)
- decl: [joints, list<gltf_node>, { call: [skin.joints] }]

# Joint indices and weights per vertex (for skinning shader)
- decl: [joint_indices, buffer<u16>, { call: [prim.joints, 0] }]   # 4 per vertex
- decl: [weights,       buffer<f32>, { call: [prim.weights, 0] }]  # 4 per vertex
```

---

## 10. Release

```aiml
# Explicit release when no longer needed
- call: [scene.free]
```

---

## 11. Namespace Reference

### `gltf` Namespace

| Call | Arguments | Return | Description |
|------|-----------|--------|-------------|
| `{ call: [gltf.load, p] }` | `path` | `gltf_scene` | Loads .glb or .gltf |
| `{ call: [gltf.model, stem] }` | `string` | `gltf_scene` | Asset registry: stem instead of path (spec/44_7) |

### `gltf_scene` Methods

| Method | Return | Description |
|--------|--------|-------------|
| `valid` | `i8` | 1 = successfully loaded |
| `error` | `string` | Error message when `valid == 0` |
| `nodes` | `list<gltf_node>` | Root nodes of the first scene |
| `node_count` | `i32` | Number of nodes in the file (all scenes) |
| `node_at(index)` | `gltf_node` | Node by glTF index, `null` when out of range |
| `image_count` | `i32` | Number of images |
| `image_at(index)` | `gltf_image` | Owned image handle by glTF index (like `tex.image()`, spec/61 Class 4), `null` when out of range |
| `meshes` | `list<gltf_mesh>` | All meshes (flat) |
| `materials` | `list<gltf_material>` | All materials |
| `animations` | `list<gltf_animation>` | All animation clips |
| `skins` | `list<gltf_skin>` | All skeletons |
| `images` | `list<gltf_image>` | All images |
| `free` | — | Releases all resources |

### `gltf_node` / `gltf_animation` Methods (transforms)

| Method | Return | Description |
|--------|--------|-------------|
| `node.local_matrix` | `buffer<f32>` | 16 values, column-major: `matrix` or T·R·S |
| `node.world_matrix` | `buffer<f32>` | 16 values, column-major: product of the parent chain |
| `anim.duration` | `f32` | Largest key time of all channels (seconds) |
| `anim.sample_translation_x/y/z(node, t)` | `f32` | Sampled translation (§8 rules) |
| `anim.sample_rotation_x/y/z/w(node, t)` | `f32` | Sampled rotation quaternion (§8 rules) |
| `anim.sample_local_matrix(node, t)` | `buffer<f32>` | Local matrix at `t` (§8 rules) |

### `gltf_primitive` Methods (Vertex Data)

| Method | Return | Description |
|--------|--------|-------------|
| `positions` | `buffer<f32>` | xyz per vertex |
| `normals` | `buffer<f32>` | xyz per vertex |
| `tangents` | `buffer<f32>` | xyzw per vertex |
| `uvs(set)` | `buffer<f32>` | uv per vertex, set=0 or 1 |
| `colors(set)` | `buffer<f32>` | rgba per vertex |
| `joints(set)` | `buffer<u16>` | 4 joint indices per vertex |
| `weights(set)` | `buffer<f32>` | 4 weights per vertex |
| `indices` | `buffer<u32>` | Index buffer (u32, always) |
| `vertex_count` | `i32` | Number of vertices |
| `index_count` | `i32` | Number of indices |
| `material` | `gltf_material` | Material of this primitive |

---

## 12. Implementation Notes

### 12.1 Backend: cgltf

[cgltf](https://github.com/jkuhlmann/cgltf) — single-header C library (MIT).
No external dependency beyond the header file.

```c
// src/sys/gltf_runtime.h / gltf_runtime.cpp
// include cgltf.h, wrapper functions for AIML C interface
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
```

### 12.2 Image Decoding

cgltf provides image data as raw PNG/JPEG bytes. For `img.pixels_rgba`,
**stb_image** (single-header, public domain) is used:

```c
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

No further external dependencies — both headers are copied into the repo.

### 12.3 Index Normalization

cgltf supports u8/u16/u32 indices. The runtime layer always normalizes to `u32`
for consistent GPU uploads.

### 12.4 Interplay with Vulkan/OpenGL

The buffer<f32>/buffer<u32> return values are directly suitable for `glBufferData` /
`vkMapMemory` uploads — no intermediate copying needed:

```aiml
# OpenGL (via FFI)
- call: [glBindBuffer, GL_ARRAY_BUFFER, vbo]
- call: [glBufferData, GL_ARRAY_BUFFER, positions.byte_len, { call: [positions.ptr] }, GL_STATIC_DRAW]
```

Details on OpenGL/Vulkan integration → Chapter 43.2.
