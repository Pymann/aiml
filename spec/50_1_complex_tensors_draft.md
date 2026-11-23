# DRAFT — Complex Tensors for AIML

Status: not final  
Maturity: draft / basis for discussion  
This spec is deliberately **not finished yet**. It describes a
possible direction for complex tensors in AIML, not a language feature
that is already settled or fully implemented.

## 1. Goal

AIML should support complex numbers **first** as a specialised numerical tool
inside the tensor system, not straight away as a new general
primitive type for the whole language.

The guiding idea:

- no immediate first-class scalar type `complex`
- instead a controlled extension of the existing tensor runtime
- focus on scientific models, signal processing, FFT,
  phase models, rotations and experimental ML architectures

## 2. Why no immediate `complex` primitive type

A general language type `complex64` or `complex128` would immediately touch many
dependent systems:

- literal syntax
- cast rules
- operator promotion
- FFI / ABI
- persistence
- debug formatting
- collections and maps
- static type rules

That is too broad for AIML at present.

Most realistic use cases are in the tensor and numerics area anyway.
A tensor-centred entry point is therefore far more realistic.

## 3. Basic idea

The existing `tensor` runtime currently stores `float32` data.
Complex tensors can be modelled as an extension of this system.

Possible target types:

- `tensor` stays real for now
- `ctensor` as a separate complex tensor type

or later:

- `tensor` with a `dtype`
- such as `f32`, `f64`, `complex64`, `complex128`

For a first AIML step, `ctensor` is probably simpler than
a complete general `dtype` rework.

## 4. Provisional recommendation

### Phase 1

A new complex tensor type:

```yaml
- decl: [z, ctensor]
```

or as the return value of factory functions:

```yaml
- decl: [z, ctensor, { call: [tensor.complex_from_parts, re, im] }]
```

### Phase 2

Later, an optional general `dtype` approach:

```yaml
- decl: [z, tensor, { call: [tensor.zeros_dtype, "complex64", 8, 8] }]
```

This second step is **not** part of this first draft spec.

## 5. Planned core operations

The following operations would be the most important scope for a first
complex tensor API.

### 5.1 Construction

```yaml
- decl: [z, ctensor, { call: [tensor.complex_from_parts, re, im] }]
- decl: [re, tensor, { call: [tensor.real, z] }]
- decl: [im, tensor, { call: [tensor.imag, z] }]
```

Semantics:

- `re` and `im` must have the same shape
- the result is a complex tensor of the same shape

### 5.2 Element-wise operations

```yaml
- decl: [zc, ctensor, { call: [tensor.conj, z] }]
- decl: [za, tensor,  { call: [tensor.abs_complex, z] }]
- decl: [zp, tensor,  { call: [tensor.phase, z] }]
- decl: [out, ctensor, { call: [tensor.cmul, a, b] }]
- decl: [out, ctensor, { call: [tensor.cadd, a, b] }]
- decl: [out, ctensor, { call: [tensor.csub, a, b] }]
```

Note:

- `tensor.abs_complex(z)` should return the magnitude, i.e. a **real**
  tensor
- `tensor.phase(z)` should return the angles in radians, also as a
  real tensor

### 5.3 Mixed scalar forms

```yaml
- decl: [out, ctensor, { call: [tensor.cmul_scalar, z, 0.5] }]
- decl: [out, ctensor, { call: [tensor.cadd_real, z, bias] }]
```

Still open:

- whether these mixed forms are really needed
- or whether they are deliberately kept small at first

### 5.4 Mathematical reductions

```yaml
- decl: [s, ctensor, { call: [tensor.csum, z, -1] }]
- decl: [m, ctensor, { call: [tensor.cmean, z, -1] }]
```

Important:

These are true complex means, i.e. component-wise averaging of the
real and imaginary parts. Not an angular mean.

### 5.5 Complex dot product

```yaml
- decl: [score, ctensor, { call: [tensor.cdot, a, b] }]
```

Semantics:

- `cdot(a, b) = sum(conj(a_i) * b_i)`

Optional additional helpers:

```yaml
- decl: [score_re, tensor, { call: [tensor.cdot_real, a, b] }]
- decl: [score_im, tensor, { call: [tensor.cdot_imag, a, b] }]
```

## 6. Extended operations

These operations look useful but should probably **not** be part
of the minimal first step:

- `tensor.cmatmul`
- `tensor.fft`
- `tensor.ifft`
- `tensor.eig`
- `tensor.svd_complex`
- complex `layer_norm`
- complex activations

The danger would be making the runtime too big too early.

## 7. Autograd

Status: **still open, not final**

This is one of the biggest open points.

For complex tensors there are at least three conceivable approaches:

1. no gradients in the first step
2. gradients via separate real/imaginary channels
3. explicit Wirtinger-oriented semantics

Recommendation for AIML v1:

- introduce complex tensors first without a full autograd guarantee
- or only for a small, safe core of ops:
  - `cadd`
  - `csub`
  - `cmul`
  - `conj`
  - `real`
  - `imag`
  - `csum`
  - `cmean`

The final gradient semantics are **not yet decided**.

## 8. Persistence

Status: **still open, not final**

A possible blob format would be:

- `ndim`
- shape
- `dtype = complex64`
- raw data, interleaved as
  - `re0, im0, re1, im1, ...`

Open questions:

- a separate `ctensor.save` / `ctensor.load` path?
- or a general `tensor.save` extension with `dtype`?
- what hashing should look like for complex tensors

## 9. Print / debug

Status: **still open, not final**

Clear rules would be needed for:

- the `tensor.print` representation
- rounding
- display as
  - `(re, im)`
  - or `a + bi`

Recommendation:

- store interleaved internally
- readable `a + bi` representation for user output

## 10. Minimal first scope

If AIML takes on complex numbers, the first scope should stay small:

- `ctensor`
- `tensor.complex_from_parts`
- `tensor.real`
- `tensor.imag`
- `tensor.conj`
- `tensor.cadd`
- `tensor.csub`
- `tensor.cmul`
- `tensor.csum`
- `tensor.cmean`
- `tensor.cdot`

That would already be enough for:

- complex resonance models
- holographic binding
- phase rotations
- scientific visualisation
- many signal-processing experiments

## 11. What is deliberately not part of this spec yet

The following are deliberately left out:

- a general `complex64` primitive language type
- complex literals such as `3 + 4i`
- operator overloading for `+`, `*`, `/` on scalars
- maps or lists of complex scalars as a basic concept
- full complex autograd coverage
- a complete FFT/DSP standard library

## 12. Recommendation

If AIML introduces complex numbers, it is best done:

1. first as a **draft spec for complex tensors**
2. then as a small runtime extension without a big bang
3. only much later as a general primitive language type, if really
   needed

## 13. Open questions

- Should the first type be called `ctensor`, or should `tensor` be prepared
  directly for a future `dtype`?
- Should complex autograd support be missing in the first step or only
  partially present?
- Should `cdot` return a complex result or offer a real
  resonance readout by default?
- How scientific should the API be, and how ML-pragmatic?

## 14. Clear notice

This spec is **not final**.

It is a working draft for deciding:

- whether AIML should take on complex numbers at all
- and if so, in what smallest reasonable form
