/*
 * gltf_runtime.cpp — AIML gltf namespace runtime
 *
 * Backend: cgltf (single-header, MIT)  for parsing
 *          stb_image (single-header)    for image decoding
 *
 * All vertex-data accessors normalise to float32 / uint32 and return a
 * heap-allocated AimlGltfBuffer* that the caller (compiler stub) copies
 * into a stack-alloca via memcpy.
 */

#define CGLTF_IMPLEMENTATION
#include "../../src/vendor/cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "gltf_runtime.h"
#include "gl_runtime.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

extern "C" char* aiml_str_dup(const char* s);

/* ── Internal helpers ────────────────────────────────────────────────────── */

struct AimlGltfScene {
    cgltf_data* data;
    char*       error_msg;
};

/* Wrapper carrying an image pointer + owning scene for buffer resolution */
struct AimlGltfImageRef {
    cgltf_image* ptr;
    cgltf_data*  data;
};

/* Wrapper for gltf_texture (needs data for sampler lookup) */
struct AimlGltfTexRef {
    cgltf_texture* ptr;
    cgltf_data*    data;
};

/* Loaded scenes, for scene-local indices of raw material/texture pointers (spec/43_5 §3). */
static std::mutex             g_live_mu;
static std::vector<cgltf_data*> g_live;

static void live_add(cgltf_data* d) {
    std::lock_guard<std::mutex> lk(g_live_mu);
    g_live.push_back(d);
}
static void live_remove(cgltf_data* d) {
    std::lock_guard<std::mutex> lk(g_live_mu);
    for (size_t i = 0; i < g_live.size(); ++i)
        if (g_live[i] == d) { g_live.erase(g_live.begin() + (long)i); return; }
}
template <typename T>
static int32_t live_index(const T* elem, T* cgltf_data::*arr, cgltf_size cgltf_data::*count) {
    if (!elem) return -1;
    std::lock_guard<std::mutex> lk(g_live_mu);
    for (cgltf_data* d : g_live) {
        T* base = d->*arr;
        if (base && elem >= base && elem < base + d->*count) return (int32_t)(elem - base);
    }
    return -1;
}

/* ── Buffer helpers ──────────────────────────────────────────────────────── */

static AimlGltfBuffer* make_buffer(uint32_t byte_count) {
    AimlGltfBuffer* b = (AimlGltfBuffer*)malloc(sizeof(AimlGltfBuffer));
    b->data       = byte_count ? (uint8_t*)malloc(byte_count) : nullptr;
    b->size       = (int32_t)byte_count;
    b->capacity   = (int32_t)byte_count;
    b->read_idx   = 0;
    b->write_idx  = (int32_t)byte_count;
    return b;
}

static AimlGltfBuffer* make_empty_buffer() {
    return make_buffer(0);
}

/* Fill an AimlGltfListRaw; realloc if needed */
static void list_push_ptr(AimlGltfListRaw* lst, void* ptr) {
    if (lst->size >= lst->capacity) {
        int32_t new_cap = lst->capacity ? lst->capacity * 2 : 8;
        lst->data     = (void**)realloc(lst->data, (size_t)new_cap * sizeof(void*));
        lst->capacity = new_cap;
    }
    lst->data[lst->size++] = ptr;
}

/* ── cgltf accessor → float buffer ──────────────────────────────────────── */

/*
 * Unpack a cgltf_accessor into a freshly malloc'd float array.
 * Always returns float32 regardless of source component type.
 * Returns NULL if accessor is NULL or has no data.
 */
static AimlGltfBuffer* accessor_to_float_buf(const cgltf_accessor* acc) {
    if (!acc || acc->count == 0) return make_empty_buffer();

    int num_components = (int)cgltf_num_components(acc->type);
    uint32_t total     = (uint32_t)(acc->count * num_components);
    AimlGltfBuffer* b  = make_buffer(total * sizeof(float));
    float* dst         = (float*)b->data;

    for (cgltf_size i = 0; i < acc->count; ++i) {
        cgltf_accessor_read_float(acc, i, dst, (cgltf_size)num_components);
        dst += num_components;
    }
    return b;
}

/* Unpack accessor → uint32 buffer (for indices; normalises u8/u16/u32). */
static AimlGltfBuffer* accessor_to_u32_buf(const cgltf_accessor* acc) {
    if (!acc || acc->count == 0) return make_empty_buffer();

    uint32_t total    = (uint32_t)acc->count;
    AimlGltfBuffer* b = make_buffer(total * sizeof(uint32_t));
    uint32_t* dst     = (uint32_t*)b->data;

    for (cgltf_size i = 0; i < acc->count; ++i) {
        uint32_t idx = 0;
        cgltf_accessor_read_uint(acc, i, &idx, 1);
        dst[i] = idx;
    }
    return b;
}

