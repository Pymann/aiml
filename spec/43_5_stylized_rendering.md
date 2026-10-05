# 43.5 - Rendering interfaces for the ShapeML 2 pipeline

Status: implementation contract, 2026-10-05. No new AIML grammar is required.
Implemented (desktop GL, V1): §3, §4, §6, §7, §8, §9 `gl.read_rgba8`; tests
`test_gltf_texture_info`, `test_gl_studio_formats`. Open: §5 cubemaps and §9 timer
queries (V2/V6); web/Android paths of §8 report `wait_events` as unsupported.

Consumers: [ShapeML 2](../../shaper/doc/spec/visual_pipeline_v2.md),
[glbview studio renderer](../../glbview/doc/spec/studio_renderer_v2.md).
Extends [glTF](43_1_gltf.md), [GL](43_2_graphics.md),
[engine](44_1_engine.md), subject to [ownership](61_memory_management_DRAFT.md).
[Stages and acceptance](../doc/plans/visual_pipeline_implementation_2026-10-05.md).

## 1. Existing functionality versus additions

Inspected specs and runtime sources on 2026-10-05; this is not a fresh execution
test of every listed builtin. Resolve registration/backend disagreements during
the first work package; do not add duplicate APIs under different names.

| Already specified / runtime implementation found | Required use or completion |
|---|---|
| gltf material base RGBA, metallic, roughness, emissive RGB | glbview must actually copy and shade them |
| base/normal/metallic_roughness/emissive/occlusion texture getters | use all channels, not just base/normal |
| alpha_mode, alpha_cutoff, double_sided | honor renderer state and shading |
| texture wrap_s/wrap_t/min_filter/mag_filter | respect each binding's sampler |
| image.pixels_rgba, texture creation/filter/mipmap calls | add explicit encoding/format paths, keep old overloads |
| framebuffers, shader uniforms, culling, blending, depth-test toggles | extend formats, ownership-safe attachment binding and state |
| engine.set_vsync, engine.target_fps, engine.time | use existing cap and monotonic time |
| engine.is_iconified/is_focused/is_visible | use existing lifecycle information |
| uniform_mat4_array runtime | validate size/count and registered signature for skin palettes |
| glTF joint/weight/skin access | finish/integrate P10; do not create a second skin API |

New work: material texture-info metadata, sRGB/float uploads, cubemaps, HDR target
formats, explicit alpha/depth state, event waiting, framebuffer pixel size, GPU
timing, reproducible readback. MikkTSpace is a build-time shaper integration,
not a new language primitive. AO/BVH, geometry builders and recipes stay in shaper.

## 2. General call and ownership rules

All signatures below use AIML types, not their C ABI representation. bool results
must be registered as bool even when native implementation uses int32_t.
Names with a dot after a type are methods. GPU calls require a live current
context on its owning thread. Native symbols, builtin registry, AOT/JIT resolver
and method dispatch must agree on one ABI; no raw GL IDs exposed to AIML code.

All input buffers/strings/handles are borrowed for the call. Uploads copy data
before returning; no retained pointer into a caller's buffer. Returned GPU handles
are unique Class-4 owners with automatic drop and existing explicit delete as
early close. Early close marks the owner invalid; a later drop is a no-op. Failed
constructors return null and free every partial allocation. Setters returning
bool return false on invalid input/unsupported capability without changing old
valid state. Getter errors return the explicitly stated sentinel. No GL error
is silently treated as successful creation. Runtime emits a contextual diagnostic.

Except binding/state calls, resource construction/upload preserves framebuffer,
texture and active-unit bindings so a failed optional allocation cannot break an
already running renderer. GL context must outlive its resources. No copying
unique handles into cache entries: one owner per entry, references/indices elsewhere.
Framebuffers own their attachments; no second owning gl_texture for an attachment.

`fbo.color_texture()` currently creates a wrapper with ambiguous ownership in
gl_runtime.cpp. Do not use it in the new renderer. The new attachment-binding
method below avoids that ambiguity without changing existing callers' types.
Audit old callers separately before altering the old method's contract.

## 3. glTF additions

