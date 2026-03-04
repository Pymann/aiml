# Kapitel 43.0 — `math`-Namespace (Vektoren, Matrizen, Quaternionen)

> **Leitgedanke:** Eingebaute Value-Typen für lineare Algebra — direkt auf dem Stack,
> keine Heap-Allokation. Mitglieder (`x`, `y`, `z`, `w`) via direktem GEP-Zugriff lesbar.
> Implementierung: C-Runtime mit SIMD-Intrinsics (SSE2/NEON) wo möglich.
> Grundlage für Vulkan/OpenGL (43.2), glTF-Verarbeitung (43.1) und Physik.

---

## 1. Typen

| Typ    | LLVM-Repräsentation         | Größe  | Beschreibung |
|--------|-----------------------------|--------|--------------|
| `vec2` | `{f32, f32}`                | 8 B    | 2D-Vektor (x, y) |
| `vec3` | `{f32, f32, f32, f32}`      | 16 B   | 3D-Vektor (x, y, z) — 16-Byte-aligned (w = 0) |
| `vec4` | `{f32, f32, f32, f32}`      | 16 B   | 4D-Vektor (x, y, z, w) |
| `quat` | `{f32, f32, f32, f32}`      | 16 B   | Quaternion (x, y, z, w) |
| `mat4` | `{f32[16]}`                 | 64 B   | 4×4 Matrix, column-major (OpenGL/Vulkan-Konvention) |
| `ivec2`| `{i32, i32}`                | 8 B    | 2D Integer-Vektor (Pixel-Koordinaten, Tile-Positionen) |
| `ivec3`| `{i32, i32, i32, i32}`      | 16 B   | 3D Integer-Vektor (Voxel-Positionen) |

Alle Typen sind **Value-Typen** — sie liegen vollständig auf dem Stack.
Bei Funktionsübergabe wird eine Kopie erstellt (analog zu `i32`).

---

## 2. Konstruktion

```aiml
# Vektoren
- decl: [a, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]
- decl: [b, vec2, { call: [math.vec2, 0.5, 0.5] }]
- decl: [c, vec4, { call: [math.vec4, 1.0, 0.0, 0.0, 1.0] }]   # RGBA oder Position

# Quaternion (Identität = keine Rotation)
- decl: [q, quat, { call: [math.quat_identity] }]

# Matrizen
- decl: [m, mat4, { call: [math.mat4_identity] }]

# Integer-Vektoren
- decl: [px, ivec2, { call: [math.ivec2, 800, 600] }]
- decl: [vx, ivec3, { call: [math.ivec3, 0, 1, 0] }]
```

---

## 3. Member-Zugriff

```aiml
- decl: [v, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]

# Lesen (direkt via GEP — kein Funktionsaufruf nötig)
- decl: [x, f32, v.x]
- decl: [y, f32, v.y]
- decl: [z, f32, v.z]

# Schreiben
- set: [v.x, 10.0]
- set: [v.z, { +: [v.z, 1.0] }]

# vec4 / quat
- decl: [q, quat]
- decl: [w, f32, q.w]

# mat4: Element (row, col) — 0-basiert
- decl: [m, mat4, { call: [math.mat4_identity] }]
- decl: [m00, f32, { call: [m.get, 0, 0] }]   # Diagonale = 1.0
- call: [m.set, 0, 3, 5.0]                     # Translation X = 5
```

---

## 4. Vektor-Operationen

