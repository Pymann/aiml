// tensor_runtime.cpp — AIML Tensor Runtime
// Schicht 1: Lifecycle, Shape, Views
// Schicht 2: Elementwise, Matmul (OpenBLAS), Reduction, Activations, Composite
// Schicht 3: Autograd (Compute-Graph + Backward) — added in Chunk 2

#include "tensor_runtime.h"
#include <cblas.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cassert>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <openssl/evp.h>
#include <sys/mman.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// Internal struct
// ═══════════════════════════════════════════════════════════════════════════

struct AimlTensor {
    float* data;
    int*   shape;
    int    ndim;
    int    size;           // product of shape dims
    int    requires_grad;
    float* grad;           // same size as data, or nullptr
    int    grad_node_id;   // index in compute graph, -1 = leaf/no-grad
    int    owns_data;      // 1 = we own data, 0 = view (reshape/slice)
    int    is_param;       // 1 = weight/persistent tensor, survives begin_graph
    int    alloc_epoch;    // g_graph_epoch at allocation time
};

// ═══════════════════════════════════════════════════════════════════════════
// Autograd — Compute Graph (global singleton)
// ═══════════════════════════════════════════════════════════════════════════

enum OpType {
    OP_ADD, OP_SUB, OP_MUL, OP_MUL_SCALAR, OP_MATMUL,
    OP_RELU, OP_GELU, OP_TANH, OP_SOFTMAX, OP_EXP, OP_LOG,
    OP_LAYER_NORM, OP_EMBEDDING_LOOKUP, OP_CROSS_ENTROPY,
    OP_SUM, OP_MEAN, OP_NEG, OP_CONCAT, OP_FUSED_MHA,
    OP_SLICE,
    OP_WEIGHTED_CROSS_ENTROPY,
    OP_ABS,
    OP_RESHAPE,
    OP_COUNT  // sentinel — must be last
};

struct GradNode {
    OpType       op;
    AimlTensor*  inputs[5];  // fused_mha needs 5: x, wq, wk, wv, wo
    int          n_inputs;
    AimlTensor*  output;
    float        scalar_arg;   // mul_scalar: the scalar
    int          int_arg;      // sum/mean: dim; embedding: seq_len; cross_entropy: batch
    int*         int_data;     // embedding: ids copy; cross_entropy: targets copy
};

// Each thread gets its own autograd graph, no-grad flag, and tensor registry.
// This enables parallel training: workers run forward+backward on separate threads
// without corrupting each other's graphs or leaking intermediates across threads.
static thread_local std::vector<GradNode> g_graph;
static thread_local bool g_no_grad = false;

// ── Per-thread tensor lifecycle tracking ─────────────────────────────────
static thread_local std::unordered_set<AimlTensor*> g_all_tensors;
static thread_local std::vector<AimlTensor*> g_pending_free;
static thread_local int g_graph_epoch = 0;

// ── MHA scratch buffers (forward-declared here so thread_cleanup can free them)
// Defined/used in the Fused MHA section below.
static thread_local float* g_mha_Q       = nullptr;
static thread_local float* g_mha_K       = nullptr;
static thread_local float* g_mha_V       = nullptr;
static thread_local float* g_mha_scores  = nullptr;
static thread_local float* g_mha_attn    = nullptr;
static thread_local float* g_mha_cat     = nullptr;
static thread_local int    g_mha_max_seq   = 0;
static thread_local int    g_mha_max_embed = 0;

// ── Arena allocator for ephemeral tensor data ────────────────────────────
// One big malloc at init. Bump-pointer allocation, instant reset per window.
// Only used for intermediate tensor .data (is_param=0, owns_data=1).
// Param tensors (weights, adam states) still use malloc for stable lifetime.
struct TensorArena {
    float*  base;        // one big allocation (mmap'd anonymous region)
    size_t  capacity;    // total usable floats
    size_t  offset;      // next free float index (bump pointer)
    size_t  peak;        // high-water mark for diagnostics
    size_t  mmap_bytes;  // exact byte count passed to mmap (page-aligned), needed for munmap
    bool    active;      // false until first init
};
static thread_local TensorArena g_arena = {nullptr, 0, 0, 0, 0, false};

static void arena_init(size_t megabytes) {
    if (g_arena.base) {
        munmap(g_arena.base, g_arena.mmap_bytes);
        g_arena.base = nullptr;
        g_arena.mmap_bytes = 0;
    }
    size_t n_floats = (megabytes * 1024 * 1024) / sizeof(float);
    size_t n_bytes  = n_floats * sizeof(float);
    long   page     = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    size_t mmap_bytes = (n_bytes + (size_t)page - 1) & ~((size_t)page - 1);
    void* p = mmap(nullptr, mmap_bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "[ARENA] FATAL: mmap failed for %zuMB arena\n", megabytes);
        g_arena.base = nullptr;
        g_arena.active = false;
        return;
    }
    g_arena.base = (float*)p;
    g_arena.capacity = n_floats;
    g_arena.offset = 0;
    g_arena.peak = 0;
    g_arena.mmap_bytes = mmap_bytes;
    g_arena.active = true;
    fprintf(stderr, "[ARENA] initialized: %zuMB (%zu floats, mmap=%zu bytes)\n",
            megabytes, n_floats, mmap_bytes);
}

// Bump-allocate from arena. Returns nullptr if arena not active or no space.
static float* arena_alloc(size_t n_floats, bool zero) {
    if (!g_arena.active) return nullptr;
    // Align to 16 floats (64 bytes) for SIMD/cache-line alignment
    size_t aligned = (n_floats + 15) & ~(size_t)15;
    if (g_arena.offset + aligned > g_arena.capacity) return nullptr;  // fallback to malloc
    float* ptr = g_arena.base + g_arena.offset;
    g_arena.offset += aligned;
    if (g_arena.offset > g_arena.peak) g_arena.peak = g_arena.offset;
    if (zero) memset(ptr, 0, n_floats * sizeof(float));
    return ptr;
}

// Reset arena — instant free of all ephemeral data
static void arena_reset() {
    g_arena.offset = 0;
}

// Free arena — release memory back to OS (call at worker thread end).
// Uses munmap so pages are returned to the kernel immediately, bypassing
// glibc's malloc heap retention.
void aiml_tensor_arena_free() {
    if (g_arena.base) {
        munmap(g_arena.base, g_arena.mmap_bytes);
        g_arena.base = nullptr;
        g_arena.capacity = 0;
        g_arena.offset = 0;
        g_arena.peak = 0;
        g_arena.mmap_bytes = 0;
        g_arena.active = false;
    }
}

// Check if pointer is inside arena
static bool arena_owns(const float* ptr) {
    if (!g_arena.active || !ptr) return false;
    return ptr >= g_arena.base && ptr < g_arena.base + g_arena.capacity;
}

// ── Struct pool for AimlTensor headers ───────────────────────────────────
// Avoids malloc/free for the small (80-byte) AimlTensor structs.
// MUST be thread_local: concurrent worker threads call struct_alloc/struct_free
// simultaneously; a shared pool causes two threads to pop the same pointer → two
// AimlTensor objects aliasing the same struct → heap corruption → SIGABRT.
// Wrapped in an RAII holder so the pooled (logically-dead) structs are actually free()'d at thread
// exit — otherwise the pool's contents show as "definitely lost" under valgrind (the pooled struct's
// data/shape/grad were already freed by aiml_tensor_free before pooling; only the shell is retained).
struct AimlStructPool {
    std::vector<AimlTensor*> pool;
    ~AimlStructPool() { for (auto* p : pool) free(p); pool.clear(); }
};
static thread_local AimlStructPool g_struct_pool_holder;
static constexpr int STRUCT_POOL_MAX = 256;

static AimlTensor* struct_alloc() {
    auto& g_struct_pool = g_struct_pool_holder.pool;
    if (!g_struct_pool.empty()) {
        auto* t = g_struct_pool.back();
        g_struct_pool.pop_back();
        return t;
    }
    return (AimlTensor*)malloc(sizeof(AimlTensor));
}

static void struct_free(AimlTensor* t) {
    auto& g_struct_pool = g_struct_pool_holder.pool;
    if ((int)g_struct_pool.size() < STRUCT_POOL_MAX) {
        g_struct_pool.push_back(t);
    } else {
        free(t);
    }
}

// Null-check macro for tensor inputs — prints diagnostic and returns fallback
#define TENSOR_NULL_CHECK(ptr, fname, argname, fallback) \
    if (!(ptr)) { \
        fprintf(stderr, "[AIML] FATAL: %s: %s is NULL\n", (fname), (argname)); \
        return (fallback); \
    }

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static int compute_size(const int* shape, int ndim) {
    int64_t s = 1;
    for (int i = 0; i < ndim; i++) {
        if (shape[i] <= 0) return 0;
        s *= (int64_t)shape[i];
        if (s > (int64_t)INT32_MAX) return -1;  // overflow
    }
    return (int)s;
}

// is_param_hint: 0 = ephemeral (try arena), 1 = persistent (always malloc)
static AimlTensor* alloc_tensor(const int* shape, int ndim, bool zero = true, int is_param_hint = 0) {
    auto* t = struct_alloc();
    if (!t) { fprintf(stderr, "[AIML] FATAL: alloc_tensor: struct alloc failed\n"); return nullptr; }
    t->ndim = ndim;
    t->shape = (int*)malloc(ndim * sizeof(int));
    memcpy(t->shape, shape, ndim * sizeof(int));
    t->size = compute_size(shape, ndim);
    if (t->size <= 0) {
        fprintf(stderr, "[AIML] FATAL: alloc_tensor: invalid size %d (shape=[", t->size);
        for (int i = 0; i < ndim; i++) fprintf(stderr, "%s%d", i?",":"", shape[i]);
        fprintf(stderr, "])\n");
        free(t->shape);
        struct_free(t);
        return nullptr;
    }
    int n = t->size;
    // Ephemeral tensors: try arena first, fallback to malloc
    t->data = nullptr;
    if (!is_param_hint) {
        t->data = arena_alloc(n, zero);
    }
    if (t->data) {
        t->owns_data = 0;  // arena owns the memory, don't free individually
    } else {
        // Persistent tensor or arena full: use malloc
        if (zero)
            t->data = (float*)calloc(n, sizeof(float));
        else
            t->data = (float*)malloc(n * sizeof(float));
        t->owns_data = 1;
        if (!t->data) {
            fprintf(stderr, "[AIML] FATAL: alloc_tensor: malloc failed for %d floats (%zu bytes)\n",
                    t->size, (size_t)t->size * sizeof(float));
            free(t->shape);
            struct_free(t);
            return nullptr;
        }
    }
    t->requires_grad = 0;
    t->grad = nullptr;
    t->grad_node_id = -1;
    t->is_param = is_param_hint;
    t->alloc_epoch = g_graph_epoch;
    g_all_tensors.insert(t);
    return t;
}

static bool needs_grad(AimlTensor* a, AimlTensor* b = nullptr) {
    if (g_no_grad) return false;
    if (a && a->requires_grad) return true;
    if (b && b->requires_grad) return true;
    return false;
}

static void record_node(OpType op, AimlTensor* out, AimlTensor* a, AimlTensor* b = nullptr,
                         float scalar = 0.0f, int intarg = 0, int* intdata = nullptr) {
    GradNode node;
    node.op = op;
    node.inputs[0] = a;
    node.inputs[1] = b;
    node.inputs[2] = nullptr;
    node.inputs[3] = nullptr;
    node.inputs[4] = nullptr;
    node.n_inputs = b ? 2 : 1;
    node.output = out;
    node.scalar_arg = scalar;
    node.int_arg = intarg;
    node.int_data = intdata;
    out->grad_node_id = (int)g_graph.size();
    out->is_param = 0;  // operation output = ephemeral intermediate
    g_graph.push_back(node);
}

static void ensure_grad(AimlTensor* t) {
    if (!t->grad) {
        t->grad = (float*)calloc(t->size, sizeof(float));
    }
}

