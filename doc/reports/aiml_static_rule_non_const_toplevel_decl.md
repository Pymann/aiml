# Task: Implement AIML Static Rule for Non-`const` Top-Level `decl`

## Goal

Implement the static-analysis rule specified in
`aiml/spec/49_static_analysis.md`:

- top-level `decl` is only allowed with `{ const: true }`
- any top-level `decl` without `{ const: true }` is a compiler error
- the diagnostic must explicitly mention `new` and recommend moving state into
  a class instance on the heap

## Implementation Notes

- Always write the test first.
- Add or update compiler tests before changing parser/compiler behavior.
- The diagnostic text should match the spec closely enough that the `new`
  guidance is guaranteed to stay present.

## Open Investigation

The implementation must explicitly investigate these edge cases before the rule
is finalized:

- prefab behavior
- external `decl` behavior, especially whether `extern` declarations should be
  exempt, rejected, or handled separately

Document the conclusion in the implementation PR or follow-up spec/task note if
special handling is required.

## Suggested Test Coverage

- top-level `decl` without modifier object fails
- top-level `decl` with unrelated modifier object but without `{ const: true }`
  fails
- top-level `decl` with `{ const: true }` succeeds
- local `decl` inside `def` still succeeds
- diagnostic mentions `new`
- prefab-related cases are covered once expected behavior is clarified
- `extern decl` cases are covered once expected behavior is clarified

## Acceptance Criteria

- failing tests are written first
- compiler enforces the rule for all normal top-level modules
- diagnostic includes the required `new` hint
- prefab behavior is investigated and documented
- external `decl` behavior is investigated and documented