```aiml
- decl: [a, vec3, { call: [math.vec3, 1.0, 0.0, 0.0] }]
- decl: [b, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]

# Arithmetik via Operator-Overloading
- decl: [sum,    vec3, { +: [a, b] }]      # a + b
- decl: [diff,   vec3, { -: [a, b] }]      # a - b
- decl: [scaled, vec3, { *: [a, 2.5] }]    # a * 2.5
- decl: [neg,    vec3, { -: [a] }]         # -a  (unär)
- decl: [div,    vec3, { /: [a, 2.0] }]    # a / 2.0

# Komponentenweises Produkt (Hadamard)
- decl: [prod, vec3, { *: [a, b] }]        # (ax*bx, ay*by, az*bz)

# Skalarprodukt und Kreuzprodukt
- decl: [d, f32,  { call: [math.dot,   a, b] }]        # 0.0 (senkrecht)
- decl: [c, vec3, { call: [math.cross, a, b] }]        # (0, 0, 1) = Z-Achse

# Länge
- decl: [len, f32, { call: [math.length, a] }]         # 1.0
- decl: [len2, f32, { call: [math.length_sq, a] }]     # 1.0 (ohne sqrt, schneller)

# Normalisieren
- decl: [n, vec3, { call: [math.normalize, a] }]

# Interpolation
- decl: [lerped, vec3, { call: [math.lerp, a, b, 0.5] }]

# Distanz
- decl: [dist, f32, { call: [math.distance, a, b] }]

# Reflect / Refract
- decl: [r, vec3, { call: [math.reflect, a, b] }]      # a reflektiert an Normale b
- decl: [rf, vec3, { call: [math.refract, a, b, 1.33] }] # a gebrochen, ior=1.33

# vec2-spezifisch
- decl: [v2, vec2, { call: [math.vec2, 3.0, 4.0] }]
- decl: [angle, f32, { call: [math.angle, v2] }]       # atan2(y, x)
- decl: [perp,  vec2, { call: [math.perp, v2] }]       # (-y, x) senkrechter Vektor

# vec4 / RGBA
- decl: [col, vec4, { call: [math.vec4, 1.0, 0.5, 0.2, 1.0] }]
- decl: [col2, vec4, { call: [math.lerp, col, { call: [math.vec4, 0.0, 0.0, 0.0, 1.0] }, 0.5] }]
```

---

## 5. Matrix-Operationen

```aiml
# Transform-Matrizen (ergeben neue mat4)
- decl: [t, mat4, { call: [math.mat4_translate, { call: [math.vec3, 1.0, 2.0, 0.0] }] }]
- decl: [r, mat4, { call: [math.mat4_rotate_y, { call: [math.to_radians, 45.0] }] }]
- decl: [s, mat4, { call: [math.mat4_scale, { call: [math.vec3, 2.0, 2.0, 2.0] }] }]

# Rotate um beliebige Achse
- decl: [axis, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]
- decl: [rot, mat4, { call: [math.mat4_rotate, axis, { call: [math.to_radians, 90.0] }] }]

# Multiplikation (Model-Matrix = T * R * S)
- decl: [tr,  mat4, { *: [t, r] }]
- decl: [trs, mat4, { *: [tr, s] }]

# Kamera-Matrizen
- decl: [eye,    vec3, { call: [math.vec3, 0.0, 0.0, 5.0] }]
- decl: [center, vec3, { call: [math.vec3, 0.0, 0.0, 0.0] }]
- decl: [up,     vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]
- decl: [view, mat4, { call: [math.mat4_lookat, eye, center, up] }]

- decl: [proj, mat4, { call: [math.mat4_perspective,
    { call: [math.to_radians, 60.0] },   # fov
    1.777,                                # aspect (16:9)
    0.1,                                  # near
    1000.0] }]                            # far

- decl: [ortho, mat4, { call: [math.mat4_ortho, -1.0, 1.0, -1.0, 1.0, 0.1, 100.0] }]

# Inverse + Transponierte
- decl: [inv,  mat4, { call: [math.mat4_inverse, view] }]
- decl: [tran, mat4, { call: [math.mat4_transpose, proj] }]

# Vektor mit Matrix transformieren
- decl: [world_pos, vec4, { *: [trs, { call: [math.vec4, 0.0, 0.0, 0.0, 1.0] }] }]

# Für GPU-Upload: als buffer<f32> (column-major, 16 Werte)
- decl: [buf, buffer<f32>, { call: [math.mat4_to_buffer, proj] }]
```

---

## 6. Quaternionen

```aiml
# Konstruktion
- decl: [q_id, quat, { call: [math.quat_identity] }]   # Keine Rotation

# Aus Achse + Winkel
- decl: [axis, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]
- decl: [q, quat, { call: [math.quat_axis_angle, axis, { call: [math.to_radians, 90.0] }] }]

# Aus Euler-Winkeln (Reihenfolge: XYZ, Radians)
- decl: [q2, quat, { call: [math.quat_euler, 0.0, { call: [math.to_radians, 45.0] }, 0.0] }]

# Operationen
- decl: [combined, quat, { call: [math.quat_mul, q, q2] }]
- decl: [qn, quat, { call: [math.quat_normalize, combined] }]
- decl: [qi, quat, { call: [math.quat_inverse, q] }]

# Vektor rotieren
- decl: [v, vec3, { call: [math.vec3, 1.0, 0.0, 0.0] }]
- decl: [rotated, vec3, { call: [math.quat_rotate_vec3, q, v] }]

# Interpolation
- decl: [lerp_q,  quat, { call: [math.quat_lerp,  q, q2, 0.5] }]  # schnell, nicht konstante Winkelgeschwindigkeit
- decl: [slerp_q, quat, { call: [math.quat_slerp, q, q2, 0.5] }]  # korrekte sphärische Interpolation

# Konversion
- decl: [rot_mat, mat4, { call: [math.quat_to_mat4, q] }]
- decl: [back, quat, { call: [math.mat4_to_quat, rot_mat] }]
```

