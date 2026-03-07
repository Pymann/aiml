#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AimlBuffer mirrors the standard AIML buffer layout:
 *   { i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data }
 */
typedef struct {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
} AimlGltfBuffer;

/*
 * AimlListRaw mirrors the AIML list layout used by io/db:
 *   { i32 size, i32 capacity, void** data }
 */
typedef struct {
    int32_t  size;
    int32_t  capacity;
    void**   data;
} AimlGltfListRaw;

/* ── Load / free ─────────────────────────────────────────────────────────── */

/* Returns AimlGltfScene* (opaque). Always non-NULL; check valid(). */
void* aiml_gltf_load(const char* path);

/* gltf_scene methods */
int32_t     aiml_gltf_scene_valid(void* scene);
const char* aiml_gltf_scene_error(void* scene);
void        aiml_gltf_scene_free(void* scene);

/* list fills: fill an AimlGltfListRaw with handle pointers */
void aiml_gltf_scene_nodes_fill      (void* scene, void* list_ptr);
void aiml_gltf_scene_meshes_fill     (void* scene, void* list_ptr);
void aiml_gltf_scene_materials_fill  (void* scene, void* list_ptr);
void aiml_gltf_scene_animations_fill (void* scene, void* list_ptr);
void aiml_gltf_scene_skins_fill      (void* scene, void* list_ptr);
void aiml_gltf_scene_images_fill     (void* scene, void* list_ptr);

/* ── gltf_node ────────────────────────────────────────────────────────────── */

const char* aiml_gltf_node_name           (void* node);
float       aiml_gltf_node_translation_x  (void* node);
float       aiml_gltf_node_translation_y  (void* node);
float       aiml_gltf_node_translation_z  (void* node);
float       aiml_gltf_node_rotation_x     (void* node);
float       aiml_gltf_node_rotation_y     (void* node);
float       aiml_gltf_node_rotation_z     (void* node);
float       aiml_gltf_node_rotation_w     (void* node);
float       aiml_gltf_node_scale_x        (void* node);
float       aiml_gltf_node_scale_y        (void* node);
float       aiml_gltf_node_scale_z        (void* node);
void*       aiml_gltf_node_mesh           (void* node);   /* gltf_mesh* or NULL */
void*       aiml_gltf_node_skin           (void* node);   /* gltf_skin* or NULL */
void        aiml_gltf_node_children_fill  (void* node, void* list_ptr);
/* Returns heap-alloc'd AimlGltfBuffer* (16 f32 column-major matrix) */
void*       aiml_gltf_node_world_matrix   (void* node);

/* ── gltf_mesh ────────────────────────────────────────────────────────────── */

const char* aiml_gltf_mesh_name            (void* mesh);
int32_t     aiml_gltf_mesh_primitive_count (void* mesh);
void*       aiml_gltf_mesh_primitive       (void* mesh, int32_t idx); /* gltf_primitive* */

/* ── gltf_primitive ──────────────────────────────────────────────────────── */

/* All vertex accessor functions return heap-alloc'd AimlGltfBuffer* */
void* aiml_gltf_prim_positions   (void* prim);              /* xyz per vertex → f32 */
void* aiml_gltf_prim_normals     (void* prim);              /* xyz per vertex → f32 */
void* aiml_gltf_prim_tangents    (void* prim);              /* xyzw per vertex → f32 */
void* aiml_gltf_prim_uvs         (void* prim, int32_t set); /* uv per vertex → f32  */
void* aiml_gltf_prim_colors      (void* prim, int32_t set); /* rgba per vertex → f32 */
void* aiml_gltf_prim_joints      (void* prim, int32_t set); /* 4 per vertex → u16   */
void* aiml_gltf_prim_weights     (void* prim, int32_t set); /* 4 per vertex → f32   */
void* aiml_gltf_prim_indices     (void* prim);              /* indices → u32         */
int32_t aiml_gltf_prim_vertex_count (void* prim);
int32_t aiml_gltf_prim_index_count  (void* prim);
void*   aiml_gltf_prim_material     (void* prim);           /* gltf_material* or NULL */

/* ── gltf_material ───────────────────────────────────────────────────────── */

const char* aiml_gltf_mat_name                          (void* mat);
float       aiml_gltf_mat_base_color_r                  (void* mat);
float       aiml_gltf_mat_base_color_g                  (void* mat);
float       aiml_gltf_mat_base_color_b                  (void* mat);
float       aiml_gltf_mat_base_color_a                  (void* mat);
float       aiml_gltf_mat_metallic                      (void* mat);
float       aiml_gltf_mat_roughness                     (void* mat);
float       aiml_gltf_mat_emissive_r                    (void* mat);
float       aiml_gltf_mat_emissive_g                    (void* mat);
float       aiml_gltf_mat_emissive_b                    (void* mat);
void*       aiml_gltf_mat_base_color_texture            (void* mat); /* gltf_texture* or NULL */
void*       aiml_gltf_mat_normal_texture                (void* mat);
void*       aiml_gltf_mat_metallic_roughness_texture    (void* mat);
void*       aiml_gltf_mat_emissive_texture              (void* mat);
void*       aiml_gltf_mat_occlusion_texture             (void* mat);
const char* aiml_gltf_mat_alpha_mode                    (void* mat);
float       aiml_gltf_mat_alpha_cutoff                  (void* mat);
int32_t     aiml_gltf_mat_double_sided                  (void* mat);

/* ── gltf_texture ────────────────────────────────────────────────────────── */

/* gltf_texture handles are AimlGltfTexRef* (ptr + scene backref) */
void*       aiml_gltf_tex_image      (void* tex);  /* AimlGltfImageRef* or NULL */
const char* aiml_gltf_tex_wrap_s     (void* tex);
const char* aiml_gltf_tex_wrap_t     (void* tex);
const char* aiml_gltf_tex_min_filter (void* tex);
const char* aiml_gltf_tex_mag_filter (void* tex);

/* ── gltf_image ──────────────────────────────────────────────────────────── */

/* gltf_image handles are AimlGltfImageRef* (ptr + scene backref).
 * pixels_rgba returns heap-alloc'd AimlGltfBuffer* (RGBA u8 data). */
void*   aiml_gltf_img_pixels_rgba (void* img);
int32_t aiml_gltf_img_width       (void* img);
int32_t aiml_gltf_img_height      (void* img);

/* ── gltf_animation ──────────────────────────────────────────────────────── */

const char* aiml_gltf_anim_name                (void* anim);
float       aiml_gltf_anim_duration            (void* anim);
float       aiml_gltf_anim_sample_translation_x(void* anim, void* node, float t);
float       aiml_gltf_anim_sample_translation_y(void* anim, void* node, float t);
float       aiml_gltf_anim_sample_translation_z(void* anim, void* node, float t);
float       aiml_gltf_anim_sample_rotation_x   (void* anim, void* node, float t);
float       aiml_gltf_anim_sample_rotation_y   (void* anim, void* node, float t);
float       aiml_gltf_anim_sample_rotation_z   (void* anim, void* node, float t);
float       aiml_gltf_anim_sample_rotation_w   (void* anim, void* node, float t);

/* ── gltf_skin ───────────────────────────────────────────────────────────── */

int32_t aiml_gltf_skin_joint_count             (void* skin);
void*   aiml_gltf_skin_inverse_bind_matrices   (void* skin); /* AimlGltfBuffer* f32[16*joints] */
void    aiml_gltf_skin_joints_fill             (void* skin, void* list_ptr);

#ifdef __cplusplus
}
#endif
