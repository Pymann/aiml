# Kapitel 43.2 — `gl`-Namespace (OpenGL 3.3+ Core Profile)

> **Leitgedanke:** AIML liefert mit dem `gl`-Namespace einen nativen,
> typisierten Wrapper um OpenGL 3.3 Core. Window-Erstellung und Context-Verwaltung
> übernimmt SDL2 oder GLFW via FFI (Kapitel 35). Shader, Buffers, Texturen und
> Draw-Calls sind vollständig in AIML ausdrückbar. Vulkan-Support ist im
> abschließenden Abschnitt als Ausblick beschrieben.

---

## 1. Typen

| Typ              | Intern  | Beschreibung |
|------------------|---------|--------------|
| `gl_buffer`      | `i8*`   | VBO / IBO — Vertex- oder Index-Buffer auf der GPU |
| `gl_vao`         | `i8*`   | Vertex Array Object — merkt sich das Vertex-Layout |
| `gl_shader`      | `i8*`   | Kompiliertes Shader-Programm (Vert + Frag) |
| `gl_texture`     | `i8*`   | 2D-Textur auf der GPU |
| `gl_framebuffer` | `i8*`   | Offscreen Render-Target (FBO) |
| `gl_renderbuffer`| `i8*`   | Depth/Stencil-Attachment eines FBO |
| `gl_mesh`        | `i8*`   | Gebündeltes Draw-Objekt: VAO + VBOs + IBO + index_count |

Alle Handles sind opak — sie wrappen intern einen `GLuint` auf dem Heap,
damit NULL-Semantik und Lebensdauer-Tracking möglich sind.

---

## 2. Initialisierung

Der OpenGL-Context wird über SDL2 oder GLFW erzeugt (FFI, Kapitel 35).
Danach muss `gl.init()` einmalig aufgerufen werden, um GLAD/GLEW zu laden:

```aiml
# SDL2-Window + OpenGL-Context (via FFI, verkürzt)
- extern: "C"
  defs:
    - def: { name: SDL_Init,             args: [[f, i32]],                                               return: i32 }
    - def: { name: SDL_CreateWindow,     args: [[t, string], [x, i32], [y, i32], [w, i32], [h, i32], [f, i32]], return: ptr }
    - def: { name: SDL_GL_CreateContext, args: [[win, ptr]],                                             return: ptr }
    - def: { name: SDL_GL_SwapWindow,    args: [[win, ptr]],                                             return: void }
    - def: { name: SDL_Quit,             args: [],                                                       return: void }

- decl: [win, ptr, { call: [SDL_CreateWindow, "AIML Game", 100, 100, 800, 600, 0x00000002] }]
- decl: [ctx, ptr, { call: [SDL_GL_CreateContext, win] }]

# AIML-seitig: GLAD initialisieren + Basisstatus setzen
- call: [gl.init]
- call: [gl.viewport, 0, 0, 800, 600]
- call: [gl.enable_depth_test]
```

`gl.init` ruft intern `gladLoadGLLoader(SDL_GL_GetProcAddress)` auf.
Bei Fehler (z.B. OpenGL < 3.3) → Exception `gl.InitError`.

---

## 3. Buffer-Management

### 3.1 Vertex Buffer Object (VBO)

```aiml
# Vertex-Daten (xyz pro Vertex)
- decl: [verts, buffer<f32>]
- call: [verts.push, 0.0]  - call: [verts.push, 0.5]  - call: [verts.push, 0.0]
- call: [verts.push, -0.5] - call: [verts.push, -0.5] - call: [verts.push, 0.0]
- call: [verts.push, 0.5]  - call: [verts.push, -0.5] - call: [verts.push, 0.0]

# VBO erstellen und hochladen
- decl: [vbo, gl_buffer, { call: [gl.create_buffer, verts] }]
```

Kurzform mit `gl.create_buffer(data)` erzeugt den Buffer und lädt `data`
in einem Schritt hoch (`GL_STATIC_DRAW`).

Für dynamische Daten (jedes Frame neu):

```aiml
- decl: [vbo, gl_buffer, { call: [gl.create_buffer_dynamic, verts] }]  # GL_DYNAMIC_DRAW

# Später: Daten aktualisieren (kein neuer Buffer nötig)
- call: [vbo.update, new_verts]
```