static void accum_grad(AimlTensor* t, const float* g, int n) {
    ensure_grad(t);
    for (int i = 0; i < n; i++) t->grad[i] += g[i];
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 1: Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void aiml_tensor_arena_init(int32_t megabytes) {
    arena_init((size_t)megabytes);
}

void* aiml_tensor_zeros(const int32_t* shape, int32_t ndim) {
    auto* t = alloc_tensor(shape, ndim, true, /*is_param_hint=*/1);
    return t;
}

void* aiml_tensor_ones(const int32_t* shape, int32_t ndim) {
    auto* t = alloc_tensor(shape, ndim, false, /*is_param_hint=*/1);
    for (int i = 0; i < t->size; i++) t->data[i] = 1.0f;
    return t;
}

void* aiml_tensor_randn(const int32_t* shape, int32_t ndim, float mean, float stddev) {
    auto* t = alloc_tensor(shape, ndim, false, /*is_param_hint=*/1);
    // Box-Muller transform
    for (int i = 0; i < t->size; i += 2) {
        float u1 = ((float)rand() / RAND_MAX);
        float u2 = ((float)rand() / RAND_MAX);
        if (u1 < 1e-10f) u1 = 1e-10f;
        float z0 = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
        float z1 = sqrtf(-2.0f * logf(u1)) * sinf(2.0f * (float)M_PI * u2);
        t->data[i] = mean + stddev * z0;
        if (i + 1 < t->size) t->data[i + 1] = mean + stddev * z1;
    }
    return t;
}

void* aiml_tensor_from_data(const float* data, const int32_t* shape, int32_t ndim) {
    auto* t = alloc_tensor(shape, ndim, false, /*is_param_hint=*/1);
    memcpy(t->data, data, t->size * sizeof(float));
    return t;
}

void* aiml_tensor_clone(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* c = alloc_tensor(t->shape, t->ndim, false, /*is_param_hint=*/t->is_param);
    memcpy(c->data, t->data, t->size * sizeof(float));
    c->requires_grad = t->requires_grad;
    return c;
}

// In-place: target[i] += src[i] * scale  (no allocation)
void aiml_tensor_fma_inplace(void* tv, void* sv, float scale) {
    auto* t = (AimlTensor*)tv;
    auto* s = (AimlTensor*)sv;
    int n = std::min(t->size, s->size);
    for (int i = 0; i < n; i++) t->data[i] += s->data[i] * scale;
}

// In-place: target[i] += src[i]^2 * scale  (no allocation)
void aiml_tensor_fma_sq_inplace(void* tv, void* sv, float scale) {
    auto* t = (AimlTensor*)tv;
    auto* s = (AimlTensor*)sv;
    int n = std::min(t->size, s->size);
    for (int i = 0; i < n; i++) t->data[i] += s->data[i] * s->data[i] * scale;
}

// In-place: target[i] += |src[i]| * scale  (no allocation)
void aiml_tensor_fma_abs_inplace(void* tv, void* sv, float scale) {
    auto* t = (AimlTensor*)tv;
    auto* s = (AimlTensor*)sv;
    int n = std::min(t->size, s->size);
    for (int i = 0; i < n; i++) t->data[i] += fabsf(s->data[i]) * scale;
}

extern "C" void* aiml_tensor_grad_view(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* v = struct_alloc();
    memset(v, 0, sizeof(AimlTensor));
    v->data          = t->data;                              // shared, kein memcpy
    v->ndim          = t->ndim;
    v->size          = t->size;
    v->shape         = (int*)malloc(t->ndim * sizeof(int)); // eigene Shape-Kopie
    memcpy(v->shape, t->shape, t->ndim * sizeof(int));
    v->owns_data     = 0;                                    // data NICHT freigeben
    v->requires_grad = 1;
    v->is_param      = 1;                                    // überlebt begin_graph
    v->grad          = (float*)calloc(t->size, sizeof(float));
    v->grad_node_id  = -1;
    v->alloc_epoch   = g_graph_epoch;
    g_all_tensors.insert(v);
    return v;
}

void aiml_tensor_free(void* tv) {
    if (!tv) return;
    auto* t = (AimlTensor*)tv;
    g_all_tensors.erase(t);
    // Arena data is freed in bulk by arena_reset — don't free individually
    if (t->owns_data && t->data && !arena_owns(t->data)) free(t->data);
    if (t->shape) free(t->shape);
    if (t->grad && !arena_owns(t->grad)) free(t->grad);
    struct_free(t);
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 1: Scalar access
// ═══════════════════════════════════════════════════════════════════════════

float aiml_tensor_get_flat(void* tv, int32_t idx) {
    auto* t = (AimlTensor*)tv;
    if (idx < 0 || idx >= t->size) return 0.0f;
    return t->data[idx];
}

void aiml_tensor_set_flat(void* tv, int32_t idx, float val) {
    auto* t = (AimlTensor*)tv;
    if (idx >= 0 && idx < t->size) t->data[idx] = val;
}

float aiml_tensor_item(void* tv) {
    auto* t = (AimlTensor*)tv;
    return (t && t->size > 0) ? t->data[0] : 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 1: Shape / metadata
// ═══════════════════════════════════════════════════════════════════════════

int32_t aiml_tensor_shape(void* tv, int32_t dim) {
    auto* t = (AimlTensor*)tv;
    if (dim < 0 || dim >= t->ndim) return 0;
    return t->shape[dim];
}

int32_t aiml_tensor_ndim(void* tv) {
    return ((AimlTensor*)tv)->ndim;
}

int32_t aiml_tensor_size(void* tv) {
    return ((AimlTensor*)tv)->size;
}

float* aiml_tensor_data_ptr(void* tv) {
    return ((AimlTensor*)tv)->data;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 1: View operations
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_reshape(void* tv, const int32_t* shape, int32_t ndim) {
    auto* src = (AimlTensor*)tv;
    int new_size = compute_size(shape, ndim);
    if (new_size != src->size) {
        fprintf(stderr, "tensor.reshape: size mismatch %d vs %d\n", src->size, new_size);
        return tv;
    }
    auto* t = (AimlTensor*)malloc(sizeof(AimlTensor));
    t->data = src->data;  // shared!
    t->ndim = ndim;
    t->shape = (int*)malloc(ndim * sizeof(int));
    memcpy(t->shape, shape, ndim * sizeof(int));
    t->size = new_size;
    t->requires_grad = src->requires_grad;
    t->grad = nullptr;
    t->grad_node_id = -1;
    t->owns_data = 0;  // view — does not own data
    t->is_param = 0;   // reshape view = ephemeral
    t->alloc_epoch = g_graph_epoch;
    g_all_tensors.insert(t);
    // Autograd: reshape is a no-op on data; grad flows through unchanged
    // (only the shape header differs).  backward_reshape just adds the
    // output's grad data into src->grad — element count is identical.
    if (needs_grad(src)) {
        record_node(OP_RESHAPE, t, src);
    }
    return t;
}

void* aiml_tensor_transpose(void* tv, int32_t d0, int32_t d1) {
    auto* src = (AimlTensor*)tv;
    if (src->ndim < 2) return aiml_tensor_clone(tv);
    if (d0 < 0 || d0 >= src->ndim || d1 < 0 || d1 >= src->ndim) return aiml_tensor_clone(tv);

    // For 2D: simple transpose
    auto* out = alloc_tensor(src->shape, src->ndim, false);
    out->is_param = 0;  // transpose output = ephemeral
    // Swap shape dims
    out->shape[d0] = src->shape[d1];
    out->shape[d1] = src->shape[d0];

    if (src->ndim == 2) {
        int rows = src->shape[0], cols = src->shape[1];
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++)
                out->data[c * rows + r] = src->data[r * cols + c];
    } else {
        // General n-dim transpose: compute strides, permute
        std::vector<int> src_strides(src->ndim), out_strides(src->ndim);
        src_strides[src->ndim - 1] = 1;
        out_strides[src->ndim - 1] = 1;
        for (int i = src->ndim - 2; i >= 0; i--) {
            src_strides[i] = src_strides[i + 1] * src->shape[i + 1];
            out_strides[i] = out_strides[i + 1] * out->shape[i + 1];
        }
        // Build permutation
        std::vector<int> perm(src->ndim);
        for (int i = 0; i < src->ndim; i++) perm[i] = i;
        std::swap(perm[d0], perm[d1]);

        for (int flat = 0; flat < src->size; flat++) {
            // Decompose flat index into src multi-index
            int remaining = flat;
            std::vector<int> idx(src->ndim);
            for (int i = 0; i < src->ndim; i++) {
                idx[i] = remaining / src_strides[i];
                remaining %= src_strides[i];
            }
            // Compute output flat index
            int out_flat = 0;
            for (int i = 0; i < src->ndim; i++) {
                out_flat += idx[perm[i]] * out_strides[i];
            }
            out->data[out_flat] = src->data[flat];
        }
    }
    return out;
}

void* aiml_tensor_slice(void* tv, int32_t dim, int32_t start, int32_t end) {
    TENSOR_NULL_CHECK(tv, "tensor.slice", "input", nullptr);
    auto* src = (AimlTensor*)tv;
    if (dim < 0 || dim >= src->ndim) return aiml_tensor_clone(tv);
    if (start < 0) start = 0;
    if (end > src->shape[dim]) end = src->shape[dim];
    int slice_len = end - start;
    if (slice_len <= 0) slice_len = 1;

    // Build output shape
    std::vector<int> out_shape(src->ndim);
    for (int i = 0; i < src->ndim; i++) out_shape[i] = src->shape[i];
    out_shape[dim] = slice_len;

    auto* out = alloc_tensor(out_shape.data(), src->ndim, false);
    out->is_param = 0;  // slice output = ephemeral

    // Compute strides
    int outer = 1, inner = 1;
    for (int i = 0; i < dim; i++) outer *= src->shape[i];
    for (int i = dim + 1; i < src->ndim; i++) inner *= src->shape[i];

    for (int o = 0; o < outer; o++) {
        for (int s = 0; s < slice_len; s++) {
            memcpy(out->data + (o * slice_len + s) * inner,
                   src->data + (o * src->shape[dim] + start + s) * inner,
                   inner * sizeof(float));
        }
    }
    if (needs_grad(src)) {
        out->requires_grad = 1;
        // int_arg = dim, scalar_arg = start (to recover offset in backward)
        record_node(OP_SLICE, out, src, nullptr, (float)start, dim);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Elementwise ops
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_add(void* av, void* bv) {
    TENSOR_NULL_CHECK(av, "tensor.add", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.add", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;
    auto* out = alloc_tensor(a->shape, a->ndim, false);
    int n = std::min(a->size, b->size);
    for (int i = 0; i < out->size; i++) out->data[i] = a->data[i % a->size] + b->data[i % b->size];
    if (needs_grad(a, b)) {
        out->requires_grad = 1;
        record_node(OP_ADD, out, a, b);
    }
    return out;
}

void* aiml_tensor_sub(void* av, void* bv) {
    TENSOR_NULL_CHECK(av, "tensor.sub", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.sub", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;
    auto* out = alloc_tensor(a->shape, a->ndim, false);
    for (int i = 0; i < out->size; i++) out->data[i] = a->data[i % a->size] - b->data[i % b->size];
    if (needs_grad(a, b)) {
        out->requires_grad = 1;
        record_node(OP_SUB, out, a, b);
    }
    return out;
}

void* aiml_tensor_mul(void* av, void* bv) {
    TENSOR_NULL_CHECK(av, "tensor.mul", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.mul", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;
    auto* out = alloc_tensor(a->shape, a->ndim, false);
    for (int i = 0; i < out->size; i++) out->data[i] = a->data[i % a->size] * b->data[i % b->size];
    if (needs_grad(a, b)) {
        out->requires_grad = 1;
        record_node(OP_MUL, out, a, b);
    }
    return out;
}

void* aiml_tensor_div(void* av, void* bv) {
    TENSOR_NULL_CHECK(av, "tensor.div", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.div", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;
    auto* out = alloc_tensor(a->shape, a->ndim, false);
    for (int i = 0; i < out->size; i++) out->data[i] = a->data[i % a->size] / b->data[i % b->size];
    // No autograd backward for div (merge-only op, no training use)
    return out;
}

void* aiml_tensor_mul_scalar(void* tv, float s) {
    TENSOR_NULL_CHECK(tv, "tensor.mul_scalar", "t", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < out->size; i++) out->data[i] = t->data[i] * s;
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_MUL_SCALAR, out, t, nullptr, s);
    }
    return out;
}

void* aiml_tensor_neg(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < out->size; i++) out->data[i] = -t->data[i];
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_NEG, out, t);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Matmul (OpenBLAS sgemm)
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_matmul(void* av, void* bv) {
    TENSOR_NULL_CHECK(av, "tensor.matmul", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.matmul", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;

    // 2D: [M x K] @ [K x N] -> [M x N]
    if (a->ndim == 2 && b->ndim == 2) {
        int M = a->shape[0], K = a->shape[1], N = b->shape[1];
        if (K != b->shape[0]) {
            fprintf(stderr, "tensor.matmul: shape mismatch [%d,%d] @ [%d,%d]\n",
                    a->shape[0], a->shape[1], b->shape[0], b->shape[1]);
            return aiml_tensor_zeros(a->shape, 2);
        }
        if (M <= 0 || K <= 0 || N <= 0) {
            fprintf(stderr, "[AIML] FATAL: tensor.matmul: zero dim M=%d K=%d N=%d\n", M, K, N);
            return nullptr;
        }
        if (!a->data || !b->data) {
            fprintf(stderr, "[AIML] FATAL: tensor.matmul: null data a=%p b=%p\n", (void*)a->data, (void*)b->data);
            return nullptr;
        }
        int out_shape[2] = {M, N};
        auto* out = alloc_tensor(out_shape, 2, false);
        if (!out || !out->data) {
            fprintf(stderr, "[AIML] FATAL: tensor.matmul: output alloc failed for [%d,%d]\n", M, N);
            return nullptr;
        }
        // C = alpha * A * B + beta * C
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    M, N, K, 1.0f,
                    a->data, K,    // A: M x K, lda = K
                    b->data, N,    // B: K x N, ldb = N
                    0.0f,
                    out->data, N); // C: M x N, ldc = N
        if (needs_grad(a, b)) {
            out->requires_grad = 1;
            record_node(OP_MATMUL, out, a, b);
        }
        return out;
    }

    // 1D dot product: [K] @ [K] -> scalar [1]
    if (a->ndim == 1 && b->ndim == 1 && a->size == b->size) {
        int out_shape[1] = {1};
        auto* out = alloc_tensor(out_shape, 1, false);
        out->data[0] = cblas_sdot(a->size, a->data, 1, b->data, 1);
        if (needs_grad(a, b)) {
            out->requires_grad = 1;
            record_node(OP_MATMUL, out, a, b);
        }
        return out;
    }

    // 3D batched: [B x M x K] @ [B x K x N] -> [B x M x N]
    if (a->ndim == 3 && b->ndim == 3 && a->shape[0] == b->shape[0]) {
        int batch = a->shape[0], M = a->shape[1], K = a->shape[2], N = b->shape[2];
        if (batch <= 0 || M <= 0 || K <= 0 || N <= 0 || !a->data || !b->data) {
            fprintf(stderr, "[AIML] FATAL: tensor.matmul 3D: invalid batch=%d M=%d K=%d N=%d a=%p b=%p\n",
                    batch, M, K, N, (void*)a->data, (void*)b->data);
            return nullptr;
        }
        int out_shape[3] = {batch, M, N};
        auto* out = alloc_tensor(out_shape, 3, false);
        if (!out || !out->data) return nullptr;
        for (int bi = 0; bi < batch; bi++) {
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                        M, N, K, 1.0f,
                        a->data + bi * M * K, K,
                        b->data + bi * K * N, N,
                        0.0f,
                        out->data + bi * M * N, N);
        }
        if (needs_grad(a, b)) {
            out->requires_grad = 1;
            record_node(OP_MATMUL, out, a, b);
        }
        return out;
    }

    // 3D x 2D broadcast: [B x M x K] @ [K x N] -> [B x M x N]
    if (a->ndim == 3 && b->ndim == 2) {
        int B = a->shape[0], M = a->shape[1], K = a->shape[2], N = b->shape[1];
        if (K != b->shape[0]) {
            fprintf(stderr, "tensor.matmul: shape mismatch [%d,%d,%d] @ [%d,%d]\n",
                    B, M, K, b->shape[0], N);
            return nullptr;
        }
        int out_shape[3] = {B, M, N};
        auto* out = alloc_tensor(out_shape, 3, false);
        if (!out || !out->data) return nullptr;
        for (int bi = 0; bi < B; bi++) {
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                        M, N, K, 1.0f,
                        a->data + bi * M * K, K,
                        b->data, N,
                        0.0f,
                        out->data + bi * M * N, N);
        }
        if (needs_grad(a, b)) {
            out->requires_grad = 1;
            record_node(OP_MATMUL, out, a, b);
        }
        return out;
    }

    fprintf(stderr, "tensor.matmul: unsupported ndim combo %d @ %d\n", a->ndim, b->ndim);
    return aiml_tensor_zeros(a->shape, a->ndim);
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Reduction
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_sum(void* tv, int32_t dim) {
    auto* t = (AimlTensor*)tv;

    if (dim == -1) {
        // Sum all elements → scalar [1]
        int out_shape[1] = {1};
        auto* out = alloc_tensor(out_shape, 1, false);
        float s = 0;
        for (int i = 0; i < t->size; i++) s += t->data[i];
        out->data[0] = s;
        if (needs_grad(t)) {
            out->requires_grad = 1;
            record_node(OP_SUM, out, t, nullptr, 0.0f, -1);
        }
        return out;
    }

    if (dim < 0 || dim >= t->ndim) return aiml_tensor_clone(tv);

    // Sum along dimension
    std::vector<int> out_shape;
    for (int i = 0; i < t->ndim; i++) {
        if (i != dim) out_shape.push_back(t->shape[i]);
    }
    if (out_shape.empty()) out_shape.push_back(1);

    auto* out = alloc_tensor(out_shape.data(), (int)out_shape.size(), true);

    int outer = 1, inner = 1;
    for (int i = 0; i < dim; i++) outer *= t->shape[i];
    for (int i = dim + 1; i < t->ndim; i++) inner *= t->shape[i];
    int dim_size = t->shape[dim];

    for (int o = 0; o < outer; o++) {
        for (int d = 0; d < dim_size; d++) {
            for (int in = 0; in < inner; in++) {
                out->data[o * inner + in] += t->data[(o * dim_size + d) * inner + in];
            }
        }
    }
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_SUM, out, t, nullptr, 0.0f, dim);
    }
    return out;
}

void* aiml_tensor_mean(void* tv, int32_t dim) {
    auto* t = (AimlTensor*)tv;

    if (dim == -1) {
        int out_shape[1] = {1};
        auto* out = alloc_tensor(out_shape, 1, false);
        float s = 0;
        for (int i = 0; i < t->size; i++) s += t->data[i];
        out->data[0] = s / (float)t->size;
        if (needs_grad(t)) {
            out->requires_grad = 1;
            record_node(OP_MEAN, out, t, nullptr, 0.0f, -1);
        }
        return out;
    }

    // Mean along dim = sum / dim_size
    void* s = aiml_tensor_sum(tv, dim);
    auto* st = (AimlTensor*)s;
    float div = (float)t->shape[dim];
    for (int i = 0; i < st->size; i++) st->data[i] /= div;
    // Note: for autograd, we record as MEAN (not SUM/div)
    if (needs_grad(t)) {
        // Replace the SUM grad node with MEAN
        if (st->grad_node_id >= 0 && st->grad_node_id < (int)g_graph.size()) {
            g_graph[st->grad_node_id].op = OP_MEAN;
        }
    }
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Unary math
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_exp(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < t->size; i++) out->data[i] = expf(t->data[i]);
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_EXP, out, t);
    }
    return out;
}

void* aiml_tensor_log_op(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < t->size; i++) out->data[i] = logf(t->data[i] + 1e-10f);
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_LOG, out, t);
    }
    return out;
}

