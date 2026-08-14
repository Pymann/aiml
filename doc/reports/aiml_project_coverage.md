# Task: Add Coverage Reporting for AIML Projects

## Goal

Enable coverage reporting for projects written in AIML, where both the
application code and its tests are authored in AIML.

The intended outcome is coverage over `.aiml` source files, not over the C++ or
generated LLVM IR implementation details.

## Implementation Notes

- Reuse LLVM's existing profiling and reporting pipeline where possible:
  `-fprofile-instr-generate`, `llvm-profdata`, and `llvm-cov`.
- Add an AIML compiler mode such as `--coverage` that emits profiling
  instrumentation together with coverage mapping for AIML source ranges.
- Coverage must map back to AIML file/line/column locations so reports stay
  useful to AIML authors.
- Prefer an MVP based on function/region/branch coverage before considering
  richer domain-specific metrics.

## MVP Scope

- function coverage for AIML `def` and other executable top-level bodies
- region coverage for executable AIML blocks
- branch coverage for `if`/`else`, match-like constructs, and other explicit
  control-flow splits
- report generation via `llvm-profdata merge` and `llvm-cov show/report/export`

## Open Investigation

The implementation should explicitly resolve these design questions:

- which AIML AST nodes become coverage regions
- whether model field initializers and default expressions count as executable
  regions
- how generated helper code is hidden or attributed so reports remain AIML-first
- whether tests should merge into one profile per suite or preserve per-test
  profile artifacts
- whether the compiler should emit JSON export helpers in addition to raw
  `llvm-cov` output

## Suggested Test Coverage

- a simple AIML function is reported as executed when called from an AIML test
- an uncalled AIML function is reported as uncovered
- both true and false arms of an `if` can be distinguished in branch coverage
- multi-file AIML projects report coverage for the correct source file
- generated/helper code does not surface as user-facing uncovered AIML code
- repeated test runs can be merged through `llvm-profdata`

## Acceptance Criteria

- AIML compiler exposes a documented coverage mode
- running AIML tests can produce `.profraw` data for the translated program
- merged profile data can be rendered with `llvm-cov` against AIML source files
- reports identify uncovered AIML functions/regions/branches with correct source
  locations
- at least one end-to-end test proves coverage collection on a real AIML
  project