/* Unpack accessor → uint16 buffer (for joint indices). */
static AimlGltfBuffer* accessor_to_u16_buf(const cgltf_accessor* acc) {
    if (!acc || acc->count == 0) return make_empty_buffer();

    int num_comp      = (int)cgltf_num_components(acc->type);
    uint32_t total    = (uint32_t)(acc->count * num_comp);
    AimlGltfBuffer* b = make_buffer(total * sizeof(uint16_t));
    uint16_t* dst     = (uint16_t*)b->data;

    for (cgltf_size i = 0; i < acc->count; ++i) {
        /* Read as uint (cgltf normalises) then truncate to u16 */
        uint32_t tmp[4] = {};
        cgltf_accessor_read_uint(acc, i, tmp, (cgltf_size)num_comp);
        for (int c = 0; c < num_comp; ++c)
            dst[i * num_comp + c] = (uint16_t)tmp[c];
    }
    return b;
}

/* ── find accessors by attribute name ───────────────────────────────────── */

static const cgltf_accessor* find_attr(const cgltf_primitive* prim,
                                       cgltf_attribute_type type, int idx) {
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        auto& a = prim->attributes[i];
        if (a.type == type && a.index == idx)
            return a.data;
    }
    return nullptr;
}

/* ── Sampler constant → string ───────────────────────────────────────────── */

static const char* wrap_str(int w) {
    switch (w) {
        case 33071: return "CLAMP_TO_EDGE";
        case 33648: return "MIRRORED_REPEAT";
        default:    return "REPEAT";
    }
}

static const char* filter_str(int f) {
    switch (f) {
        case 9728: return "NEAREST";
        case 9729: return "LINEAR";
        case 9984: return "NEAREST_MIPMAP_NEAREST";
        case 9985: return "LINEAR_MIPMAP_NEAREST";
        case 9986: return "NEAREST_MIPMAP_LINEAR";
        case 9987: return "LINEAR_MIPMAP_LINEAR";
        default:   return "";   /* undefined: the viewer chooses (glTF 2.0 §5.26) */
    }
}

/* ── Node world matrix (TRS → 4x4 column-major) ─────────────────────────── */

static void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mat4_mul(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += a[k*4+r] * b[c*4+k];
            tmp[c*4+r] = s;
        }
    memcpy(out, tmp, 16 * sizeof(float));
}

static void node_local_matrix(const cgltf_node* n, float* m) {
    if (n->has_matrix) {
        memcpy(m, n->matrix, 16 * sizeof(float));
        return;
    }
    mat4_identity(m);
    /* Translation */
    if (n->has_translation) {
        m[12] = n->translation[0];
        m[13] = n->translation[1];
        m[14] = n->translation[2];
    }
    /* Rotation (quaternion → mat3 embedded in mat4) */
    if (n->has_rotation) {
        float x = n->rotation[0], y = n->rotation[1],
              z = n->rotation[2], w = n->rotation[3];
        m[0]  = 1 - 2*(y*y + z*z);
        m[1]  =     2*(x*y + w*z);
        m[2]  =     2*(x*z - w*y);
        m[4]  =     2*(x*y - w*z);
        m[5]  = 1 - 2*(x*x + z*z);
        m[6]  =     2*(y*z + w*x);
        m[8]  =     2*(x*z + w*y);
        m[9]  =     2*(y*z - w*x);
        m[10] = 1 - 2*(x*x + y*y);
    }
    /* Scale */
    if (n->has_scale) {
        m[0]  *= n->scale[0];
        m[1]  *= n->scale[0];
        m[2]  *= n->scale[0];
        m[4]  *= n->scale[1];
        m[5]  *= n->scale[1];
        m[6]  *= n->scale[1];
        m[8]  *= n->scale[2];
        m[9]  *= n->scale[2];
        m[10] *= n->scale[2];
    }
}

static void node_world_matrix_impl(const cgltf_data* data,
                                   const cgltf_node* n, float* out) {
    float local[16];
    node_local_matrix(n, local);
    if (n->parent) {
        float parent[16];
        node_world_matrix_impl(data, n->parent, parent);
        mat4_mul(out, parent, local);
    } else {
        memcpy(out, local, 16 * sizeof(float));
    }
}

/* ── Animation sampling ──────────────────────────────────────────────────── */

/* ────────────────────────────────────────────────────────────────────────── */
/* Public API                                                                 */
/* ────────────────────────────────────────────────────────────────────────── */

