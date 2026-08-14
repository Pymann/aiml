# AIML Performance Summary

Date: 2026-06-04

## Local AIML measurements

| Benchmark | AIML JIT `compile+run` | AIML AOT to `.o` | Status | Notes |
|---|---:|---:|---|---|
| `loop_sum` | `0.05s` | `0.03s` | runnable | small integer loop |
| `list_push` | `0.05s` | `0.04s` | runnable | append-heavy list case |
| `string_build` | `0.17s` | `0.03s` | runnable | repeated string growth |
| `struct_array_walk` | `0.05s` | `0.03s` | runnable | list of structs + traversal |
| `map_put_get` | `4.87s` | `0.04s` | runnable | strong outlier; likely map/runtime hot path |

## Public reference numbers

These numbers are not the same workload as the local AIML cases above.
They are only context for where common C++/Rust/Python implementations often land.

| Public benchmark | C++ | Rust | Python 3 | Source |
|---|---:|---:|---:|---|
| `n-body` | `2.15s` | `2.19s` | about `6 min` | [Benchmarks Game: n-body](https://benchmarksgame-team.pages.debian.net/benchmarksgame/performance/nbody.html) |
| `spectral-norm` | `0.72s` | `0.72s` | `90.37s` | [Benchmarks Game: spectral-norm](https://benchmarksgame-team.pages.debian.net/benchmarksgame/performance/spectralnorm.html) |
| `binary-trees` | `1.01s` | `1.07s` | n/a here | [Benchmarks Game: binary-trees](https://benchmarksgame-team.pages.debian.net/benchmarksgame/performance/binarytrees.html) |
| `fannkuch-redux` | `3.33s` | `3.81s` | about `5 min` | [Benchmarks Game: fannkuch-redux](https://benchmarksgame-team.pages.debian.net/benchmarksgame/performance/fannkuchredux.html) |

## Notes

- AIML JIT values above include compile plus execution.
- AOT numbers here stop at object generation because full binary linking was not available on this machine (`clang` missing in link step).
- The current `map_put_get` result is the most suspicious performance issue in the local AIML benchmark set.