void* aiml_tensor_tanh_op(void* tv) {
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < t->size; i++) out->data[i] = tanhf(t->data[i]);
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_TANH, out, t);
    }
    return out;
}

void* aiml_tensor_relu(void* tv) {
    TENSOR_NULL_CHECK(tv, "tensor.relu", "t", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    for (int i = 0; i < t->size; i++) out->data[i] = t->data[i] > 0.0f ? t->data[i] : 0.0f;
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_RELU, out, t);
    }
    return out;
}

void* aiml_tensor_gelu(void* tv) {
    TENSOR_NULL_CHECK(tv, "tensor.gelu", "t", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);
    // GELU approximation: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
    const float c = 0.7978845608f; // sqrt(2/pi)
    for (int i = 0; i < t->size; i++) {
        float x = t->data[i];
        float inner = c * (x + 0.044715f * x * x * x);
        out->data[i] = 0.5f * x * (1.0f + tanhf(inner));
    }
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_GELU, out, t);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Composite ops
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_softmax(void* tv, int32_t dim) {
    TENSOR_NULL_CHECK(tv, "tensor.softmax", "t", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim, false);

    if (dim == -1 || t->ndim == 1) {
        // Softmax over entire tensor / 1D
        float mx = t->data[0];
        for (int i = 1; i < t->size; i++) if (t->data[i] > mx) mx = t->data[i];
        float sum = 0;
        for (int i = 0; i < t->size; i++) {
            out->data[i] = expf(t->data[i] - mx);
            sum += out->data[i];
        }
        for (int i = 0; i < t->size; i++) out->data[i] /= sum;
    } else if (t->ndim == 2 && dim == 1) {
        // Softmax per row: [rows x cols], softmax along cols (dim=1)
        int rows = t->shape[0], cols = t->shape[1];
        for (int r = 0; r < rows; r++) {
            float mx = t->data[r * cols];
            for (int c = 1; c < cols; c++)
                if (t->data[r * cols + c] > mx) mx = t->data[r * cols + c];
            float sum = 0;
            for (int c = 0; c < cols; c++) {
                out->data[r * cols + c] = expf(t->data[r * cols + c] - mx);
                sum += out->data[r * cols + c];
            }
            for (int c = 0; c < cols; c++) out->data[r * cols + c] /= sum;
        }
    } else {
        // Fallback: treat as flat
        float mx = t->data[0];
        for (int i = 1; i < t->size; i++) if (t->data[i] > mx) mx = t->data[i];
        float sum = 0;
        for (int i = 0; i < t->size; i++) {
            out->data[i] = expf(t->data[i] - mx);
            sum += out->data[i];
        }
        for (int i = 0; i < t->size; i++) out->data[i] /= sum;
    }

    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_SOFTMAX, out, t, nullptr, 0.0f, dim);
    }
    return out;
}