extern "C" {

/* ── Load / free ─────────────────────────────────────────────────────────── */

void* aiml_gltf_load(const char* path) {
    AimlGltfScene* s = (AimlGltfScene*)calloc(1, sizeof(AimlGltfScene));

    cgltf_options opts = {};
    cgltf_result  rc   = cgltf_parse_file(&opts, path, &s->data);
    if (rc != cgltf_result_success) {
        char buf[256];
        snprintf(buf, sizeof(buf), "cgltf_parse_file failed: %d", (int)rc);
        s->error_msg = strdup(buf);
        return s;
    }
    rc = cgltf_load_buffers(&opts, s->data, path);
    if (rc != cgltf_result_success) {
        char buf[256];
        snprintf(buf, sizeof(buf), "cgltf_load_buffers failed: %d", (int)rc);
        s->error_msg = strdup(buf);
        cgltf_free(s->data);
        s->data = nullptr;
        return s;
    }
    live_add(s->data);
    return s;
}

int32_t aiml_gltf_scene_valid(void* scene) {
    auto* s = (AimlGltfScene*)scene;
    return (s && s->data && !s->error_msg) ? 1 : 0;
}

const char* aiml_gltf_scene_error(void* scene) {
    auto* s = (AimlGltfScene*)scene;
    return (s && s->error_msg) ? s->error_msg : "";
}

void aiml_gltf_scene_free(void* scene) {
    auto* s = (AimlGltfScene*)scene;
    if (!s) return;
    if (s->data) { live_remove(s->data); cgltf_free(s->data); }
    if (s->error_msg) free(s->error_msg);
    free(s);
}

/* ── Scene list fills ────────────────────────────────────────────────────── */

void aiml_gltf_scene_nodes_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    /* Use the first scene's root nodes if present, otherwise all nodes */
    if (s->data->scenes_count > 0) {
        auto& sc = s->data->scenes[0];
        for (cgltf_size i = 0; i < sc.nodes_count; ++i)
            list_push_ptr(lst, sc.nodes[i]);
    } else {
        for (cgltf_size i = 0; i < s->data->nodes_count; ++i)
            list_push_ptr(lst, &s->data->nodes[i]);
    }
}

void aiml_gltf_scene_meshes_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    for (cgltf_size i = 0; i < s->data->meshes_count; ++i)
        list_push_ptr(lst, &s->data->meshes[i]);
}

void aiml_gltf_scene_materials_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    for (cgltf_size i = 0; i < s->data->materials_count; ++i)
        list_push_ptr(lst, &s->data->materials[i]);
}

void aiml_gltf_scene_animations_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    for (cgltf_size i = 0; i < s->data->animations_count; ++i)
        list_push_ptr(lst, &s->data->animations[i]);
}

void aiml_gltf_scene_skins_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    for (cgltf_size i = 0; i < s->data->skins_count; ++i)
        list_push_ptr(lst, &s->data->skins[i]);
}

void aiml_gltf_scene_images_fill(void* scene, void* list_ptr) {
    auto* s   = (AimlGltfScene*)scene;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s || !s->data) return;
    for (cgltf_size i = 0; i < s->data->images_count; ++i) {
        AimlGltfImageRef* ref = (AimlGltfImageRef*)malloc(sizeof(AimlGltfImageRef));
        ref->ptr  = &s->data->images[i];
        ref->data = s->data;
        list_push_ptr(lst, ref);
    }
}

/* ── gltf_node ───────────────────────────────────────────────────────────── */

const char* aiml_gltf_node_name(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->name) ? n->name : "";
}
float aiml_gltf_node_translation_x(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_translation) ? n->translation[0] : 0.f;
}
float aiml_gltf_node_translation_y(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_translation) ? n->translation[1] : 0.f;
}
float aiml_gltf_node_translation_z(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_translation) ? n->translation[2] : 0.f;
}
float aiml_gltf_node_rotation_x(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_rotation) ? n->rotation[0] : 0.f;
}
float aiml_gltf_node_rotation_y(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_rotation) ? n->rotation[1] : 0.f;
}
float aiml_gltf_node_rotation_z(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_rotation) ? n->rotation[2] : 0.f;
}
float aiml_gltf_node_rotation_w(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_rotation) ? n->rotation[3] : 1.f;
}
float aiml_gltf_node_scale_x(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_scale) ? n->scale[0] : 1.f;
}
float aiml_gltf_node_scale_y(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_scale) ? n->scale[1] : 1.f;
}
float aiml_gltf_node_scale_z(void* node) {
    auto* n = (cgltf_node*)node;
    return (n && n->has_scale) ? n->scale[2] : 1.f;
}
void* aiml_gltf_node_mesh(void* node) {
    auto* n = (cgltf_node*)node;
    return n ? n->mesh : nullptr;
}
void* aiml_gltf_node_skin(void* node) {
    auto* n = (cgltf_node*)node;
    return n ? n->skin : nullptr;
}
void aiml_gltf_node_children_fill(void* node, void* list_ptr) {
    auto* n   = (cgltf_node*)node;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!n) return;
    for (cgltf_size i = 0; i < n->children_count; ++i)
        list_push_ptr(lst, n->children[i]);
}
void* aiml_gltf_node_local_matrix(void* node) {
    auto* n = (cgltf_node*)node;
    if (!n) return make_empty_buffer();
    AimlGltfBuffer* b = make_buffer(16 * sizeof(float));
    node_local_matrix(n, (float*)b->data);
    return b;
}
void* aiml_gltf_node_world_matrix(void* node) {
    /* We need the cgltf_data* for parent traversal — walk parent chain to root */
    auto* n = (cgltf_node*)node;
    if (!n) return make_empty_buffer();
    AimlGltfBuffer* b = make_buffer(16 * sizeof(float));
    /* cgltf provides cgltf_node_transform_world if CGLTF_IMPLEMENTATION is defined */
    cgltf_node_transform_world(n, (float*)b->data);
    return b;
}

