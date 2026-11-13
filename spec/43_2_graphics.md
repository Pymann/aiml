# Chapter 43.2 — `gl` Namespace (OpenGL 3.3+ Core Profile)

> **Core idea:** AIML provides a native, typed wrapper around OpenGL 3.3 Core via the `gl`
> namespace. Windows and GL contexts come from `engine.create_window` (Chapter 44.1) or from
> GLFW/SDL2 via FFI (Chapter 35). Shaders, buffers, textures, and draw calls are fully expressible in AIML.
> Vulkan support is described in the final section as an outlook.

---

## 1. Types

| Type             | Internal | Description |
|------------------|---------|--------------|
| `gl_buffer`      | `i8*`   | VBO / IBO — vertex or index buffer on the GPU |
| `gl_vao`         | `i8*`   | Vertex Array Object — stores the vertex layout |
| `gl_shader`      | `i8*`   | Compiled shader program (vert + frag) |
| `gl_texture`     | `i8*`   | 2D texture on the GPU |
| `gl_framebuffer` | `i8*`   | Offscreen render target (FBO) |
| `gl_renderbuffer`| `i8*`   | Depth/stencil attachment of an FBO |
| `gl_mesh`        | `i8*`   | Bundled draw object: VAO + VBOs + IBO + index_count |

All handles are opaque — they internally wrap a `GLuint` on the heap,
enabling NULL semantics and lifetime tracking.

---

## 2. Initialization

With `engine.create_window` (Chapter 44.1) everything is set up already — **no `gl.init` call**.
Only a window/context created yourself via FFI (GLFW or SDL2, Chapter 35) needs `gl.init()`
once, after the context has been made current:

```aiml
# SDL2 window + OpenGL context (via FFI, abbreviated)
- extern: "C"
  defs:
    - def: { name: SDL_Init,             args: [[f, i32]],                                               return: i32 }
    - def: { name: SDL_CreateWindow,     args: [[t, string], [x, i32], [y, i32], [w, i32], [h, i32], [f, i32]], return: ptr }
    - def: { name: SDL_GL_CreateContext, args: [[win, ptr]],                                             return: ptr }
    - def: { name: SDL_GL_SwapWindow,    args: [[win, ptr]],                                             return: void }
    - def: { name: SDL_Quit,             args: [],                                                       return: void }

- decl: [win, ptr, { call: [SDL_CreateWindow, "AIML Game", 100, 100, 800, 600, 0x00000002] }]
- decl: [ctx, ptr, { call: [SDL_GL_CreateContext, win] }]

# AIML side: load the GL functions + set base state
- call: [gl.init]
- call: [gl.viewport, 0, 0, 800, 600]
- call: [gl.enable_depth_test]
```

`gl.init` loads the OpenGL functions with GLEW (`glewInit`, desktop) for the context that is
current; under WebGL2 (WASM) no loader is needed and the call does nothing.

On failure (no current context, OpenGL < 3.3, driver error) `gl.init` prints
`[aiml_gl] glewInit failed: <reason>` to stderr and returns normally — **no exception, no status
value**. Later `gl.*` calls are then undefined (typically a crash or no output).

> **Possible future improvement:** report the failure to the program — e.g. an exception
> `gl.InitError` or an `i32` status returned by `gl.init` — so it can exit cleanly instead of
> failing at the next GL call.

---

## 3. Buffer Management

### 3.1 Vertex Buffer Object (VBO)

```aiml
# Vertex data (xyz per vertex)
- decl: [verts, buffer<f32>]
- call: [verts.alloc, 128]   # alloc vor jedem Schreiben (spec/11 §Methoden)
- call: [verts.push, 0.0]  - call: [verts.push, 0.5]  - call: [verts.push, 0.0]
- call: [verts.push, -0.5] - call: [verts.push, -0.5] - call: [verts.push, 0.0]
- call: [verts.push, 0.5]  - call: [verts.push, -0.5] - call: [verts.push, 0.0]

# Create VBO and upload
- decl: [vbo, gl_buffer, { call: [gl.create_buffer, verts] }]
```