### 3.2 Index Buffer Object (IBO)

```aiml
- decl: [idx, buffer<u32>]
- call: [idx.push, 0] - call: [idx.push, 1] - call: [idx.push, 2]

- decl: [ibo, gl_buffer, { call: [gl.create_index_buffer, idx] }]
```

### 3.3 Vertex Array Object (VAO) + Attribut-Layout

```aiml
# VAO mit VBO binden und Attribute definieren
- decl: [vao, gl_vao, { call: [gl.create_vao] }]

# Attribut 0: position (3 f32, Stride = 32 Byte, Offset = 0)
# Attribut 1: normal   (3 f32, Stride = 32 Byte, Offset = 12)
# Attribut 2: uv       (2 f32, Stride = 32 Byte, Offset = 24)
- call: [vao.bind_vbo, vbo]
- call: [vao.attrib, 0, 3, 32, 0]    # index, components, stride_bytes, offset_bytes
- call: [vao.attrib, 1, 3, 32, 12]
- call: [vao.attrib, 2, 2, 32, 24]
- call: [vao.bind_ibo, ibo]
```

### 3.4 `gl_mesh` — gebündeltes Mesh-Objekt

`gl_mesh` fasst VAO, VBOs, IBO und den Index-Count in einem Handle zusammen.
Erstellt mit `gl.create_mesh` (allgemein) oder `prim.to_gl_mesh()` (direkt aus glTF).

**Allgemeine Erstellung:**

```aiml
# positions = buffer<f32>  (3 floats/vertex: xyz)
# uvs       = buffer<f32>  (2 floats/vertex: uv)    — optional, null erlaubt
# normals   = buffer<f32>  (3 floats/vertex: xyz)   — optional, null erlaubt
# indices   = buffer<u32>

- decl: [mesh, gl_mesh, { call: [gl.create_mesh, positions, uvs, normals, indices] }]
```

Intern generiert `gl.create_mesh` separate VBOs je Accessor und konfiguriert
das VAO mit festem Attrib-Layout:

| Attrib-Index | Inhalt    | Komponenten | Stride |
|-------------|-----------|-------------|--------|
| 0           | position  | 3 × f32     | 12 B   |
| 1           | uv        | 2 × f32     | 8 B    |
| 2           | normal    | 3 × f32     | 12 B   |

**Aus glTF-Primitive (Kurzform):**

```aiml
# prim.to_gl_mesh() erkennt automatisch vorhandene Accessoren
# (POSITION, TEXCOORD_0, NORMAL) und ruft intern gl.create_mesh auf.
- decl: [mesh, gl_mesh, { call: [prim.to_gl_mesh] }]
```

**Draw-Call:**

```aiml
- call: [mesh.draw]                  # gl.draw_elements mit gespeichertem index_count
- call: [mesh.draw_count, 36]        # override: nur n Indizes zeichnen
```

**Cleanup:**

```aiml
- call: [mesh.delete]   # gibt VAO + alle VBOs + IBO frei
```

---

## 4. Shader

### 4.1 Kompilierung

```aiml
- decl: [vert_src, string, "
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;
}"]

- decl: [frag_src, string, "
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    FragColor = texture(uTex, vUV);
}"]

- decl: [sh, gl_shader, { call: [gl.compile_shader, vert_src, frag_src] }]
```

Bei Compile-/Link-Fehler → Exception `gl.ShaderError` mit dem GLSL-Info-Log als Message.

### 4.2 Aus Datei laden

```aiml
- decl: [sh, gl_shader, { call: [gl.load_shader, "/shaders/mesh.vert", "/shaders/mesh.frag"] }]
```

### 4.3 Uniforms setzen

```aiml
- call: [sh.use]

# Skalare und Vektoren
- call: [sh.uniform_f32, "uTime", 1.23]
- call: [sh.uniform_i32, "uTex",  0]           # Texture Unit 0
- call: [sh.uniform_vec3, "uColor", { call: [math.vec3, 1.0, 0.5, 0.2] }]
- call: [sh.uniform_vec4, "uFog",   { call: [math.vec4, 0.6, 0.7, 0.8, 1.0] }]

# Matrizen (column-major, transponiert=false)
- decl: [mvp, mat4, { *: [proj, { *: [view, model] }] }]
- call: [sh.uniform_mat4, "uMVP", mvp]
```