/* ── gltf_mesh ───────────────────────────────────────────────────────────── */

const char* aiml_gltf_mesh_name(void* mesh) {
    auto* m = (cgltf_mesh*)mesh;
    return (m && m->name) ? m->name : "";
}
int32_t aiml_gltf_mesh_primitive_count(void* mesh) {
    auto* m = (cgltf_mesh*)mesh;
    return m ? (int32_t)m->primitives_count : 0;
}
void* aiml_gltf_mesh_primitive(void* mesh, int32_t idx) {
    auto* m = (cgltf_mesh*)mesh;
    if (!m || idx < 0 || (cgltf_size)idx >= m->primitives_count) return nullptr;
    return &m->primitives[idx];
}

/* ── gltf_primitive ──────────────────────────────────────────────────────── */

void* aiml_gltf_prim_positions(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_position, 0) : nullptr);
}
void* aiml_gltf_prim_normals(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_normal, 0) : nullptr);
}
void* aiml_gltf_prim_tangents(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_tangent, 0) : nullptr);
}
void* aiml_gltf_prim_uvs(void* prim, int32_t set) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_texcoord, set) : nullptr);
}
void* aiml_gltf_prim_colors(void* prim, int32_t set) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_color, set) : nullptr);
}
void* aiml_gltf_prim_joints(void* prim, int32_t set) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_u16_buf(p ? find_attr(p, cgltf_attribute_type_joints, set) : nullptr);
}
void* aiml_gltf_prim_weights(void* prim, int32_t set) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_float_buf(p ? find_attr(p, cgltf_attribute_type_weights, set) : nullptr);
}
void* aiml_gltf_prim_indices(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return accessor_to_u32_buf(p ? p->indices : nullptr);
}
int32_t aiml_gltf_prim_vertex_count(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    if (!p) return 0;
    const cgltf_accessor* pos = find_attr(p, cgltf_attribute_type_position, 0);
    return pos ? (int32_t)pos->count : 0;
}
int32_t aiml_gltf_prim_index_count(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return (p && p->indices) ? (int32_t)p->indices->count : 0;
}
void* aiml_gltf_prim_material(void* prim) {
    auto* p = (cgltf_primitive*)prim;
    return p ? p->material : nullptr;
}

/* ── gltf_material ───────────────────────────────────────────────────────── */

const char* aiml_gltf_mat_name(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->name) ? m->name : "";
}
const char* aiml_gltf_mat_extras(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->extras.data) ? m->extras.data : "";
}
float aiml_gltf_mat_base_color_r(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.base_color_factor[0] : 1.f;
}
float aiml_gltf_mat_base_color_g(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.base_color_factor[1] : 1.f;
}
float aiml_gltf_mat_base_color_b(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.base_color_factor[2] : 1.f;
}
float aiml_gltf_mat_base_color_a(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.base_color_factor[3] : 1.f;
}
float aiml_gltf_mat_metallic(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.metallic_factor : 0.f;
}
float aiml_gltf_mat_roughness(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_pbr_metallic_roughness)
        ? m->pbr_metallic_roughness.roughness_factor : 1.f;
}
float aiml_gltf_mat_emissive_r(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->emissive_factor[0] : 0.f;
}
float aiml_gltf_mat_emissive_g(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->emissive_factor[1] : 0.f;
}
float aiml_gltf_mat_emissive_b(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->emissive_factor[2] : 0.f;
}

static AimlGltfTexRef* make_tex_ref(cgltf_texture* tex, cgltf_data* data) {
    if (!tex) return nullptr;
    AimlGltfTexRef* r = (AimlGltfTexRef*)malloc(sizeof(AimlGltfTexRef));
    r->ptr  = tex;
    r->data = data;
    return r;
}

void* aiml_gltf_mat_base_color_texture(void* mat) {
    /* mat is cgltf_material*, but we need data* for tex ref creation.
     * We use a workaround: cgltf_material is owned by cgltf_data; traverse
     * parent pointer is not available. Return raw texture ptr for simplicity
     * — callers that need sampler info can still use it. */
    auto* m = (cgltf_material*)mat;
    if (!m || !m->has_pbr_metallic_roughness) return nullptr;
    return m->pbr_metallic_roughness.base_color_texture.texture;
}
void* aiml_gltf_mat_normal_texture(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->normal_texture.texture : nullptr;
}
void* aiml_gltf_mat_metallic_roughness_texture(void* mat) {
    auto* m = (cgltf_material*)mat;
    if (!m || !m->has_pbr_metallic_roughness) return nullptr;
    return m->pbr_metallic_roughness.metallic_roughness_texture.texture;
}
void* aiml_gltf_mat_emissive_texture(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->emissive_texture.texture : nullptr;
}
void* aiml_gltf_mat_occlusion_texture(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->occlusion_texture.texture : nullptr;
}