The shorthand `gl.create_buffer(data)` creates the buffer and uploads `data`
in one step (`GL_STATIC_DRAW`).

For dynamic data (updated every frame):

```aiml
- decl: [vbo, gl_buffer, { call: [gl.create_buffer_dynamic, verts] }]  # GL_DYNAMIC_DRAW

# Later: update data (no new buffer needed)
- call: [vbo.update, new_verts]
```

### 3.2 Index Buffer Object (IBO)

```aiml
- decl: [idx, buffer<u32>]
- call: [idx.alloc, 64]
- call: [idx.push, 0] - call: [idx.push, 1] - call: [idx.push, 2]

- decl: [ibo, gl_buffer, { call: [gl.create_index_buffer, idx] }]
```

### 3.3 Vertex Array Object (VAO) + Attribute Layout

```aiml
# Bind VAO with VBO and define attributes
- decl: [vao, gl_vao, { call: [gl.create_vao] }]

# Attribute 0: position (3 f32, stride = 32 bytes, offset = 0)
# Attribute 1: normal   (3 f32, stride = 32 bytes, offset = 12)
# Attribute 2: uv       (2 f32, stride = 32 bytes, offset = 24)
- call: [vao.bind_vbo, vbo]
- call: [vao.attrib, 0, 3, 32, 0]    # index, components, stride_bytes, offset_bytes
- call: [vao.attrib, 1, 3, 32, 12]
- call: [vao.attrib, 2, 2, 32, 24]
- call: [vao.bind_ibo, ibo]
```

### 3.4 `gl_mesh` — Bundled Mesh Object

`gl_mesh` combines VAO, VBOs, IBO, and index count into a single handle.
Created with `gl.create_mesh` (general) or `prim.to_gl_mesh()` (directly from glTF).

**General creation:**

```aiml
# positions = buffer<f32>  (3 floats/vertex: xyz)
# uvs       = buffer<f32>  (2 floats/vertex: uv)    — optional, null allowed
# normals   = buffer<f32>  (3 floats/vertex: xyz)   — optional, null allowed
# indices   = buffer<u32>

- decl: [mesh, gl_mesh, { call: [gl.create_mesh, positions, uvs, normals, indices] }]
```

Internally, `gl.create_mesh` generates separate VBOs per accessor and configures
the VAO with a fixed attribute layout:

| Attrib Index | Content   | Components  | Stride |
|-------------|-----------|-------------|--------|
| 0           | position  | 3 × f32     | 12 B   |
| 1           | uv        | 2 × f32     | 8 B    |
| 2           | normal    | 3 × f32     | 12 B   |
| 3           | tangent   | 4 × f32 (xyz + handedness w) | 16 B   |
| 4           | joints    | 4 × f32 (joint indices of the skin, from `JOINTS_0` u8/u16) | 16 B |
| 5           | weights   | 4 × f32 (from `WEIGHTS_0`) | 16 B |

Attribute 3 is filled by `prim.to_gl_mesh()` when the glTF primitive has `TANGENT`
(normal mapping); attributes 4 and 5 when it has `JOINTS_0` and `WEIGHTS_0` (skinning: the
vertex shader blends `uBones[int(aJoints[i])]` by `aWeights[i]`, see `uniform_mat4_array`);
`gl.create_mesh` leaves them unset.

**From glTF primitive (shorthand):**

```aiml
# prim.to_gl_mesh() automatically detects available accessors
# (POSITION, TEXCOORD_0, NORMAL) and calls gl.create_mesh internally.
- decl: [mesh, gl_mesh, { call: [prim.to_gl_mesh] }]
```

**Draw call:**

```aiml
- call: [mesh.draw]                  # gl.draw_elements with stored index_count
- call: [mesh.draw_count, 36]        # override: draw only n indices
```

