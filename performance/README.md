# AIML Performance

This directory holds standalone performance benchmarks for AIML.

It is intentionally separate from `tests/` and from the normal test runner.
The goal is repeatable measurement, not functional pass/fail coverage.

## Scope

The first benchmark set focuses on language/runtime building blocks that are
easy to compare against C++, Rust, and Python:

- integer loop + reduction
- list append
- map put/get
- string building
- struct array walk

These cases avoid graphics, network, filesystem, and database effects so the
results stay dominated by compiler/runtime behavior.

## Layout

- `cases/` AIML benchmark programs
- `baselines/` optional reference implementations in other languages
- `results/` local output files from benchmark runs
- `run.sh` simple benchmark runner

## Metrics

The runner is meant to capture three different costs:

1. JIT cold run
2. AOT compile time
3. AOT binary run time

That separation matters. AIML may be good at one of these and weak at another,
and comparing only a single end-to-end number would hide that.

## Usage

From the AIML repo root:

```bash
./performance/run.sh
```

Single benchmark:

```bash
./performance/run.sh loop_sum
```

Results are written under `performance/results/`.

## Notes

- Prefer release builds of the `aiml` compiler before running benchmarks.
- Run on a quiet machine.
- Repeat the same case multiple times and compare medians, not single runs.
- Keep baseline implementations algorithmically identical to the AIML case.
