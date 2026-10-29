# Chapter 43.0 — `math` Namespace (Vectors, Matrices, Quaternions)

> **Core idea:** Built-in value types for linear algebra — directly on the stack,
> no heap allocation. Members (`x`, `y`, `z`, `w`) readable via direct GEP access.
> Implementation: C runtime with SIMD intrinsics (SSE2/NEON) where possible.
> Foundation for Vulkan/OpenGL (43.2), glTF processing (43.1), and physics.

---

## 1. Types

| Type   | LLVM Representation         | Size   | Description |
|--------|-----------------------------|--------|-------------|
| `vec2` | `{f32, f32}`                | 8 B    | 2D vector (x, y) |
| `vec3` | `{f32, f32, f32, f32}`      | 16 B   | 3D vector (x, y, z) — 16-byte aligned (w = 0) |
| `vec4` | `{f32, f32, f32, f32}`      | 16 B   | 4D vector (x, y, z, w) |
| `quat` | `{f32, f32, f32, f32}`      | 16 B   | Quaternion (x, y, z, w) |
| `mat4` | `{f32[16]}`                 | 64 B   | 4×4 matrix, column-major (OpenGL/Vulkan convention) |
| `ivec2`| `{i32, i32}`                | 8 B    | 2D integer vector (pixel coordinates, tile positions) |
| `ivec3`| `{i32, i32, i32, i32}`      | 16 B   | 3D integer vector (voxel positions) |

All types are **value types** — they reside entirely on the stack.
When passed to functions, a copy is made (analogous to `i32`).

---

## 2. Construction

```aiml
# Vectors
- decl: [a, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]
- decl: [b, vec2, { call: [math.vec2, 0.5, 0.5] }]
- decl: [c, vec4, { call: [math.vec4, 1.0, 0.0, 0.0, 1.0] }]   # RGBA or position

# Quaternion (identity = no rotation)
- decl: [q, quat, { call: [math.quat_identity] }]

# Matrices
- decl: [m, mat4, { call: [math.mat4_identity] }]

# Integer vectors
- decl: [px, ivec2, { call: [math.ivec2, 800, 600] }]
- decl: [vx, ivec3, { call: [math.ivec3, 0, 1, 0] }]
```

---

## 3. Member Access

```aiml
- decl: [v, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]

# Read (directly via GEP — no function call needed)
- decl: [x, f32, v.x]
- decl: [y, f32, v.y]
- decl: [z, f32, v.z]

# Write
- set: [v.x, 10.0]
- set: [v.z, { +: [v.z, 1.0] }]

# vec4 / quat
- decl: [q, quat]
- decl: [w, f32, q.w]

# mat4: element (row, col) — 0-based
- decl: [m, mat4, { call: [math.mat4_identity] }]
- decl: [m00, f32, { call: [m.get, 0, 0] }]   # diagonal = 1.0
- call: [m.set, 0, 3, 5.0]                     # translation X = 5
```

---

## 4. Vector Operations

```aiml
- decl: [a, vec3, { call: [math.vec3, 1.0, 0.0, 0.0] }]
- decl: [b, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]

# Arithmetic via operator overloading
- decl: [sum,    vec3, { +: [a, b] }]      # a + b
- decl: [diff,   vec3, { -: [a, b] }]      # a - b
- decl: [scaled, vec3, { "*": [a, 2.5] }]   # a * 2.5
- decl: [neg,    vec3, { -: [a] }]         # -a  (unary)
- decl: [div,    vec3, { /: [a, 2.0] }]    # a / 2.0

# Component-wise product (Hadamard)
- decl: [prod, vec3, { "*": [a, b] }]       # (ax*bx, ay*by, az*bz)

# Dot product and cross product
- decl: [d, f32,  { call: [math.dot,   a, b] }]        # 0.0 (perpendicular)
- decl: [c, vec3, { call: [math.cross, a, b] }]        # (0, 0, 1) = Z-axis

# Length
- decl: [len, f32, { call: [math.length, a] }]         # 1.0
- decl: [len2, f32, { call: [math.length_sq, a] }]     # 1.0 (no sqrt, faster)

# Normalize
- decl: [n, vec3, { call: [math.normalize, a] }]

# Interpolation
- decl: [lerped, vec3, { call: [math.lerp, a, b, 0.5] }]

# Distance
- decl: [dist, f32, { call: [math.distance, a, b] }]

# Reflect / Refract
- decl: [r, vec3, { call: [math.reflect, a, b] }]      # a reflected on normal b
- decl: [rf, vec3, { call: [math.refract, a, b, 1.33] }] # a refracted, ior=1.33

# vec2-specific
- decl: [v2, vec2, { call: [math.vec2, 3.0, 4.0] }]
- decl: [angle, f32, { call: [math.angle, v2] }]       # atan2(y, x)
- decl: [perp,  vec2, { call: [math.perp, v2] }]       # (-y, x) perpendicular vector

# vec4 / RGBA
- decl: [col, vec4, { call: [math.vec4, 1.0, 0.5, 0.2, 1.0] }]
- decl: [col2, vec4, { call: [math.lerp, col, { call: [math.vec4, 0.0, 0.0, 0.0, 1.0] }, 0.5] }]
```