**Extra attributes** (locations 6..15, e.g. a second UV set at 6 for VFX surface flow):

```aiml
- decl: [uv1, buffer<f32>, { call: [prim.uvs, 1] }]
- decl: [ok, bool, { call: [mesh.set_attribute, 6, 2, uv1] }]   # location, components 1..4, f32 data
```

The data is borrowed for the call and uploaded into a mesh-owned VBO (freed by `mesh.delete`);
setting the same location again replaces it. Returns `false` (no GPU change) for a location
outside 6..15, components outside 1..4, empty data, a byte size that is not a multiple of
`components * 4`, or more than 4 extra attributes per mesh. Locations 0..5 keep the fixed layout.

**Cleanup:**

```aiml
- call: [mesh.delete]   # frees VAO + all VBOs + IBO
```

---

### 3.5 Bounded uploads and instancing (VFX 1)

These calls implement the AIML portion of [ShapeML VFX 1](../../shaper/doc/spec/vfx_v1.md),
sections 10 and 13.1. They use the GL 3.3 / ES 3 / WebGL2 baseline, not compute or
geometry shaders. All calls require the resource's owning GL context current.

- `buffer.reserve_bytes(capacity: i32) -> bool`: negative capacity/null handle fails.
  A request at or below current capacity succeeds without allocation or shrinking.
  Growth stages new dynamic GPU storage, preserves the old prefix, and commits only
  after allocation/copy succeeds. Failure preserves old contents/capacity and VAO
  bindings. Successful growth preserves logical bindings of all AIML VAOs. New VBO
  bytes are unspecified until uploaded; new IBO bytes are zero. Reserve at load or
  quality changes, not per frame.
- `buffer.update_range(offset: i32, data: buffer<u8>) -> bool`: `offset` and the
  buffer's logical byte size must fit the reserved capacity without overflow.
  Negative offsets, invalid data or an out-of-range write fail without a GPU write.
  Empty data succeeds for offsets through capacity inclusive. Upload is synchronous:
  input data is borrowed for the call, never retained or consumed. A temporary
  producer buffer is released after the call; a named buffer stays with its owner.
  No reallocation occurs. The formerly listed `(data, offset, size)` overload was
  not implemented and is replaced by this signature.
- `vao.attrib_divisor(location: i32, divisor: i32) -> bool`: validates location
  against GL_MAX_VERTEX_ATTRIBS and rejects negative divisors. Zero means per-vertex;
  divisor N>0 selects element floor(instance/N). Set 1 for particle attributes.
- `gl.draw_elements_instanced(vao, index_count, instance_count) -> bool`: negative
  counts fail; either zero count succeeds as a no-op. Nonempty draws require a live
  VAO/IBO and enough storage for all requested u32 indices and enabled vertex/instance
  attributes (including stride, offset, maximum referenced index and divisor).
  Failure returns false instead of issuing an out-of-bounds draw. No implicit buffer
  growth or CPU upload occurs. Index validation uses a CPU shadow maintained by the
  IBO upload API; mutate these resources through AIML, not external raw GL calls.

Existing `update`, `attrib`, `attrib_i` and draw signatures remain unchanged.
`update` ignores invalid/oversized writes; it no longer submits such writes to GL.
The new calls return bool (C ABI i32 status); incorrect arity/types are compile errors.
The GPU owner and upload-data owner are separate; reserving GPU storage never changes
or retains the caller's Class-3 buffer.

### 3.6 Buffer/VAO ownership (spec 61)

`gl_buffer` and `gl_vao` are **Class-4 unique resources**, not shared handles.
Constructors/producer returns transfer ownership to the receiving slot. Local
owners release at scope exit, including return and exception unwind. Owning model
fields release with their model; assignment transfers the handle and clears the
source, while reassignment releases the previous destination. `delete()` is an
optional early release and clears the slot so the eventual drop is harmless.
A `persistent` local explicitly opts out of scope release; the escaping owner
must release it. Parameters used by rendering/upload calls are borrows.

