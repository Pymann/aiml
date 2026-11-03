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
| `tensor.seed` | `(n) -> void` | Fix the start of the random generator (§17.1) |

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
| `t.sha256` | `() -> string` | SHA-256 over tensor shape and raw `float32` payload |

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
| `tensor.div` | `(a, b) -> tensor` | Elementwise divide (`a[i] / b[i]`); no autograd |
| `tensor.mul_scalar` | `(t, s) -> tensor` | Scalar multiply |
| `tensor.neg` | `(t) -> tensor` | Negate |

**Broadcasting**: `tensor.add` supports 1D+2D broadcasting. A bias tensor `[dim]` added to a matrix `[seq, dim]` broadcasts automatically.

### In-Place Accumulation

Zero-allocation in-place operations for fused-multiply-add patterns. Modify the
`target` tensor directly — no new tensor is allocated, no arena memory is used.
Safe across `tensor.begin_graph` / `arena_reset` cycles because `target` must
already be heap-allocated (e.g. from `tensor.clone` of a parameter tensor).

| Function | Signature | Semantics |
|----------|-----------|-----------|
| `tensor.fma_inplace` | `(target, src, scale) -> void` | `target[i] += src[i] * scale` |
| `tensor.fma_sq_inplace` | `(target, src, scale) -> void` | `target[i] += src[i]² * scale` |
| `tensor.fma_abs_inplace` | `(target, src, scale) -> void` | `target[i] += |src[i]| * scale` |

**No autograd** — these are pure data mutations, not tracked by the compute graph.

Typical use: trajectory accumulation where S1/S2/R tensors live on the heap
and delta tensors are ephemeral (arena). The delta may be freed by a subsequent
`begin_graph`, but the target tensor survives because it was never arena-allocated.

```yaml
# Example: accumulate trajectory statistics in-place
- decl: [delta, tensor, { call: [tensor.sub, current_weight, initial_weight] }]
- call: [tensor.fma_inplace,     s1_accum, delta, alpha]   # S1 += alpha * delta
- call: [tensor.fma_sq_inplace,  s2_accum, delta, alpha]   # S2 += alpha * delta²
- call: [tensor.fma_abs_inplace, r_accum,  delta, alpha]   # R  += alpha * |delta|
```

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
| `tensor.weighted_cross_entropy` | `(logits, targets, weights, seq_len) -> tensor` | Weighted cross-entropy loss (autograd) |
| `tensor.abs` | `(t) -> tensor` | Element-wise absolute value (autograd) |
| `tensor.sign` | `(t) -> tensor` | Element-wise sign: -1/0/+1 (no autograd) |
| `tensor.step` | `(t) -> tensor` | Heaviside step: 0/0.5/1 (no autograd) |

**Token ID convention**: `embedding_lookup`, `cross_entropy` and `weighted_cross_entropy` accept a 1D float tensor where each float is a float-encoded integer ID (e.g., `3.0` means token ID 3). The C runtime casts `float -> int` internally. This avoids needing a separate integer tensor type.

**weighted_cross_entropy**: Like `cross_entropy` but each position's loss is scaled by `weights[b]`. Normalization: `L = Σ_b w[b] * loss[b] / Σ_b w[b]` (only valid positions). Masked positions (target < 0) are skipped regardless of weight. Backward: `grad[b,c] = w[b] * (softmax[b,c] - one_hot[b,c]) / Z`.

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

All forward operations that produce tensors track their computation graph when `requires_grad=1`. Backward is supported for: `add`, `sub`, `mul`, `mul_scalar`, `matmul`, `relu`, `gelu`, `tanh`, `softmax`, `exp`, `log`, `layer_norm`, `embedding_lookup`, `cross_entropy`, `weighted_cross_entropy`, `abs`, `sum`, `mean`, `neg`, `concat`.

### Gradient lifecycle (read before writing a training loop)

The runtime **accumulates** gradients into `t.grad` with `+=` on every backward
pass.  `tensor.begin_graph` resets the **graph** (arena + node list) but
**does not** zero parameter gradients.  Neither does `tensor.adam_step`.

That means **every training loop is responsible for calling `zero_grad`** on
each trainable parameter, otherwise gradients from previous iterations leak
into the next step.  Symptoms range from slow/incorrect convergence to
catastrophic loss explosion (often visible first as `inf`/`NaN` in larger
multi-head models with BCE-style auxiliary heads).  Two real incidents:

- `aiml/doc/issues/2026-06-04_autodiff_bce_multihead_gradient_accumulation.md`
- regression test: `aiml/tests/test_grad_lifecycle.aiml` (verifies the
  accumulator + zero_grad reset semantics)

**Required ordering** for every parameter `p`:

1. `tensor.begin_graph` once at the start of the iteration
2. forward pass (uses `p`)
3. `tensor.backward(loss)`
4. `tensor.adam_step(p, m, v, …)`
5. `p.zero_grad`   ← do not omit