void* aiml_tensor_layer_norm(void* tv, void* gv, void* bv, float eps) {
    TENSOR_NULL_CHECK(tv, "tensor.layer_norm", "input", nullptr);
    TENSOR_NULL_CHECK(gv, "tensor.layer_norm", "gamma", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.layer_norm", "beta", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* gamma = (AimlTensor*)gv;
    auto* beta  = (AimlTensor*)bv;

    auto* out = alloc_tensor(t->shape, t->ndim, false);

    // Normalize along last dimension
    int last_dim = t->shape[t->ndim - 1];
    int batch = t->size / last_dim;

    for (int b = 0; b < batch; b++) {
        float* row = t->data + b * last_dim;
        float* orow = out->data + b * last_dim;
        // Mean
        float mean = 0;
        for (int i = 0; i < last_dim; i++) mean += row[i];
        mean /= (float)last_dim;
        // Variance
        float var = 0;
        for (int i = 0; i < last_dim; i++) {
            float d = row[i] - mean;
            var += d * d;
        }
        var /= (float)last_dim;
        float inv_std = 1.0f / sqrtf(var + eps);
        // Normalize + scale + shift
        for (int i = 0; i < last_dim; i++) {
            float normed = (row[i] - mean) * inv_std;
            orow[i] = normed * gamma->data[i] + beta->data[i];
        }
    }

    if (needs_grad(t) || needs_grad(gamma, beta)) {
        out->requires_grad = 1;
        GradNode node;
        node.op = OP_LAYER_NORM;
        node.inputs[0] = t;
        node.inputs[1] = gamma;
        node.inputs[2] = beta;
        node.inputs[3] = nullptr;
        node.n_inputs = 3;
        node.output = out;
        node.scalar_arg = eps;
        node.int_arg = 0;
        node.int_data = nullptr;
        out->grad_node_id = (int)g_graph.size();
        g_graph.push_back(node);
    }
    return out;
}

void* aiml_tensor_embedding_lookup(void* wv, void* id_tensor, int32_t seq_len) {
    TENSOR_NULL_CHECK(wv, "tensor.embedding_lookup", "weights", nullptr);
    TENSOR_NULL_CHECK(id_tensor, "tensor.embedding_lookup", "ids", nullptr);
    auto* w = (AimlTensor*)wv;
    auto* ids_t = (AimlTensor*)id_tensor;
    if (w->ndim != 2) {
        fprintf(stderr, "tensor.embedding_lookup: weight must be 2D\n");
        return wv;
    }
    int dim = w->shape[1];

    // Batched case: ids_t is [B, S] → output [B, S, E]
    if (ids_t->ndim == 2) {
        int B = ids_t->shape[0], S = ids_t->shape[1];
        int out_shape[3] = {B, S, dim};
        auto* out = alloc_tensor(out_shape, 3, false);
        int total = B * S;
        int* int_ids = (int*)malloc(total * sizeof(int));
        for (int i = 0; i < total; i++) int_ids[i] = (int)ids_t->data[i];
        for (int i = 0; i < total; i++) {
            int idx = int_ids[i];
            if (idx >= 0 && idx < w->shape[0])
                memcpy(out->data + i * dim, w->data + idx * dim, dim * sizeof(float));
            else
                memset(out->data + i * dim, 0, dim * sizeof(float));
        }
        if (needs_grad(w)) {
            out->requires_grad = 1;
            record_node(OP_EMBEDDING_LOOKUP, out, w, nullptr, 0.0f, total, int_ids);
        } else {
            free(int_ids);
        }
        return out;
    }

    // 1D case: ids_t is [S] → output [S, E]
    int out_shape[2] = {seq_len, dim};
    auto* out = alloc_tensor(out_shape, 2, false);
    int* int_ids = (int*)malloc(seq_len * sizeof(int));
    for (int s = 0; s < seq_len; s++) int_ids[s] = (int)ids_t->data[s];
    for (int s = 0; s < seq_len; s++) {
        int idx = int_ids[s];
        if (idx >= 0 && idx < w->shape[0])
            memcpy(out->data + s * dim, w->data + idx * dim, dim * sizeof(float));
        else
            memset(out->data + s * dim, 0, dim * sizeof(float));
    }
    if (needs_grad(w)) {
        out->requires_grad = 1;
        record_node(OP_EMBEDDING_LOOKUP, out, w, nullptr, 0.0f, seq_len, int_ids);
    } else {
        free(int_ids);
    }
    return out;
}

void* aiml_tensor_cross_entropy(void* lv, void* target_tensor, int32_t batch_size) {
    TENSOR_NULL_CHECK(lv, "tensor.cross_entropy", "logits", nullptr);
    TENSOR_NULL_CHECK(target_tensor, "tensor.cross_entropy", "targets", nullptr);
    auto* logits = (AimlTensor*)lv;
    auto* tgt_t = (AimlTensor*)target_tensor;
    // logits: [batch x num_classes], tgt_t: 1D float tensor with float-encoded target IDs
    if (logits->ndim != 2) {
        fprintf(stderr, "tensor.cross_entropy: logits must be 2D\n");
        int s[1] = {1};
        return aiml_tensor_zeros(s, 1);
    }
    int classes = logits->shape[1];
    int out_shape[1] = {1};
    auto* out = alloc_tensor(out_shape, 1, false);

    // Build int targets from float tensor data
    int* int_targets = (int*)malloc(batch_size * sizeof(int));
    for (int b = 0; b < batch_size; b++) {
        int_targets[b] = (int)tgt_t->data[b];
    }

    float total_loss = 0;
    int valid_count = 0;
    for (int b = 0; b < batch_size; b++) {
        int target = int_targets[b];
        if (target < 0 || target >= classes) continue; // ignore_index: skip masked positions
        valid_count++;
        float* row = logits->data + b * classes;
        // Stable log-softmax
        float mx = row[0];
        for (int c = 1; c < classes; c++) if (row[c] > mx) mx = row[c];
        float log_sum_exp = 0;
        for (int c = 0; c < classes; c++) log_sum_exp += expf(row[c] - mx);
        log_sum_exp = mx + logf(log_sum_exp);
        total_loss += -(row[target] - log_sum_exp);
    }
    out->data[0] = (valid_count > 0) ? total_loss / (float)valid_count : 0.0f;

    if (needs_grad(logits)) {
        out->requires_grad = 1;
        record_node(OP_CROSS_ENTROPY, out, logits, nullptr, 0.0f, batch_size, int_targets);
    } else {
        free(int_targets);
    }
    return out;
}

void* aiml_tensor_weighted_cross_entropy(void* lv, void* target_tensor, void* weight_tensor, int32_t batch_size) {
    TENSOR_NULL_CHECK(lv, "tensor.weighted_cross_entropy", "logits", nullptr);
    TENSOR_NULL_CHECK(target_tensor, "tensor.weighted_cross_entropy", "targets", nullptr);
    TENSOR_NULL_CHECK(weight_tensor, "tensor.weighted_cross_entropy", "weights", nullptr);
    auto* logits = (AimlTensor*)lv;
    auto* tgt_t = (AimlTensor*)target_tensor;
    auto* wt = (AimlTensor*)weight_tensor;
    if (logits->ndim != 2) {
        fprintf(stderr, "tensor.weighted_cross_entropy: logits must be 2D\n");
        int s[1] = {1};
        return aiml_tensor_zeros(s, 1);
    }
    int classes = logits->shape[1];
    int out_shape[1] = {1};
    auto* out = alloc_tensor(out_shape, 1, false);

    int* int_targets = (int*)malloc(batch_size * sizeof(int));
    for (int b = 0; b < batch_size; b++) {
        int_targets[b] = (int)tgt_t->data[b];
    }

    float total_loss = 0;
    float Z = 0;
    for (int b = 0; b < batch_size; b++) {
        int target = int_targets[b];
        if (target < 0 || target >= classes) continue;
        float w = wt->data[b];
        Z += w;
        float* row = logits->data + b * classes;
        float mx = row[0];
        for (int c = 1; c < classes; c++) if (row[c] > mx) mx = row[c];
        float log_sum_exp = 0;
        for (int c = 0; c < classes; c++) log_sum_exp += expf(row[c] - mx);
        log_sum_exp = mx + logf(log_sum_exp);
        total_loss += w * (-(row[target] - log_sum_exp));
    }
    out->data[0] = (Z > 0) ? total_loss / Z : 0.0f;

    if (needs_grad(logits)) {
        out->requires_grad = 1;
        record_node(OP_WEIGHTED_CROSS_ENTROPY, out, logits, wt, 0.0f, batch_size, int_targets);
    } else {
        free(int_targets);
    }
    return out;
}

void* aiml_tensor_abs(void* tv) {
    TENSOR_NULL_CHECK(tv, "tensor.abs", "input", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim);
    for (int i = 0; i < t->size; i++) out->data[i] = fabsf(t->data[i]);
    if (needs_grad(t)) {
        out->requires_grad = 1;
        record_node(OP_ABS, out, t);
    }
    return out;
}

void* aiml_tensor_sign(void* tv) {
    TENSOR_NULL_CHECK(tv, "tensor.sign", "input", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim);
    for (int i = 0; i < t->size; i++) {
        float x = t->data[i];
        out->data[i] = (x > 0) ? 1.0f : (x < 0) ? -1.0f : 0.0f;
    }
    return out;
}

void* aiml_tensor_step(void* tv) {
    TENSOR_NULL_CHECK(tv, "tensor.step", "input", nullptr);
    auto* t = (AimlTensor*)tv;
    auto* out = alloc_tensor(t->shape, t->ndim);
    for (int i = 0; i < t->size; i++) {
        float x = t->data[i];
        out->data[i] = (x > 0) ? 1.0f : (x < 0) ? 0.0f : 0.5f;
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 2: Argmax
// ═══════════════════════════════════════════════════════════════════════════

int32_t aiml_tensor_argmax_scalar(void* tv) {
    auto* t = (AimlTensor*)tv;
    int best = 0;
    for (int i = 1; i < t->size; i++)
        if (t->data[i] > t->data[best]) best = i;
    return best;
}

void* aiml_tensor_argmax(void* tv, int32_t dim) {
    auto* t = (AimlTensor*)tv;
    if (dim == -1) {
        int s[1] = {1};
        auto* out = alloc_tensor(s, 1, false);
        out->data[0] = (float)aiml_tensor_argmax_scalar(tv);
        return out;
    }
    // Argmax along dim — similar structure to sum
    std::vector<int> out_shape;
    for (int i = 0; i < t->ndim; i++) if (i != dim) out_shape.push_back(t->shape[i]);
    if (out_shape.empty()) out_shape.push_back(1);

    auto* out = alloc_tensor(out_shape.data(), (int)out_shape.size(), false);

    int outer = 1, inner = 1;
    for (int i = 0; i < dim; i++) outer *= t->shape[i];
    for (int i = dim + 1; i < t->ndim; i++) inner *= t->shape[i];
    int dim_size = t->shape[dim];

    for (int o = 0; o < outer; o++) {
        for (int in = 0; in < inner; in++) {
            float best_val = t->data[(o * dim_size) * inner + in];
            int best_idx = 0;
            for (int d = 1; d < dim_size; d++) {
                float v = t->data[(o * dim_size + d) * inner + in];
                if (v > best_val) { best_val = v; best_idx = d; }
            }
            out->data[o * inner + in] = (float)best_idx;
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Schicht 3: Autograd
// ═══════════════════════════════════════════════════════════════════════════

void aiml_tensor_set_requires_grad(void* tv, int32_t flag) {
    auto* t = (AimlTensor*)tv;
    t->requires_grad = flag;
    if (flag) t->is_param = 1;  // weights survive begin_graph
}

int32_t aiml_tensor_requires_grad(void* tv) {
    return ((AimlTensor*)tv)->requires_grad;
}

void* aiml_tensor_grad(void* tv) {
    auto* t = (AimlTensor*)tv;
    if (!t->grad) return nullptr;
    // Return a view tensor over the grad data
    auto* g = (AimlTensor*)malloc(sizeof(AimlTensor));
    g->data = t->grad;
    g->ndim = t->ndim;
    g->shape = (int*)malloc(t->ndim * sizeof(int));
    memcpy(g->shape, t->shape, t->ndim * sizeof(int));
    g->size = t->size;
    g->requires_grad = 0;
    g->grad = nullptr;
    g->grad_node_id = -1;
    g->owns_data = 0;  // does NOT own the grad data
    g->is_param = 0;
    g_all_tensors.insert(g);
    return g;
}

void aiml_tensor_zero_grad(void* tv) {
    auto* t = (AimlTensor*)tv;
    if (t->grad) memset(t->grad, 0, t->size * sizeof(float));
}

void aiml_tensor_begin_graph(void) {
    // 1. Free ephemeral tensors from PREVIOUS iteration.
    //    Arena tensors: just remove tracking (data freed in bulk by arena_reset).
    //    Malloc tensors: free individually.
    int freed_count = (int)g_pending_free.size();
    size_t freed_bytes = 0;
    for (auto* t : g_pending_free) {
        freed_bytes += t->size * sizeof(float);
        if (t->grad) freed_bytes += t->size * sizeof(float);
        aiml_tensor_free(t);  // arena_owns check inside prevents double-free
    }
    g_pending_free.clear();

    // 2. Reset arena — instant bulk free of all ephemeral data
    arena_reset();

    g_graph_epoch++;

    // 3. Collect tensors for freeing at NEXT begin_graph.
    //    - is_param=0: always collect (graph outputs, slices, masks)
    //    - is_param=1 + requires_grad=0 + grad_node_id=-1 + stale epoch:
    //      user-allocated temporaries (e.g. ids_to_tensor outputs)
    int collected = 0;
    int stale = 0;
    for (auto* t : g_all_tensors) {
        if (!t->is_param) {
            g_pending_free.push_back(t);
            collected++;
        } else if (!t->requires_grad && t->grad_node_id == -1
                   && t->alloc_epoch < g_graph_epoch - 1) {
            g_pending_free.push_back(t);
            collected++;
            stale++;
        }
    }

    // Diagnostic
    static thread_local int bg_call = 0;
    bg_call++;
    if (false) {
        size_t arena_kb = g_arena.active ? (g_arena.peak * sizeof(float)) / 1024 : 0;
        fprintf(stderr, "[GC] #%d freed=%d(%.1fMB) collected=%d(stale=%d) live=%zu params=%zu arena_peak=%zuKB\n",
                bg_call, freed_count, freed_bytes / (1024.0*1024.0), collected, stale,
                g_all_tensors.size(), g_all_tensors.size() - collected, arena_kb);
    }

    // 4. Reset grad_node_id for next iteration
    for (auto* t : g_all_tensors) {
        t->grad_node_id = -1;
    }

    // 5. Free graph metadata (int_data copies).
    for (auto& n : g_graph) {
        if (n.int_data) { free(n.int_data); n.int_data = nullptr; }
    }
    g_graph.clear();
}

// Free ALL thread-local tensors and reset graph state.
// Must be called by worker threads before exit, after PS has consumed all grad pointers.
// Fixes the leak: is_param=1 clones survive begin_graph and are never freed otherwise.
void aiml_tensor_thread_cleanup(void) {
    // Free g_graph int_data
    for (auto& n : g_graph) {
        if (n.int_data) { free(n.int_data); n.int_data = nullptr; }
    }
    g_graph.clear();
    g_pending_free.clear();

    // Free every tensor tracked on this thread (both is_param=0 and is_param=1)
    std::vector<AimlTensor*> all(g_all_tensors.begin(), g_all_tensors.end());
    g_all_tensors.clear();
    for (auto* t : all) {
        if (t->owns_data && t->data && !arena_owns(t->data)) free(t->data);
        if (t->shape)                                          free(t->shape);
        if (t->grad  && !arena_owns(t->grad))                  free(t->grad);
        struct_free(t);
    }

    // Reset arena
    arena_reset();
    g_graph_epoch = 0;

    // Drain the thread-local struct pool: since pool entries are raw AimlTensor*
    // allocated with malloc, they must be freed explicitly — the vector destructor
    // only destroys the vector, not the pointed-to objects. (The holder's destructor
    // does the same at thread exit; this explicit drain covers thread-reuse resets.)
    for (auto* t : g_struct_pool_holder.pool) free(t);
    g_struct_pool_holder.pool.clear();

    // Free MHA scratch buffers. These are plain float* (no destructor) so they
    // would otherwise be leaked on thread-reuse (glibc re-initialises TLS to nullptr,
    // losing the pointer without freeing the malloc'd block).
    if (g_mha_Q)      { free(g_mha_Q);      g_mha_Q      = nullptr; }
    if (g_mha_K)      { free(g_mha_K);      g_mha_K      = nullptr; }
    if (g_mha_V)      { free(g_mha_V);      g_mha_V      = nullptr; }
    if (g_mha_scores) { free(g_mha_scores); g_mha_scores = nullptr; }
    if (g_mha_attn)   { free(g_mha_attn);   g_mha_attn   = nullptr; }
    if (g_mha_cat)    { free(g_mha_cat);    g_mha_cat    = nullptr; }
    g_mha_max_seq   = 0;
    g_mha_max_embed = 0;
}

// ── Backward implementations ────────────────────────────────────────────

// Helper: accumulate with broadcast reduce — when input is smaller than output,
// sum the output gradient along the broadcast dimensions.
static void accum_grad_broadcast(AimlTensor* input, float* og, int out_size) {
    ensure_grad(input);
    int in_size = input->size;
    if (in_size == out_size) {
        // Same shape — direct accumulation
        for (int i = 0; i < in_size; i++) input->grad[i] += og[i];
    } else {
        // Broadcast case: sum-reduce og into input->grad
        // Works for [seq,dim] + [dim] → sum over seq positions
        for (int i = 0; i < out_size; i++)
            input->grad[i % in_size] += og[i];
    }
}

static void backward_add(GradNode& n) {
    float* og = n.output->grad;
    int sz = n.output->size;
    if (n.inputs[0]->requires_grad) accum_grad_broadcast(n.inputs[0], og, sz);
    if (n.inputs[1]->requires_grad) accum_grad_broadcast(n.inputs[1], og, sz);
}

static void backward_sub(GradNode& n) {
    float* og = n.output->grad;
    int sz = n.output->size;
    if (n.inputs[0]->requires_grad) accum_grad(n.inputs[0], og, std::min(sz, n.inputs[0]->size));
    if (n.inputs[1]->requires_grad) {
        ensure_grad(n.inputs[1]);
        for (int i = 0; i < std::min(sz, n.inputs[1]->size); i++)
            n.inputs[1]->grad[i] -= og[i];
    }
}

static void backward_mul(GradNode& n) {
    float* og = n.output->grad;
    auto* a = n.inputs[0];
    auto* b = n.inputs[1];
    int sz = n.output->size;
    if (a->requires_grad) {
        ensure_grad(a);
        for (int i = 0; i < std::min(sz, a->size); i++)
            a->grad[i] += og[i] * b->data[i % b->size];
    }
    if (b->requires_grad) {
        ensure_grad(b);
        for (int i = 0; i < std::min(sz, b->size); i++)
            b->grad[i] += og[i] * a->data[i % a->size];
    }
}

static void backward_mul_scalar(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float s = n.scalar_arg;
        float* og = n.output->grad;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i] * s;
    }
}

static void backward_neg(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] -= og[i];
    }
}

// Reshape: data layout in memory is identical (same flat float* on input
// and output), only shape header differs.  Gradient propagates element-wise.
static void backward_reshape(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        if (og == nullptr) return;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i];
    }
}