A VAO borrows buffer bindings; it never takes ownership of their AIML wrappers.
Keep buffers alive while drawing with the VAO. Early buffer release invalidates
its tracked bindings so checked instanced draws fail rather than dereference a
stale wrapper. Scope GPU owners inside a function/block that finishes **before**
`engine.destroy_window`; automatic release cannot operate after context destruction.
No global shutdown list substitutes for deterministic owner cleanup. Copying or
cloning these owners (also recursively through owning models/containers) is not a
GPU duplication operation; cloning is rejected. Discarded producer results and
fresh resource arguments borrowed by calls are released by the caller.

Implementation and validation: [VFX AIML acceptance](../doc/analysis/vfx_aiml_implementation_2026-10-05.md).

## 4. Shaders

### 4.1 Compilation

```aiml
- decl: [vert_src, string, "
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
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

On compile/link error → exception `gl.ShaderError` with the GLSL info log as the message.

### 4.2 Load from File

```aiml
- decl: [sh, gl_shader, { call: [gl.load_shader, "/shaders/mesh.vert", "/shaders/mesh.frag"] }]
```

### 4.3 Setting Uniforms

```aiml
- call: [sh.use]

# Scalars and vectors
- call: [sh.uniform_f32, "uTime", 1.23]
- call: [sh.uniform_i32, "uTex",  0]           # Texture unit 0
- call: [sh.uniform_vec3, "uColor", { call: [math.vec3, 1.0, 0.5, 0.2] }]
- call: [sh.uniform_vec4, "uFog",   { call: [math.vec4, 0.6, 0.7, 0.8, 1.0] }]

# Matrices (column-major, transpose=false)
- decl: [mvp, mat4, { "*": [proj, { "*": [view, model] }] }]
- call: [sh.uniform_mat4, "uMVP", mvp]
```

`sh.uniform_mat4` internally calls `glUniformMatrix4fv(..., GL_FALSE, mat4_ptr)` — no
manual `mat4_to_buffer` required.

---

## 5. Textures

Explicit color formats, HDR targets, render state, capabilities and readback are
specified in [43.5](43_5_stylized_rendering.md) (cubemaps and timer queries still open),
separately from the API below.

### 5.1 Load from File

```aiml
- decl: [tex, gl_texture, { call: [gl.load_texture, "/textures/albedo.png"] }]
```

Internally: stb_image → RGBA → `glTexImage2D` → mipmaps (`glGenerateMipmap`).
Default filtering: `GL_LINEAR_MIPMAP_LINEAR` (min) / `GL_LINEAR` (mag).

**Row order.** A texture from `gl.load_texture` / `gl.tex` arrives **bottom-up**
(V = 0 is the bottom row), the convention the 2D paths sample with. Images that
come out of a glTF/GLB (`gltf.*`, embedded or by URI) and `img.*` pixel buffers
keep their file order, **top-down**, as glTF prescribes.

Each decode states its own row order, and no decode leaves that choice behind
for the next one. This is normative because the decoder's flag is process-wide:
a loader that set it and walked away made every later decode come out upside
down — a GLB loaded after one `gl.tex` call sampled the wrong part of its atlas
and rendered with scrambled materials (found 2026-11-13, native only; the web
path decodes in the browser). A caller may therefore load in any order.

### 5.2 From Pixel Data (e.g. from glTF)

```aiml
# pixels = buffer<u8> (RGBA, width*height*4 bytes)
- decl: [tex, gl_texture, { call: [gl.create_texture, w, h, pixels] }]
```

### 5.3 Directly from glTF Image (shorthand)

```aiml
# Decode (stb_image) + GPU upload in one step — no buffer<u8> needed
- decl: [tex, gl_texture, { call: [img.to_gl_texture] }]
- call: [tex.gen_mipmaps]
```

`img` is a `gltf_image` handle (from `tex.image()` or `scene.images().get(i)`).
Internally: stb_image decodes directly → `glTexImage2D`, no AIML-side pixel buffer.

### 5.3 Setting Sampler Parameters

```aiml
- call: [tex.set_wrap, gl.REPEAT, gl.REPEAT]           # s, t
- call: [tex.set_filter, gl.LINEAR_MIPMAP_LINEAR, gl.LINEAR]  # min, mag
- call: [tex.gen_mipmaps]
```

### 5.4 Binding

```aiml
- call: [gl.bind_texture, tex, 0]   # slot 0 → uniform uTex=0
- call: [gl.bind_texture, normal_tex, 1]  # slot 1 → uniform uNormal=1
```

### 5.5 Ownership (spec/61 Class 4)

A `gl_texture` from `gl.load_texture`, `gl.create_texture`, `gl.create_texture_format`,
`gl.create_texture_f32`, `gl.decode_texture`, `img.to_gl_texture` or a user function whose
return is `lifetime: producer` (spec/61 §3.2) is **owned**: it is
move-only and deleted exactly once when its owner drops — a local at scope exit, a model
field with the model (spec/61 §6.1). Return, `push`, a member/field store and binding
from a named local **move** it (the source becomes null); storing into a field deletes
the field's previous owned texture. A model with a `gl_texture` field is move-only
(borrow it with `ref<T>`). A local initialised from any other expression (a field, a
user call without `producer`, `gl.tex`) is a borrow and is not dropped.

`tex.delete()` is the optional early close: it deletes the owned texture and nulls the
variable or field, so the later drop is a no-op.

Not owned by the program: `gl.tex(stem)` (asset cache), `gl.source_texture` (one
runtime-held preview texture), `fbo.color_texture()` (a view of the framebuffer's
attachment — the local holding it frees only the view at drop or `delete()`, never the
attachment).
`gl.register_texture(stem, tex)` hands the texture to the asset cache, which keeps it
for the process; the program's later drop of `tex` does not delete it.

---

## 6. Draw Calls

```aiml
- call: [sh.use]
- call: [sh.uniform_mat4, "uMVP", mvp]
- call: [gl.bind_texture, tex, 0]