| Signature | Result and defaults |
|---|---|
| gltf_material.index() -> i32 | material index in its owning scene, -1 invalid |
| gltf_texture.image_index() -> i32 | image index in owning scene, -1 absent |
| gltf_material.normal_scale() -> f32 | normalTexture.scale, default 1 |
| gltf_material.occlusion_strength() -> f32 | occlusionTexture.strength, default 1 |
| gltf_material.emissive_strength() -> f32 | KHR_materials_emissive_strength factor, default 1 |
| gltf_material.texture_coord(slot: string) -> i32 | referenced textureInfo.texCoord, default 0; -1 absent/invalid slot |
| gltf_material.texture_has_transform(slot: string) -> bool | KHR_texture_transform exists on that binding |
| gltf_scene.extensions_used() -> list<string> | owned copied names, empty when absent |
| gltf_scene.extensions_required() -> list<string> | owned copied names, empty when absent |
| gltf_scene.extras() -> string | owned top-level extras JSON, empty if absent |

Slot names: base_color, normal, metallic_roughness, occlusion, emissive. These
accessors must inspect the material's textureInfo, not the shared cgltf_texture:
normal scale, texCoord and transforms differ between references to the same image.
No new owning wrapper is required just to read these scalars. Existing texture
getters keep their Class-4 wrapper/scene-lifetime rules. Index identities are only
valid within one load, never globally stable IDs. Material names are not IDs.

Validate finite factors while loading a renderer material. Where glTF defaults
and current runtime differ, fix the getter against glTF conformance tests; do not
introduce a viewer-only contradictory default. Unsupported texture transforms are
detectable and diagnosed by the viewer rather than silently dropped in the API.

## 4. Explicit texture formats

New constants: gl.RGBA8, gl.SRGB8_ALPHA8, gl.RGBA16F. Format is an enum-like i32
constant, not arbitrary internal GL format accepted unchecked. Existing
gl.create_texture/load_texture/image.to_gl_texture retain their current defaults.

| Signature | Contract |
|---|---|
| gl.create_texture_format(w:i32,h:i32,pixels:buffer<u8>,format:i32,mipmaps:bool) -> gl_texture | RGBA8 or SRGB8_ALPHA8; exactly w*h*4 input bytes |
| gl.create_texture_f32(w:i32,h:i32,pixels:buffer<f32>,mipmaps:bool) -> gl_texture | RGBA16F; exactly w*h*4 finite components, converted by driver |
| gl_texture.width() / height() -> i32 | stored dimensions, 0 invalid |
| gl_texture.format() -> i32 | actual format constant, 0 invalid |
| gl_texture.levels() -> i32 | allocated levels, 0 invalid |

Dimensions positive, within context maximum; multiplication overflow checked
before allocation. No automatic pixel flip or implicit premultiplication. New
textures use clamp-to-edge, linear mag; min trilinear with full mip chain when
mipmaps=true, otherwise linear with only level 0. Existing set_wrap/set_filter
apply afterward; validate legal combinations to avoid incomplete textures.
Hardware sRGB decoding affects RGB only. gen_mipmaps on SRGB8_ALPHA8 must filter
in linear light according to backend semantics, tested with a black/white fixture.

## 5. Cubemap resources

New type `gl_cubemap`: unique Class-4 GPU owner, native opaque handle. It is not
interchangeable with gl_texture; bind target is fixed by type.

| Signature | Contract |
|---|---|
| gl.create_cubemap(size:i32,levels:i32) -> gl_cubemap | RGBA16F faces, 1<=levels<=floor(log2(size))+1; positive size within limit |
| gl_cubemap.upload_face(face:i32,level:i32,pixels:buffer<f32>) -> bool | exact levelWidth^2*4 finite floats, copied synchronously |
| gl.bind_cubemap(cube:gl_cubemap,unit:i32) -> void | unit range validated against fragment-stage limit |
| gl_cubemap.delete() -> void | optional early close |

Face enums POSITIVE_X, NEGATIVE_X, POSITIVE_Y, NEGATIVE_Y, POSITIVE_Z, NEGATIVE_Z
are accepted; bundle face numbers 0..5 are explicitly mapped to these constants.
For texture coordinates u,v in [-1,1], directions before normalization are
+X:(1,-v,-u), -X:(-1,-v,u), +Y:(u,1,v), -Y:(u,-1,-v),
+Z:(u,-v,1), -Z:(-u,-v,-1). This follows GL cubemap orientation; no image-style
vertical flip during float upload. Allocate zero-filled valid storage for all
faces/levels, clamp-to-edge, linear mag, trilinear min when levels>1.
Do not generate mipmaps after prefiltered faces were uploaded. The runtime enables
seamless cubemap filtering on desktop when using this path; ES/WebGL uses native
cube filtering behavior. New resources participate in leak/drop tests.

## 6. Render targets and state