static void backward_concat(GradNode& n) {
    // Backward of concat: slice the output gradient back to inputs
    float* og = n.output->grad;
    int dim = n.int_arg;
    auto* a = n.inputs[0];
    auto* b = n.inputs[1];

    if (a->ndim == 2 && dim == 1) {
        int rows = a->shape[0];
        int a_cols = a->shape[1], b_cols = b->shape[1];
        int out_cols = a_cols + b_cols;
        if (a->requires_grad) {
            ensure_grad(a);
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < a_cols; c++)
                    a->grad[r * a_cols + c] += og[r * out_cols + c];
        }
        if (b->requires_grad) {
            ensure_grad(b);
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < b_cols; c++)
                    b->grad[r * b_cols + c] += og[r * out_cols + a_cols + c];
        }
    } else {
        // dim==0 or 1D: first a->size elements go to a, rest to b
        if (a->requires_grad) accum_grad(a, og, a->size);
        if (b->requires_grad) accum_grad(b, og + a->size, b->size);
    }
}

static void backward_matmul(GradNode& n) {
    auto* a = n.inputs[0];
    auto* b = n.inputs[1];
    float* og = n.output->grad;

    if (a->ndim == 2 && b->ndim == 2) {
        int M = a->shape[0], K = a->shape[1], N = b->shape[1];
        // ∂a = og @ b^T  (M x N) @ (N x K) = (M x K)
        if (a->requires_grad) {
            ensure_grad(a);
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                        M, K, N, 1.0f, og, N, b->data, N, 1.0f, a->grad, K);
        }
        // ∂b = a^T @ og  (K x M) @ (M x N) = (K x N)
        if (b->requires_grad) {
            ensure_grad(b);
            cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                        K, N, M, 1.0f, a->data, K, og, N, 1.0f, b->grad, N);
        }
    } else if (a->ndim == 1 && b->ndim == 1) {
        // Dot product backward: ∂a = og * b, ∂b = og * a
        float g = og[0];
        if (a->requires_grad) {
            ensure_grad(a);
            for (int i = 0; i < a->size; i++) a->grad[i] += g * b->data[i];
        }
        if (b->requires_grad) {
            ensure_grad(b);
            for (int i = 0; i < b->size; i++) b->grad[i] += g * a->data[i];
        }
    } else if (a->ndim == 3 && b->ndim == 3) {
        int batch = a->shape[0], M = a->shape[1], K = a->shape[2], N = b->shape[2];
        for (int bi = 0; bi < batch; bi++) {
            if (a->requires_grad) {
                ensure_grad(a);
                cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                            M, K, N, 1.0f,
                            og + bi * M * N, N,
                            b->data + bi * K * N, N,
                            1.0f, a->grad + bi * M * K, K);
            }
            if (b->requires_grad) {
                ensure_grad(b);
                cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                            K, N, M, 1.0f,
                            a->data + bi * M * K, K,
                            og + bi * M * N, N,
                            1.0f, b->grad + bi * K * N, N);
            }
        }
    } else if (a->ndim == 3 && b->ndim == 2) {
        // [B, M, K] @ [K, N] → [B, M, N]
        // ∂a[bi] = og[bi] @ b^T   [M,N] @ [N,K] = [M,K]
        // ∂b    = sum_bi a[bi]^T @ og[bi]   [K,M] @ [M,N] = [K,N]
        int B = a->shape[0], M = a->shape[1], K = a->shape[2], N = b->shape[1];
        for (int bi = 0; bi < B; bi++) {
            if (a->requires_grad) {
                ensure_grad(a);
                cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                            M, K, N, 1.0f,
                            og + bi * M * N, N,
                            b->data, N,
                            1.0f, a->grad + bi * M * K, K);
            }
            if (b->requires_grad) {
                ensure_grad(b);
                cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                            K, N, M, 1.0f,
                            a->data + bi * M * K, K,
                            og + bi * M * N, N,
                            1.0f, b->grad, N);
            }
        }
    }
}

static void backward_relu(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        float* inp = n.inputs[0]->data;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i] * (inp[i] > 0.0f ? 1.0f : 0.0f);
    }
}

static void backward_gelu(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        float* x = n.inputs[0]->data;
        const float c = 0.7978845608f;
        for (int i = 0; i < n.inputs[0]->size; i++) {
            float xi = x[i];
            float inner = c * (xi + 0.044715f * xi * xi * xi);
            float tanh_val = tanhf(inner);
            float sech2 = 1.0f - tanh_val * tanh_val;
            float inner_deriv = c * (1.0f + 3.0f * 0.044715f * xi * xi);
            float gelu_grad = 0.5f * (1.0f + tanh_val) + 0.5f * xi * sech2 * inner_deriv;
            n.inputs[0]->grad[i] += og[i] * gelu_grad;
        }
    }
}

static void backward_tanh(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        float* out_data = n.output->data;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i] * (1.0f - out_data[i] * out_data[i]);
    }
}

static void backward_exp(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        float* out_data = n.output->data;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i] * out_data[i];
    }
}

static void backward_log(GradNode& n) {
    if (n.inputs[0]->requires_grad) {
        ensure_grad(n.inputs[0]);
        float* og = n.output->grad;
        float* inp = n.inputs[0]->data;
        for (int i = 0; i < n.inputs[0]->size; i++)
            n.inputs[0]->grad[i] += og[i] / (inp[i] + 1e-10f);
    }
}

static void backward_softmax(GradNode& n) {
    auto* inp = n.inputs[0];
    if (!inp->requires_grad) return;
    ensure_grad(inp);
    float* og = n.output->grad;
    float* sm = n.output->data;
    int dim = n.int_arg;

    if (dim == -1 || inp->ndim == 1) {
        // ∂a_i = s_i * (og_i - sum(og * s))
        float dot = 0;
        for (int i = 0; i < inp->size; i++) dot += og[i] * sm[i];
        for (int i = 0; i < inp->size; i++)
            inp->grad[i] += sm[i] * (og[i] - dot);
    } else if (inp->ndim == 2 && dim == 1) {
        int rows = inp->shape[0], cols = inp->shape[1];
        for (int r = 0; r < rows; r++) {
            float dot = 0;
            for (int c = 0; c < cols; c++)
                dot += og[r * cols + c] * sm[r * cols + c];
            for (int c = 0; c < cols; c++)
                inp->grad[r * cols + c] += sm[r * cols + c] * (og[r * cols + c] - dot);
        }
    } else {
        // Fallback: flat
        float dot = 0;
        for (int i = 0; i < inp->size; i++) dot += og[i] * sm[i];
        for (int i = 0; i < inp->size; i++)
            inp->grad[i] += sm[i] * (og[i] - dot);
    }
}

static void backward_sum(GradNode& n) {
    auto* inp = n.inputs[0];
    if (!inp->requires_grad) return;
    ensure_grad(inp);
    float* og = n.output->grad;
    int dim = n.int_arg;

    if (dim == -1) {
        // Broadcast scalar grad to all elements
        float g = og[0];
        for (int i = 0; i < inp->size; i++) inp->grad[i] += g;
    } else {
        int outer = 1, inner = 1;
        for (int i = 0; i < dim; i++) outer *= inp->shape[i];
        for (int i = dim + 1; i < inp->ndim; i++) inner *= inp->shape[i];
        int dim_size = inp->shape[dim];
        for (int o = 0; o < outer; o++)
            for (int d = 0; d < dim_size; d++)
                for (int in = 0; in < inner; in++)
                    inp->grad[(o * dim_size + d) * inner + in] += og[o * inner + in];
    }
}

static void backward_mean(GradNode& n) {
    auto* inp = n.inputs[0];
    if (!inp->requires_grad) return;
    ensure_grad(inp);
    float* og = n.output->grad;
    int dim = n.int_arg;

    if (dim == -1) {
        float g = og[0] / (float)inp->size;
        for (int i = 0; i < inp->size; i++) inp->grad[i] += g;
    } else {
        float div = (float)inp->shape[dim];
        int outer = 1, inner = 1;
        for (int i = 0; i < dim; i++) outer *= inp->shape[i];
        for (int i = dim + 1; i < inp->ndim; i++) inner *= inp->shape[i];
        int dim_size = inp->shape[dim];
        for (int o = 0; o < outer; o++)
            for (int d = 0; d < dim_size; d++)
                for (int in = 0; in < inner; in++)
                    inp->grad[(o * dim_size + d) * inner + in] += og[o * inner + in] / div;
    }
}

static void backward_layer_norm(GradNode& n) {
    auto* inp = n.inputs[0];
    auto* gamma = n.inputs[1];
    // auto* beta = n.inputs[2];  // beta grad is just og
    float eps = n.scalar_arg;
    float* og = n.output->grad;

    int last_dim = inp->shape[inp->ndim - 1];
    int batch = inp->size / last_dim;

    if (gamma->requires_grad) ensure_grad(gamma);
    if (n.inputs[2]->requires_grad) ensure_grad(n.inputs[2]);
    if (inp->requires_grad) ensure_grad(inp);

    for (int b = 0; b < batch; b++) {
        float* row = inp->data + b * last_dim;
        float* grow = og + b * last_dim;

        float mean = 0;
        for (int i = 0; i < last_dim; i++) mean += row[i];
        mean /= (float)last_dim;
        float var = 0;
        for (int i = 0; i < last_dim; i++) { float d = row[i] - mean; var += d * d; }
        var /= (float)last_dim;
        float inv_std = 1.0f / sqrtf(var + eps);

        // Compute x_hat for this row
        std::vector<float> xhat(last_dim);
        for (int i = 0; i < last_dim; i++) xhat[i] = (row[i] - mean) * inv_std;

        // gamma.grad += og * x_hat, beta.grad += og
        if (gamma->requires_grad)
            for (int i = 0; i < last_dim; i++) gamma->grad[i] += grow[i] * xhat[i];
        if (n.inputs[2]->requires_grad)
            for (int i = 0; i < last_dim; i++) n.inputs[2]->grad[i] += grow[i];

        if (inp->requires_grad) {
            // dx = gamma * inv_std * (dout - mean(dout) - xhat * mean(dout * xhat))
            float sum_dout = 0, sum_dout_xhat = 0;
            for (int i = 0; i < last_dim; i++) {
                float d = grow[i] * gamma->data[i];
                sum_dout += d;
                sum_dout_xhat += d * xhat[i];
            }
            float N_inv = 1.0f / (float)last_dim;
            for (int i = 0; i < last_dim; i++) {
                float d = grow[i] * gamma->data[i];
                inp->grad[b * last_dim + i] += inv_std * (d - N_inv * sum_dout - N_inv * xhat[i] * sum_dout_xhat);
            }
        }
    }
}