# Indexed draw (VAO has IBO bound)
- call: [gl.draw_elements, vao, index_count]    # GL_TRIANGLES, GL_UNSIGNED_INT

# Non-indexed draw
- call: [gl.draw_arrays, vao, vertex_count]     # GL_TRIANGLES

# With explicit primitive type
- call: [gl.draw_arrays_mode, vao, gl.LINES, vertex_count]
- call: [gl.draw_arrays_mode, vao, gl.POINTS, vertex_count]
```

---

## 7. Render State

```aiml
# Viewport
- call: [gl.viewport, 0, 0, 1920, 1080]

# Depth test
- call: [gl.enable_depth_test]
- call: [gl.disable_depth_test]

# Alpha blending (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
- call: [gl.enable_blend]
- call: [gl.disable_blend]

# Face culling
- call: [gl.enable_cull_face]
- call: [gl.disable_cull_face]   # off again
- call: [gl.cull_front]     # GL_FRONT
- call: [gl.cull_back]      # GL_BACK (default)

# Wireframe
- call: [gl.wireframe, 1]   # on
- call: [gl.wireframe, 0]   # off

# Clear framebuffer
- call: [gl.clear, 0.1, 0.1, 0.15, 1.0]     # color (r,g,b,a)
- call: [gl.clear_depth]                      # depth buffer
- call: [gl.clear_all, 0.1, 0.1, 0.15, 1.0] # color + depth in one call
```

---

## 8. Framebuffer (Offscreen Rendering)

```aiml
# FBO with color texture + depth RBO
- decl: [fbo, gl_framebuffer, { call: [gl.create_framebuffer, 1920, 1080] }]

# Render into FBO
- call: [gl.bind_framebuffer, fbo]
- call: [gl.clear_all, 0.0, 0.0, 0.0, 1.0]
# ... draw calls ...