const char* aiml_gltf_mat_alpha_mode(void* mat) {
    auto* m = (cgltf_material*)mat;
    if (!m) return "OPAQUE";
    switch (m->alpha_mode) {
        case cgltf_alpha_mode_mask:  return "MASK";
        case cgltf_alpha_mode_blend: return "BLEND";
        default:                     return "OPAQUE";
    }
}
float aiml_gltf_mat_alpha_cutoff(void* mat) {
    auto* m = (cgltf_material*)mat;
    return m ? m->alpha_cutoff : 0.5f;
}
int32_t aiml_gltf_mat_double_sided(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->double_sided) ? 1 : 0;
}

/* spec/43_5 §3: textureInfo of one material binding, not the shared cgltf_texture. */
static const cgltf_texture_view* mat_slot(void* mat, const char* slot) {
    auto* m = (cgltf_material*)mat;
    if (!m || !slot) return nullptr;
    const cgltf_texture_view* v = nullptr;
    if      (!strcmp(slot, "base_color"))         v = &m->pbr_metallic_roughness.base_color_texture;
    else if (!strcmp(slot, "metallic_roughness")) v = &m->pbr_metallic_roughness.metallic_roughness_texture;
    else if (!strcmp(slot, "normal"))             v = &m->normal_texture;
    else if (!strcmp(slot, "occlusion"))          v = &m->occlusion_texture;
    else if (!strcmp(slot, "emissive"))           v = &m->emissive_texture;
    else fprintf(stderr, "gltf_material: unknown texture slot '%s'\n", slot);
    return (v && v->texture) ? v : nullptr;
}

int32_t aiml_gltf_mat_index(void* mat) {
    return live_index((const cgltf_material*)mat, &cgltf_data::materials, &cgltf_data::materials_count);
}
float aiml_gltf_mat_normal_scale(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->normal_texture.texture) ? m->normal_texture.scale : 1.0f;
}
float aiml_gltf_mat_occlusion_strength(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->occlusion_texture.texture) ? m->occlusion_texture.scale : 1.0f;
}
float aiml_gltf_mat_emissive_strength(void* mat) {
    auto* m = (cgltf_material*)mat;
    return (m && m->has_emissive_strength) ? m->emissive_strength.emissive_strength : 1.0f;
}
int32_t aiml_gltf_mat_texture_coord(void* mat, const char* slot) {
    const cgltf_texture_view* v = mat_slot(mat, slot);
    return v ? (int32_t)v->texcoord : -1;
}
int32_t aiml_gltf_mat_texture_has_transform(void* mat, const char* slot) {
    const cgltf_texture_view* v = mat_slot(mat, slot);
    return (v && v->has_transform) ? 1 : 0;
}

static void fill_names(char** names, cgltf_size n, void* list_ptr) {
    auto* lst = (AimlGltfListRaw*)list_ptr;
    for (cgltf_size i = 0; i < n; ++i) list_push_ptr(lst, aiml_str_dup(names[i] ? names[i] : ""));
}
void aiml_gltf_scene_extensions_used_fill(void* scene, void* list_ptr) {
    auto* s = (AimlGltfScene*)scene;
    if (s && s->data) fill_names(s->data->extensions_used, s->data->extensions_used_count, list_ptr);
}
void aiml_gltf_scene_extensions_required_fill(void* scene, void* list_ptr) {
    auto* s = (AimlGltfScene*)scene;
    if (s && s->data) fill_names(s->data->extensions_required, s->data->extensions_required_count, list_ptr);
}
const char* aiml_gltf_scene_extras(void* scene) {
    auto* s = (AimlGltfScene*)scene;
    return (s && s->data && s->data->extras.data) ? s->data->extras.data : "";
}

/* ── gltf_texture ────────────────────────────────────────────────────────── */

void* aiml_gltf_tex_image(void* tex) {
    /* tex = cgltf_texture* */
    auto* t = (cgltf_texture*)tex;
    if (!t || !t->image) return nullptr;
    AimlGltfImageRef* ref = (AimlGltfImageRef*)malloc(sizeof(AimlGltfImageRef));
    ref->ptr  = t->image;
    ref->data = nullptr; /* No scene backref from texture path — decode from URI if needed */
    return ref;
}

int32_t aiml_gltf_tex_image_index(void* tex) {
    auto* t = (cgltf_texture*)tex;
    return t ? live_index((const cgltf_image*)t->image, &cgltf_data::images, &cgltf_data::images_count) : -1;
}

/* spec/61 Class 4: the handle block from tex.image() is released at owner drop (null-safe). */
void aiml_gltf_image_release(void* img) {
    free(img);
}