---

## 7. Skalare Funktionen

```aiml
# Winkel
- decl: [r, f32, { call: [math.to_radians, 180.0] }]   # → 3.14159...
- decl: [d, f32, { call: [math.to_degrees, 3.14159] }]  # → 180.0

# Trigonometrie
- decl: [s, f32, { call: [math.sin, r] }]
- decl: [c, f32, { call: [math.cos, r] }]
- decl: [t, f32, { call: [math.tan, r] }]
- decl: [as, f32, { call: [math.asin, 0.5] }]
- decl: [ac, f32, { call: [math.acos, 0.5] }]
- decl: [at, f32, { call: [math.atan2, 1.0, 1.0] }]

# Potenz / Wurzel / Log
- decl: [sq, f32, { call: [math.sqrt, 2.0] }]
- decl: [p,  f32, { call: [math.pow,  2.0, 10.0] }]    # 1024.0
- decl: [l,  f32, { call: [math.log,  2.718] }]        # ≈ 1.0
- decl: [l2, f32, { call: [math.log2, 8.0] }]          # 3.0

# Runden
- decl: [fl, f32, { call: [math.floor, 2.7] }]         # 2.0
- decl: [cl, f32, { call: [math.ceil,  2.3] }]         # 3.0
- decl: [ro, f32, { call: [math.round, 2.5] }]         # 3.0
- decl: [fr, f32, { call: [math.fract, 2.7] }]         # 0.7

# Min / Max / Clamp
- decl: [mn, f32, { call: [math.min,   1.0, 2.0] }]    # 1.0
- decl: [mx, f32, { call: [math.max,   1.0, 2.0] }]    # 2.0
- decl: [cl2, f32, { call: [math.clamp, 1.5, 0.0, 1.0] }]  # 1.0
- decl: [sm, f32, { call: [math.smoothstep, 0.0, 1.0, 0.5] }]  # smoothstep

# Absolut / Vorzeichen
- decl: [ab, f32, { call: [math.abs,  -3.5] }]         # 3.5
- decl: [sg, f32, { call: [math.sign, -3.5] }]         # -1.0

# Lineare Interpolation (Skalare)
- decl: [lp, f32, { call: [math.lerp, 0.0, 10.0, 0.3] }]   # 3.0

# Modulo (für Shaders / Tiling)
- decl: [md, f32, { call: [math.fmod, 7.5, 3.0] }]     # 1.5
```

---

## 8. Konstanten

```aiml
- decl: [pi,  f32, math.PI]    # 3.14159265...
- decl: [tau, f32, math.TAU]   # 6.28318530... (= 2π)
- decl: [e,   f32, math.E]     # 2.71828182...
- decl: [inf, f32, math.INF]   # +Unendlich
- decl: [nan, f32, math.NAN]   # Not a Number
```

---

## 9. Integer-Vektoren

```aiml
- decl: [screen, ivec2, { call: [math.ivec2, 1920, 1080] }]
- decl: [tile,   ivec3, { call: [math.ivec3, 5, 0, 3] }]

# Member-Zugriff
- decl: [w, i32, screen.x]
- decl: [h, i32, screen.y]

# Arithmetik
- decl: [half, ivec2, { call: [math.idiv, screen, 2] }]   # (960, 540)
- decl: [sum,  ivec2, { call: [math.iadd, half, { call: [math.ivec2, 10, 10] }] }]

# Konversion f32 ↔ i32
- decl: [fv, vec2, { call: [math.ivec2_to_vec2, screen] }]
- decl: [iv, ivec2, { call: [math.vec2_to_ivec2, fv] }]   # floor-Konversion
```

---

## 10. Namespace-Referenz