static void backward_embedding_lookup(GradNode& n) {
    auto* w = n.inputs[0];
    if (!w->requires_grad) return;
    ensure_grad(w);
    float* og = n.output->grad;
    int seq_len = n.int_arg;
    int dim = w->shape[1];
    int* ids = n.int_data;
    // scatter_add: for each token, add output grad row to weight grad row
    for (int s = 0; s < seq_len; s++) {
        int idx = ids[s];
        if (idx >= 0 && idx < w->shape[0]) {
            for (int d = 0; d < dim; d++)
                w->grad[idx * dim + d] += og[s * dim + d];
        }
    }
}

static void backward_cross_entropy(GradNode& n) {
    auto* logits = n.inputs[0];
    if (!logits->requires_grad) return;
    ensure_grad(logits);
    int batch = n.int_arg;
    int classes = logits->shape[1];
    int* targets = n.int_data;
    float* og = n.output->grad;

    // Count valid (non-masked) positions for correct gradient scaling
    int valid_count = 0;
    for (int b = 0; b < batch; b++)
        if (targets[b] >= 0 && targets[b] < classes) valid_count++;
    if (valid_count == 0) return;
    float scale = og[0] / (float)valid_count;

    // ∂logits = (softmax(logits) - one_hot(target)) / valid_count
    // Masked positions (target < 0) are skipped — no gradient contribution
    for (int b = 0; b < batch; b++) {
        int t = targets[b];
        if (t < 0 || t >= classes) continue;
        float* row = logits->data + b * classes;
        float* grow = logits->grad + b * classes;
        // Compute softmax
        float mx = row[0];
        for (int c = 1; c < classes; c++) if (row[c] > mx) mx = row[c];
        float sum = 0;
        for (int c = 0; c < classes; c++) sum += expf(row[c] - mx);
        for (int c = 0; c < classes; c++) {
            float sm = expf(row[c] - mx) / sum;
            float target_val = (c == t) ? 1.0f : 0.0f;
            grow[c] += (sm - target_val) * scale;
        }
    }
}

static void backward_fused_mha(GradNode& node);  // forward decl

static void backward_slice(GradNode& n) {
    auto* src = n.inputs[0];
    if (!src->requires_grad) return;
    ensure_grad(src);
    int dim   = n.int_arg;
    int start = (int)n.scalar_arg;
    auto* out = n.output;
    float* og = out->grad;
    int outer = 1, inner = 1;
    for (int i = 0; i < dim; i++) outer *= src->shape[i];
    for (int i = dim + 1; i < src->ndim; i++) inner *= src->shape[i];
    int slice_len = out->shape[dim];
    for (int o = 0; o < outer; o++)
        for (int s = 0; s < slice_len; s++) {
            float* dst = src->grad + (o * src->shape[dim] + start + s) * inner;
            float* src_og = og + (o * slice_len + s) * inner;
            for (int k = 0; k < inner; k++) dst[k] += src_og[k];
        }
}

static void backward_weighted_cross_entropy(GradNode& n) {
    auto* logits = n.inputs[0];
    if (!logits->requires_grad) return;
    ensure_grad(logits);
    auto* wt = (AimlTensor*)n.inputs[1];
    int batch = n.int_arg;
    int classes = logits->shape[1];
    int* targets = n.int_data;
    float* og = n.output->grad;
    float* w = wt->data;

    float Z = 0;
    for (int b = 0; b < batch; b++)
        if (targets[b] >= 0 && targets[b] < classes) Z += w[b];
    if (Z <= 0) return;

    for (int b = 0; b < batch; b++) {
        int t = targets[b];
        if (t < 0 || t >= classes) continue;
        float* row = logits->data + b * classes;
        float* grow = logits->grad + b * classes;
        float scale = og[0] * w[b] / Z;
        float mx = row[0];
        for (int c = 1; c < classes; c++) if (row[c] > mx) mx = row[c];
        float sum = 0;
        for (int c = 0; c < classes; c++) sum += expf(row[c] - mx);
        for (int c = 0; c < classes; c++) {
            float sm = expf(row[c] - mx) / sum;
            float target_val = (c == t) ? 1.0f : 0.0f;
            grow[c] += (sm - target_val) * scale;
        }
    }
}

static void backward_abs(GradNode& n) {
    auto* inp = n.inputs[0];
    if (!inp->requires_grad) return;
    ensure_grad(inp);
    float* og = n.output->grad;
    for (int i = 0; i < inp->size; i++) {
        float x = inp->data[i];
        float s = (x > 0) ? 1.0f : (x < 0) ? -1.0f : 0.0f;
        inp->grad[i] += og[i] * s;
    }
}

void aiml_tensor_backward(void* lv) {
    if (!lv) { fprintf(stderr, "[AIML] FATAL: tensor.backward: loss is NULL\n"); return; }
    auto* loss = (AimlTensor*)lv;
    // Initialize loss gradient to 1.0
    ensure_grad(loss);
    for (int i = 0; i < loss->size; i++) loss->grad[i] = 1.0f;

    // Backward dispatch table — indexed by OpType, O(1) lookup
    using BackwardFn = void(*)(GradNode&);
    static const BackwardFn dispatch[OP_COUNT] = {
        backward_add,              // OP_ADD
        backward_sub,              // OP_SUB
        backward_mul,              // OP_MUL
        backward_mul_scalar,       // OP_MUL_SCALAR
        backward_matmul,           // OP_MATMUL
        backward_relu,             // OP_RELU
        backward_gelu,             // OP_GELU
        backward_tanh,             // OP_TANH
        backward_softmax,          // OP_SOFTMAX
        backward_exp,              // OP_EXP
        backward_log,              // OP_LOG
        backward_layer_norm,       // OP_LAYER_NORM
        backward_embedding_lookup, // OP_EMBEDDING_LOOKUP
        backward_cross_entropy,    // OP_CROSS_ENTROPY
        backward_sum,              // OP_SUM
        backward_mean,             // OP_MEAN
        backward_neg,              // OP_NEG
        backward_concat,           // OP_CONCAT
        backward_fused_mha,                // OP_FUSED_MHA
        backward_slice,                    // OP_SLICE
        backward_weighted_cross_entropy,   // OP_WEIGHTED_CROSS_ENTROPY
        backward_abs,                      // OP_ABS
        backward_reshape,                  // OP_RESHAPE
    };

    // Traverse graph in reverse order
    for (int i = (int)g_graph.size() - 1; i >= 0; i--) {
        auto& node = g_graph[i];
        if (!node.output->grad) continue;
        dispatch[node.op](node);
    }
}

void aiml_tensor_no_grad_begin(void) { g_no_grad = true; }
void aiml_tensor_no_grad_end(void) { g_no_grad = false; }

// ═══════════════════════════════════════════════════════════════════════════
// Optimizer: Adam
// ═══════════════════════════════════════════════════════════════════════════

void aiml_tensor_adam_step(void* pv, void* mv, void* vv,
                           float lr, float beta1, float beta2, float eps, int32_t step) {
    auto* p = (AimlTensor*)pv;
    auto* m = (AimlTensor*)mv;
    auto* v = (AimlTensor*)vv;
    // Adam states must survive GC — refresh epoch so they're never "stale"
    m->is_param = 1;  m->alloc_epoch = g_graph_epoch;
    v->is_param = 1;  v->alloc_epoch = g_graph_epoch;
    if (!p->grad) return;

    float bc1 = 1.0f - powf(beta1, (float)step);
    float bc2 = 1.0f - powf(beta2, (float)step);

    for (int i = 0; i < p->size; i++) {
        float g = p->grad[i];
        m->data[i] = beta1 * m->data[i] + (1.0f - beta1) * g;
        v->data[i] = beta2 * v->data[i] + (1.0f - beta2) * g * g;
        float m_hat = m->data[i] / bc1;
        float v_hat = v->data[i] / bc2;
        p->data[i] -= lr * m_hat / (sqrtf(v_hat) + eps);
    }
}

