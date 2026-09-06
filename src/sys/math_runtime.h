#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Plain-C structs that mirror the LLVM struct layouts ──────────────────
 *  vec2  = {f32, f32}
 *  vec3  = {f32, f32, f32, f32}   (4th float = 0 padding, 16-byte aligned)
 *  vec4  = {f32, f32, f32, f32}
 *  quat  = {f32, f32, f32, f32}   (x, y, z, w — w=1 for identity)
 *  mat4  = {f32[16]}              (column-major)
 *  ivec2 = {i32, i32}
 *  ivec3 = {i32, i32, i32, i32}  (4th int = 0 padding)
 */
typedef struct { float x, y;           } AimlVec2;
typedef struct { float x, y, z, _w;   } AimlVec3;   /* _w always 0 for 3-D ops */
typedef struct { float x, y, z, w;    } AimlVec4;
typedef struct { float x, y, z, w;    } AimlQuat;
typedef struct { float m[16];          } AimlMat4;
typedef struct { int32_t x, y;         } AimlIVec2;
typedef struct { int32_t x, y, z, _w; } AimlIVec3;

/* AimlBuffer (matches buffer<f32> layout used by mat4_to_buffer) */
typedef struct {
    int32_t size;       /* used bytes */
    int32_t capacity;   /* allocated bytes */
    int32_t read_idx;
    int32_t write_idx;
    float*  data;
} AimlMathBuffer;

/* ── vec2 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec2_add      (const AimlVec2* a, const AimlVec2* b, AimlVec2* out);
void  aiml_math_vec2_sub      (const AimlVec2* a, const AimlVec2* b, AimlVec2* out);
void  aiml_math_vec2_mul      (const AimlVec2* a, const AimlVec2* b, AimlVec2* out);
void  aiml_math_vec2_scale    (const AimlVec2* a, float s,            AimlVec2* out);
void  aiml_math_vec2_div      (const AimlVec2* a, float s,            AimlVec2* out);
void  aiml_math_vec2_neg      (const AimlVec2* a,                     AimlVec2* out);
float aiml_math_vec2_dot      (const AimlVec2* a, const AimlVec2* b);
float aiml_math_vec2_length   (const AimlVec2* a);
float aiml_math_vec2_length_sq(const AimlVec2* a);
void  aiml_math_vec2_normalize(const AimlVec2* a,                     AimlVec2* out);
void  aiml_math_vec2_lerp     (const AimlVec2* a, const AimlVec2* b, float t, AimlVec2* out);
float aiml_math_vec2_distance (const AimlVec2* a, const AimlVec2* b);
float aiml_math_vec2_angle    (const AimlVec2* a);
void  aiml_math_vec2_perp     (const AimlVec2* a,                     AimlVec2* out);
int   aiml_math_vec2_eq       (const AimlVec2* a, const AimlVec2* b);

/* ── vec3 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec3_add      (const AimlVec3* a, const AimlVec3* b, AimlVec3* out);
void  aiml_math_vec3_sub      (const AimlVec3* a, const AimlVec3* b, AimlVec3* out);
void  aiml_math_vec3_mul      (const AimlVec3* a, const AimlVec3* b, AimlVec3* out);
void  aiml_math_vec3_scale    (const AimlVec3* a, float s,            AimlVec3* out);
void  aiml_math_vec3_div      (const AimlVec3* a, float s,            AimlVec3* out);
void  aiml_math_vec3_neg      (const AimlVec3* a,                     AimlVec3* out);
float aiml_math_vec3_dot      (const AimlVec3* a, const AimlVec3* b);
void  aiml_math_vec3_cross    (const AimlVec3* a, const AimlVec3* b, AimlVec3* out);
float aiml_math_vec3_length   (const AimlVec3* a);
float aiml_math_vec3_length_sq(const AimlVec3* a);
void  aiml_math_vec3_normalize(const AimlVec3* a,                     AimlVec3* out);
void  aiml_math_vec3_lerp     (const AimlVec3* a, const AimlVec3* b, float t, AimlVec3* out);
float aiml_math_vec3_distance (const AimlVec3* a, const AimlVec3* b);
void  aiml_math_vec3_reflect  (const AimlVec3* v, const AimlVec3* n, AimlVec3* out);
void  aiml_math_vec3_refract  (const AimlVec3* v, const AimlVec3* n, float ior, AimlVec3* out);
int   aiml_math_vec3_eq       (const AimlVec3* a, const AimlVec3* b);

/* ── vec4 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec4_add      (const AimlVec4* a, const AimlVec4* b, AimlVec4* out);
void  aiml_math_vec4_sub      (const AimlVec4* a, const AimlVec4* b, AimlVec4* out);
void  aiml_math_vec4_mul      (const AimlVec4* a, const AimlVec4* b, AimlVec4* out);
void  aiml_math_vec4_scale    (const AimlVec4* a, float s,            AimlVec4* out);
void  aiml_math_vec4_div      (const AimlVec4* a, float s,            AimlVec4* out);
void  aiml_math_vec4_neg      (const AimlVec4* a,                     AimlVec4* out);
void  aiml_math_vec4_lerp     (const AimlVec4* a, const AimlVec4* b, float t, AimlVec4* out);
int   aiml_math_vec4_eq       (const AimlVec4* a, const AimlVec4* b);

/* ── quat ─────────────────────────────────────────────────────────────── */
void  aiml_math_quat_identity     (AimlQuat* out);
void  aiml_math_quat_axis_angle   (const AimlVec3* axis, float angle, AimlQuat* out);
void  aiml_math_quat_euler        (float ex, float ey, float ez,       AimlQuat* out);
void  aiml_math_quat_mul          (const AimlQuat* a, const AimlQuat* b, AimlQuat* out);
void  aiml_math_quat_normalize    (const AimlQuat* q,                    AimlQuat* out);
void  aiml_math_quat_inverse      (const AimlQuat* q,                    AimlQuat* out);
void  aiml_math_quat_rotate_vec3  (const AimlQuat* q, const AimlVec3* v, AimlVec3* out);
void  aiml_math_quat_lerp         (const AimlQuat* a, const AimlQuat* b, float t, AimlQuat* out);
void  aiml_math_quat_slerp        (const AimlQuat* a, const AimlQuat* b, float t, AimlQuat* out);
void  aiml_math_quat_to_mat4      (const AimlQuat* q,                    AimlMat4* out);
void  aiml_math_mat4_to_quat      (const AimlMat4* m,                    AimlQuat* out);

