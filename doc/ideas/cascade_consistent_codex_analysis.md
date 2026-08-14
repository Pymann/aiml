# Codex analysis: `cascade<T>` idea

This note belongs to `cascade_consistent_codex_design.aiml` and explains why that draft was written the way it was.

## Intent

The draft is intentionally not written as an implementation plan for the current editor. It is a consistency-first attempt to define what `cascade<T>` would need to mean as a language concept before UI helpers, prefab conveniences, or bytecode-specific sugar are layered on top.

## Core position

The central idea is:

`cascade<T>` should be treated as a real tagged variant value, not as a flat model with recycled parameter slots.

That implies a few invariants the draft tries to protect:

- exactly one active node at a time
- only the payload of the active node exists
- node switches replace payload rather than partially mutating it
- wire-relevant node ids must be explicit and stable
- empty/unset should not be implicit inside the type

## Why the text became dense

The draft became verbose because it tried to pin down semantics that are often left implicit:

- what `new SomeCascadeType` means
- whether an unset state exists
- how recursive cascades remain finite
- what survives a node switch
- how serialization stays deterministic

That makes it more complete, but also more rigid and heavier to scan, especially for AI-assisted iteration.

## Likely next improvement

If this idea is kept, the better next version is probably shorter and split into two layers:

1. a small normative core defining only `cascade` semantics
2. a separate ideas/runtime note for prefab binding, list widgets, editor behavior, and ergonomics

## Non-goal

This draft does not claim to be "the right syntax". It is mainly a consistency sketch for the meaning of the type.