const char* aiml_gltf_tex_wrap_s(void* tex) {
    auto* t = (cgltf_texture*)tex;
    return (t && t->sampler) ? wrap_str(t->sampler->wrap_s) : "REPEAT";
}
const char* aiml_gltf_tex_wrap_t(void* tex) {
    auto* t = (cgltf_texture*)tex;
    return (t && t->sampler) ? wrap_str(t->sampler->wrap_t) : "REPEAT";
}
const char* aiml_gltf_tex_min_filter(void* tex) {
    auto* t = (cgltf_texture*)tex;
    return (t && t->sampler) ? filter_str(t->sampler->min_filter) : "";
}
const char* aiml_gltf_tex_mag_filter(void* tex) {
    auto* t = (cgltf_texture*)tex;
    return (t && t->sampler) ? filter_str(t->sampler->mag_filter) : "";
}

/* ── gltf_image ──────────────────────────────────────────────────────────── */

void* aiml_gltf_img_pixels_rgba(void* img) {
    auto* ref = (AimlGltfImageRef*)img;
    if (!ref || !ref->ptr) return make_empty_buffer();

    cgltf_image* ci = ref->ptr;

    /* Case 1: embedded buffer view */
    if (ci->buffer_view && ci->buffer_view->buffer &&
        ci->buffer_view->buffer->data) {
        const uint8_t* raw  = (const uint8_t*)ci->buffer_view->buffer->data
                              + ci->buffer_view->offset;
        int len             = (int)ci->buffer_view->size;
        int w = 0, h = 0, ch = 0;
        stbi_uc* px = stbi_load_from_memory(raw, len, &w, &h, &ch, 4);
        if (!px) return make_empty_buffer();
        uint32_t sz = (uint32_t)(w * h * 4);
        AimlGltfBuffer* b = make_buffer(sz);
        memcpy(b->data, px, sz);
        stbi_image_free(px);
        return b;
    }

    /* Case 2: URI file path */
    if (ci->uri) {
        int w = 0, h = 0, ch = 0;
        stbi_uc* px = stbi_load(ci->uri, &w, &h, &ch, 4);
        if (!px) return make_empty_buffer();
        uint32_t sz = (uint32_t)(w * h * 4);
        AimlGltfBuffer* b = make_buffer(sz);
        memcpy(b->data, px, sz);
        stbi_image_free(px);
        return b;
    }

    return make_empty_buffer();
}

int32_t aiml_gltf_img_width(void* img) {
    auto* ref = (AimlGltfImageRef*)img;
    if (!ref || !ref->ptr) return 0;
    cgltf_image* ci = ref->ptr;
    if (ci->buffer_view && ci->buffer_view->buffer && ci->buffer_view->buffer->data) {
        const uint8_t* raw = (const uint8_t*)ci->buffer_view->buffer->data
                             + ci->buffer_view->offset;
        int w = 0, h = 0, ch = 0;
        stbi_info_from_memory(raw, (int)ci->buffer_view->size, &w, &h, &ch);
        return w;
    }
    if (ci->uri) {
        int w = 0, h = 0, ch = 0;
        stbi_info(ci->uri, &w, &h, &ch);
        return w;
    }
    return 0;
}

int32_t aiml_gltf_img_height(void* img) {
    auto* ref = (AimlGltfImageRef*)img;
    if (!ref || !ref->ptr) return 0;
    cgltf_image* ci = ref->ptr;
    if (ci->buffer_view && ci->buffer_view->buffer && ci->buffer_view->buffer->data) {
        const uint8_t* raw = (const uint8_t*)ci->buffer_view->buffer->data
                             + ci->buffer_view->offset;
        int w = 0, h = 0, ch = 0;
        stbi_info_from_memory(raw, (int)ci->buffer_view->size, &w, &h, &ch);
        return h;
    }
    if (ci->uri) {
        int w = 0, h = 0, ch = 0;
        stbi_info(ci->uri, &w, &h, &ch);
        return h;
    }
    return 0;
}

void* aiml_gltf_img_to_gl_texture(void* img) {
    auto* ref = (AimlGltfImageRef*)img;
    if (!ref || !ref->ptr) return nullptr;

    cgltf_image* ci = ref->ptr;
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = nullptr;

    if (ci->buffer_view && ci->buffer_view->buffer && ci->buffer_view->buffer->data) {
        const uint8_t* raw = (const uint8_t*)ci->buffer_view->buffer->data
                             + ci->buffer_view->offset;
        px = stbi_load_from_memory(raw, (int)ci->buffer_view->size, &w, &h, &ch, 4);
    } else if (ci->uri) {
        px = stbi_load(ci->uri, &w, &h, &ch, 4);
    }

    if (!px) return nullptr;
    void* tex = aiml_gl_create_texture_raw(w, h, px);
    stbi_image_free(px);
    return tex;
}

/* ── gltf_animation ──────────────────────────────────────────────────────── */