/* ── mat4 ─────────────────────────────────────────────────────────────── */
void  aiml_math_mat4_identity    (AimlMat4* out);
void  aiml_math_mat4_mul         (const AimlMat4* a, const AimlMat4* b,   AimlMat4* out);
void  aiml_math_mat4_mul_vec4    (const AimlMat4* m, const AimlVec4* v,   AimlVec4* out);
void  aiml_math_mat4_translate   (const AimlVec3* v,                       AimlMat4* out);
void  aiml_math_mat4_rotate      (const AimlVec3* axis, float angle,       AimlMat4* out);
void  aiml_math_mat4_rotate_x    (float angle,                             AimlMat4* out);
void  aiml_math_mat4_rotate_y    (float angle,                             AimlMat4* out);
void  aiml_math_mat4_rotate_z    (float angle,                             AimlMat4* out);
void  aiml_math_mat4_scale       (const AimlVec3* v,                       AimlMat4* out);
void  aiml_math_mat4_perspective (float fov, float aspect, float nearZ, float farZ, AimlMat4* out);
void  aiml_math_mat4_ortho       (float l, float r, float b, float t, float nearZ, float farZ, AimlMat4* out);
void  aiml_math_mat4_lookat      (const AimlVec3* eye, const AimlVec3* center, const AimlVec3* up, AimlMat4* out);
void  aiml_math_mat4_inverse     (const AimlMat4* m,  AimlMat4* out);
void  aiml_math_mat4_transpose   (const AimlMat4* m,  AimlMat4* out);
void* aiml_math_mat4_to_buffer   (const AimlMat4* m);   /* returns heap-alloc'd AimlMathBuffer* */
float aiml_math_mat4_get         (const AimlMat4* m, int row, int col);
void  aiml_math_mat4_set         (AimlMat4* m, int row, int col, float val);

/* ── ivec ─────────────────────────────────────────────────────────────── */
void aiml_math_ivec2_add          (const AimlIVec2* a, const AimlIVec2* b, AimlIVec2* out);
void aiml_math_ivec2_div          (const AimlIVec2* a, int32_t s,           AimlIVec2* out);
void aiml_math_ivec2_to_vec2      (const AimlIVec2* a,                      AimlVec2*  out);
void aiml_math_vec2_to_ivec2      (const AimlVec2*  a,                      AimlIVec2* out);
void aiml_math_ivec3_add          (const AimlIVec3* a, const AimlIVec3* b, AimlIVec3* out);

/* ── list / raw-array reductions ──────────────────────────────────────── */
float aiml_math_fmin_list(const float* data, int32_t len);
float aiml_math_fmax_list(const float* data, int32_t len);

/* ── random ───────────────────────────────────────────────────────────── */
/* math.random(lo, hi) -> f32 — uniform in [lo, hi).
 * math.random_int(lo, hi) -> i32 — uniform integer in [lo, hi).
 * Thread-safe (thread_local mt19937, seeded from random_device on first use).
 */
float   aiml_math_random_f32(float lo, float hi);
int32_t aiml_math_random_int(int32_t lo, int32_t hi);
char*   aiml_math_random_hex(int32_t nbytes);   /* CSPRNG hex (RAND_bytes); refcounted result */

#ifdef __cplusplus
}
#endif
