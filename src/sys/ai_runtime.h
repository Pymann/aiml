#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── ai.embedding_lookup(flat_data, token_id, dim) → writes into result ──
// flat_data: raw uint8_t* from buffer<u8>.data field
// result: float* (pre-allocated, dim floats)
// Replaces: dim × (seek + read + push) loop → 1 memcpy
void aiml_ai_embedding_lookup(const uint8_t* flat_data, int32_t token_id,
                               int32_t dim, float* result);

// ── ai.embedding_update(flat_data, token_id, grad_data, lr, dim) ──
// flat_data: mutable uint8_t* from buffer<u8>.data field
// grad_data: float* from list<f32>.data field
// In-place SGD: flat[base+d] -= lr * grad[d]
void aiml_ai_embedding_update(uint8_t* flat_data, int32_t token_id,
                               const float* grad_data, float lr, int32_t dim);

// ── ai.dot(a_data, b_data, dim) → f32 ──
// Dot product of two float arrays. Used for triplet loss etc.
float aiml_ai_dot(const float* a, const float* b, int32_t dim);

// ── ai.encode_dense(token_ids, n_tokens, emb_flat, dim) → writes into result ──
void aiml_ai_encode_dense(const int32_t* token_ids, int32_t n_tokens,
                           const uint8_t* emb_flat, int32_t dim, float* result);

// ── ai.encode_dense_weighted(token_ids, n_tokens, emb_flat, dim) → writes into result ──
void aiml_ai_encode_dense_weighted(const int32_t* token_ids, int32_t n_tokens,
                                    const uint8_t* emb_flat, int32_t dim, float* result);

// ── ai.triplet_loss(q, pos, neg, margin, dim) → f32 ──
float aiml_ai_triplet_loss(const float* q, const float* pos, const float* neg,
                            float margin, int32_t dim);

// ── ai.triplet_grad(q, pos, neg, margin, dim, grad_out) ──
void aiml_ai_triplet_grad(const float* q, const float* pos, const float* neg,
                           float margin, int32_t dim, float* grad_out);

// ── ai.sample_logits(tensor_ptr, temperature, top_k) → i32 ──
// Temperature-scaled top-k sampling from a tensor's data.
// tensor_ptr: opaque AimlTensor* (reads .data and .size)
// top_k=0 means full vocab. Returns sampled token index.
int32_t aiml_ai_sample_logits(void* tensor_ptr, float temperature, int32_t top_k);

// ── bpe.train_on_corpus ──
// texts: array of C strings, n_texts: count, max_merges: limit
// Returns heap-allocated array of merge triples: [left0, right0, left1, right1, ...]
// out_count: set to number of merges found
// Caller must free the returned array and each string in it.
struct AimlBpeMergeResult {
    char** left;      // left[i] = left token of merge i
    char** right;     // right[i] = right token of merge i
    int32_t count;    // number of merges
};

AimlBpeMergeResult* aiml_bpe_train_merges(const char** texts, int32_t n_texts,
                                           int32_t max_merges);
void aiml_bpe_merge_result_free(AimlBpeMergeResult* r);

#ifdef __cplusplus
}
#endif
