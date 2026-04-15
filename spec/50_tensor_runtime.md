# 50 — Tensor Runtime

The `tensor` namespace provides a complete tensor computation library with automatic differentiation (autograd), optimizer support, and persistence. All tensors are opaque heap pointers (`i8*`).

## 1. Type

```yaml
- decl: [t, tensor]   # tensor = i8* (opaque pointer to AimlTensor)
```

Tensors store `float32` data with up to 8 dimensions.

## 2. Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.zeros` | `(d0, d1, ...) -> tensor` | Zero-filled tensor |
| `tensor.ones` | `(d0, d1, ...) -> tensor` | One-filled tensor |
| `tensor.randn` | `(d0, d1, ...) -> tensor` | Normal(0,1) random |
| `tensor.randn_ex` | `(mean, stddev, d0, d1, ...) -> tensor` | Normal(mean,stddev) random |
| `tensor.from_data` | `(data_ptr, d0, d1, ...) -> tensor` | From float array |
| `tensor.clone` | `(t) -> tensor` | Deep copy |
| `tensor.free` | `(t) -> void` | Free memory |

**Important**: `randn_ex` takes mean/stddev as the **first two** arguments, followed by shape dimensions.

## 3. Scalar Access

| Method | Signature | Description |
|--------|-----------|-------------|
| `t.get` | `(idx) -> f32` | Flat-index read |
| `t.set` | `(idx, val) -> void` | Flat-index write |
| `t.item` | `() -> f32` | Scalar tensor to float |

## 4. Shape / Metadata

| Method | Signature | Description |
|--------|-----------|-------------|
| `t.shape` | `(dim) -> i32` | Size along dimension |
| `t.ndim` | `() -> i32` | Number of dimensions |
| `t.size` | `() -> i32` | Total element count |
| `t.data_ptr` | `() -> i8*` | Raw float pointer |

## 5. View Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.reshape` | `(t, d0, d1, ...) -> tensor` | Reshape (shared data) |
| `tensor.transpose` | `(t, d0, d1) -> tensor` | Swap two dimensions |
| `tensor.slice` | `(t, dim, start, end) -> tensor` | Slice along dimension |

## 6. Elementwise Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.add` | `(a, b) -> tensor` | Addition (broadcast-aware: 1D+2D) |
| `tensor.sub` | `(a, b) -> tensor` | Subtraction |
| `tensor.mul` | `(a, b) -> tensor` | Elementwise multiply |
| `tensor.mul_scalar` | `(t, s) -> tensor` | Scalar multiply |
| `tensor.neg` | `(t) -> tensor` | Negate |

**Broadcasting**: `tensor.add` supports 1D+2D broadcasting. A bias tensor `[dim]` added to a matrix `[seq, dim]` broadcasts automatically.

## 7. Matmul

```yaml
- decl: [c, tensor, { call: [tensor.matmul, a, b] }]  # [M,K] x [K,N] -> [M,N]
```

Uses OpenBLAS `cblas_sgemm` for performance.

## 8. Reduction

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.sum` | `(t, dim) -> tensor` | Sum along dim (-1 = all) |
| `tensor.mean` | `(t, dim) -> tensor` | Mean along dim (-1 = all) |

## 9. Unary Math

| Function | Description |
|----------|-------------|
| `tensor.exp` | Exponential |
| `tensor.log_op` | Natural logarithm |
| `tensor.tanh_op` | Hyperbolic tangent |
| `tensor.relu` | ReLU activation |
| `tensor.gelu` | GELU activation |

## 10. Composite Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.softmax` | `(t, dim) -> tensor` | Softmax along dimension |
| `tensor.layer_norm` | `(t, gamma, beta, eps) -> tensor` | Layer normalization |
| `tensor.embedding_lookup` | `(weight, id_tensor, seq_len) -> tensor` | Embedding lookup |
| `tensor.cross_entropy` | `(logits, target_tensor, batch_size) -> tensor` | Cross-entropy loss |

**Token ID convention**: `embedding_lookup` and `cross_entropy` accept a 1D float tensor where each float is a float-encoded integer ID (e.g., `3.0` means token ID 3). The C runtime casts `float -> int` internally. This avoids needing a separate integer tensor type.

## 11. Argmax