---

## 5. Matrix Operations

```aiml
# Transform matrices (each returns a new mat4)
- decl: [t, mat4, { call: [math.mat4_translate, { call: [math.vec3, 1.0, 2.0, 0.0] }] }]
- decl: [r, mat4, { call: [math.mat4_rotate_y, { call: [math.to_radians, 45.0] }] }]
- decl: [s, mat4, { call: [math.mat4_scale, { call: [math.vec3, 2.0, 2.0, 2.0] }] }]

# Rotate around arbitrary axis
- decl: [axis, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]
- decl: [rot, mat4, { call: [math.mat4_rotate, axis, { call: [math.to_radians, 90.0] }] }]

# Multiplication (model matrix = T * R * S)
- decl: [tr,  mat4, { "*": [t, r] }]
- decl: [trs, mat4, { "*": [tr, s] }]

# Camera matrices
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

# Inverse + transpose
- decl: [inv,  mat4, { call: [math.mat4_inverse, view] }]
- decl: [tran, mat4, { call: [math.mat4_transpose, proj] }]

# Transform a vector with a matrix
- decl: [world_pos, vec4, { "*": [trs, { call: [math.vec4, 0.0, 0.0, 0.0, 1.0] }] }]

# For GPU upload: as buffer<f32> (column-major, 16 values)
- decl: [buf, buffer<f32>, { call: [math.mat4_to_buffer, proj] }]
```

---

## 6. Quaternions

```aiml
# Construction
- decl: [q_id, quat, { call: [math.quat_identity] }]   # no rotation

# From axis + angle
- decl: [axis, vec3, { call: [math.vec3, 0.0, 1.0, 0.0] }]
- decl: [q, quat, { call: [math.quat_axis_angle, axis, { call: [math.to_radians, 90.0] }] }]

# From Euler angles (order: XYZ, radians)
- decl: [q2, quat, { call: [math.quat_euler, 0.0, { call: [math.to_radians, 45.0] }, 0.0] }]

# Operations
- decl: [combined, quat, { call: [math.quat_mul, q, q2] }]
- decl: [qn, quat, { call: [math.quat_normalize, combined] }]
- decl: [qi, quat, { call: [math.quat_inverse, q] }]

# Rotate a vector
- decl: [v, vec3, { call: [math.vec3, 1.0, 0.0, 0.0] }]
- decl: [rotated, vec3, { call: [math.quat_rotate_vec3, q, v] }]

# Interpolation
- decl: [lerp_q,  quat, { call: [math.quat_lerp,  q, q2, 0.5] }]  # fast, not constant angular velocity
- decl: [slerp_q, quat, { call: [math.quat_slerp, q, q2, 0.5] }]  # correct spherical interpolation

# Conversion
- decl: [rot_mat, mat4, { call: [math.quat_to_mat4, q] }]
- decl: [back, quat, { call: [math.mat4_to_quat, rot_mat] }]
```

---

## 7. Scalar Functions