`sh.uniform_mat4` ruft intern `glUniformMatrix4fv(..., GL_FALSE, mat4_ptr)` — kein
manuelles `mat4_to_buffer` nötig.

---

## 5. Texturen

### 5.1 Aus Datei laden

```aiml
- decl: [tex, gl_texture, { call: [gl.load_texture, "/textures/albedo.png"] }]
```

Intern: stb_image → RGBA → `glTexImage2D` → Mipmaps (`glGenerateMipmap`).
Default-Filterung: `GL_LINEAR_MIPMAP_LINEAR` (min) / `GL_LINEAR` (mag).

### 5.2 Aus Pixel-Daten (z.B. aus glTF)

```aiml
# pixels = buffer<u8> (RGBA, width*height*4 Bytes)
- decl: [tex, gl_texture, { call: [gl.create_texture, w, h, pixels] }]
```

### 5.3 Direkt aus glTF-Image (Kurzform)

```aiml
# Decode (stb_image) + GPU-Upload in einem Schritt — kein buffer<u8> nötig
- decl: [tex, gl_texture, { call: [img.to_gl_texture] }]
- call: [tex.gen_mipmaps]
```

`img` ist ein `gltf_image`-Handle (aus `tex.image()` oder `scene.images().get(i)`).
Intern: stb_image dekodiert direkt → `glTexImage2D`, kein AIML-seitiger Pixel-Buffer.

### 5.3 Sampler-Parameter setzen

```aiml
- call: [tex.set_wrap, gl.REPEAT, gl.REPEAT]           # s, t
- call: [tex.set_filter, gl.LINEAR_MIPMAP_LINEAR, gl.LINEAR]  # min, mag
- call: [tex.gen_mipmaps]
```

### 5.4 Binden

```aiml
- call: [gl.bind_texture, tex, 0]   # Slot 0 → uniform uTex=0
- call: [gl.bind_texture, normal_tex, 1]  # Slot 1 → uniform uNormal=1
```

---

## 6. Draw-Calls

```aiml
- call: [sh.use]
- call: [sh.uniform_mat4, "uMVP", mvp]
- call: [gl.bind_texture, tex, 0]

# Indexed Draw (VAO hat IBO gebunden)
- call: [gl.draw_elements, vao, index_count]    # GL_TRIANGLES, GL_UNSIGNED_INT

# Non-indexed Draw
- call: [gl.draw_arrays, vao, vertex_count]     # GL_TRIANGLES

# Mit explizitem Primitiv-Typ
- call: [gl.draw_arrays_mode, vao, gl.LINES, vertex_count]
- call: [gl.draw_arrays_mode, vao, gl.POINTS, vertex_count]
```

---

## 7. Render-State

```aiml
# Viewport
- call: [gl.viewport, 0, 0, 1920, 1080]

# Depth-Test
- call: [gl.enable_depth_test]
- call: [gl.disable_depth_test]

# Alpha-Blending (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
- call: [gl.enable_blend]
- call: [gl.disable_blend]

# Face Culling
- call: [gl.enable_cull_face]
- call: [gl.cull_front]     # GL_FRONT
- call: [gl.cull_back]      # GL_BACK (Standard)

# Wireframe
- call: [gl.wireframe, 1]   # an
- call: [gl.wireframe, 0]   # aus

# Framebuffer löschen
- call: [gl.clear, 0.1, 0.1, 0.15, 1.0]     # Farbe (r,g,b,a)
- call: [gl.clear_depth]                      # Depth-Buffer
- call: [gl.clear_all, 0.1, 0.1, 0.15, 1.0] # Farbe + Tiefe in einem
```

---

## 8. Framebuffer (Offscreen Rendering)