| Function | Signature | Description |
|----------|-----------|-------------|
| `tensor.argmax_scalar` | `(t) -> i32` | Index of maximum element |
| `tensor.argmax` | `(t, dim) -> tensor` | Argmax along dimension |

## 12. Concat

```yaml
- decl: [c, tensor, { call: [tensor.concat, a, b, dim] }]
```

Concatenates two tensors along `dim`. Used for multi-head attention head concatenation.

## 13. Causal Mask

```yaml
- decl: [mask, tensor, { call: [tensor.causal_mask, seq_len] }]
```

Creates a `[seq_len, seq_len]` matrix: `0.0` on and below diagonal, `-1e9` above. Used in decoder self-attention to prevent attending to future tokens.

## 14. Autograd

The tensor runtime includes a full automatic differentiation engine.

### Control

| Function | Description |
|----------|-------------|
| `tensor.begin_graph` | Start recording operations for autograd |
| `tensor.backward` | `(loss) -> void` — Backpropagate gradients from loss |
| `tensor.no_grad_begin` | Disable gradient tracking (inference mode) |
| `tensor.no_grad_end` | Re-enable gradient tracking |

### Per-Tensor

| Method | Description |
|--------|-------------|
| `t.set_requires_grad` | `(flag) -> void` — Enable/disable gradient for parameter |
| `t.requires_grad` | `() -> i32` — Check if gradient enabled |
| `t.grad` | `() -> tensor` — Get gradient tensor |
| `t.zero_grad` | `() -> void` — Zero out accumulated gradients |

### Supported Backward Ops

All forward operations that produce tensors track their computation graph when `requires_grad=1`. Backward is supported for: `add`, `sub`, `mul`, `mul_scalar`, `matmul`, `relu`, `gelu`, `tanh`, `softmax`, `exp`, `log`, `layer_norm`, `embedding_lookup`, `cross_entropy`, `sum`, `mean`, `neg`, `concat`.

### Training Pattern

```yaml
- call: [tensor.begin_graph]
- decl: [logits, tensor, { call: [model_forward, input, ...weights...] }]
- decl: [loss, tensor, { call: [tensor.cross_entropy, logits, targets, batch_size] }]
- call: [tensor.backward, loss]
- call: [tensor.adam_step, param, m, v, lr, beta1, beta2, eps, step]
- call: [param.zero_grad]
```

## 15. Optimizer: Adam

```yaml
- call: [tensor.adam_step, param, m, v, lr, beta1, beta2, eps, step]
```

| Argument | Type | Description |
|----------|------|-------------|
| `param` | tensor | Parameter to update |
| `m` | tensor | First moment (same shape as param, init to zeros) |
| `v` | tensor | Second moment (same shape as param, init to zeros) |
| `lr` | f32 | Learning rate |
| `beta1` | f32 | First moment decay (typically 0.9) |
| `beta2` | f32 | Second moment decay (typically 0.999) |
| `eps` | f32 | Epsilon for numerical stability (typically 1e-8) |
| `step` | i32 | Current step number (1-indexed, for bias correction) |

## 16. Persistence

Tensors can be saved to and loaded from AionDB KV stores (persisted to disk via WAL).

```yaml
# Save
- call: [tensor.save, my_tensor, "store_name", "tensor_key"]

# Load
- decl: [t, tensor, { call: [tensor.load, "store_name", "tensor_key"] }]
```

- **No KV handle needed** — the C runtime opens/caches the KV store internally (singleton)
- **Blob format**: `[ndim:i32][shape[0]:i32]...[shape[ndim-1]:i32][float32 data...]`
- Stored via `aiml_db_kv_set_blob` / `aiml_db_kv_get_blob` (binary, not string-serialized)
- Persistence via AionDB WAL (Write-Ahead Log) — survives process restart
- Store is created in `./aiondb/` directory by default

## 17. Utility

| Function | Description |
|----------|-------------|
| `tensor.print` | `(t, label) -> void` — Print shape and data |
| `tensor.fill` | `(t, val) -> void` — Fill all elements with value |

## 18. Implementation Notes

- Backend: OpenBLAS for matmul, pure C for everything else
- All tensor ops return new tensors (immutable-style); slice/reshape share data
- Autograd compute graph is global singleton, reset on `begin_graph`
- No GPU support (CPU only, suitable for small models)
- Max 8 dimensions per tensor
