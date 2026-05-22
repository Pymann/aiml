#pragma once
// tensor_runtime.h — AIML Tensor Runtime (C-Interface)
// Opaque tensor handle: i8* → AimlTensor*

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Arena (call once before training to pre-allocate all ephemeral memory) ──
void    aiml_tensor_arena_init(int32_t megabytes);
void    aiml_tensor_arena_free(void);   // free arena memory (call at worker end)

// ── Lifecycle ───────────────────────────────────────────────────────────
void*   aiml_tensor_zeros(const int32_t* shape, int32_t ndim);
void*   aiml_tensor_ones(const int32_t* shape, int32_t ndim);
void*   aiml_tensor_randn(const int32_t* shape, int32_t ndim, float mean, float stddev);
void*   aiml_tensor_from_data(const float* data, const int32_t* shape, int32_t ndim);
void*   aiml_tensor_clone(void* t);
void    aiml_tensor_free(void* t);

// ── In-place accumulation (no allocation) ────────────────────────────────
void    aiml_tensor_fma_inplace(void* target, void* src, float scale);      // t[i] += s[i] * scale
void    aiml_tensor_fma_sq_inplace(void* target, void* src, float scale);   // t[i] += s[i]^2 * scale
void    aiml_tensor_fma_abs_inplace(void* target, void* src, float scale);  // t[i] += |s[i]| * scale

// ── Scalar access ───────────────────────────────────────────────────────
float   aiml_tensor_get_flat(void* t, int32_t idx);
void    aiml_tensor_set_flat(void* t, int32_t idx, float val);
float   aiml_tensor_item(void* t);  // scalar tensor → float

// ── Shape / metadata ────────────────────────────────────────────────────
int32_t aiml_tensor_shape(void* t, int32_t dim);
int32_t aiml_tensor_ndim(void* t);
int32_t aiml_tensor_size(void* t);
float*  aiml_tensor_data_ptr(void* t);

// ── View operations ─────────────────────────────────────────────────────
void*   aiml_tensor_reshape(void* t, const int32_t* shape, int32_t ndim);
void*   aiml_tensor_transpose(void* t, int32_t d0, int32_t d1);
void*   aiml_tensor_slice(void* t, int32_t dim, int32_t start, int32_t end);

// ── Elementwise ops (return new tensor) ─────────────────────────────────
void*   aiml_tensor_add(void* a, void* b);
void*   aiml_tensor_sub(void* a, void* b);
void*   aiml_tensor_mul(void* a, void* b);
void*   aiml_tensor_div(void* a, void* b);
void*   aiml_tensor_mul_scalar(void* t, float s);
void*   aiml_tensor_neg(void* t);

// ── Matmul ──────────────────────────────────────────────────────────────
void*   aiml_tensor_matmul(void* a, void* b);

// ── Reduction ───────────────────────────────────────────────────────────
void*   aiml_tensor_sum(void* t, int32_t dim);       // dim=-1 → all
void*   aiml_tensor_mean(void* t, int32_t dim);

// ── Unary math ──────────────────────────────────────────────────────────
void*   aiml_tensor_exp(void* t);
void*   aiml_tensor_log_op(void* t);
void*   aiml_tensor_tanh_op(void* t);
void*   aiml_tensor_relu(void* t);
void*   aiml_tensor_gelu(void* t);

// ── Composite ops ───────────────────────────────────────────────────────
void*   aiml_tensor_softmax(void* t, int32_t dim);
void*   aiml_tensor_layer_norm(void* t, void* gamma, void* beta, float eps);
void*   aiml_tensor_embedding_lookup(void* weight, void* id_tensor, int32_t seq_len);
void*   aiml_tensor_cross_entropy(void* logits, void* target_tensor, int32_t batch_size);
void*   aiml_tensor_weighted_cross_entropy(void* logits, void* targets, void* weights, int32_t seq_len);

// ── Elementwise unary (additional) ──────────────────────────────────────
void*   aiml_tensor_abs(void* t);
void*   aiml_tensor_sign(void* t);
void*   aiml_tensor_step(void* t);

// ── Argmax / topk ───────────────────────────────────────────────────────
int32_t aiml_tensor_argmax_scalar(void* t);
void*   aiml_tensor_argmax(void* t, int32_t dim);

// ── Autograd ────────────────────────────────────────────────────────────
void    aiml_tensor_set_requires_grad(void* t, int32_t flag);
int32_t aiml_tensor_requires_grad(void* t);
void*   aiml_tensor_grad(void* t);
void    aiml_tensor_zero_grad(void* t);
void    aiml_tensor_begin_graph(void);
void    aiml_tensor_backward(void* loss);
void    aiml_tensor_thread_cleanup(void);  // free ALL thread-local tensors; call in worker threads before exit
void    aiml_tensor_no_grad_begin(void);
void    aiml_tensor_no_grad_end(void);

// ── Optimizer ───────────────────────────────────────────────────────────
void    aiml_tensor_adam_step(void* param, void* m, void* v,
                              float lr, float beta1, float beta2, float eps, int32_t step);
void    aiml_tensor_adam_step_with_grad(void* param, void* m, void* v, void* grad,
                                         float lr, float beta1, float beta2, float eps, int32_t step);

// ── Concat ────────────────────────────────────────────────────────────
void*   aiml_tensor_concat(void* a, void* b, int32_t dim);

// ── Causal Mask (cached — same seq_len reuses same tensor) ──────────
void*   aiml_tensor_causal_mask(int32_t seq_len);

// ── Bulk conversion ─────────────────────────────────────────────────
void*   aiml_tensor_from_int_list(const int32_t* ids, int32_t count);
void*   aiml_tensor_from_int_list_batch(const int32_t* ids, int32_t batch, int32_t seq);

// ── Persistence (via AionDB Kv blob, auto-cached store) ─────────────────
// store_name: KV store name (opened/cached internally via singleton)
// key: tensor name within the store
void    aiml_tensor_save(void* tensor, const char* store_name, const char* key);
void*   aiml_tensor_load(const char* store_name, const char* key);
void    aiml_tensor_kv_evict(const char* store_name);  // evict tensor store from RAM, keep WAL
// bind: zero-copy — tensor.data points directly into DB blob.
// If key exists, loads it; otherwise creates zeroed blob.
// adam_step modifies DB memory directly. save() becomes NOOP.
void*   aiml_tensor_bind(const char* store_name, const char* key,
                          const int32_t* shape, int32_t ndim);

// ── Fused Multi-Head Attention ─────────────────────────────────────────
void*   aiml_tensor_fused_mha(void* x, void* wq, void* wk, void* wv, void* wo,
                               int32_t n_heads, int32_t embed_dim);

// ── Utility ─────────────────────────────────────────────────────────────
void    aiml_tensor_print(void* t, const char* label);
void    aiml_tensor_fill(void* t, float val);
char*   aiml_tensor_sha256(void* t);

#ifdef __cplusplus
}
#endif
