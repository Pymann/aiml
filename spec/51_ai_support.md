# 51 — AI Support (Native Acceleration)

Native C functions that accelerate ML-critical operations. These replace AIML loops
that are bottlenecked by per-element function calls, string allocations, and list-push overhead.

Design principle: **Keep the AIML API simple, move hot loops to C.**
Compiler pattern: **`extractvalue` to access struct fields from loaded values — zero temp allocas.**

---

## Status

| # | Function | Status | Test |
|---|----------|--------|------|
| 1 | `ai.embedding_lookup` | ✅ Done | test_ai_support A1–A3 |
| 2 | `ai.embedding_update` | ✅ Done | test_ai_support B1 |
| 3 | `ai.dot` | ✅ Done | test_ai_support C1 |
| 4 | `ai.train_bpe_merges` | ✅ Done | test_ai_support D1 |
| 5 | `ai.bpe_merge_count/left/right/free` | ✅ Done | test_ai_support D1 |
| 6 | `ai.encode_dense` | Planned | — |
| 7 | `ai.encode_dense_weighted` | Planned | — |
| 8 | `ai.triplet_loss` | Planned | — |
| 9 | `ai.triplet_grad` | Planned | — |

---

## 1. Embedding Lookup — `ai.embedding_lookup`

Direct memcpy from flat buffer instead of per-element seek/read loop.

```yaml
- decl: [vec, list<f32>, { call: [ai.embedding_lookup, emb_flat, token_id, dim] }]
#                                                       ↑ buffer<u8> ↑ i32  ↑ i32
```

### C Signature

```c
void aiml_ai_embedding_lookup(
    const uint8_t* emb_flat,
    int32_t token_id,
    int32_t dim,
    float* result              // pre-allocated by handler
);
```

### Algorithm

```c
memcpy(result, emb_flat + token_id * dim * sizeof(float), dim * sizeof(float));
```

### Compiler Handler

Handler uses `extractvalue` to get `buffer.data` (field 4) directly from the loaded struct value.
Allocates result `list<f32>` with `{size=dim, capacity=dim, data=malloc(dim*4)}`.

### Speedup: **50–200×** (1 memcpy vs dim × seek/read/push)

---

## 2. Embedding SGD Update — `ai.embedding_update`

In-place gradient descent on embedding buffer. Replaces per-element seek/read/compute/seek/write.

```yaml
- call: [ai.embedding_update, emb_flat, token_id, gradient, lr, dim]
#                              ↑ buffer<u8> ↑ i32   ↑ list<f32> ↑ f32 ↑ i32
```

### C Signature

```c
void aiml_ai_embedding_update(
    uint8_t* emb_flat,       // mutable
    int32_t token_id,
    const float* gradient,   // dim floats
    float learning_rate,
    int32_t dim
);
```

### Algorithm

```c
float* base = (float*)(emb_flat + token_id * dim * sizeof(float));
for (int d = 0; d < dim; d++)
    base[d] -= learning_rate * gradient[d];
```

### Compiler Handler

Uses `extractvalue` on buffer (field 4 → data ptr) and list (field 2 → data ptr).
Auto-coerces lr from f64/i32 to f32.

### Speedup: **100–500×** (pointer arithmetic vs seek/read/write per element)

---

## 3. Dot Product — `ai.dot`

```yaml
- decl: [d, f32, { call: [ai.dot, list_a, list_b, dim] }]
#                                  ↑ list<f32> ↑ list<f32> ↑ i32
```

### C Signature

```c
float aiml_ai_dot(const float* a, const float* b, int32_t dim);
```

### Compiler Handler

Uses `extractvalue` on both lists (field 2 → data ptr).

### Speedup: **10–50×** (C loop vs AIML per-element index + multiply + accumulate)

---

## 4. BPE Training — `ai.train_bpe_merges`

The single biggest bottleneck. Current AIML implementation scans all texts N×merges times
with Kv-based bigram counting. Native C uses incremental pair updates (SentencePiece-style).

```yaml
- decl: [result, any, { call: [ai.train_bpe_merges, texts, max_merges] }]
#                                                    ↑ list<string> ↑ i32
- decl: [n, i32, { call: [ai.bpe_merge_count, result] }]
- decl: [left, string, { call: [ai.bpe_merge_left, result, 0] }]
- decl: [right, string, { call: [ai.bpe_merge_right, result, 0] }]
- call: [ai.bpe_merge_free, result]
```

### C Signatures

```c
// Returns heap-allocated result struct (opaque to AIML)
AimlBpeMergeResult* aiml_bpe_train_merges(
    const char** texts, int32_t n_texts, int32_t max_merges
);

// Accessors
int32_t count;        // result->count
char*   left[i];      // result->left[i]
char*   right[i];     // result->right[i]

void aiml_bpe_merge_result_free(AimlBpeMergeResult* r);
```

### Algorithm (SentencePiece-style)

1. **Init**: Split all words into characters, build initial vocabulary
2. **Pair counting**: Count all adjacent token pairs using `unordered_map<uint64_t, int64_t>` (pair = two packed 32-bit token IDs)
3. **Best-pair selection**: Linear scan of pair map (sufficient for typical vocabulary sizes)
4. **Incremental merge**: When merging pair (A,B)→AB:
   - Only update words containing (A,B)
   - Decrement old neighbor pairs, increment new neighbor pairs
5. **Minimum threshold**: Stop when best pair count ≤ 1

### Complexity

| | AIML (current) | C (native) |
|---|---|---|
| Per merge | O(total_tokens) full rescan | O(affected_words) incremental |
| Pair storage | `Kv<string:i32>` string keys | `unordered_map<u64,i64>` int keys |
| Total | O(merges × corpus) | O(merges × avg_affected) |