```aiml
# FBO mit Color-Texture + Depth-RBO
- decl: [fbo, gl_framebuffer, { call: [gl.create_framebuffer, 1920, 1080] }]

# Rendern in FBO
- call: [gl.bind_framebuffer, fbo]
- call: [gl.clear_all, 0.0, 0.0, 0.0, 1.0]
# ... draw calls ...

# Zurück zum Default-Framebuffer (Screen)
- call: [gl.bind_default_framebuffer]
- call: [gl.viewport, 0, 0, 1920, 1080]

# FBO-Farbtextur als normale Textur verwenden (Post-Processing)
- decl: [color_tex, gl_texture, { call: [fbo.color_texture] }]
- call: [gl.bind_texture, color_tex, 0]
- call: [gl.draw_elements, screen_vao, 6]   # Fullscreen-Quad
```

---

## 9. Konstanten

```aiml
gl.TRIANGLES     # GL_TRIANGLES   = 0x0004
gl.LINES         # GL_LINES       = 0x0001
gl.POINTS        # GL_POINTS      = 0x0000
gl.TRIANGLE_STRIP# GL_TRIANGLE_STRIP = 0x0005

gl.REPEAT           # GL_REPEAT           = 0x2901
gl.CLAMP_TO_EDGE    # GL_CLAMP_TO_EDGE    = 0x812F
gl.MIRRORED_REPEAT  # GL_MIRRORED_REPEAT  = 0x8370

gl.LINEAR                # GL_LINEAR                = 0x2601
gl.NEAREST               # GL_NEAREST               = 0x2600
gl.LINEAR_MIPMAP_LINEAR  # GL_LINEAR_MIPMAP_LINEAR  = 0x2703
gl.NEAREST_MIPMAP_LINEAR # GL_NEAREST_MIPMAP_LINEAR = 0x2702
```

Verwendung:
```aiml
- call: [tex.set_wrap, gl.CLAMP_TO_EDGE, gl.CLAMP_TO_EDGE]
- call: [gl.draw_arrays_mode, vao, gl.LINES, edge_count]
```

---

## 10. Vollständiges Beispiel — Texturiertes Dreieck

**`exodus.json`:**
```json
{
  "linker": {
    "libraries": ["SDL2", "GL", "dl"]
  }
}
```

