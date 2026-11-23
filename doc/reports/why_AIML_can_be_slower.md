# Why AIML Can Still Be Slower Than Runtime C++

Yes: AIML is compiled.

That means the performance question is **not** "interpreted vs compiled".
The real question is:

- how much work the compiler can lower efficiently
- how many runtime objects and helper calls the generated code produces
- how much direct control the implementation has over memory layout and traversal state

---

## 1. Compiled Does Not Mean Optimal

Compiled AIML still runs through:

- generated IR
- runtime helper calls
- generic list/model/string operations
- builtin boundaries

If an operation is naturally:

- streaming
- cursor-based
- allocation-sensitive
- data-structure-heavy

then a direct runtime implementation in C++ can still be much tighter than a compiled AIML version.

---

## 2. The Main Reasons Performance Can Be Worse

### 2.1 More temporary allocations

In AIML, complex logic often creates:

- intermediate `string`
- intermediate `list<T>`
- temporary models
- temporary filtered subsets

In C++, the same algorithm can often:

- reuse buffers
- mutate in place
- avoid materializing temporary collections

This matters a lot for:

- large directory walks
- text chunking
- pattern matching
- vector preprocessing

### 2.2 More generic helper calls

A tight C++ loop can do all of this inline:

- iterate entries
- test wildcard
- append result
- stop at limit

An AIML implementation typically expands into many generic operations:

- `list.get`
- `string.len`
- `string.substr`
- `if`
- `call`
- `push`
- more bounds/type handling

Even when compiled, this usually means more instructions and more helper traffic.

### 2.3 Less control over hidden state

Many efficient algorithms need compact internal state:

- cursors
- iterators
- stacks
- queues
- caches
- overflow state

In C++, this can live in a private struct and stay compact.
In AIML, the same logic often has to be represented more explicitly and less compactly.

That increases:

- memory traffic
- state copying risk
- synchronization overhead in some designs

### 2.4 Strings and lists are expensive at scale

AIML is very productive when composing logic around:

- `string`
- `list<T>`
- models

But those are not free.
If the hot path becomes "split, substr, compare, push, rebuild", performance drops fast.

This is especially true when the algorithm is fundamentally:

- character-level
- entry-level
- traversal-heavy

### 2.5 Generic code is harder to specialize

A dedicated runtime path can be hand-optimized for one specific job.

Example:

- match glob pattern
- walk one directory
- fill one result list
- stop exactly at `max_findings_per_step`

That is highly specialized logic.

AIML code tends to be more general and compositional.
That is good for flexibility, but worse for peak efficiency.

---

## 3. Where AIML Is Still Good

AIML is often perfectly fine when it is used for:

- orchestration
- routing
- policy logic
- glue code
- small and medium control flows
- prototyping semantics
- composing existing builtins

This is why an AIML implementation is often the right first step when:

- semantics are still evolving
- datasets are small
- the feature is not on a hot path

---

## 4. Where Runtime C++ Usually Wins

Runtime C++ usually wins when the task is:

- filesystem traversal
- wildcard matching
- parser core
- streaming transform
- dense numeric kernel
- memory-sensitive iteration
- large-scale filtering

These are the places where:

- fewer allocations
- tighter loops
- hidden cursor state
- direct container access

matter much more than language ergonomics.

---

## 5. Example: Why `Globber` Should Be a Builtin

The proposed `Globber` is a good example.

It needs:

- wildcard matching for `*`, `?`, `**`
- incremental traversal
- directory-by-directory output
- bounded result allocation
- hidden traversal state

You *can* prototype parts of this in AIML.
But the full feature is better as a runtime builtin because:

- the traversal state is naturally internal
- result limiting must be strict
- recursive walks can get large
- directory iteration should stay close to `std::filesystem`

This is exactly the kind of feature where compiled AIML is still the wrong layer for the final implementation.

---

## 6. Practical Rule

Use AIML first when:

- you are still discovering semantics
- performance is not yet the bottleneck
- the problem is mainly orchestration or policy

Move to runtime/compiler when:

- the semantics are stable
- the path is hot
- memory usage matters
- data sizes are large
- the feature is fundamentally a reusable primitive

---

## 7. Short Summary

AIML can be slower than direct runtime C++ even though it is compiled because:

- it often creates more temporary objects
- it uses more generic helper operations
- it has less compact internal state
- it has less direct control over memory and traversal layout

So the real distinction is not:

- compiled vs interpreted

but:

- generic high-level composition vs specialized low-level runtime implementation