### Speedup: **100–500×**

---

## 5. Dense Encoding — `ai.encode_dense` (Planned)

Mean pooling of token embeddings. Replaces N × `table.lookup` + N × `vec_add` + `vec_scale`.

```yaml
- decl: [vec, list<f32>, { call: [ai.encode_dense, token_ids, emb_flat, dim] }]
#                                                   ↑ list<i32>  ↑ buffer<u8>  ↑ i32
```

### C Signature

```c
void aiml_ai_encode_dense(
    const int32_t* token_ids, int32_t n_tokens,
    const uint8_t* emb_flat,
    int32_t dim,
    float* result              // pre-allocated dim floats
);
```

### Algorithm

```c
memset(result, 0, dim * sizeof(float));
for (int i = 0; i < n_tokens; i++) {
    const float* emb = (const float*)(emb_flat + token_ids[i] * dim * 4);
    for (int d = 0; d < dim; d++)
        result[d] += emb[d];
}
float inv = 1.0f / n_tokens;
for (int d = 0; d < dim; d++)
    result[d] *= inv;
```

### Speedup: **20–100×** (eliminates N vector allocations + N vec_add calls)

---

## 6. Weighted Dense Encoding — `ai.encode_dense_weighted` (Planned)

Positional U-shaped weighting: tokens at start/end of sequence get 1.5× weight.

```yaml
- decl: [vec, list<f32>, { call: [ai.encode_dense_weighted, token_ids, emb_flat, dim] }]
```

### C Signature

```c
void aiml_ai_encode_dense_weighted(
    const int32_t* token_ids, int32_t n_tokens,
    const uint8_t* emb_flat,
    int32_t dim,
    float* result
);
```

### Algorithm

```c
memset(result, 0, dim * sizeof(float));
float weight_sum = 0.0f;
for (int i = 0; i < n_tokens; i++) {
    float pos = (float)i / (float)n_tokens;
    float w = 1.0f + 0.5f * fabsf(2.0f * pos - 1.0f);  // U-shape
    const float* emb = (const float*)(emb_flat + token_ids[i] * dim * 4);
    for (int d = 0; d < dim; d++)
        result[d] += emb[d] * w;
    weight_sum += w;
}
float inv = 1.0f / weight_sum;
for (int d = 0; d < dim; d++)
    result[d] *= inv;
```

### Speedup: **20–100×**

---

## 7. Triplet Loss + Gradient (Planned)

Combined forward + backward for contrastive embedding training.

```yaml
- decl: [loss, f32, { call: [ai.triplet_loss, q, pos, neg, margin, dim] }]
- call: [ai.triplet_grad, q, pos, neg, margin, dim, grad_out]
```

### C Signatures

```c
float aiml_ai_triplet_loss(
    const float* q, const float* pos, const float* neg,
    float margin, int32_t dim
);

void aiml_ai_triplet_grad(
    const float* q, const float* pos, const float* neg,
    float margin, int32_t dim,
    float* grad_out   // dim floats, written if loss > 0
);
```

### Speedup: **10–50×** (eliminates 3× vector allocations per triplet)

---

## 8. Namespace Registration

All functions in `ai` namespace:

```yaml
# Done
ai.embedding_lookup(emb_flat, token_id, dim) → list<f32>
ai.embedding_update(emb_flat, token_id, gradient, lr, dim) → void
ai.dot(a, b, dim) → f32
ai.train_bpe_merges(texts, max_merges) → any (opaque result ptr)
ai.bpe_merge_count(result) → i32
ai.bpe_merge_left(result, index) → string
ai.bpe_merge_right(result, index) → string
ai.bpe_merge_free(result) → void

# Planned
ai.encode_dense(token_ids, emb_flat, dim) → list<f32>
ai.encode_dense_weighted(token_ids, emb_flat, dim) → list<f32>
ai.triplet_loss(q, pos, neg, margin, dim) → f32
ai.triplet_grad(q, pos, neg, margin, dim, grad_out) → void
```

---

## 9. Implementation Notes

- **Struct field access**: Compiler handlers use `getBufferData` / `getListField` helpers that handle two cases:
  - **Aggregate (loaded struct)**: `CreateExtractValue` — zero overhead, direct SSA extraction (e.g. local `buffer<u8>` variable)
  - **Pointer (model/class field)**: `CreateStructGEP + CreateLoad` — standard GEP path (e.g. `<.flat` class member, `a.data` model field)
  - The check is `val->getType()->isAggregateType()`. This is necessary because FuncHandler args arrive as loaded values for local variables but as pointers for model field / class member access.
- All `list<f32>` return values use the standard AIML list ABI (`{i32 size, i32 capacity, ptr data}`)
- `emb_flat` is `buffer<u8>.data` — raw pointer to the embedding table's backing buffer
- `token_ids` from `list<i32>` — direct pointer to list data
- No heap allocations in hot paths — caller provides output buffers where possible
- Thread safety: functions are stateless (no globals), safe for concurrent use

---

## 10. Priority

| Priority | Function | Status | Impact |
|----------|----------|--------|--------|
| P0 | `ai.embedding_lookup` | ✅ Done | dim × seek/read → 1 memcpy |
| P0 | `ai.embedding_update` | ✅ Done | dim × seek/read/write → pointer arithmetic |
| P0 | `ai.dot` | ✅ Done | AIML loop → C loop |
| P0 | `ai.train_bpe_merges` | ✅ Done | O(merges × corpus) → O(merges × affected) |
| P1 | `ai.encode_dense` | Planned | N vector allocs → zero allocs |
| P1 | `ai.encode_dense_weighted` | Planned | N vector allocs → zero allocs |
| P2 | `ai.triplet_loss/grad` | Planned | 3 vector allocs per call → zero allocs |