**`main.aiml`:**
```aiml
- extern: "C"
  defs:
    - def: { name: SDL_Init,              args: [[f, i32]],                                                       return: i32 }
    - def: { name: SDL_CreateWindow,      args: [[t, string], [x, i32], [y, i32], [w, i32], [h, i32], [f, i32]], return: ptr }
    - def: { name: SDL_GL_CreateContext,  args: [[win, ptr]],                                                     return: ptr }
    - def: { name: SDL_GL_SwapWindow,     args: [[win, ptr]],                                                     return: void }
    - def: { name: SDL_PollEvent,         args: [[e, ptr]],                                                       return: i32 }
    - def: { name: SDL_DestroyWindow,     args: [[win, ptr]],                                                     return: void }
    - def: { name: SDL_Quit,              args: [],                                                                return: void }

# --- Fenster + Context ---
- call: [SDL_Init, 0x00000020]
- decl: [win, ptr, { call: [SDL_CreateWindow, "Hello AIML GL", 100, 100, 800, 600, 0x00000002] }]
- decl: [glctx, ptr, { call: [SDL_GL_CreateContext, win] }]
- call: [gl.init]
- call: [gl.enable_depth_test]
- call: [gl.viewport, 0, 0, 800, 600]

# --- Geometrie ---
- decl: [verts, buffer<f32>]
# pos (xyz), uv (xy)
- call: [verts.push,  0.0] - call: [verts.push,  0.5] - call: [verts.push, 0.0]
- call: [verts.push,  0.5] - call: [verts.push,  1.0]
- call: [verts.push, -0.5] - call: [verts.push, -0.5] - call: [verts.push, 0.0]
- call: [verts.push,  0.0] - call: [verts.push,  0.0]
- call: [verts.push,  0.5] - call: [verts.push, -0.5] - call: [verts.push, 0.0]
- call: [verts.push,  1.0] - call: [verts.push,  0.0]

- decl: [vbo, gl_buffer, { call: [gl.create_buffer, verts] }]
- decl: [vao, gl_vao, { call: [gl.create_vao] }]
- call: [vao.bind_vbo, vbo]
- call: [vao.attrib, 0, 3, 20, 0]   # position: 3 floats, stride 20, offset 0
- call: [vao.attrib, 1, 2, 20, 12]  # uv:       2 floats, stride 20, offset 12

# --- Shader ---
- decl: [sh, gl_shader, { call: [gl.load_shader, "/shaders/basic.vert", "/shaders/basic.frag"] }]

# --- Textur ---
- decl: [tex, gl_texture, { call: [gl.load_texture, "/textures/grid.png"] }]

# --- Projection ---
- decl: [proj, mat4, { call: [math.mat4_perspective,
    { call: [math.to_radians, 60.0] }, 1.333, 0.1, 100.0] }]
- decl: [view, mat4, { call: [math.mat4_lookat,
    { call: [math.vec3, 0.0, 0.0, 3.0] },
    { call: [math.vec3, 0.0, 0.0, 0.0] },
    { call: [math.vec3, 0.0, 1.0, 0.0] }] }]

# --- Game Loop ---
- decl: [running, i8, 1]
- decl: [angle, f32, 0.0]

- while: [{ ==: [running, 1] }]
  body:
    # Events (SDL_PollEvent gibt 0 zurück wenn Queue leer)
    - decl: [ev_buf, ptr]   # SDL_Event (52 Bytes, opak)
    - while: [{ call: [SDL_PollEvent, ev_buf] }]
      body:
        - set: [running, 0]   # jedes Event beendet (Demo — echtes Quit prüft ev.type)

    # Update
    - set: [angle, { +: [angle, 0.016] }]
    - decl: [model, mat4, { call: [math.mat4_rotate_y, angle] }]
    - decl: [mvp, mat4, { *: [proj, { *: [view, model] }] }]

    # Render
    - call: [gl.clear_all, 0.1, 0.1, 0.15, 1.0]
    - call: [sh.use]
    - call: [sh.uniform_mat4, "uMVP", mvp]
    - call: [sh.uniform_i32, "uTex", 0]
    - call: [gl.bind_texture, tex, 0]
    - call: [gl.draw_arrays, vao, 3]
    - call: [SDL_GL_SwapWindow, win]

# --- Cleanup ---
- call: [vbo.delete]
- call: [vao.delete]
- call: [tex.delete]
- call: [sh.delete]
- call: [SDL_DestroyWindow, win]
- call: [SDL_Quit]
```

---

## 11. glTF-Mesh zu OpenGL

Mit `prim.to_gl_mesh()` und `img.to_gl_texture()` lassen sich glTF-Assets
in wenigen Zeilen rendern. Die manuelle VBO/VAO-Einrichtung entfällt vollständig.

**Kurzform (empfohlen):**

```aiml
- decl: [scene, gltf_scene,     { call: [gltf.load, "/assets/character.glb"] }]
- decl: [meshes, list<gltf_mesh>]
- set:  [meshes, { call: [scene.meshes] }]
- decl: [prim,  gltf_primitive, { call: [{ call: [meshes.get, 0] }.primitive, 0] }]

# Geometrie + Textur auf GPU laden — je eine Zeile
- decl: [mesh, gl_mesh,    { call: [prim.to_gl_mesh] }]
- decl: [mat,  gltf_material, { call: [prim.material] }]
- decl: [img,  gltf_image, { call: [{ call: [mat.base_color_texture] }.image] }]
- decl: [tex,  gl_texture, { call: [img.to_gl_texture] }]
- call: [tex.gen_mipmaps]

# Draw
- call: [sh.use]
- call: [sh.uniform_mat4, "uMVP", mvp]
- call: [gl.bind_texture, tex, 0]
- call: [mesh.draw]

# Cleanup
- call: [mesh.delete]
- call: [tex.delete]
- call: [scene.free]
```

**Ausführliche Form** (wenn Zugriff auf einzelne Buffer nötig, z.B. für Physik-Mesh):

