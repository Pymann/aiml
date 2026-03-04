# Vulkan vs. OpenGL — Erklärung

## OpenGL (1992)

OpenGL ist eine **High-Level-Abstraktion** über die GPU. Der Treiber (von NVIDIA, AMD, Intel) erledigt intern sehr viel Arbeit automatisch:

- **State Machine**: `glEnable(GL_DEPTH_TEST)` setzt globalen Zustand, der Treiber merkt sich alles
- **Implizite Synchronisation**: Der Treiber entscheidet, wann Draw-Calls wirklich ausgeführt werden
- **Shader-Kompilierung zur Laufzeit**: GLSL wird erst im Treiber zu GPU-Code übersetzt — Timing und Caching sind Treibergeheimnis
- **Ein Thread**: OpenGL-Context ist per Design single-threaded

Das Problem: Der Treiber muss viele Entscheidungen raten, was zu **unvorhersehbaren Stutter-Frames** und Overhead führt. Außerdem verhält sich jeder Hersteller-Treiber leicht anders.

---

## Vulkan (2016, Nachfolger von OpenGL)

Vulkan ist eine **Low-Level-Schnittstelle** — der Treiber tut fast gar nichts automatisch. Du kontrollierst alles explizit:

| Thema | OpenGL | Vulkan |
|-------|--------|--------|
| Speicher | Treiber allokiert | Du allokierst manuell (`VkDeviceMemory`) |
| Synchronisation | Implizit | Explizit via Semaphores, Fences, Barriers |
| Render-Reihenfolge | Treiber entscheidet | Du baust `CommandBuffer`s |
| Shader-Kompilierung | GLSL im Treiber | SPIR-V (Binär, vorab kompiliert) |
| Threading | Single-threaded | `CommandBuffer`s können parallel aufgebaut werden |
| Validierung | Treiber-spezifisch | Separater Validation Layer (an/ausschaltbar) |

**Vorteile von Vulkan:**
- Kein Stutter durch überraschende Treiber-Arbeit
- Multi-Core-CPU-Auslastung (Spiele bauen CommandBuffers parallel)
- Identisches Verhalten auf allen Plattformen (kein Treiber-Wildwuchs)
- Funktioniert auf Android, Linux, Windows — und via **MoltenVK** auch auf macOS/iOS (Metal-Wrapper)

**Nachteil:**
- Boilerplate: Ein Dreieck zeichnen braucht ~800 Zeilen C-Code (Device, Swapchain, RenderPass, Pipeline, CommandPool, CommandBuffer, Semaphores, Framebuffer, ...)

---

## Wie hängen sie zusammen?

```
GPU-Hardware
    │
    ├── Vulkan (direkt, explizit)       ← Khronos Group (selbe Organisation wie OpenGL)
    │
    └── OpenGL (Treiber-Abstraktion)    ← intern: viele Treiber implementieren OpenGL
                                           heute z.T. selbst über Vulkan (Mesa/RADV)
```

- Beide kommen von der **Khronos Group** (Industriekonsortium)
- OpenGL ist **nicht** deprecated — läuft überall, wird weiter gewartet (letztes Release: 4.6, 2017)
- Vulkan ersetzt OpenGL nicht für alle Anwendungsfälle — für ein Indie-Spiel ist OpenGL 3.3 vollkommen ausreichend
- **DirectX 12** (Microsoft) und **Metal** (Apple) sind Vulkans Äquivalente auf ihren Plattformen — vergleichbares Abstraktionsniveau

---

## Für AIML

Die Entscheidung im Spec (`43_2`): **OpenGL zuerst**, Vulkan später.

- OpenGL passt zur AIML-Philosophie: ein nativer `gl`-Namespace mit ~30 Funktionen deckt 95% der Spielprogrammier-Anforderungen ab
- Vulkan bräuchte einen `vk`-Namespace mit ~200+ Funktionen plus komplexe Lebenszeit-Verwaltung (`VkPipeline`, `VkRenderPass`, `VkSwapchain`, ...) — das ist ein eigenes Kapitel in Buchgröße
- Wer Vulkan braucht: kann es heute schon vollständig via FFI (`extern: "C"`) nutzen, da Vulkan eine C-API ist