# Back to default framebuffer (screen)
- call: [gl.bind_default_framebuffer]
- call: [gl.viewport, 0, 0, 1920, 1080]

# Use FBO color texture as a regular texture (post-processing)
- decl: [color_tex, gl_texture, { call: [fbo.color_texture] }]
- call: [gl.bind_texture, color_tex, 0]
- call: [gl.draw_elements, screen_vao, 6]   # fullscreen quad
```

---

## 9. Constants

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

Usage:
```aiml
- call: [tex.set_wrap, gl.CLAMP_TO_EDGE, gl.CLAMP_TO_EDGE]
- call: [gl.draw_arrays_mode, vao, gl.LINES, edge_count]
```

---

## 10. Complete Example — Textured Triangle

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

# --- Window + context ---
- call: [SDL_Init, 0x00000020]
- decl: [win, ptr, { call: [SDL_CreateWindow, "Hello AIML GL", 100, 100, 800, 600, 0x00000002] }]
- decl: [glctx, ptr, { call: [SDL_GL_CreateContext, win] }]
- call: [gl.init]
- call: [gl.enable_depth_test]
- call: [gl.viewport, 0, 0, 800, 600]

# --- Geometry ---
- decl: [verts, buffer<f32>]
- call: [verts.alloc, 128]   # alloc vor jedem Schreiben (spec/11 §Methoden)
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

# --- Texture ---
- decl: [tex, gl_texture, { call: [gl.load_texture, "/textures/grid.png"] }]

# --- Projection ---
- decl: [proj, mat4, { call: [math.mat4_perspective,
    { call: [math.to_radians, 60.0] }, 1.333, 0.1, 100.0] }]
- decl: [view, mat4, { call: [math.mat4_lookat,
    { call: [math.vec3, 0.0, 0.0, 3.0] },
    { call: [math.vec3, 0.0, 0.0, 0.0] },
    { call: [math.vec3, 0.0, 1.0, 0.0] }] }]

# --- Game loop ---
- decl: [running, i8, 1]
- decl: [angle, f32, 0.0]

- while: [{ eq: [running, 1] }]
  body:
    # Events (SDL_PollEvent returns 0 when queue is empty)
    - decl: [ev_buf, ptr]   # SDL_Event (52 bytes, opaque)
    - while: [{ call: [SDL_PollEvent, ev_buf] }]
      body:
        - set: [running, 0]   # any event exits (demo — real quit checks ev.type)

    # Update
    - set: [angle, { +: [angle, 0.016] }]
    - decl: [model, mat4, { call: [math.mat4_rotate_y, angle] }]
    - decl: [mvp, mat4, { "*": [proj, { "*": [view, model] }] }]

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

## 11. glTF Mesh to OpenGL

With `prim.to_gl_mesh()` and `img.to_gl_texture()`, glTF assets can be rendered
in just a few lines. Manual VBO/VAO setup is entirely unnecessary.

**Shorthand (recommended):**

```aiml
- decl: [scene, gltf_scene,     { call: [gltf.load, "/assets/character.glb"] }]
- decl: [meshes, list<gltf_mesh>]
- set:  [meshes, { call: [scene.meshes] }]
- decl: [prim,  gltf_primitive, { call: [{ call: [meshes.get, 0] }.primitive, 0] }]

# Upload geometry + texture to GPU — one line each
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

**Verbose form** (when access to individual buffers is needed, e.g. for a physics mesh):

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

## 12. Namespace Reference

### `gl` Functions (static)