const char* aiml_gltf_anim_name(void* anim) {
    auto* a = (cgltf_animation*)anim;
    return (a && a->name) ? a->name : "";
}

float aiml_gltf_anim_duration(void* anim) {
    auto* a = (cgltf_animation*)anim;
    if (!a) return 0.f;
    float dur = 0.f;
    for (cgltf_size i = 0; i < a->samplers_count; ++i) {
        const cgltf_accessor* inp = a->samplers[i].input;
        if (inp && inp->count > 0) {
            float t = 0.f;
            cgltf_accessor_read_float(inp, inp->count - 1, &t, 1);
            if (t > dur) dur = t;
        }
    }
    return dur;
}

static const cgltf_animation_sampler* find_channel_sampler(
        const cgltf_animation* a, const cgltf_node* node,
        cgltf_animation_path_type path_type) {
    for (cgltf_size i = 0; i < a->channels_count; ++i) {
        auto& ch = a->channels[i];
        if (ch.target_node == node && ch.target_path == path_type && ch.sampler)
            return ch.sampler;
    }
    return nullptr;
}

/* spec/43_1 §8: value of one animated property at t; false when the clip does not animate it. */
static bool anim_sample_vec(const cgltf_animation* a, const cgltf_node* n,
                            cgltf_animation_path_type path, float t, float* out) {
    const cgltf_animation_sampler* s = find_channel_sampler(a, n, path);
    if (!s || !s->input || !s->output || s->input->count == 0) return false;
    int nc = (int)cgltf_num_components(s->output->type);
    if (nc > 4) nc = 4;
    bool cubic = s->interpolation == cgltf_interpolation_type_cubic_spline;
    cgltf_size cnt = s->input->count;
    cgltf_size have = cubic ? s->output->count / 3 : s->output->count;
    if (have < cnt) cnt = have;
    if (cnt == 0) return false;
    auto read_key = [&](cgltf_size k, float* dst) {
        cgltf_accessor_read_float(s->output, cubic ? 3 * k + 1 : k, dst, (cgltf_size)nc);
    };
    float t0 = 0.f, t1 = 0.f;
    cgltf_accessor_read_float(s->input, 0, &t0, 1);
    cgltf_accessor_read_float(s->input, cnt - 1, &t1, 1);
    if (cnt == 1 || t <= t0) { read_key(0, out); return true; }
    if (t >= t1) { read_key(cnt - 1, out); return true; }
    cgltf_size seg = 0;
    for (cgltf_size i = 0; i + 1 < cnt; ++i) {
        float ta = 0.f;
        cgltf_accessor_read_float(s->input, i, &ta, 1);
        if (ta <= t) seg = i; else break;
    }
    float ta = 0.f, tb = 0.f;
    cgltf_accessor_read_float(s->input, seg, &ta, 1);
    cgltf_accessor_read_float(s->input, seg + 1, &tb, 1);
    float va[4] = {}, vb[4] = {};
    read_key(seg, va);
    read_key(seg + 1, vb);
    if (s->interpolation == cgltf_interpolation_type_step) {
        memcpy(out, va, sizeof(float) * (size_t)nc);
        return true;
    }
    float f = (tb > ta) ? (t - ta) / (tb - ta) : 0.f;
    if (path == cgltf_animation_path_type_rotation && nc == 4) {
        double dot = 0.0;
        for (int c = 0; c < 4; ++c) dot += (double)va[c] * vb[c];
        if (dot < 0.0) { for (int c = 0; c < 4; ++c) vb[c] = -vb[c]; dot = -dot; }
        double wa = 1.0 - f, wb = f;
        if (dot < 0.9995) {
            double th = acos(dot), st = sin(th);
            wa = sin((1.0 - f) * th) / st;
            wb = sin(f * th) / st;
        }
        double len = 0.0, q[4];
        for (int c = 0; c < 4; ++c) { q[c] = wa * va[c] + wb * vb[c]; len += q[c] * q[c]; }
        len = len > 0.0 ? sqrt(len) : 1.0;
        for (int c = 0; c < 4; ++c) out[c] = (float)(q[c] / len);
        return true;
    }
    for (int c = 0; c < nc; ++c) out[c] = va[c] + f * (vb[c] - va[c]);
    return true;
}

static void node_rest_vec(const cgltf_node* n, cgltf_animation_path_type path, float* out) {
    float zero[4] = {0.f, 0.f, 0.f, 1.f}, one[3] = {1.f, 1.f, 1.f};
    if (path == cgltf_animation_path_type_translation)
        memcpy(out, n && n->has_translation ? n->translation : zero, 3 * sizeof(float));
    else if (path == cgltf_animation_path_type_rotation)
        memcpy(out, n && n->has_rotation ? n->rotation : zero, 4 * sizeof(float));
    else
        memcpy(out, n && n->has_scale ? n->scale : one, 3 * sizeof(float));
}