| Signature | Contract |
|---|---|
| gl.create_framebuffer_format(w:i32,h:i32,format:i32,depth:bool) -> gl_framebuffer | RGBA8 or RGBA16F; optional depth24-stencil8 renderbuffer; null if incomplete |
| gl_framebuffer.bind_color_texture(unit:i32) -> void | bind owned attachment for sampling, no wrapper allocation |
| gl_framebuffer.resize_checked(w:i32,h:i32) -> bool | atomically replace storage, retain format/depth; old valid target survives failure |
| gl_framebuffer.width()/height() -> i32 | dimensions, 0 invalid |
| gl_framebuffer.format() -> i32 | actual color format, 0 invalid |
| gl.depth_write(on:bool) -> void | glDepthMask |
| gl.depth_func(mode:i32) -> void | LESS or LEQUAL only for this addition |
| gl.front_face(mode:i32) -> void | CW or CCW |
| gl.blend_func_separate(src_rgb:i32,dst_rgb:i32,src_a:i32,dst_a:i32) -> void | ZERO, ONE, SRC_ALPHA, ONE_MINUS_SRC_ALPHA |
| gl.framebuffer_srgb(on:bool) -> bool | desktop capability-gated enable/disable; false if on unsupported |

New targets have no mip chain, linear filters and clamp-to-edge. No depth texture
or multisample attachment is needed for the baseline; shadow maps and MSAA are
not hidden requirements. A framebuffer may not be sampled while bound as its
draw target. Detect this for bind_color_texture and diagnose rather than creating
undefined feedback. Binding default framebuffer does not guess viewport size;
viewer sets it from framebuffer_size. Buffer clears honor masks, so renderer
must restore depth_write=true before depth clears.

Existing gl.enable_blend selects the old alpha factors; new renderer sets its
explicit factors after enabling. Every renderer queue sets required state; custom
passes do not rely on sticky state. Existing gl_framebuffer.resize keeps its API;
new code uses resize_checked for recovery and format retention.

New constants needed beyond formats/faces: gl.LESS, gl.LEQUAL, gl.CW, gl.CCW,
gl.ZERO, gl.ONE, gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA. Reuse registered names if
already available; values match corresponding GL constants.

## 7. Uniform arrays and capabilities

`gl_shader.uniform_vec3_array(name:string,values:buffer<f32>,count:i32) -> bool`
uploads count packed triples; require count>=0 and buffer length>=3*count, checked
without overflow. count=0 succeeds without accessing buffer. Missing optimized-out
uniform is a successful no-op. Never read past the buffer.
Existing uniform_mat4_array must similarly validate count and >=16*count floats;
retain its existing public return type. Bad input diagnoses and performs no upload.
Skin palettes require sufficient vertex-uniform capacity before compiling variants.

`gl.supports(feature:string) -> bool` supports exact keys:
srgb_texture, rgba16f_renderable, rgba16f_filterable, cubemap_float,
timer_query, framebuffer_srgb. Unknown key diagnoses and returns false.
Capabilities describe this current context, not compile-time platform guesses.
Framebuffer format support includes a complete create/check/delete probe cached
per context. All temporary probe resources are released.

`gl.limit(name:string) -> i32`: max_texture_size, max_cube_size,
max_fragment_texture_units, max_vertex_uniform_components. Unknown -> -1 plus
diagnostic. glbview requires 7 fragment samplers and room for 64 palette matrices
plus vertex transforms for skinned rendering, with clear failure if unavailable.
Caps may be queried only after context creation.

## 8. Event waiting and framebuffer size

Existing engine.window_size keeps its behavior (pixels per spec/44_1; on desktop
it already reads the framebuffer size); add
`engine.framebuffer_size(win:window_handle) -> ivec2` as the explicit render-pixel
query on every backend.
Desktop uses glfwGetFramebufferSize; web canvas backing dimensions; Android
current EGL surface dimensions. A minimized drawable may return [0,0]; callers
skip allocation/drawing until both positive. Pointer coordinates are transformed
using logical-to-framebuffer ratios; do not double-scale HiDPI input.

`engine.wait_events(win:window_handle,timeout_ms:i32) -> i32` accepts 0..1000.
0 polls without blocking; positive waits up to the timeout or an event.
Return bit flags: 1 input, 2 resize/expose, 4 focus/visibility, 8 close;
0 timeout. Multiple bits may be set. Queue pending input for the next normal
input.update; waiting must not consume a click/scroll edge irretrievably. Pending
events cause immediate return. No swap, frame-counter increment or animation
advance. Native implementation uses glfwWaitEventsTimeout with existing callbacks
preserved/chained. Do not install a second competing input callback set.

