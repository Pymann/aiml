# Vulkan vs. OpenGL — Explanation

## OpenGL (1992)

OpenGL is a **high-level abstraction** over the GPU. The driver (from NVIDIA, AMD, Intel) handles a lot of work internally and automatically:

- **State Machine**: `glEnable(GL_DEPTH_TEST)` sets global state; the driver remembers everything
- **Implicit Synchronisation**: The driver decides when draw calls are actually executed
- **Runtime Shader Compilation**: GLSL is only translated to GPU code inside the driver — timing and caching are driver secrets
- **Single Thread**: OpenGL context is single-threaded by design

The problem: the driver has to guess many decisions, which leads to **unpredictable stutter frames** and overhead. In addition, every vendor driver behaves slightly differently.

---

## Vulkan (2016, successor to OpenGL)

Vulkan is a **low-level interface** — the driver does almost nothing automatically. You control everything explicitly:

| Topic | OpenGL | Vulkan |
|-------|--------|--------|
| Memory | Driver allocates | You allocate manually (`VkDeviceMemory`) |
| Synchronisation | Implicit | Explicit via Semaphores, Fences, Barriers |
| Render order | Driver decides | You build `CommandBuffer`s |
| Shader compilation | GLSL in driver | SPIR-V (binary, pre-compiled) |
| Threading | Single-threaded | `CommandBuffer`s can be built in parallel |
| Validation | Driver-specific | Separate Validation Layer (toggleable) |

**Advantages of Vulkan:**
- No stutter from unexpected driver work
- Multi-core CPU utilisation (games build CommandBuffers in parallel)
- Identical behaviour across all platforms (no driver fragmentation)
- Works on Android, Linux, Windows — and via **MoltenVK** also on macOS/iOS (Metal wrapper)

**Disadvantage:**
- Boilerplate: drawing a triangle requires ~800 lines of C code (Device, Swapchain, RenderPass, Pipeline, CommandPool, CommandBuffer, Semaphores, Framebuffer, ...)

---

## How are they related?

```
GPU Hardware
    │
    ├── Vulkan (direct, explicit)       ← Khronos Group (same organisation as OpenGL)
    │
    └── OpenGL (driver abstraction)     ← internally: many drivers implement OpenGL
                                           today partly via Vulkan themselves (Mesa/RADV)
```

- Both come from the **Khronos Group** (industry consortium)
- OpenGL is **not** deprecated — runs everywhere, still maintained (last release: 4.6, 2017)
- Vulkan does not replace OpenGL for all use cases — for an indie game, OpenGL 3.3 is perfectly sufficient
- **DirectX 12** (Microsoft) and **Metal** (Apple) are Vulkan's equivalents on their platforms — comparable level of abstraction

---

## For AIML

The decision in the spec (`43_2`): **OpenGL first**, Vulkan later.

- OpenGL fits the AIML philosophy: a native `gl` namespace with ~30 functions covers 95% of game programming requirements
- Vulkan would need a `vk` namespace with ~200+ functions plus complex lifetime management (`VkPipeline`, `VkRenderPass`, `VkSwapchain`, ...) — that is a book-sized chapter on its own
- Anyone who needs Vulkan can already use it fully today via FFI (`extern: "C"`), since Vulkan is a C API