static float anim_component(void* anim, void* node, cgltf_animation_path_type path,
                            int comp, float t) {
    float v[4] = {0.f, 0.f, 0.f, 1.f};
    auto* a = (cgltf_animation*)anim; auto* n = (cgltf_node*)node;
    if (!a || !n || !anim_sample_vec(a, n, path, t, v)) node_rest_vec(n, path, v);
    return v[comp];
}

float aiml_gltf_anim_sample_translation_x(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_translation, 0, t);
}
float aiml_gltf_anim_sample_translation_y(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_translation, 1, t);
}
float aiml_gltf_anim_sample_translation_z(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_translation, 2, t);
}
float aiml_gltf_anim_sample_rotation_x(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_rotation, 0, t);
}
float aiml_gltf_anim_sample_rotation_y(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_rotation, 1, t);
}
float aiml_gltf_anim_sample_rotation_z(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_rotation, 2, t);
}
float aiml_gltf_anim_sample_rotation_w(void* anim, void* node, float t) {
    return anim_component(anim, node, cgltf_animation_path_type_rotation, 3, t);
}

void* aiml_gltf_anim_sample_local_matrix(void* anim, void* node, float t) {
    auto* a = (cgltf_animation*)anim; auto* n = (cgltf_node*)node;
    if (!n) return make_empty_buffer();
    AimlGltfBuffer* b = make_buffer(16 * sizeof(float));
    cgltf_node tmp = *n;
    if (!n->has_matrix) {
        const cgltf_animation_path_type paths[3] = {cgltf_animation_path_type_translation,
            cgltf_animation_path_type_rotation, cgltf_animation_path_type_scale};
        float* dst[3] = {tmp.translation, tmp.rotation, tmp.scale};
        for (int k = 0; k < 3; ++k) {
            float v[4] = {0.f, 0.f, 0.f, 1.f};
            if (!a || !anim_sample_vec(a, n, paths[k], t, v)) node_rest_vec(n, paths[k], v);
            memcpy(dst[k], v, (k == 1 ? 4 : 3) * sizeof(float));
        }
        tmp.has_translation = tmp.has_rotation = tmp.has_scale = 1;
    }
    node_local_matrix(&tmp, (float*)b->data);
    return b;
}

/* ── gltf_skin ───────────────────────────────────────────────────────────── */

int32_t aiml_gltf_skin_joint_count(void* skin) {
    auto* s = (cgltf_skin*)skin;
    return s ? (int32_t)s->joints_count : 0;
}

void* aiml_gltf_skin_inverse_bind_matrices(void* skin) {
    auto* s = (cgltf_skin*)skin;
    return accessor_to_float_buf(s ? s->inverse_bind_matrices : nullptr);
}

void aiml_gltf_skin_joints_fill(void* skin, void* list_ptr) {
    auto* s   = (cgltf_skin*)skin;
    auto* lst = (AimlGltfListRaw*)list_ptr;
    if (!s) return;
    for (cgltf_size i = 0; i < s->joints_count; ++i)
        list_push_ptr(lst, s->joints[i]);
}

/* ── prim.to_gl_mesh() ───────────────────────────────────────────────────── */

void* aiml_gltf_prim_to_gl_mesh(void* prim) {
    if (!prim) return nullptr;
    /* Extract buffers (heap-allocated AimlGltfBuffer*) */
    void* pos  = aiml_gltf_prim_positions(prim);
    void* uvs  = aiml_gltf_prim_uvs(prim, 0);
    void* norm = aiml_gltf_prim_normals(prim);
    void* idx  = aiml_gltf_prim_indices(prim);

    void* mesh = aiml_gl_create_mesh(pos, uvs, norm, idx);
    void* tan  = aiml_gltf_prim_tangents(prim);
    if (mesh && tan) aiml_gl_mesh_set_tangents(mesh, tan);
    if (tan)  { free(((AimlGltfBuffer*)tan)->data);  free(tan); }
    void* jnt = accessor_to_float_buf(find_attr((cgltf_primitive*)prim, cgltf_attribute_type_joints, 0));
    void* wgt = accessor_to_float_buf(find_attr((cgltf_primitive*)prim, cgltf_attribute_type_weights, 0));
    if (mesh && jnt && wgt) aiml_gl_mesh_set_skin(mesh, jnt, wgt);
    if (jnt) { free(((AimlGltfBuffer*)jnt)->data); free(jnt); }
    if (wgt) { free(((AimlGltfBuffer*)wgt)->data); free(wgt); }

    /* Free intermediate heap buffers — gl_create_mesh has already uploaded to GPU */
    if (pos)  { free(((AimlGltfBuffer*)pos)->data);  free(pos); }
    if (uvs)  { free(((AimlGltfBuffer*)uvs)->data);  free(uvs); }
    if (norm) { free(((AimlGltfBuffer*)norm)->data); free(norm); }
    if (idx)  { free(((AimlGltfBuffer*)idx)->data);  free(idx); }

    return mesh;
}

} /* extern "C" */