| Call | Arguments | Return | Description |
|--------|-----------|----------|--------------|
| `gl.init` | — | — | Load the GL functions with GLEW (desktop); no-op under WebGL2 (§2) |
| `gl.supports` | `feature: string` | `bool` | Context capability; keys and semantics in `43_5_stylized_rendering.md` §7 |
| `gl.limit` | `name: string` | `i32` | Context limit (`max_texture_size`, …), `-1` for an unknown key — 43_5 §7 |
| `gl.read_rgba8` | `x, y, w, h: i32` | `buffer<u8>` | Read back the bound framebuffer — 43_5 §9; basis for headless pixel checks |
| `gl.viewport` | `x, y, w, h: i32` | — | glViewport |
| `gl.clear` | `r, g, b, a: f32` | — | Clear color buffer |
| `gl.clear_depth` | — | — | Clear depth buffer |
| `gl.clear_all` | `r, g, b, a: f32` | — | Color + depth in one call |
| `gl.enable_depth_test` | — | — | GL_DEPTH_TEST on |
| `gl.disable_depth_test` | — | — | GL_DEPTH_TEST off |
| `gl.enable_blend` | — | — | Alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) |
| `gl.disable_blend` | — | — | |
| `gl.enable_cull_face` | — | — | Face culling on |
| `gl.disable_cull_face` | — | — | Face culling off (both sides drawn) |
| `gl.cull_back` | — | — | GL_BACK (default) |
| `gl.cull_front` | — | — | GL_FRONT |
| `gl.wireframe` | `on: i8` | — | Polygon mode LINE / FILL |
| `gl.create_buffer` | `data: buffer<f32>` | `gl_buffer` | VBO, GL_STATIC_DRAW |
| `gl.create_buffer_dynamic` | `data: buffer<f32>` | `gl_buffer` | VBO, GL_DYNAMIC_DRAW |
| `gl.create_index_buffer` | `data: buffer<u32>` | `gl_buffer` | IBO, GL_STATIC_DRAW |
| `gl.create_vao` | — | `gl_vao` | Empty VAO |
| `gl.compile_shader` | `vert: string, frag: string` | `gl_shader` | Compile + link GLSL |
| `gl.load_shader` | `vert: path, frag: path` | `gl_shader` | Shader from files |
| `gl.load_texture` | `p: path` | `gl_texture` | PNG/JPG → GPU (stb_image) |
| `gl.create_texture` | `w, h: i32, px: buffer<u8>` | `gl_texture` | RGBA data → GPU |
| `gl.create_mesh` | `pos, uvs, normals: buffer<f32>, idx: buffer<u32>` | `gl_mesh` | Bundle VAO+VBOs+IBO; `uvs`/`normals` may be null |
| `gl.bind_texture` | `tex: gl_texture, slot: i32` | — | Bind texture unit |
| `gl.draw_arrays` | `vao: gl_vao, count: i32` | — | GL_TRIANGLES |
| `gl.draw_elements` | `vao: gl_vao, count: i32` | — | GL_TRIANGLES, GL_UNSIGNED_INT |
| `gl.draw_elements_instanced` | `vao: gl_vao, index_count, instance_count: i32` | `bool` | Checked instanced GL_TRIANGLES, GL_UNSIGNED_INT |
| `gl.draw_arrays_mode` | `vao, mode: i32, count: i32` | — | Explicit primitive type |
| `gl.create_framebuffer` | `w, h: i32` | `gl_framebuffer` | Color tex + depth RBO |
| `gl.bind_framebuffer` | `fb: gl_framebuffer` | — | |
| `gl.bind_default_framebuffer` | — | — | Back to screen |

### `gl_vao` Methods

| Method | Arguments | Description |
|---------|-----------|--------------|
| `bind_vbo` | `vbo: gl_buffer` | Bind VBO to VAO |
| `bind_ibo` | `ibo: gl_buffer` | Bind IBO to VAO |
| `attrib` | `index, components, stride, offset: i32` | `glVertexAttribPointer` (f32) |
| `attrib_i` | `index, components, stride, offset: i32` | `glVertexAttribIPointer` (i32) |
| `attrib_divisor` | `location, divisor: i32` | `bool`: 0 = per vertex, positive = per N instances |
| `delete` | — | `glDeleteVertexArrays` |

### `gl_buffer` Methods

