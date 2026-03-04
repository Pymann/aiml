# Kapitel 35 — FFI (Foreign Function Interface)

> **Leitgedanke:** AIML spricht C-ABI nativ. External C-Funktionen werden direkt
> im `.aiml`-Code deklariert. Library-Abhängigkeiten gehören in `exodus.json` —
> nicht in `.aiml`-Dateien.

---

## 1. Library-Abhängigkeiten — `exodus.json`

Externe Bibliotheken werden ausschließlich im Build-System konfiguriert:

```json
{
  "linker": {
    "libraries": ["GL", "SDL2", "m"],
    "library_paths": ["/usr/lib/x86_64-linux-gnu"]
  }
}
```

Es gibt **kein** `- config: link:` in `.aiml`-Dateien.

---

## 2. Externe Funktionen deklarieren — `extern: "C"`

```aiml
- extern: "C"
  defs:
    - def:
        name: SDL_Init
        args: [[flags, i32]]
        return: i32

    - def:
        name: SDL_CreateWindow
        args: [[title, string], [x, i32], [y, i32], [w, i32], [h, i32], [flags, i32]]
        return: ptr

    - def:
        name: SDL_DestroyWindow
        args: [[window, ptr]]
        return: void

    - def:
        name: SDL_Quit
        args: []
        return: void
```

`extern: "C"` Blöcke können überall im Toplevel stehen, auch mehrfach.
Im JIT-Modus werden die Symbole via `dlopen`/`dlsym` aufgelöst;
im AOT-Modus übernimmt der Linker die Auflösung.

---

## 3. Pointer-Typen

| Typ | Beschreibung |
|-----|--------------|
| `ptr` | `void*` — opaker Pointer, keine Typ-Information |
| `ptr<T>` | Typisierter Pointer auf AIML-Typ `T` (z.B. `ptr<i32>`, `ptr<vec3>`) |

```aiml
- extern: "C"
  defs:
    - def:
        name: malloc
        args: [[size, i64]]
        return: ptr

    - def:
        name: free
        args: [[p, ptr]]
        return: void

    - def:
        name: memcpy
        args: [[dst, ptr], [src, ptr], [n, i64]]
        return: ptr

# Verwendung
- decl: [buf, ptr, { call: [malloc, 1024] }]
- call: [free, buf]
```

Typisierter Pointer — für C-Funktionen die auf konkrete Typen zeigen:

```aiml
- extern: "C"
  defs:
    - def:
        name: glGetFloatv
        args: [[pname, i32], [data, ptr<f32>]]
        return: void

- decl: [val, f32]
- call: [glGetFloatv, 0x0BA6, &val]   # & = Adresse von val
```

---

## 4. Structs — Opak vs. Layout-bekannt

### 4.1 Opake Handles (häufigster Fall)

Viele C-Bibliotheken liefern opake Pointer (SDL_Window*, GLFWwindow*, ...).
Diese werden als `ptr` behandelt:

```aiml
# SDL_Window* ist einfach ptr — kein struct-Layout nötig
- decl: [win, ptr, { call: [SDL_CreateWindow, "Title", 0, 0, 800, 600, 4] }]
- call: [SDL_DestroyWindow, win]
```

### 4.2 Structs mit bekanntem Layout

Für Structs deren Felder man lesen/schreiben muss, wird ein AIML-`model` mit
identischem Memory-Layout deklariert. Der `packed:`-Modifier stellt sicher,
dass kein Padding eingefügt wird:

```aiml
# C-Seite:
# typedef struct { float x, y, z; } Vertex;

- model:
    name: Vertex
    packed: true
    members:
      - [x, f32]
      - [y, f32]
      - [z, f32]

- extern: "C"
  defs:
    - def:
        name: mesh_get_vertex
        args: [[mesh, ptr], [index, i32]]
        return: ptr<Vertex>

- decl: [v, ptr<Vertex>, { call: [mesh_get_vertex, mesh_ptr, 0] }]
- decl: [x, f32, v.x]
```

---

## 5. Callback-Funktionen (Function Pointer)

C-Bibliotheken die Callbacks erwarten, erhalten AIML-Funktionen als `fn`:

```aiml
- extern: "C"
  defs:
    - def:
        name: glfwSetKeyCallback
        args: [[window, ptr], [callback, fn]]
        return: void

- def:
    name: on_key
    args: [[window, ptr], [key, i32], [scancode, i32], [action, i32], [mods, i32]]
    body:
      - if: [{ ==: [key, 256] }, true]   # GLFW_KEY_ESCAPE
        then:
          - call: [glfwSetWindowShouldClose, window, 1]

- call: [glfwSetKeyCallback, win, on_key]
```

---

## 6. Vollständiges Beispiel: SDL2 + OpenGL Fenster

**`exodus.json`:**
```json
{
  "linker": {
    "libraries": ["SDL2", "GL", "m"]
  }
}
```

**`main.aiml`:**
```aiml
- extern: "C"
  defs:
    - def: { name: SDL_Init,            args: [[flags, i32]],                                           return: i32  }
    - def: { name: SDL_CreateWindow,    args: [[t, string], [x, i32], [y, i32], [w, i32], [h, i32], [f, i32]], return: ptr  }
    - def: { name: SDL_GL_CreateContext,args: [[win, ptr]],                                             return: ptr  }
    - def: { name: SDL_GL_SwapWindow,   args: [[win, ptr]],                                             return: void }
    - def: { name: SDL_DestroyWindow,   args: [[win, ptr]],                                             return: void }
    - def: { name: SDL_Quit,            args: [],                                                       return: void }
    - def: { name: glClearColor,        args: [[r, f32], [g, f32], [b, f32], [a, f32]],                return: void }
    - def: { name: glClear,             args: [[mask, i32]],                                            return: void }

- decl: [SDL_INIT_VIDEO,        i32, 0x00000020]
- decl: [SDL_WINDOW_OPENGL,     i32, 0x00000002]
- decl: [GL_COLOR_BUFFER_BIT,   i32, 0x00004000]

- decl: [win, ptr, { call: [SDL_CreateWindow, "AIML", 100, 100, 800, 600,
    { |: [SDL_WINDOW_OPENGL] }] }]
- decl: [ctx, ptr, { call: [SDL_GL_CreateContext, win] }]

- call: [glClearColor, 0.1, 0.1, 0.15, 1.0]
- call: [glClear, GL_COLOR_BUFFER_BIT]
- call: [SDL_GL_SwapWindow, win]

- call: [SDL_DestroyWindow, win]
- call: [SDL_Quit]
```

---

## 7. JIT vs. AOT

| Modus | Symbol-Auflösung |
|-------|-----------------|
| JIT | `dlopen(nullptr)` + `dlsym` für vorgelinkte Libs; `dlopen(path)` für dynamische |
| AOT | Linker löst auf — `exodus.json` `linker.libraries` |

Für dynamisch geladene Bibliotheken zur Laufzeit (Plugin-System):

```aiml
# Laufzeit-Loading (JIT + AOT)
- decl: [lib, ptr, { call: [sys.load_lib, "/usr/lib/libSDL2.so"] }]
- decl: [fn_ptr, ptr, { call: [sys.load_sym, lib, "SDL_Init"] }]
```

`sys.load_lib` / `sys.load_sym` sind Wrapper um `dlopen`/`dlsym` —
definiert in Kapitel 23 (sys-Namespace).