// PS variant: applies Adam using an EXPLICIT grad tensor instead of param->grad.
// Used by the Parameter Server thread which receives averaged gradient tensors
// from worker threads and applies them to the master parameters.
void aiml_tensor_adam_step_with_grad(void* pv, void* mv, void* vv, void* gv,
                                      float lr, float beta1, float beta2,
                                      float eps, int32_t step) {
    auto* p = (AimlTensor*)pv;
    auto* m = (AimlTensor*)mv;
    auto* v = (AimlTensor*)vv;
    auto* g = (AimlTensor*)gv;
    if (!p || !m || !v || !g) return;
    m->is_param = 1;  m->alloc_epoch = g_graph_epoch;
    v->is_param = 1;  v->alloc_epoch = g_graph_epoch;

    float bc1 = 1.0f - powf(beta1, (float)step);
    float bc2 = 1.0f - powf(beta2, (float)step);

    for (int i = 0; i < p->size; i++) {
        float gi = g->data[i];
        m->data[i] = beta1 * m->data[i] + (1.0f - beta1) * gi;
        v->data[i] = beta2 * v->data[i] + (1.0f - beta2) * gi * gi;
        float m_hat = m->data[i] / bc1;
        float v_hat = v->data[i] / bc2;
        p->data[i] -= lr * m_hat / (sqrtf(v_hat) + eps);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Concat
// ═══════════════════════════════════════════════════════════════════════════

void* aiml_tensor_concat(void* av, void* bv, int32_t dim) {
    TENSOR_NULL_CHECK(av, "tensor.concat", "a", nullptr);
    TENSOR_NULL_CHECK(bv, "tensor.concat", "b", nullptr);
    auto* a = (AimlTensor*)av;
    auto* b = (AimlTensor*)bv;
    if (a->ndim != b->ndim) return nullptr;
    if (dim < 0) dim += a->ndim;
    if (dim < 0 || dim >= a->ndim) return nullptr;

    // Build output shape: same as a except dim is a->shape[dim] + b->shape[dim]
    int out_shape[8];
    for (int i = 0; i < a->ndim; i++) {
        if (i == dim) out_shape[i] = a->shape[i] + b->shape[i];
        else {
            if (a->shape[i] != b->shape[i]) return nullptr; // shape mismatch
            out_shape[i] = a->shape[i];
        }
    }
    auto* out = alloc_tensor(out_shape, a->ndim, false);

    // Copy data: iterate in row-major order
    // For 2D [rows, cols] concat on dim=1: row i has a_cols + b_cols elements
    if (a->ndim == 2) {
        int rows = a->shape[0];
        if (dim == 1) {
            int a_cols = a->shape[1], b_cols = b->shape[1];
            for (int r = 0; r < rows; r++) {
                memcpy(out->data + r * (a_cols + b_cols),
                       a->data + r * a_cols, a_cols * sizeof(float));
                memcpy(out->data + r * (a_cols + b_cols) + a_cols,
                       b->data + r * b_cols, b_cols * sizeof(float));
            }
        } else { // dim == 0
            memcpy(out->data, a->data, a->size * sizeof(float));
            memcpy(out->data + a->size, b->data, b->size * sizeof(float));
        }
    } else if (a->ndim == 1) {
        memcpy(out->data, a->data, a->size * sizeof(float));
        memcpy(out->data + a->size, b->data, b->size * sizeof(float));
    } else {
        // Generic N-D: compute strides and copy blocks
        // For now, use simple flat copy for dim=0 case
        if (dim == 0) {
            memcpy(out->data, a->data, a->size * sizeof(float));
            memcpy(out->data + a->size, b->data, b->size * sizeof(float));
        } else {
            // Fallback: element-by-element (slow but correct)
            // TODO: optimize for common 3D cases
            int outer = 1, inner_a = 1, inner_b = 1;
            for (int i = 0; i < dim; i++) outer *= a->shape[i];
            for (int i = dim + 1; i < a->ndim; i++) { inner_a *= a->shape[i]; inner_b *= b->shape[i]; }
            int a_dim = a->shape[dim], b_dim = b->shape[dim];
            int out_dim = a_dim + b_dim;
            int inner = inner_a; // same for a and b
            int o_idx = 0;
            for (int o = 0; o < outer; o++) {
                for (int d = 0; d < a_dim; d++)
                    for (int i = 0; i < inner; i++)
                        out->data[o_idx++] = a->data[(o * a_dim + d) * inner + i];
                for (int d = 0; d < b_dim; d++)
                    for (int i = 0; i < inner; i++)
                        out->data[o_idx++] = b->data[(o * b_dim + d) * inner + i];
            }
        }
    }

    if (needs_grad(a, b)) {
        out->requires_grad = 1;
        record_node(OP_CONCAT, out, a, b, 0.0f, dim);
    }
    return out;
}

// ── Causal Mask ──────────────────────────────────────────────────────────
// Returns [seq_len, seq_len] tensor: 0 on lower triangle + diagonal, -1e9 above
// Cached causal mask — same seq_len always produces identical mask.
// Stored as is_param=1 so it survives begin_graph forever.
// MUST be thread_local: multiple worker threads call this concurrently;
// a non-thread_local map causes a data race → heap corruption → SIGABRT.
static thread_local std::unordered_map<int, AimlTensor*> g_causal_mask_cache;

void* aiml_tensor_causal_mask(int32_t seq_len) {
    auto it = g_causal_mask_cache.find(seq_len);
    if (it != g_causal_mask_cache.end()) return it->second;
    // First call for this seq_len: build and cache
    int shape[2] = {seq_len, seq_len};
    AimlTensor* t = alloc_tensor(shape, 2, false, /*is_param_hint=*/1);
    for (int i = 0; i < seq_len; i++)
        for (int j = 0; j < seq_len; j++)
            t->data[i * seq_len + j] = (j > i) ? -1e9f : 0.0f;
    g_causal_mask_cache[seq_len] = t;
    return t;
}

// Bulk convert int list to float tensor — replaces element-by-element ids_to_tensor loop
void* aiml_tensor_from_int_list(const int32_t* ids, int32_t count) {
    int shape[1] = {count};
    auto* t = alloc_tensor(shape, 1, false);  // ephemeral by default
    for (int i = 0; i < count; i++) t->data[i] = (float)ids[i];
    return t;
}

// Bulk convert flat int array to 2D [batch x seq] float tensor for batched embedding lookup
void* aiml_tensor_from_int_list_batch(const int32_t* ids, int32_t batch, int32_t seq) {
    int shape[2] = {batch, seq};
    auto* t = alloc_tensor(shape, 2, false);
    for (int i = 0; i < batch * seq; i++) t->data[i] = (float)ids[i];
    return t;
}

// ═══════════════════════════════════════════════════════════════════════════
// Fused Multi-Head Attention (Forward + Backward)
// ═══════════════════════════════════════════════════════════════════════════
// Replaces ~50 separate tensor ops per attention block with a single C call.
// x [S, E] @ W_Q/K/V [E, E] → Q/K/V [S, E]
// Per head: slice to [S, D], scaled dot-product attention, concat back
// Output: concat @ W_O [E, E] → [S, E]

// Scratch buffers — thread_local, forward-declared at top of file.

static void mha_ensure_scratch(int seq, int embed) {
    if (seq <= g_mha_max_seq && embed <= g_mha_max_embed) return;
    int S = seq > g_mha_max_seq ? seq : g_mha_max_seq;
    int E = embed > g_mha_max_embed ? embed : g_mha_max_embed;
    g_mha_Q = (float*)realloc(g_mha_Q, S * E * sizeof(float));
    g_mha_K = (float*)realloc(g_mha_K, S * E * sizeof(float));
    g_mha_V = (float*)realloc(g_mha_V, S * E * sizeof(float));
    g_mha_scores = (float*)realloc(g_mha_scores, S * S * sizeof(float));
    g_mha_attn = (float*)realloc(g_mha_attn, S * S * sizeof(float));
    g_mha_cat = (float*)realloc(g_mha_cat, S * E * sizeof(float));
    g_mha_max_seq = S;
    g_mha_max_embed = E;
}

// Single-sequence MHA forward: x_row[S,E] → out_row[S,E] (writes to out->data + b*S*E)
static void mha_forward_single(const float* x_row, float* out_row,
                                const float* wq_data, const float* wk_data,
                                const float* wv_data, const float* wo_data,
                                int S, int E, int n_heads) {
    int D = E / n_heads;
    float scale = 1.0f / sqrtf((float)D);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wq_data, E, 0.0f, g_mha_Q, E);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wk_data, E, 0.0f, g_mha_K, E);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wv_data, E, 0.0f, g_mha_V, E);
    float* qh = (float*)alloca(S * D * sizeof(float));
    float* kh = (float*)alloca(S * D * sizeof(float));
    float* vh = (float*)alloca(S * D * sizeof(float));
    float* oh = (float*)alloca(S * D * sizeof(float));
    for (int h = 0; h < n_heads; h++) {
        int off = h * D;
        for (int s = 0; s < S; s++) {
            memcpy(qh + s * D, g_mha_Q + s * E + off, D * sizeof(float));
            memcpy(kh + s * D, g_mha_K + s * E + off, D * sizeof(float));
            memcpy(vh + s * D, g_mha_V + s * E + off, D * sizeof(float));
        }
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, S, D, scale, qh, D, kh, D, 0.0f, g_mha_scores, S);
        for (int i = 0; i < S; i++) {
            for (int j = i + 1; j < S; j++) g_mha_scores[i * S + j] = -1e9f;
            float max_val = g_mha_scores[i * S];
            for (int j = 1; j < S; j++)
                if (g_mha_scores[i * S + j] > max_val) max_val = g_mha_scores[i * S + j];
            float sum = 0.0f;
            for (int j = 0; j < S; j++) {
                g_mha_attn[i * S + j] = expf(g_mha_scores[i * S + j] - max_val);
                sum += g_mha_attn[i * S + j];
            }
            float inv_sum = 1.0f / (sum + 1e-10f);
            for (int j = 0; j < S; j++) g_mha_attn[i * S + j] *= inv_sum;
        }
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    S, D, S, 1.0f, g_mha_attn, S, vh, D, 0.0f, oh, D);
        for (int s = 0; s < S; s++)
            memcpy(g_mha_cat + s * E + off, oh + s * D, D * sizeof(float));
    }
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, g_mha_cat, E, wo_data, E, 0.0f, out_row, E);
}

void* aiml_tensor_fused_mha(void* xv, void* wqv, void* wkv, void* wvv, void* wov,
                              int32_t n_heads, int32_t embed_dim) {
    auto* x  = (AimlTensor*)xv;
    auto* wq = (AimlTensor*)wqv;
    auto* wk = (AimlTensor*)wkv;
    auto* wv = (AimlTensor*)wvv;
    auto* wo = (AimlTensor*)wov;
    if (!x || !wq || !wk || !wv || !wo) return nullptr;

    int E = embed_dim;

    // Batched: x is [B, S, E]
    if (x->ndim == 3) {
        int B = x->shape[0], S = x->shape[1];
        mha_ensure_scratch(S, E);
        int out_shape[3] = {B, S, E};
        auto* out = alloc_tensor(out_shape, 3, false);
        if (!out) return nullptr;
        for (int bi = 0; bi < B; bi++) {
            mha_forward_single(x->data + bi * S * E, out->data + bi * S * E,
                               wq->data, wk->data, wv->data, wo->data, S, E, n_heads);
        }
        if (needs_grad(x) || needs_grad(wq) || needs_grad(wk) || needs_grad(wv) || needs_grad(wo)) {
            out->requires_grad = 1;
            GradNode node;
            node.op = OP_FUSED_MHA;
            node.inputs[0] = x; node.inputs[1] = wq; node.inputs[2] = wk;
            node.inputs[3] = wv; node.inputs[4] = wo;
            node.n_inputs = 5; node.output = out;
            node.scalar_arg = 0.0f; node.int_arg = n_heads; node.int_data = nullptr;
            out->grad_node_id = (int)g_graph.size();
            out->is_param = 0;
            g_graph.push_back(node);
        }
        return out;
    }

    // Single sequence: x is [S, E]
    int S = x->shape[0];
    mha_ensure_scratch(S, E);
    int out_shape[2] = {S, E};
    auto* out = alloc_tensor(out_shape, 2, false);
    if (!out) return nullptr;
    mha_forward_single(x->data, out->data, wq->data, wk->data, wv->data, wo->data, S, E, n_heads);

    if (needs_grad(x) || needs_grad(wq) || needs_grad(wk) || needs_grad(wv) || needs_grad(wo)) {
        out->requires_grad = 1;
        GradNode node;
        node.op = OP_FUSED_MHA;
        node.inputs[0] = x; node.inputs[1] = wq; node.inputs[2] = wk;
        node.inputs[3] = wv; node.inputs[4] = wo;
        node.n_inputs = 5; node.output = out;
        node.scalar_arg = 0.0f; node.int_arg = n_heads; node.int_data = nullptr;
        out->grad_node_id = (int)g_graph.size();
        out->is_param = 0;
        g_graph.push_back(node);
    }
    return out;
}

// ── Fused MHA Backward ──────────────────────────────────────────────────
// Recomputes forward intermediates (Q, K, V, attn weights) for gradient calc.
// Gradients: d_x, d_wq, d_wk, d_wv, d_wo

// Single-sequence MHA backward — accumulates into d_x/d_wq/d_wk/d_wv/d_wo
static void mha_backward_single(
        const float* x_row, const float* dout_row,
        float* d_x_row,  // may be nullptr if not needed
        AimlTensor* wq, AimlTensor* wk, AimlTensor* wv, AimlTensor* wo,
        int S, int E, int n_heads,
        bool need_dx, bool need_dwq, bool need_dwk, bool need_dwv, bool need_dwo) {
    int D = E / n_heads;
    float scale = 1.0f / sqrtf((float)D);

    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wq->data, E, 0.0f, g_mha_Q, E);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wk->data, E, 0.0f, g_mha_K, E);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                S, E, E, 1.0f, x_row, E, wv->data, E, 0.0f, g_mha_V, E);

    float* d_cat = (float*)calloc(S * E, sizeof(float));
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                S, E, E, 1.0f, dout_row, E, wo->data, E, 0.0f, d_cat, E);

    float* d_Q = (float*)calloc(S * E, sizeof(float));
    float* d_K = (float*)calloc(S * E, sizeof(float));
    float* d_V = (float*)calloc(S * E, sizeof(float));

    float* qh = (float*)alloca(S * D * sizeof(float));
    float* kh = (float*)alloca(S * D * sizeof(float));
    float* vh = (float*)alloca(S * D * sizeof(float));
    float* d_oh = (float*)alloca(S * D * sizeof(float));
    float* d_attn = (float*)alloca(S * S * sizeof(float));
    float* d_scores = (float*)alloca(S * S * sizeof(float));
    float* d_qh = (float*)alloca(S * D * sizeof(float));
    float* d_kh = (float*)alloca(S * D * sizeof(float));
    float* d_vh = (float*)alloca(S * D * sizeof(float));

    for (int h = 0; h < n_heads; h++) {
        int off = h * D;
        for (int s = 0; s < S; s++) {
            memcpy(qh + s * D, g_mha_Q + s * E + off, D * sizeof(float));
            memcpy(kh + s * D, g_mha_K + s * E + off, D * sizeof(float));
            memcpy(vh + s * D, g_mha_V + s * E + off, D * sizeof(float));
            memcpy(d_oh + s * D, d_cat + s * E + off, D * sizeof(float));
        }
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, S, D, scale, qh, D, kh, D, 0.0f, g_mha_scores, S);
        for (int i = 0; i < S; i++) {
            for (int j = i + 1; j < S; j++) g_mha_scores[i * S + j] = -1e9f;
            float max_val = g_mha_scores[i * S];
            for (int j = 1; j < S; j++) if (g_mha_scores[i * S + j] > max_val) max_val = g_mha_scores[i * S + j];
            float sum = 0.0f;
            for (int j = 0; j < S; j++) { g_mha_attn[i * S + j] = expf(g_mha_scores[i * S + j] - max_val); sum += g_mha_attn[i * S + j]; }
            float inv = 1.0f / (sum + 1e-10f);
            for (int j = 0; j < S; j++) g_mha_attn[i * S + j] *= inv;
        }
        float* oh_tmp = (float*)alloca(S * D * sizeof(float));
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    S, D, S, 1.0f, g_mha_attn, S, vh, D, 0.0f, oh_tmp, D);
        for (int s = 0; s < S; s++)
            memcpy(g_mha_cat + s * E + off, oh_tmp + s * D, D * sizeof(float));
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, S, D, 1.0f, d_oh, D, vh, D, 0.0f, d_attn, S);
        cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                    S, D, S, 1.0f, g_mha_attn, S, d_oh, D, 0.0f, d_vh, D);
        for (int i = 0; i < S; i++) {
            float dot = 0.0f;
            for (int j = 0; j < S; j++) dot += d_attn[i * S + j] * g_mha_attn[i * S + j];
            for (int j = 0; j < S; j++)
                d_scores[i * S + j] = g_mha_attn[i * S + j] * (d_attn[i * S + j] - dot);
        }
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    S, D, S, scale, d_scores, S, kh, D, 0.0f, d_qh, D);
        cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                    S, D, S, scale, d_scores, S, qh, D, 0.0f, d_kh, D);
        for (int s = 0; s < S; s++) {
            for (int d = 0; d < D; d++) {
                d_Q[s * E + off + d] += d_qh[s * D + d];
                d_K[s * E + off + d] += d_kh[s * D + d];
                d_V[s * E + off + d] += d_vh[s * D + d];
            }
        }
    }
    if (need_dwq) cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                               E, E, S, 1.0f, x_row, E, d_Q, E, 1.0f, wq->grad, E);
    if (need_dwk) cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                               E, E, S, 1.0f, x_row, E, d_K, E, 1.0f, wk->grad, E);
    if (need_dwv) cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                               E, E, S, 1.0f, x_row, E, d_V, E, 1.0f, wv->grad, E);
    if (need_dwo) cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                               E, E, S, 1.0f, g_mha_cat, E, dout_row, E, 1.0f, wo->grad, E);
    if (need_dx && d_x_row) {
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, E, E, 1.0f, d_Q, E, wq->data, E, 1.0f, d_x_row, E);
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, E, E, 1.0f, d_K, E, wk->data, E, 1.0f, d_x_row, E);
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    S, E, E, 1.0f, d_V, E, wv->data, E, 1.0f, d_x_row, E);
    }
    free(d_cat); free(d_Q); free(d_K); free(d_V);
}