`engine.reset_frame_clock(win:window_handle) -> void` resets dt baseline and cap
deadline to now after idle/hidden wait; next dt=0. It does not reset engine.time,
frame count or viewer-owned animation time. time remains monotonic f64. Existing
target_fps/swap limiter must include time spent in VSync and sleep only the remaining
deadline; no additive full-frame sleep after an already blocking swap.

Desktop is required initially. Web waiting yields through its existing Asyncify/
event-loop path, never busy waits or blocks the browser main thread. If a backend
cannot support manual waiting, returns an explicit unsupported diagnostic and
viewer keeps a capped loop; it may not claim zero idle draws. Android uses its
looper lifecycle path. Per-platform completion is reported separately.

## 9. GPU timing and image readback

New unique Class-4 `gl_timer_query`:

| Signature | Contract |
|---|---|
| gl.create_timer_query() -> gl_timer_query | null when unsupported; no emulated GPU timings |
| gl_timer_query.begin() -> bool | begin TIME_ELAPSED; fail if another elapsed query active |
| gl_timer_query.end() -> bool | end this active query; invalid ordering fails |
| gl_timer_query.ready() -> bool | nonblocking availability; false for active/invalid |
| gl_timer_query.result_ns() -> i64 | elapsed ns when ready/valid, otherwise -1 without waiting |
| gl_timer_query.delete() -> void | close; active query ended before release |

Results can be read repeatedly until next begin. Begin while a prior result is
pending fails; renderer skips a sample instead of blocking. Disjoint GPU timing
invalidates samples on affected backends. Implement 64-bit native query retrieval,
not truncated 32-bit milliseconds. Profiling must never issue glFinish.

`gl.read_rgba8(x:i32,y:i32,w:i32,h:i32) -> buffer<u8>` copies the currently bound
read framebuffer region after rendering; positive in-bounds rectangle required.
Returns an owned buffer with exactly w*h*4 bytes, top row first, stored RGBA.
Readback does not implicitly unpremultiply or gamma-convert stored pixels.
Invalid input/allocation yields empty buffer and diagnostic. Preserve pack alignment
and read state. Reading HDR to bytes clamps; screenshot tests read the final
display-encoded target, never use this as HDR export. Readback may stall, so it
is a test/screenshot API, not frame telemetry. Existing buffer.encode_png handles
encoding; no new PNG subsystem.

## 10. Backend and compiler integration checklist

Each new call requires declarations/registrations, native symbol binding, method
receiver mapping, IR lowering and matching runtime implementation. Handle-return
calls require Class-4 ownership metadata and destructor binding. Buffer inputs
must match existing buffer layout; no bypass of bounds/lifetime checks.

Desktop GL 3.3 is mandatory. WebGL2/ES3 implementations check float-render/filter
extensions and GPU timer availability; report unsupported features rather than
compile dead functions as silent success. Software GL desktop can run correctness
tests but cannot establish hardware performance. New interfaces must not require
OpenGL 4.x, compute shaders, bindless textures or Vulkan.

## 11. Focused acceptance

- Compile and run minimal AIML callers for every signature under AOT; exercise JIT
  registrations separately and report known broader JIT limitations independently.
- Black/white sRGB texture filters to linear ~0.5 before lighting; alpha unaffected;
  normal/data channel 0.5 remains ~0.5. Factor-only/textured gray agree within one
  normalized-byte encoding step under an unlit comparison shader.
- Float target retains a test value >1 before tone mapping; LDR fallback is explicit.
- Cube face labels/orientation and roughness mip selection match reference samples.
- Short/oversized/overflowing buffer counts, negative dimensions, unsupported formats
  and null handles cannot read memory or leak partial GPU resources.
- Repeat HDR creation/resize/failure/cleanup and attachment binds in one process;
  attachment binds allocate no heap wrapper and do not create a second owner.
- Timer polling never stalls on unavailable data; invalid samples stay -1/null.
- Idle waiting wakes on click, scroll, resize and close; input edges preserved;
  HiDPI render dimensions differ correctly from input dimensions.
- Drop owners and early-close owners on success and failure paths, with resources
  released before context destruction; follow spec 61 memory acceptance.

No edits to unrelated language/lifetime defects are implied by this interface
package. Existing reports remain separate; do not disable ownership diagnostics
to make graphics acceptance pass.