Steps 4 and 5 are paired: every `adam_step` must be followed by a `zero_grad`
on the same tensor (or `zero_grad` must be called before the next
`backward`).  The runtime does not enforce this; missing it is silent.

If you intentionally want to accumulate gradients across multiple backward
passes (e.g. micro-batching), simply skip `zero_grad` until you have
accumulated all the desired contributions, then run `adam_step` + `zero_grad`
once.

### Training Pattern (multi-parameter)

```yaml
- call: [tensor.begin_graph]
- decl: [logits, tensor, { call: [model_forward, input, ...weights...] }]
- decl: [loss, tensor, { call: [tensor.cross_entropy, logits, targets, batch_size] }]
- call: [tensor.backward, loss]

# Adam step for every trainable parameter
- call: [tensor.adam_step, w1, m_w1, v_w1, lr, beta1, beta2, eps, step]
- call: [tensor.adam_step, b1, m_b1, v_b1, lr, beta1, beta2, eps, step]
- call: [tensor.adam_step, w2, m_w2, v_w2, lr, beta1, beta2, eps, step]

# MANDATORY: clear accumulated gradients before the next iteration
- call: [w1.zero_grad]
- call: [b1.zero_grad]
- call: [w2.zero_grad]
```

For larger models, factor the optimizer + zero_grad pair into a helper
function (see `tul/train/train_transformer.aiml` for a reference layout).

### 14.1 What `begin_graph` frees — and `tensor.keep`

`tensor.begin_graph` does more than reset the graph: it also **collects tensors for freeing**,
in two groups.

| Tensor | Collected? |
|--------|-----------|
| a graph output, slice or mask (`is_param = 0`) | yes, at the next `begin_graph` |
| a weight (`requires_grad = 1`) or one with a graph node | no |
| anything else older than one epoch — **including tensors you allocated yourself** | yes |

The third row is the trap.  An index or constant tensor built **once before** the loop —
`tensor.zeros(n)` for a row of ids, say — has `is_param = 1`, no gradient and no graph node, so
from the second iteration on it is older than one epoch and gets collected.  The loop then reads
freed memory, and `embedding_lookup` crashes on a nonsense shape.

Adam state does not hit this: `adam_step` refreshes the epoch of the tensors it touches.

Mark anything else you intend to keep:

```yaml
- decl: [ids, tensor, { call: [tensor.zeros, 64] }]
- call: [ids.keep]                      # mine — begin_graph leaves it alone

- loop: ...
    - call: [tensor.begin_graph]        # `ids` still valid in iteration 2, 3, ...
```

`tensor.keep` is idempotent, takes no arguments, and is only needed for tensors that live
**across** `begin_graph` calls without being weights.  A tensor marked `keep` is freed when its
own binding goes away, like any other value.

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
| `tensor.sha256` | `(t) -> string` — SHA-256 over tensor shape and raw `float32` payload |
| `t.keep` | `-> void` — exempt this tensor from `begin_graph`'s collection (§14.1) |

### 17.1 Reproducible randomness — `tensor.seed`

`tensor.randn` and `tensor.randn_ex` draw from the tensor runtime's **own** generator, not from
libc `rand()`.  It is a fixed splitmix64, so the same seed yields the same numbers in every
process and on every platform — a run can be repeated and compared.

```yaml
- call: [tensor.seed, 42]
- decl: [w_a, tensor, { call: [tensor.randn, 4, 4] }]

- call: [tensor.seed, 42]
- decl: [w_b, tensor, { call: [tensor.randn, 4, 4] }]
# w_a and w_b hold the same numbers
```

- The seed is an integer; any value is accepted.
- The state is **per thread**, so one thread's draws cannot shift another's.  Seed each thread
  that draws.
- Without a `tensor.seed` call the generator starts from a fixed constant, so an unseeded program
  is reproducible too — it is **not** seeded from the clock.
- This generator is for initialisation and noise, **not** for secrets; use `math.random_hex`
  (spec/43_0 §7) for anything security-sensitive.

### Tensor Hashing

`tensor.sha256(t)` computes a deterministic hash for a tensor's structural identity and values.

The hash input contains:

- a fixed runtime format marker
- `ndim`
- total element count
- all shape dimensions
- raw contiguous `float32` payload bytes in tensor storage order

The hash intentionally ignores autograd metadata such as `requires_grad`, gradient buffers, graph node IDs, and ownership flags. This makes it suitable for verifying model weight compatibility and detecting whether two weight tensors contain identical numerical data.

## 18. Implementation Notes

- Backend: OpenBLAS for matmul, pure C for everything else
- Most tensor ops return new tensors (immutable-style); slice/reshape share data
- Exception: `fma_inplace`, `fma_sq_inplace`, `fma_abs_inplace` mutate the target tensor in-place (zero allocation)
- Autograd compute graph is global singleton, reset on `begin_graph`
- No GPU support (CPU only, suitable for small models)
- Max 8 dimensions per tensor