static void backward_fused_mha(GradNode& node) {
    auto* x  = node.inputs[0];
    auto* wq = node.inputs[1];
    auto* wk = node.inputs[2];
    auto* wv = node.inputs[3];
    auto* wo = node.inputs[4];
    auto* out = node.output;
    int n_heads = node.int_arg;

    int E = (x->ndim == 3) ? x->shape[2] : x->shape[1];
    mha_ensure_scratch((x->ndim == 3) ? x->shape[1] : x->shape[0], E);

    bool need_dx  = x->requires_grad;
    bool need_dwq = wq->requires_grad;
    bool need_dwk = wk->requires_grad;
    bool need_dwv = wv->requires_grad;
    bool need_dwo = wo->requires_grad;
    if (need_dwq) ensure_grad(wq);
    if (need_dwk) ensure_grad(wk);
    if (need_dwv) ensure_grad(wv);
    if (need_dwo) ensure_grad(wo);
    if (need_dx)  ensure_grad(x);

    float* dout = out->grad;

    // Batched: x [B, S, E]
    if (x->ndim == 3) {
        int B = x->shape[0], S = x->shape[1];
        for (int bi = 0; bi < B; bi++) {
            mha_backward_single(x->data + bi * S * E, dout + bi * S * E,
                                need_dx ? x->grad + bi * S * E : nullptr,
                                wq, wk, wv, wo, S, E, n_heads,
                                need_dx, need_dwq, need_dwk, need_dwv, need_dwo);
        }
        return;
    }

    // Single sequence: x [S, E]
    int S = x->shape[0];
    mha_ensure_scratch(S, E);
    mha_backward_single(x->data, dout,
                        need_dx ? x->grad : nullptr,
                        wq, wk, wv, wo, S, E, n_heads,
                        need_dx, need_dwq, need_dwk, need_dwv, need_dwo);
}

// ═══════════════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════════════

void aiml_tensor_print(void* tv, const char* label) {
    auto* t = (AimlTensor*)tv;
    printf("tensor(%s): shape=[", label ? label : "");
    for (int i = 0; i < t->ndim; i++) {
        if (i) printf(",");
        printf("%d", t->shape[i]);
    }
    printf("], size=%d, requires_grad=%d\n  data=[", t->size, t->requires_grad);
    int show = t->size < 20 ? t->size : 20;
    for (int i = 0; i < show; i++) {
        if (i) printf(", ");
        printf("%.4f", t->data[i]);
    }
    if (t->size > 20) printf(", ...");
    printf("]\n");
}

void aiml_tensor_fill(void* tv, float val) {
    auto* t = (AimlTensor*)tv;
    if (!t) return;
    for (int i = 0; i < t->size; i++) t->data[i] = val;
}

static char* aiml_tensor_digest_to_hex(const unsigned char* bytes, unsigned int len) {
    char* s = (char*)malloc((size_t)len * 2 + 1);
    if (!s) return nullptr;
    static const char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < len; ++i) {
        s[i * 2] = hex[bytes[i] >> 4];
        s[i * 2 + 1] = hex[bytes[i] & 0x0F];
    }
    s[(size_t)len * 2] = '\0';
    return s;
}

static void aiml_tensor_digest_i32(EVP_MD_CTX* ctx, int32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    EVP_DigestUpdate(ctx, b, sizeof(b));
}

char* aiml_tensor_sha256(void* tv) {
    auto* t = (AimlTensor*)tv;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return nullptr;

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    const char magic[] = "AIML_TENSOR_SHA256_V1";
    EVP_DigestUpdate(ctx, magic, sizeof(magic) - 1);

    if (!t) {
        aiml_tensor_digest_i32(ctx, -1);
    } else {
        aiml_tensor_digest_i32(ctx, (int32_t)t->ndim);
        aiml_tensor_digest_i32(ctx, (int32_t)t->size);
        for (int i = 0; i < t->ndim; ++i) {
            aiml_tensor_digest_i32(ctx, (int32_t)t->shape[i]);
        }
        if (t->data && t->size > 0) {
            EVP_DigestUpdate(ctx, t->data, (size_t)t->size * sizeof(float));
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    EVP_DigestFinal_ex(ctx, digest, &dlen);
    EVP_MD_CTX_free(ctx);
    return aiml_tensor_digest_to_hex(digest, dlen);
}

extern "C" void aiml_tensor_add_inplace(void* dstv, void* srcv) {
    auto* dst = (AimlTensor*)dstv;
    auto* src = (AimlTensor*)srcv;
    int n = dst->size < src->size ? dst->size : src->size;
    for (int i = 0; i < n; i++) dst->data[i] += src->data[i];
}

extern "C" void aiml_tensor_scale_inplace(void* tv, float s) {
    auto* t = (AimlTensor*)tv;
    for (int i = 0; i < t->size; i++) t->data[i] *= s;
}

extern "C" float aiml_tensor_l2_norm_sq(void* tv) {
    auto* t = (AimlTensor*)tv;
    if (!t) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < t->size; i++) sum += t->data[i] * t->data[i];
    return sum;
}

// Returns scale factor for global gradient norm clipping.
// clip_norm <= 0 or norm_sq == 0: returns 1.0 (no clipping).
// Otherwise: returns min(1.0, clip_norm / sqrt(norm_sq)).
extern "C" float aiml_compute_clip_scale(float norm_sq, float clip_norm) {
    if (clip_norm <= 0.0f || norm_sq <= 0.0f) return 1.0f;
    float norm = sqrtf(norm_sq);
    return norm > clip_norm ? clip_norm / norm : 1.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Persistence — save/load tensor to/from AionDB Kv blob (auto-cached store)
// Blob format: [ndim:i32][shape[0]:i32]...[shape[ndim-1]:i32][float data]
// ═══════════════════════════════════════════════════════════════════════════

// AionDB blob API (from db_runtime)
extern "C" void  aiml_db_kv_set_blob(void* kv, const char* key, const void* data, int32_t size);
extern "C" void* aiml_db_kv_get_blob(void* kv, const char* key, int32_t* out_size);
extern "C" void* aiml_db_kv_open_persist(const char* name, int64_t val_size, const char* base_path);

// Internal: get-or-create a persistent KV store by name (singleton-cached in db_runtime)
static void* tensor_get_kv(const char* store_name) {
    return aiml_db_kv_open_persist(store_name, 0, nullptr);
}

void aiml_tensor_save(void* tv, const char* store_name, const char* key) {
    if (!tv || !store_name || !key) return;
    auto* t = (AimlTensor*)tv;
    // If tensor is bound to DB (owns_data=0 and data points into a blob),
    // the data is already in the DB — no copy needed.
    if (!t->owns_data && !arena_owns(t->data)) {
        // Bound tensor — data is already in DB. Just flush WAL if persistent.
        return;
    }
    void* kv = tensor_get_kv(store_name);
    if (!kv) return;
    int header_bytes = (1 + t->ndim) * (int)sizeof(int32_t);
    int data_bytes = t->size * (int)sizeof(float);
    int total = header_bytes + data_bytes;
    char* blob = (char*)malloc(total);
    if (!blob) return;
    memcpy(blob, &t->ndim, sizeof(int32_t));
    memcpy(blob + sizeof(int32_t), t->shape, t->ndim * sizeof(int32_t));
    memcpy(blob + header_bytes, t->data, data_bytes);
    aiml_db_kv_set_blob(kv, key, blob, total);
    free(blob);
}

void* aiml_tensor_load(const char* store_name, const char* key) {
    if (!store_name || !key) return nullptr;
    void* kv = tensor_get_kv(store_name);
    if (!kv) return nullptr;
    int32_t blob_size = 0;
    void* blob = aiml_db_kv_get_blob(kv, key, &blob_size);
    if (!blob || blob_size < (int)sizeof(int32_t)) return nullptr;
    const char* p = (const char*)blob;
    int32_t ndim;
    memcpy(&ndim, p, sizeof(int32_t));
    p += sizeof(int32_t);
    if (ndim <= 0 || ndim > 8) return nullptr;
    int header_bytes = (1 + ndim) * (int)sizeof(int32_t);
    if (blob_size < header_bytes) return nullptr;
    int shape[8];
    memcpy(shape, p, ndim * sizeof(int32_t));
    p += ndim * sizeof(int32_t);
    auto* t = alloc_tensor(shape, ndim, false, /*is_param_hint=*/1);
    int data_bytes = t->size * (int)sizeof(float);
    if (blob_size >= header_bytes + data_bytes) {
        memcpy(t->data, p, data_bytes);
    }
    return t;
}

// ── tensor.kv_evict: free in-memory map for a tensor store, keep WAL ──
extern "C" void aiml_db_kv_evict(void* kv);

void aiml_tensor_kv_evict(const char* store_name) {
    if (!store_name) return;
    void* kv = tensor_get_kv(store_name);
    if (kv) aiml_db_kv_evict(kv);
}

// ── tensor.bind: zero-copy binding to DB KV blob ──────────────────────
// If key exists: tensor.data points directly into DB blob (zero-copy load).
// If key doesn't exist: creates blob in DB, tensor.data points into it.
// In both cases: owns_data=0 (DB owns memory), adam_step modifies DB directly.
// tensor.save on a bound tensor is effectively a NOOP (data already in DB).
void* aiml_tensor_bind(const char* store_name, const char* key,
                        const int32_t* shape, int32_t ndim) {
    if (!store_name || !key || !shape || ndim <= 0) return nullptr;
    void* kv = tensor_get_kv(store_name);
    if (!kv) return nullptr;

    int header_bytes = (1 + ndim) * (int)sizeof(int32_t);
    int size = compute_size(shape, ndim);
    int data_bytes = size * (int)sizeof(float);
    int total = header_bytes + data_bytes;

    // Check if key already exists with correct size
    int32_t blob_size = 0;
    void* existing = aiml_db_kv_get_blob(kv, key, &blob_size);

    if (!existing || blob_size < total) {
        // Create new blob: [ndim][shape...][zeroed float data]
        char* blob = (char*)calloc(1, total);
        if (!blob) return nullptr;
        int32_t nd = ndim;
        memcpy(blob, &nd, sizeof(int32_t));
        memcpy(blob + sizeof(int32_t), shape, ndim * sizeof(int32_t));
        // float data is already zeroed by calloc
        aiml_db_kv_set_blob(kv, key, blob, total);
        free(blob);
        // Re-fetch to get stable pointer into DB storage
        existing = aiml_db_kv_get_blob(kv, key, &blob_size);
        if (!existing) return nullptr;
    }

    // Build tensor struct pointing directly into DB blob
    auto* t = struct_alloc();
    if (!t) return nullptr;
    t->ndim = ndim;
    t->shape = (int*)malloc(ndim * sizeof(int));
    memcpy(t->shape, shape, ndim * sizeof(int));
    t->size = size;
    // Point data directly into DB blob (after header)
    t->data = (float*)((char*)existing + header_bytes);
    t->owns_data = 0;  // DB owns this memory
    t->requires_grad = 0;
    t->grad = nullptr;
    t->grad_node_id = -1;
    t->is_param = 1;
    t->alloc_epoch = g_graph_epoch;
    g_all_tensors.insert(t);
    return t;
}