```aiml
# Angles
- decl: [r, f32, { call: [math.to_radians, 180.0] }]   # → 3.14159...
- decl: [d, f32, { call: [math.to_degrees, 3.14159] }]  # → 180.0

# Trigonometry
- decl: [s, f32, { call: [math.sin, r] }]
- decl: [c, f32, { call: [math.cos, r] }]
- decl: [t, f32, { call: [math.tan, r] }]
- decl: [as, f32, { call: [math.asin, 0.5] }]
- decl: [ac, f32, { call: [math.acos, 0.5] }]
- decl: [at, f32, { call: [math.atan2, 1.0, 1.0] }]

# Power / root / log
- decl: [sq, f32, { call: [math.sqrt, 2.0] }]
- decl: [p,  f32, { call: [math.pow,  2.0, 10.0] }]    # 1024.0
- decl: [l,  f32, { call: [math.log,  2.718] }]        # ≈ 1.0
- decl: [l2, f32, { call: [math.log2, 8.0] }]          # 3.0

# Rounding
- decl: [fl, f32, { call: [math.floor, 2.7] }]         # 2.0
- decl: [cl, f32, { call: [math.ceil,  2.3] }]         # 3.0
- decl: [ro, f32, { call: [math.round, 2.5] }]         # 3.0
- decl: [fr, f32, { call: [math.fract, 2.7] }]         # 0.7

# Min / Max / Clamp
# --- 2-argument form (scalars) ---
- decl: [mn, f32, { call: [math.min,   1.0, 2.0] }]    # 1.0
- decl: [mx, f32, { call: [math.max,   1.0, 2.0] }]    # 2.0

# --- 1-argument form: list<f32> ---
- decl: [vals, list<f32>]
- call: [vals.push, 3.5]
- call: [vals.push, 1.2]
- call: [vals.push, 7.8]
- decl: [lo, f32, { call: [math.min, vals] }]   # 1.2
- decl: [hi, f32, { call: [math.max, vals] }]   # 7.8

# --- 2-argument form: raw memory (ptr: i8*, len: i32) ---
# Enables direct reduction over a float memory region without list overhead.
# (Typical use: buffer slices, GPU upload arrays, FFI data)
# - decl: [lo2, f32, { call: [math.min, data_ptr, 1024] }]
# - decl: [hi2, f32, { call: [math.max, data_ptr, 1024] }]

- decl: [cl2, f32, { call: [math.clamp, 1.5, 0.0, 1.0] }]  # 1.0
- decl: [sm, f32, { call: [math.smoothstep, 0.0, 1.0, 0.5] }]  # smoothstep

# Absolute value / sign
- decl: [ab, f32, { call: [math.abs,  -3.5] }]         # 3.5
- decl: [sg, f32, { call: [math.sign, -3.5] }]         # -1.0

# Linear interpolation (scalars)
- decl: [lp, f32, { call: [math.lerp, 0.0, 10.0, 0.3] }]   # 3.0

# Modulo (for shaders / tiling)
- decl: [md, f32, { call: [math.fmod, 7.5, 3.0] }]     # 1.5

# Random — thread-local mt19937, seeded from std::random_device on first use
- decl: [r,  f32, { call: [math.random,     0.0, 1.0] }]   # uniform in [0.0, 1.0)
- decl: [ri, i32, { call: [math.random_int, 0,   10 ] }]   # uniform integer in [0, 10)

# Secure random — cryptographically-secure bytes (OpenSSL RAND_bytes), NOT the mt19937 above.
# Returns lowercase hex of `nbytes` bytes (so 2*nbytes chars). Default 16, clamped to 1..1024.
# Use for salts, tokens, session-ids — never math.random/random_int for those.
- decl: [salt,  string, { call: [math.random_hex, 16] }]   # 32 hex chars, e.g. "80b02d85…"
- decl: [token, string, { call: [math.random_hex, 32] }]   # 64 hex chars
```

> **`math.random` / `math.random_int` are NOT cryptographically secure** (deterministic
> Mersenne-Twister). For any security-sensitive value — password salts, session tokens,
> CSRF/nonce values — use `math.random_hex`, which draws from the OS CSPRNG via OpenSSL
> `RAND_bytes`. Pair it with `hash.pbkdf2` (spec 33_1) for password storage.

---

## 8. Constants

```aiml
- decl: [pi,  f32, math.PI]    # 3.14159265...
- decl: [tau, f32, math.TAU]   # 6.28318530... (= 2π)
- decl: [e,   f32, math.E]     # 2.71828182...
- decl: [inf, f32, math.INF]   # +infinity
- decl: [nan, f32, math.NAN]   # Not a Number
```

---

## 9. Integer Vectors

```aiml
- decl: [screen, ivec2, { call: [math.ivec2, 1920, 1080] }]
- decl: [tile,   ivec3, { call: [math.ivec3, 5, 0, 3] }]

# Member access
- decl: [w, i32, screen.x]
- decl: [h, i32, screen.y]

# Arithmetic
- decl: [half, ivec2, { call: [math.idiv, screen, 2] }]   # (960, 540)
- decl: [sum,  ivec2, { call: [math.iadd, half, { call: [math.ivec2, 10, 10] }] }]

# Conversion f32 ↔ i32
- decl: [fv, vec2, { call: [math.ivec2_to_vec2, screen] }]
- decl: [iv, ivec2, { call: [math.vec2_to_ivec2, fv] }]   # floor conversion
```

---

## 10. Namespace Reference

### Constructors
| Call | Return |
|------|--------|
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

