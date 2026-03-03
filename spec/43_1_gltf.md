# Kapitel 43.1 — `gltf`-Namespace (3D Asset Loader)

> **Leitgedanke:** glTF 2.0 ist das Standard-Austauschformat für 3D-Assets in Games.
> Der `gltf`-Namespace lädt `.glb`/`.gltf`-Dateien und gibt strukturierten Zugriff auf
> Meshes, Materialien, Animationen und die Szenen-Hierarchie.
> Implementierung intern via **cgltf** (single-header C-Bibliothek, MIT-Lizenz).

---

## 1. Typen

| Typ | LLVM | Beschreibung |
|-----|------|--------------|
| `gltf_scene` | `i8*` | Geladene Szene — Root-Container für alles |
| `gltf_node` | `i8*` | Node in der Szenen-Hierarchie (Transform + Kinder) |
| `gltf_mesh` | `i8*` | Mesh (enthält ein oder mehrere Primitives) |
| `gltf_primitive` | `i8*` | Submesh: Vertex-Buffer + Index-Buffer + Material |
| `gltf_material` | `i8*` | PBR-Material (Farbe, Metallic, Roughness, Textures) |
| `gltf_animation` | `i8*` | Animations-Clip (Keyframes für Nodes) |
| `gltf_skin` | `i8*` | Skelett für Skinning/Skeletal Animation |
| `gltf_texture` | `i8*` | Textur-Referenz (URI oder embedded buffer) |
| `gltf_image` | `i8*` | Rohe Bilddaten (RGBA, width, height) |

---

## 2. Laden

```aiml
# .glb laden (binary, alles in einer Datei)
- decl: [p, path, "/assets/character.glb"]
- decl: [scene, gltf_scene, { call: [gltf.load, p] }]

# .gltf laden (JSON + externe Dateien)
- decl: [p2, path, "/assets/level.gltf"]
- decl: [scene2, gltf_scene, { call: [gltf.load, p2] }]

# Fehlerprüfung
- decl: [ok, i8, { call: [scene.valid] }]
- if: [{ ==: [ok, 0] }, true]
  then:
    - decl: [err, string, { call: [scene.error] }]
    - print: ["glTF load error: ${err}"]
```

---

## 3. Szenen-Hierarchie

```aiml
# Alle Root-Nodes der ersten Szene
- decl: [nodes, list<gltf_node>, { call: [scene.nodes] }]
- decl: [i, i32, 0]
- while: [{ <: [i, { call: [nodes.len] }] }]
  body:
    - decl: [node, gltf_node]
    - set: [node, { call: [nodes.get, i] }]
    - decl: [name, string, { call: [node.name] }]
    - print: ["node: ${name}"]
    - set: [i, { +: [i, 1] }]

# Alle Meshes direkt
- decl: [meshes, list<gltf_mesh>, { call: [scene.meshes] }]
```

---

## 4. Mesh-Zugriff

```aiml
- decl: [mesh, gltf_mesh, { call: [meshes.get, 0] }]
- decl: [name, string, { call: [mesh.name] }]
- decl: [prim_count, i32, { call: [mesh.primitive_count] }]
- print: ["mesh '${name}' has ${prim_count} primitives"]

# Primitive (Submesh)
- decl: [prim, gltf_primitive, { call: [mesh.primitive, 0] }]

# Vertex-Daten als buffer<f32>
- decl: [positions, buffer<f32>, { call: [prim.positions] }]   # xyz pro Vertex
- decl: [normals,   buffer<f32>, { call: [prim.normals] }]     # xyz pro Vertex
- decl: [uvs,       buffer<f32>, { call: [prim.uvs, 0] }]      # uv pro Vertex (Texcoord 0)
- decl: [tangents,  buffer<f32>, { call: [prim.tangents] }]    # xyzw pro Vertex

# Index-Buffer als buffer<u32>
- decl: [indices, buffer<u32>, { call: [prim.indices] }]

# Vertex-Anzahl
- decl: [vc, i32, { call: [prim.vertex_count] }]
- decl: [ic, i32, { call: [prim.index_count] }]
- print: ["${vc} vertices, ${ic} indices"]
```

---

## 5. Material