```aiml
- decl: [positions, buffer<f32>, { call: [prim.positions] }]
- decl: [normals,   buffer<f32>, { call: [prim.normals] }]
- decl: [uvs,       buffer<f32>, { call: [prim.uvs, 0] }]
- decl: [indices,   buffer<u32>, { call: [prim.indices] }]
- decl: [ic,        i32,         { call: [prim.index_count] }]

- decl: [vbo, gl_buffer, { call: [gl.create_buffer, positions] }]
- decl: [ibo, gl_buffer, { call: [gl.create_index_buffer, indices] }]
- decl: [vao, gl_vao,   { call: [gl.create_vao] }]
- call: [vao.bind_vbo, vbo]
- call: [vao.attrib, 0, 3, 12, 0]
- call: [vao.bind_ibo, ibo]
- call: [gl.draw_elements, vao, ic]
```

---

## 12. Namespace-Referenz

### `gl`-Funktionen (statisch)

| Aufruf | Argumente | Rückgabe | Beschreibung |
|--------|-----------|----------|--------------|
| `gl.init` | — | — | GLAD laden, OpenGL-State initialisieren |
| `gl.viewport` | `x, y, w, h: i32` | — | glViewport |
| `gl.clear` | `r, g, b, a: f32` | — | Farb-Buffer löschen |
| `gl.clear_depth` | — | — | Depth-Buffer löschen |
| `gl.clear_all` | `r, g, b, a: f32` | — | Farbe + Depth in einem Aufruf |
| `gl.enable_depth_test` | — | — | GL_DEPTH_TEST ein |
| `gl.disable_depth_test` | — | — | GL_DEPTH_TEST aus |
| `gl.enable_blend` | — | — | Alpha-Blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) |
| `gl.disable_blend` | — | — | |
| `gl.enable_cull_face` | — | — | Face-Culling ein |
| `gl.cull_back` | — | — | GL_BACK (Standard) |
| `gl.cull_front` | — | — | GL_FRONT |
| `gl.wireframe` | `on: i8` | — | Polygon-Mode LINE / FILL |
| `gl.create_buffer` | `data: buffer<f32>` | `gl_buffer` | VBO, GL_STATIC_DRAW |
| `gl.create_buffer_dynamic` | `data: buffer<f32>` | `gl_buffer` | VBO, GL_DYNAMIC_DRAW |
| `gl.create_index_buffer` | `data: buffer<u32>` | `gl_buffer` | IBO, GL_STATIC_DRAW |
| `gl.create_vao` | — | `gl_vao` | Leeres VAO |
| `gl.compile_shader` | `vert: string, frag: string` | `gl_shader` | GLSL kompilieren + linken |
| `gl.load_shader` | `vert: path, frag: path` | `gl_shader` | Shader aus Dateien |
| `gl.load_texture` | `p: path` | `gl_texture` | PNG/JPG → GPU (stb_image) |
| `gl.create_texture` | `w, h: i32, px: buffer<u8>` | `gl_texture` | RGBA-Daten → GPU |
| `gl.create_mesh` | `pos, uvs, normals: buffer<f32>, idx: buffer<u32>` | `gl_mesh` | VAO+VBOs+IBO bündeln; `uvs`/`normals` dürfen null sein |
| `gl.bind_texture` | `tex: gl_texture, slot: i32` | — | Texture Unit binden |
| `gl.draw_arrays` | `vao: gl_vao, count: i32` | — | GL_TRIANGLES |
| `gl.draw_elements` | `vao: gl_vao, count: i32` | — | GL_TRIANGLES, GL_UNSIGNED_INT |
| `gl.draw_arrays_mode` | `vao, mode: i32, count: i32` | — | Expliziter Primitiv-Typ |
| `gl.create_framebuffer` | `w, h: i32` | `gl_framebuffer` | Color-Tex + Depth-RBO |
| `gl.bind_framebuffer` | `fb: gl_framebuffer` | — | |
| `gl.bind_default_framebuffer` | — | — | Zurück zum Screen |

### `gl_vao`-Methoden

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `bind_vbo` | `vbo: gl_buffer` | VBO an VAO binden |
| `bind_ibo` | `ibo: gl_buffer` | IBO an VAO binden |
| `attrib` | `index, components, stride, offset: i32` | `glVertexAttribPointer` (f32) |
| `attrib_i` | `index, components, stride, offset: i32` | `glVertexAttribIPointer` (i32) |
| `delete` | — | `glDeleteVertexArrays` |

### `gl_buffer`-Methoden

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `update` | `data: buffer<f32>` | `glBufferSubData` (gleiche Größe) |
| `update_range` | `data, offset, size: i32` | Teilbereich aktualisieren |
| `delete` | — | `glDeleteBuffers` |