### Konstruktoren
| Aufruf | Rückgabe |
|--------|----------|
| `math.vec2(x, y)` | `vec2` |
| `math.vec3(x, y, z)` | `vec3` |
| `math.vec4(x, y, z, w)` | `vec4` |
| `math.quat_identity` | `quat` |
| `math.quat_axis_angle(axis, angle)` | `quat` |
| `math.quat_euler(x, y, z)` | `quat` |
| `math.mat4_identity` | `mat4` |
| `math.mat4_translate(v)` | `mat4` |
| `math.mat4_rotate(axis, angle)` | `mat4` |
| `math.mat4_rotate_x/y/z(angle)` | `mat4` |
| `math.mat4_scale(v)` | `mat4` |
| `math.mat4_perspective(fov, aspect, near, far)` | `mat4` |
| `math.mat4_ortho(l, r, b, t, near, far)` | `mat4` |
| `math.mat4_lookat(eye, center, up)` | `mat4` |
| `math.ivec2(x, y)` | `ivec2` |
| `math.ivec3(x, y, z)` | `ivec3` |

### Operator-Overloads (kein Funktionsaufruf nötig)
| Ausdruck | Typen | Rückgabe |
|----------|-------|----------|
| `{ +: [a, b] }` | `vec2/3/4` + gleich | gleich |
| `{ -: [a, b] }` | `vec2/3/4` + gleich | gleich |
| `{ *: [a, b] }` | `vec2/3/4` + gleich | gleich (Hadamard) |
| `{ *: [a, s] }` | `vec2/3/4` + `f32` | gleich (Scale) |
| `{ /: [a, s] }` | `vec2/3/4` + `f32` | gleich |
| `{ -: [a] }` | `vec2/3/4` (unär) | gleich (Negation) |
| `{ *: [m1, m2] }` | `mat4` + `mat4` | `mat4` |
| `{ *: [m, v] }` | `mat4` + `vec4` | `vec4` |
| `{ ==: [a, b] }` | `vec2/3/4` + gleich | `i8` |

### Vektor-Funktionen (math-Namespace)
| Aufruf | Rückgabe |
|--------|----------|
| `math.dot(a, b)` | `f32` |
| `math.cross(a, b)` | `vec3` |
| `math.length(v)` | `f32` |
| `math.length_sq(v)` | `f32` |
| `math.normalize(v)` | gleich wie Eingabe |
| `math.lerp(a, b, t)` | gleich wie Eingabe |
| `math.distance(a, b)` | `f32` |
| `math.reflect(v, n)` | gleich wie Eingabe |
| `math.refract(v, n, ior)` | gleich wie Eingabe |

### Matrix-Funktionen
| Aufruf | Rückgabe |
|--------|----------|
| `math.mat4_inverse(m)` | `mat4` |
| `math.mat4_transpose(m)` | `mat4` |
| `math.mat4_to_buffer(m)` | `buffer<f32>` (16 Werte, column-major) |
| `math.mat4_to_quat(m)` | `quat` |

### Quaternion-Operationen
| Aufruf | Rückgabe |
|--------|----------|
| `math.quat_mul(a, b)` | `quat` |
| `math.quat_normalize(q)` | `quat` |
| `math.quat_inverse(q)` | `quat` |
| `math.quat_rotate_vec3(q, v)` | `vec3` |
| `math.quat_lerp(a, b, t)` | `quat` |
| `math.quat_slerp(a, b, t)` | `quat` |
| `math.quat_to_mat4(q)` | `mat4` |

---

## 11. Implementierungshinweise

### 11.1 LLVM-Repräsentation

`vec3`, `vec4`, `quat` werden als 16-Byte-aligned LLVM-Struct `{f32, f32, f32, f32}` gespeichert —
passt direkt in ein SSE/NEON-Register. `mat4` ist `{[16 x float]}`.

Da diese Typen Value-Typen sind, werden sie wie User-`model`s behandelt:
`resolveProperty` dispatcht `v.x` → GEP index 0, `v.y` → 1, etc.

### 11.2 C-Runtime

```c
// src/sys/math_runtime.h
typedef struct { float x, y, z, w; } AimlVec3; // w immer 0
typedef struct { float x, y, z, w; } AimlVec4;
typedef struct { float x, y, z, w; } AimlQuat;
typedef struct { float m[16]; }      AimlMat4;  // column-major
```

Skalare Funktionen (`sin`, `cos`, `sqrt`, ...) werden direkt auf `libm`-Calls gemapped
oder via LLVM-Intrinsics (`llvm.sin.f32`) für inlineable Optimierung.

### 11.3 math.PI / math.TAU als Konstanten

`math.PI` etc. werden als compile-time `f32`-Literale aufgelöst — kein Funktionsaufruf,
keine Alloca. Im Visitor: `resolveProperty("math", "PI")` → `llvm::ConstantFP::get(3.14159...)`.