### Operator Overloads (no function call needed)
| Expression | Types | Return |
|------------|-------|--------|
| `{ +: [a, b] }` | `vec2/3/4` + same | same |
| `{ -: [a, b] }` | `vec2/3/4` + same | same |
| `{ "*": [a, b] }` | `vec2/3/4` + same | same (Hadamard) |
| `{ "*": [a, s] }` | `vec2/3/4` + `f32` | same (scale) |
| `{ /: [a, s] }` | `vec2/3/4` + `f32` | same |
| `{ -: [a] }` | `vec2/3/4` (unary) | same (negation) |
| `{ "*": [m1, m2] }` | `mat4` + `mat4` | `mat4` |
| `{ "*": [m, v] }` | `mat4` + `vec4` | `vec4` |
| `{ eq: [a, b] }` | `vec2/3/4` + same | `i8` |

Vektorgleichheit heißt `eq` (die Symbolform `==` lehnt der Compiler ab, siehe spec/04_1).
`neq` ist für Vektoren noch nicht umgesetzt („Cannot apply eq/neq to struct type without
__eq__“, gemessen 29.10.2026); Ungleichheit vorerst als `{ not: [{ eq: [a, b] }] }`.

Operands may be any expression of the type: variables, parameters, fields, builtin calls and
calls of user methods/functions returning the math type.

### Vector Functions (math namespace)
| Call | Return |
|------|--------|
| `math.dot(a, b)` | `f32` |
| `math.cross(a, b)` | `vec3` |
| `math.length(v)` | `f32` |
| `math.length_sq(v)` | `f32` |
| `math.normalize(v)` | same as input |
| `math.lerp(a, b, t)` | same as input |
| `math.distance(a, b)` | `f32` |
| `math.reflect(v, n)` | same as input |
| `math.refract(v, n, ior)` | same as input |

### Matrix Functions
| Call | Return |
|------|--------|
| `math.mat4_inverse(m)` | `mat4` |
| `math.mat4_transpose(m)` | `mat4` |
| `math.mat4_to_buffer(m)` | `buffer<f32>` (16 values, column-major) |
| `math.mat4_to_quat(m)` | `quat` |

### Quaternion Operations
| Call | Return |
|------|--------|
| `math.quat_mul(a, b)` | `quat` |
| `math.quat_normalize(q)` | `quat` |
| `math.quat_inverse(q)` | `quat` |
| `math.quat_rotate_vec3(q, v)` | `vec3` |
| `math.quat_lerp(a, b, t)` | `quat` |
| `math.quat_slerp(a, b, t)` | `quat` |
| `math.quat_to_mat4(q)` | `mat4` |

### Scalar Reductions (min/max overloads)

`math.min` and `math.max` support three signatures:

| Call | Description | Return |
|------|-------------|--------|
| `math.min(a, b)` | Smallest of two `f32` values | `f32` |
| `math.max(a, b)` | Largest of two `f32` values | `f32` |
| `math.min(lst)` | Minimum over a `list<f32>` | `f32` |
| `math.max(lst)` | Maximum over a `list<f32>` | `f32` |
| `math.min(ptr, len)` | Minimum over raw memory (`i8*`, `i32`) | `f32` |
| `math.max(ptr, len)` | Maximum over raw memory (`i8*`, `i32`) | `f32` |

The `ptr` form is intended for FFI scenarios (buffer slices, external float arrays).
For empty list / `len=0`, `0.0` is returned.

---

## 11. Implementation Notes

### 11.1 LLVM Representation

`vec3`, `vec4`, `quat` are stored as 16-byte-aligned LLVM structs `{f32, f32, f32, f32}` —
fitting directly into an SSE/NEON register. `mat4` is `{[16 x float]}`.

Since these types are value types, they are treated like user `model`s:
`resolveProperty` dispatches `v.x` → GEP index 0, `v.y` → 1, etc.

### 11.2 C Runtime

```c
// src/sys/math_runtime.h
typedef struct { float x, y, z, w; } AimlVec3; // w always 0
typedef struct { float x, y, z, w; } AimlVec4;
typedef struct { float x, y, z, w; } AimlQuat;
typedef struct { float m[16]; }      AimlMat4;  // column-major
```

Scalar functions (`sin`, `cos`, `sqrt`, ...) are mapped directly to `libm` calls
or via LLVM intrinsics (`llvm.sin.f32`) for inlineable optimization.

### 11.3 math.PI / math.TAU as Constants

`math.PI` etc. are resolved as compile-time `f32` literals — no function call,
no alloca. In the visitor: `resolveProperty("math", "PI")` → `llvm::ConstantFP::get(3.14159...)`.