### `gl_shader`-Methoden

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `use` | — | `glUseProgram` |
| `uniform_f32` | `name: string, v: f32` | |
| `uniform_i32` | `name: string, v: i32` | |
| `uniform_vec2` | `name: string, v: vec2` | |
| `uniform_vec3` | `name: string, v: vec3` | |
| `uniform_vec4` | `name: string, v: vec4` | |
| `uniform_mat4` | `name: string, m: mat4` | Kein Transponieren (column-major) |
| `uniform_mat4_array` | `name: string, buf: buffer<f32>, count: i32` | Für Skinning |
| `delete` | — | `glDeleteProgram` |

### `gl_texture`-Methoden

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `set_wrap` | `s, t: i32` | `GL_REPEAT` / `GL_CLAMP_TO_EDGE` / ... |
| `set_filter` | `min, mag: i32` | Filter-Modi |
| `gen_mipmaps` | — | `glGenerateMipmap` |
| `delete` | — | `glDeleteTextures` |

### `gl_mesh`-Methoden

| Methode | Argumente | Rückgabe | Beschreibung |
|---------|-----------|----------|--------------|
| `draw` | — | — | `gl.draw_elements` mit gespeichertem index_count |
| `draw_count` | `n: i32` | — | Draw mit override index_count |
| `index_count` | — | `i32` | Anzahl Indizes |
| `delete` | — | — | VAO + alle VBOs + IBO freigeben |

### `gl_framebuffer`-Methoden

| Methode | Rückgabe | Beschreibung |
|---------|----------|--------------|
| `color_texture` | `gl_texture` | Die Color-Attachment-Textur (für Post-Processing) |
| `resize` | `w, h: i32` | FBO + Texturen neu allokieren |
| `delete` | — | FBO + Attachments freigeben |

---

## 13. Implementierungshinweise

### 13.1 Backend: GLAD

GLAD (OpenGL Loader, MIT-Lizenz) wird als Single-Header ins Repo kopiert.
`gl.init` ruft `gladLoadGLLoader` auf. Keine externe Abhängigkeit außer dem
System-OpenGL (`libGL.so` / `opengl32.lib`).

### 13.2 Uniform-Caching

`sh.uniform_*` cacht Uniform-Locations in einer `std::unordered_map<string, GLint>`
pro Shader-Instanz — `glGetUniformLocation` wird nur beim ersten Aufruf aufgerufen.

### 13.3 Skinning — Bone-Matrices

Für Skeletal Animation wird `sh.uniform_mat4_array` genutzt:

```aiml
# Pose aus glTF-Animation samplen
- decl: [pose, list<buffer<f32>>, { call: [anim.sample_pose, time] }]

# Alle Bone-Matrizen als flachen f32-Buffer
- decl: [bones, buffer<f32>]
- decl: [j, i32, 0]
- while: [{ <: [j, { call: [pose.len] }] }]
  body:
    - decl: [bm, buffer<f32>, { call: [pose.get, j] }]
    - call: [bones.append, bm]
    - set: [j, { +: [j, 1] }]

- call: [sh.uniform_mat4_array, "uBones", bones, { call: [pose.len] }]
```

---

## 14. Ausblick: Vulkan

Vulkan erfordert deutlich mehr Boilerplate als OpenGL (Device, Swapchain,
RenderPass, Pipeline, CommandBuffer, Synchronisation, ...). In AIML ist Vulkan
via FFI (Kapitel 35) vollständig nutzbar:

```json
{
  "linker": {
    "libraries": ["vulkan"]
  }
}
```

```aiml
- extern: "C"
  defs:
    - def: { name: vkCreateInstance,   args: [[info, ptr], [alloc, ptr], [out, ptr]], return: i32 }
    - def: { name: vkDestroyInstance,  args: [[inst, ptr], [alloc, ptr]],             return: void }
    # ... alle weiteren Vulkan-Funktionen
```

Ein nativer `vk`-Namespace (analog zu `gl`) ist für eine spätere
Spezifikationsversion geplant. Priorität: zunächst vollständige
OpenGL-Implementierung.
