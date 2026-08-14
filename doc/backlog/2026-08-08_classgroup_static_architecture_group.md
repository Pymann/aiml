# ClassGroup Static Architecture Group

Date: 2026-08-08
Status: backlog

## Summary

AIML should support a purely declarative `classgroup` YAML construct that
describes a small architecture cluster in one place: which classes participate,
which named instances exist, and which sibling instances each one is allowed to
see.

This is intentionally a **static structure feature**, not a hidden runtime
magic feature. The value is that the source file itself becomes a compact
architecture description that the compiler can validate and tooling can render
into an architecture graph.

The proposed shape is:

```yaml
classgroup:
  name: MatchRuntime
  instances:
    - class: ActionQueue
      instance: queue
      sees: [resolver, log]

    - class: DamageResolver
      instance: resolver
      sees: [log]

    - class: CombatLog
      instance: log
      sees: []
```

Each entry names:

- the class type (`class`)
- the concrete named instance (`instance`)
- the sibling instances it may reference (`sees`)

## Motivation

Today AIML can model the participating classes and the wiring separately, but it
does not have one static declaration that makes the intended subsystem topology
obvious to both humans and the compiler.

A `classgroup` would help with:

- making subsystem structure explicit
- documenting intended visibility between peer instances
- enabling compiler validation that declared links only target known instances
- generating architecture diagrams directly from source

This is especially attractive for tightly related subsystems where the main
question is not inheritance but the allowed peer-to-peer visibility graph.

## Proposed Constraints

- `classgroup` uses plain YAML syntax; no custom inline DSL.
- At most one `classgroup` may appear per file.
- `classgroup.name` is required.
- `classgroup.instances` is required and contains dictionaries.
- Every entry must contain `class`, `instance`, and `sees`.
- `instance` names must be unique within the group.
- Every `sees` target must name another `instance` declared in the same group.
- The compiler must reject unknown `sees` targets statically.

Likely additional validation worth deciding explicitly:

- whether self-reference in `sees` is allowed
- whether duplicate names inside `sees` are rejected
- whether cycles are fine or should only be diagnosed optionally

## Non-Goals

- No automatic "instantiate everything in the group" semantics in the first cut.
- No implicit ownership/lifecycle semantics derived from `sees`.
- No cross-file graph resolution in the first version; the core value already
  exists with one-file local validation.

`sees` should mean "this instance may reference that sibling instance" and not
quietly imply construction order, storage ownership, or mandatory runtime call
edges.

## Compiler / Tooling Payoff

- semantic validation of local architecture declarations
- clearer diagnostics when a visibility target is misspelled or removed
- direct graph export for docs or review tooling
- a path toward future checks that illegal peer references in code match the
  declared `sees` graph

## Open Questions

- Is `classgroup` initially only documentary + validated metadata, or should
  later phases enforce actual code-level reference restrictions from it?
- Should exported/public instances be modeled separately from purely internal
  peer visibility?
- Does the first version live only at the source/spec level, or is there also a
  runtime reflection representation?

## References

- AIML backlog direction: stronger static architecture declarations
- Related use case discussed for explicit subsystem instance graphs and diagram
  generation