| Method | Arguments | Description |
|---------|-----------|--------------|
| `update` | `data: buffer<f32>` | `glBufferSubData` (same size) |
| `reserve_bytes` | `capacity: i32` | `bool`: reserve at least this many bytes, never shrink |
| `update_range` | `offset: i32, data: buffer<u8>` | `bool`: synchronous checked byte upload, no reallocation |
| `delete` | — | `glDeleteBuffers` |

### `gl_shader` Methods

| Method | Arguments | Description |
|---------|-----------|--------------|
| `use` | — | `glUseProgram` |
| `uniform_f32` | `name: string, v: f32` | |
| `uniform_i32` | `name: string, v: i32` | |
| `uniform_vec2` | `name: string, v: vec2` | |
| `uniform_vec3` | `name: string, v: vec3` | |
| `uniform_vec4` | `name: string, v: vec4` | |
| `uniform_mat4` | `name: string, m: mat4` | No transpose (column-major) |
| `uniform_mat4_array` | `name: string, buf: buffer<f32>, count: i32` | For skinning |
| `delete` | — | `glDeleteProgram` |

### `gl_texture` Methods

| Method | Arguments | Description |
|---------|-----------|--------------|
| `set_wrap` | `s, t: i32` | `GL_REPEAT` / `GL_CLAMP_TO_EDGE` / ... |
| `set_filter` | `min, mag: i32` | Filter modes |
| `gen_mipmaps` | — | `glGenerateMipmap` |
| `delete` | — | `glDeleteTextures` |

### `gl_mesh` Methods

| Method | Arguments | Return | Description |
|---------|-----------|----------|--------------|
| `draw` | — | — | `gl.draw_elements` with stored index_count |
| `draw_count` | `n: i32` | — | Draw with override index_count |
| `index_count` | — | `i32` | Number of indices |
| `delete` | — | — | Free VAO + all VBOs + IBO |

### `gl_framebuffer` Methods

| Method | Return | Description |
|---------|----------|--------------|
| `color_texture` | `gl_texture` | The color attachment texture (for post-processing) |
| `resize` | `w, h: i32` | Reallocate FBO + textures |
| `delete` | — | Free FBO + attachments |

---

## 13. Implementation Notes

### 13.1 Backend: GLEW

`gl.init` calls `glewInit` (desktop).  GLEW is a declared build dependency
(`libglew-dev` / `libglew2.2` in `exodus.json`) and is linked into `libaiml.so`;
there is no vendored loader in the repo.  Under WebGL2 (WASM) no loader is needed
and `gl.init` does nothing (§2).  Besides that only the system OpenGL is required
(`libGL.so` / `opengl32.lib`).

### 13.2 Uniform Caching

`sh.uniform_*` caches uniform locations in a `std::unordered_map<string, GLint>`
per shader instance — `glGetUniformLocation` is only called on the first access.

### 13.3 Skinning — Bone Matrices

For skeletal animation, `sh.uniform_mat4_array` is used:

```aiml
# Sample pose from glTF animation
- decl: [pose, list<buffer<f32>>, { call: [anim.sample_pose, time] }]

# All bone matrices as a flat f32 buffer
- decl: [bones, buffer<f32>]
- call: [bones.alloc, 4096]  # 64 Matrizen * 16 floats * 4 Byte
- decl: [j, i32, 0]
- while: [{ <: [j, { call: [pose.len] }] }]
  body:
    - decl: [bm, buffer<f32>, { call: [pose.get, j] }]
    - call: [bones.append, bm]
    - set: [j, { +: [j, 1] }]

- call: [sh.uniform_mat4_array, "uBones", bones, { call: [pose.len] }]
```

---

## 14. Outlook: Vulkan

Vulkan requires significantly more boilerplate than OpenGL (device, swapchain,
render pass, pipeline, command buffer, synchronization, ...). In AIML, Vulkan is
fully usable via FFI (Chapter 35):

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
    # ... all further Vulkan functions
```

A native `vk` namespace (analogous to `gl`) is planned for a future
specification version. Priority: complete OpenGL implementation first.
