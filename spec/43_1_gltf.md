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
- if: [{ ==: [ok, 0] }, true]
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
- while: [{ <: [i, { call: [nodes.len] }] }]
  body:
    - decl: [node, gltf_node]
    - set: [node, { call: [nodes.get, i] }]
    - decl: [name, string, { call: [node.name] }]
    - print: ["node: ${name}"]
    - set: [i, { +: [i, 1] }]

# All meshes directly
- decl: [meshes, list<gltf_mesh>, { call: [scene.meshes] }]
```

---

## 4. Mesh Access

```aiml
- decl: [mesh, gltf_mesh, { call: [meshes.get, 0] }]
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
```

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

---

## 7. Node Transform

```aiml
- decl: [node, gltf_node, { call: [nodes.get, 0] }]

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

# Children
- decl: [children, list<gltf_node>, { call: [node.children] }]
- decl: [mesh, gltf_mesh, { call: [node.mesh] }]    # null if no mesh
```

---

## 8. Animation

```aiml
# All animation clips
- decl: [anims, list<gltf_animation>, { call: [scene.animations] }]
- decl: [anim, gltf_animation, { call: [anims.get, 0] }]

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

# Or: all transforms at once as a matrix buffer (for GPU upload)
# Returns list<buffer<f32>> — one buffer<f32>[16] per node
- decl: [pose, list<buffer<f32>>, { call: [anim.sample_pose, 0.5] }]
```

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

### `gltf_scene` Methods

| Method | Return | Description |
|--------|--------|-------------|
| `valid` | `i8` | 1 = successfully loaded |
| `error` | `string` | Error message when `valid == 0` |
| `nodes` | `list<gltf_node>` | Root nodes of the first scene |
| `meshes` | `list<gltf_mesh>` | All meshes (flat) |
| `materials` | `list<gltf_material>` | All materials |
| `animations` | `list<gltf_animation>` | All animation clips |
| `skins` | `list<gltf_skin>` | All skeletons |
| `images` | `list<gltf_image>` | All images |
| `free` | — | Releases all resources |

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
- call: [glBufferData, GL_ARRAY_BUFFER, { call: [positions.byte_len] }, { call: [positions.ptr] }, GL_STATIC_DRAW]
```

Details on OpenGL/Vulkan integration → Chapter 43.2.