```aiml
- decl: [mat, gltf_material, { call: [prim.material] }]
- decl: [mat_name, string, { call: [mat.name] }]

# PBR-Basiswerte
- decl: [base_r,    f32, { call: [mat.base_color_r] }]
- decl: [base_g,    f32, { call: [mat.base_color_g] }]
- decl: [base_b,    f32, { call: [mat.base_color_b] }]
- decl: [base_a,    f32, { call: [mat.base_color_a] }]
- decl: [metallic,  f32, { call: [mat.metallic] }]
- decl: [roughness, f32, { call: [mat.roughness] }]
- decl: [emissive_r, f32, { call: [mat.emissive_r] }]
- decl: [emissive_g, f32, { call: [mat.emissive_g] }]
- decl: [emissive_b, f32, { call: [mat.emissive_b] }]

# Texturen (null wenn nicht vorhanden)
- decl: [albedo_tex,   gltf_texture, { call: [mat.base_color_texture] }]
- decl: [normal_tex,   gltf_texture, { call: [mat.normal_texture] }]
- decl: [metal_tex,    gltf_texture, { call: [mat.metallic_roughness_texture] }]
- decl: [emissive_tex, gltf_texture, { call: [mat.emissive_texture] }]
- decl: [occlusion_tex,gltf_texture, { call: [mat.occlusion_texture] }]

# Alpha-Modus: "OPAQUE", "MASK", "BLEND"
- decl: [alpha, string, { call: [mat.alpha_mode] }]
- decl: [alpha_cutoff, f32, { call: [mat.alpha_cutoff] }]
- decl: [double_sided, i8, { call: [mat.double_sided] }]
```

---

## 6. Texturen und Bilder

```aiml
- decl: [img, gltf_image, { call: [albedo_tex.image] }]

# Bilddaten als RGBA-Buffer (decoded)
- decl: [pixels, buffer<u8>, { call: [img.pixels_rgba] }]
- decl: [w, i32, { call: [img.width] }]
- decl: [h, i32, { call: [img.height] }]
- print: ["texture: ${w}x${h}"]

# Sampler-Parameter
- decl: [wrap_s, string, { call: [albedo_tex.wrap_s] }]   # "REPEAT", "CLAMP_TO_EDGE", "MIRRORED_REPEAT"
- decl: [wrap_t, string, { call: [albedo_tex.wrap_t] }]
- decl: [min_filter, string, { call: [albedo_tex.min_filter] }]  # "LINEAR", "NEAREST", "LINEAR_MIPMAP_LINEAR" ...
- decl: [mag_filter, string, { call: [albedo_tex.mag_filter] }]
```

---

## 7. Node-Transform

```aiml
- decl: [node, gltf_node, { call: [nodes.get, 0] }]

# TRS-Komponenten (lokaler Raum)
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

# World-Transform-Matrix (column-major f32[16], für direktes Upload zu OpenGL/Vulkan)
- decl: [matrix, buffer<f32>, { call: [node.world_matrix] }]

# Kinder
- decl: [children, list<gltf_node>, { call: [node.children] }]
- decl: [mesh, gltf_mesh, { call: [node.mesh] }]    # null wenn kein Mesh
```

---

## 8. Animation

```aiml
# Alle Animations-Clips
- decl: [anims, list<gltf_animation>, { call: [scene.animations] }]
- decl: [anim, gltf_animation, { call: [anims.get, 0] }]

- decl: [anim_name, string, { call: [anim.name] }]
- decl: [duration,  f32,    { call: [anim.duration] }]   # Sekunden
- print: ["animation '${anim_name}' ${duration}s"]

# Sample: Node-Transform bei einem bestimmten Zeitpunkt (in Sekunden)
# Gibt interpolierte TRS-Werte für den Node zurück
- decl: [tx, f32, { call: [anim.sample_translation_x, node, 0.5] }]
- decl: [ty, f32, { call: [anim.sample_translation_y, node, 0.5] }]
- decl: [tz, f32, { call: [anim.sample_translation_z, node, 0.5] }]
- decl: [rx, f32, { call: [anim.sample_rotation_x, node, 0.5] }]
- decl: [ry, f32, { call: [anim.sample_rotation_y, node, 0.5] }]
- decl: [rz, f32, { call: [anim.sample_rotation_z, node, 0.5] }]
- decl: [rw, f32, { call: [anim.sample_rotation_w, node, 0.5] }]

# Oder: alle Transforms auf einmal als Matrix-Buffer (für GPU-Upload)
# Gibt list<buffer<f32>> zurück — ein buffer<f32>[16] pro Node
- decl: [pose, list<buffer<f32>>, { call: [anim.sample_pose, 0.5] }]
```

---

## 9. Skinning

