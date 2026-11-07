# Chapter 35 — FFI (Foreign Function Interface)

> **Core idea:** AIML speaks the C ABI natively. External C functions are declared directly
> in `.aiml` code. Library dependencies belong in `exodus.json` —
> not in `.aiml` files.

---

## 1. Library Dependencies — `exodus.json`

External libraries are configured exclusively in the build system:

```json
{
  "linker": {
    "libraries": ["GL", "SDL2", "m"],
    "library_paths": ["/usr/lib/x86_64-linux-gnu"]
  }
}
```

There is **no** `- config: link:` in `.aiml` files.

---

## 2. Declaring External Functions — `extern: "C"`

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

`extern: "C"` blocks can appear anywhere at the top level, including multiple times.
In JIT mode, symbols are resolved via `dlopen`/`dlsym`;
in AOT mode, the linker handles resolution.

---

## 3. Pointer Types

| Type | Description |
|------|-------------|
| `ptr` | `void*` — opaque pointer, no type information |
| `ptr<T>` | Typed pointer to AIML type `T` (e.g. `ptr<i32>`, `ptr<vec3>`) |

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

# Usage
- decl: [buf, ptr, { call: [malloc, 1024] }]
- call: [free, buf]
```

Typed pointer — for C functions that point to concrete types:

```aiml
- extern: "C"
  defs:
    - def:
        name: glGetFloatv
        args: [[pname, i32], [data, ptr<f32>]]
        return: void

- decl: [val, f32]
- call: [glGetFloatv, 0x0BA6, &val]   # & = address of val
```

---

## 4. Structs — Opaque vs. Layout-Known

### 4.1 Opaque Handles (most common case)

Many C libraries return opaque pointers (SDL_Window*, GLFWwindow*, ...).
These are treated as `ptr`:

```aiml
# SDL_Window* is simply ptr — no struct layout needed
- decl: [win, ptr, { call: [SDL_CreateWindow, "Title", 0, 0, 800, 600, 4] }]
- call: [SDL_DestroyWindow, win]
```

### 4.2 Structs with Known Layout

For structs whose fields need to be read/written, an AIML `model` with
identical memory layout is declared. The `packed:` modifier ensures
that no padding is inserted:

```aiml
# C side:
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

## 5. Callback Functions (Function Pointer)

C libraries that expect callbacks receive AIML functions as `fn`:

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
      - if: [{ eq: [key, 256] }, true]   # GLFW_KEY_ESCAPE
        then:
          - call: [glfwSetWindowShouldClose, window, 1]

- call: [glfwSetKeyCallback, win, on_key]
```

---

## 6. Complete Example: SDL2 + OpenGL Window

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
    { "|": [SDL_WINDOW_OPENGL] }] }]
- decl: [ctx, ptr, { call: [SDL_GL_CreateContext, win] }]

- call: [glClearColor, 0.1, 0.1, 0.15, 1.0]
- call: [glClear, GL_COLOR_BUFFER_BIT]
- call: [SDL_GL_SwapWindow, win]

- call: [SDL_DestroyWindow, win]
- call: [SDL_Quit]
```

---

## 7. JIT vs. AOT

| Mode | Symbol resolution |
|------|-------------------|
| JIT | `dlopen(nullptr)` + `dlsym` for pre-linked libs; `dlopen(path)` for dynamic |
| AOT | Linker resolves — `exodus.json` `linker.libraries` |

For dynamically loaded libraries at runtime (plugin system):

```aiml
# Runtime loading (JIT + AOT)
- decl: [lib, ptr, { call: [sys.load_lib, "/usr/lib/libSDL2.so"] }]
- decl: [fn_ptr, ptr, { call: [sys.load_sym, lib, "SDL_Init"] }]
```

`sys.load_lib` / `sys.load_sym` are wrappers around `dlopen`/`dlsym` —
defined in Chapter 23 (sys namespace).
