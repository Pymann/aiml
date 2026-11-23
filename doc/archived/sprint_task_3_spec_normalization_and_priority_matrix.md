# sprint_task_3

## Title

Normalise the spec status and maintain it as a living priority matrix

## Status

Partly prepared by existing review documents, but not yet condensed into a reliable working matrix.

## Goal

The AIML spec should not just be collected but turned into a current, actionable status: binding, open, contradictory or already superseded.

## Current state

- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md) and [spec_impl_completeness.md](../../spec_impl_completeness.md) already exist.
- So there is groundwork already; the bottleneck today is maintenance, prioritisation and slimming down rather than an initial survey.

## What was outdated in the old version

- The wording sounded like a complete initial inventory.
- The number of old issues is too coarse as a steering model.

## Focus

1. Condense the existing review work into "normative / open / superseded / replaced".
2. Name only a few concrete blockers per language chapter.
3. Derive an order for compiler, runtime and test work from that.

## Affected places

- [spec](../../spec)
- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md)
- [spec_impl_completeness.md](../../spec_impl_completeness.md)

## Acceptance criteria

- For every important language chapter it is visible whether it is normative, open or superseded.
- Subsequent sprint tasks no longer have to carry a spec inventory along.
- Contradictions and legacy issues are clearly separated from real follow-up work.