```aiml
- decl: [skin, gltf_skin, { call: [node.skin] }]   # null wenn kein Skinning

- decl: [joint_count, i32, { call: [skin.joint_count] }]
- print: ["joints: ${joint_count}"]

# Inverse-Bind-Matrizen als buffer<f32> (joint_count * 16 f32-Werte, für GPU)
- decl: [ibm, buffer<f32>, { call: [skin.inverse_bind_matrices] }]

# Joint-Nodes (Knochen)
- decl: [joints, list<gltf_node>, { call: [skin.joints] }]

# Joint-Indices und Weights pro Vertex (für Skinning-Shader)
- decl: [joint_indices, buffer<u16>, { call: [prim.joints, 0] }]   # 4 pro Vertex
- decl: [weights,       buffer<f32>, { call: [prim.weights, 0] }]  # 4 pro Vertex
```

---

## 10. Freigabe

```aiml
# Explizites Freigeben wenn nicht mehr benötigt
- call: [scene.free]
```

---

## 11. Namespace-Referenz

### `gltf`-Namespace

| Aufruf | Argumente | Rückgabe | Beschreibung |
|--------|-----------|----------|--------------|
| `{ call: [gltf.load, p] }` | `path` | `gltf_scene` | Lädt .glb oder .gltf |

### `gltf_scene`-Methoden

| Methode | Rückgabe | Beschreibung |
|---------|----------|--------------|
| `valid` | `i8` | 1 = erfolgreich geladen |
| `error` | `string` | Fehlermeldung wenn `valid == 0` |
| `nodes` | `list<gltf_node>` | Root-Nodes der ersten Szene |
| `meshes` | `list<gltf_mesh>` | Alle Meshes (flach) |
| `materials` | `list<gltf_material>` | Alle Materialien |
| `animations` | `list<gltf_animation>` | Alle Animations-Clips |
| `skins` | `list<gltf_skin>` | Alle Skelette |
| `images` | `list<gltf_image>` | Alle Bilder |
| `free` | — | Gibt alle Ressourcen frei |

### `gltf_primitive`-Methoden (Vertex-Daten)

| Methode | Rückgabe | Beschreibung |
|---------|----------|--------------|
| `positions` | `buffer<f32>` | xyz pro Vertex |
| `normals` | `buffer<f32>` | xyz pro Vertex |
| `tangents` | `buffer<f32>` | xyzw pro Vertex |
| `uvs(set)` | `buffer<f32>` | uv pro Vertex, set=0 oder 1 |
| `colors(set)` | `buffer<f32>` | rgba pro Vertex |
| `joints(set)` | `buffer<u16>` | 4 Joint-Indices pro Vertex |
| `weights(set)` | `buffer<f32>` | 4 Weights pro Vertex |
| `indices` | `buffer<u32>` | Index-Buffer (u32, immer) |
| `vertex_count` | `i32` | Anzahl Vertices |
| `index_count` | `i32` | Anzahl Indices |
| `material` | `gltf_material` | Material dieser Primitive |

---

## 12. Implementierungshinweise

### 12.1 Backend: cgltf

[cgltf](https://github.com/jkuhlmann/cgltf) — single-header C-Bibliothek (MIT).
Keine externe Abhängigkeit außer der Header-Datei.

```c
// src/sys/gltf_runtime.h / gltf_runtime.cpp
// cgltf.h einbinden, Wrapper-Funktionen für AIML-C-Interface
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
```

### 12.2 Bild-Dekodierung

cgltf liefert Bilddaten als rohe PNG/JPEG-Bytes. Für `img.pixels_rgba` wird
**stb_image** (single-header, public domain) verwendet:

```c
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

Kein weiterer externer Dep — beide Header werden ins Repo kopiert.

### 12.3 Index-Normalisierung

cgltf unterstützt u8/u16/u32-Indices. Der Runtime-Layer normalisiert immer auf `u32`
für konsistente GPU-Uploads.

### 12.4 Zusammenspiel mit Vulkan/OpenGL

Die buffer<f32>/buffer<u32>-Rückgaben sind direkt für `glBufferData` /
`vkMapMemory`-Uploads geeignet — kein Zwischenkopieren nötig:

```aiml
# OpenGL (über FFI)
- call: [glBindBuffer, GL_ARRAY_BUFFER, vbo]
- call: [glBufferData, GL_ARRAY_BUFFER, { call: [positions.byte_len] }, { call: [positions.ptr] }, GL_STATIC_DRAW]
```

Details zu OpenGL/Vulkan-Integration → Kapitel 43.2.
