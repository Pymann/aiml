# AIML Issue: Autodiff instability in multi-head BCE training

Date: 2026-06-04
Status: investigated
Area: tensor runtime / autograd
Severity: high

## Summary

In multi-head training setups with several BCE-style auxiliary heads sharing a hidden representation, training can explode catastrophically. A representative failure is the `names` head loss jumping to values like `6.35e+30` and only later self-recovering, which is enough to destroy the training run.

The initial suspicion was a numerical or autodiff bug in the BCE-with-logits formulation. After tracing the AIML tensor runtime, the more likely root cause is broader and more fundamental:

Parameter gradients are accumulated across iterations and are not cleared automatically between optimization steps.

This causes shared parameters to receive stale gradients from previous samples, and the effect scales badly with large output dimensions and multiple auxiliary heads.

## Observed training behavior

The user-reported pattern was:

| Test | Stable? |
| --- | --- |
| BCE alone, small dims, same inputs | yes |
| 2 BCE heads sharing hidden | yes |
| BCE plus `mul_scalar(0.5)` | yes |
| CE plus 2 BCE | yes |
| CE plus 3 BCE, varying inputs, small dims | no, sporadic explosions to `1e+30` |
| 4-head full `main.aiml`, large dims | no, severe accumulation and instability |

Representative large-dimension setup from `discovergen/sources/main.aiml`:

- `Nt = 240`
- `Nm = 584`
- `Nn = 491`

These dimensions are large enough that stale gradient accumulation becomes destructive very quickly.

## Affected training graph shape

The relevant training structure appears in:

- `discovergen/sources/main.aiml`
- `discovergen/sources/test_full_replica.aiml`

The model uses one shared hidden layer feeding:

1. entity head via cross-entropy
2. tag head via BCE-style loss
3. material head via BCE-style loss
4. name-token head via BCE-style loss

Losses are combined and a single `tensor.backward(loss)` is executed, followed by `tensor.adam_step(...)` calls for all parameters.

## Key runtime finding

The main issue is in the AIML tensor runtime:

- file: `src/sys/tensor_runtime.cpp`

Gradients are accumulated with `+=` during backprop, which is correct within a single backward pass.
However, parameter gradients are not reset between training iterations.

### Evidence

Gradient buffers are allocated lazily and then reused:

- `ensure_grad(...)` in `src/sys/tensor_runtime.cpp:288`

Most backward implementations accumulate into existing gradient buffers:

- `backward_add(...)` at `src/sys/tensor_runtime.cpp:1402`
- `backward_mul(...)` at `src/sys/tensor_runtime.cpp:1420`
- `backward_matmul(...)` at `src/sys/tensor_runtime.cpp:1498`
- `backward_log(...)` at `src/sys/tensor_runtime.cpp:1622`
- `backward_abs(...)` at `src/sys/tensor_runtime.cpp:1873`

`aiml_tensor_backward(...)` initializes only the loss gradient:

- `src/sys/tensor_runtime.cpp:1885`

`aiml_tensor_begin_graph(...)` clears graph metadata and ephemerals, but does not zero parameter gradients:

- `src/sys/tensor_runtime.cpp:1282`

`aiml_tensor_adam_step(...)` consumes `p->grad` but does not clear it afterward:

- `src/sys/tensor_runtime.cpp:1935`

A `zero_grad` API does exist:

- implementation: `src/sys/tensor_runtime.cpp:1277`
- declaration: `src/sys/tensor_runtime.h:86`

But the training loops in the AIML examples do not call it before each step.

## Why this matches the observed symptom

This explains the reported failure mode well:

- shared hidden parameters receive gradients from several heads in the same pass
- those gradients remain in the parameter grad buffers into the next iteration
- later iterations add new gradients on top of stale ones
- larger heads make the accumulation much stronger
- varying inputs make the effect look sporadic instead of trivially monotonic
- the biggest BCE heads are the first place where loss values visibly explode

So while BCE-with-logits may still deserve additional testing, the first concrete runtime bug is that gradient lifecycle semantics are unsafe for normal iterative training.

## Likely root cause

The runtime currently behaves as if gradients are persistent accumulators across optimization steps unless the caller manually clears them. That is a dangerous default for AIML training code, because the example and production-style training loops naturally assume PyTorch-like semantics:

- begin graph
- forward
- backward
- optimizer step
- next iteration starts from fresh parameter gradients

That semantic expectation is not currently guaranteed by the runtime.

## Most likely fix directions

### Option 1: zero all parameter grads in `aiml_tensor_begin_graph`

Pros:

- aligns with typical training-loop expectations
- fixes all existing training scripts without requiring call-site changes
- safest default behavior

Cons:

- would change semantics for any code intentionally accumulating gradients across iterations

### Option 2: zero `p->grad` inside `aiml_tensor_adam_step` after applying the update

Pros:

- also fixes common training loops
- ensures gradients are not reused after a step

Cons:

- less explicit
- if multiple optimizers or delayed step logic exist, this may be surprising

### Option 3: require explicit `tensor.zero_grad()` calls in all training code

Pros:

- explicit and conventional

Cons:

- all existing examples and user code become fragile until updated
- easy to forget
- does not address unsafe default runtime behavior

## Recommended first fix

Recommended immediate fix:

1. make `aiml_tensor_begin_graph()` clear gradients for all live parameter tensors with `requires_grad == 1`
2. add a regression test that fails if parameter gradients persist into the next iteration
3. optionally still expose explicit `zero_grad()` for advanced accumulation use cases

This gives safe default semantics while keeping manual control available if needed.

## Suggested regression tests

### 1. single-parameter accumulation test

Create a 2-iteration training loop with:

- one parameter tensor
- one scalar loss depending on it
- `backward`
- `adam_step`
- next iteration repeats

Assert that the second-step gradient matches only the second sample, not sample1 plus sample2.

### 2. multi-head shared-hidden test

Use the same structure as `discovergen/sources/test_full_replica.aiml`:

- CE head
- 3 BCE heads
- varying inputs each iteration

Track per-head loss values for 20 to 50 iterations and assert no runaway explosion under a deterministic seed.

### 3. explicit gradient accumulation test

If deliberate accumulation is desired as a supported feature, add a separate test where gradients are intentionally preserved across two backward passes without `begin_graph()` clearing them.
That would require an explicit API mode rather than implicit current behavior.

## Additional note on BCE suspicion

The BCE-with-logits forward formulation in the AIML scripts appears mathematically reasonable and earlier targeted tests reportedly matched expected values.

That means the BCE expression itself is probably not the first bug to fix.
The stronger evidence currently points to incorrect gradient lifecycle management in the runtime.

## Concrete code references

- `discovergen/sources/main.aiml:478-499`
- `discovergen/sources/test_full_replica.aiml:136-163`
- `src/sys/tensor_runtime.cpp:288-297`
- `src/sys/tensor_runtime.cpp:1277-1337`
- `src/sys/tensor_runtime.cpp:1402-1630`
- `src/sys/tensor_runtime.cpp:1873-1925`
- `src/sys/tensor_runtime.cpp:1935-1955`
- `src/sys/tensor_runtime.h:82-89`

## Conclusion

The most actionable current diagnosis is:

The AIML tensor runtime does not automatically clear parameter gradients between optimization steps, causing cross-iteration gradient accumulation. In shared multi-head training with large BCE outputs, this produces the observed catastrophic loss explosions.

Before investigating deeper BCE-specific autodiff issues, this gradient lifecycle bug should be fixed and regression-tested.
