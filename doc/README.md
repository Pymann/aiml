# `doc/` — internal working material

This tree is **not** user documentation. It is the project's own working
record: implementation plans, audit reports, leak analyses, design proposals,
backlog items and post-mortems, many of them written for one specific change and
never revised afterwards. Dates in filenames are the date of writing; a document
describes the state of that day, not the current one.

Read it when you want to know *why* something is the way it is. Do not read it
to learn the language or the toolchain:

* the language and runtime are specified in [`../spec/`](../spec/) — that is the
  normative source, and it wins over both the code and this directory;
* using the language: [`../GETTING_STARTED.md`](../GETTING_STARTED.md);
* building it: [`../BUILDING.md`](../BUILDING.md).

Notable subdirectories: `analysis/` (audits), `plans/` and `backlog/` (open work),
`issues/` and `issue/` (investigations), `reports/`, `ideas/`, `embedded/`,
`review/`, `tech_sheet/` (can aiml do X today? — a question, a measured answer, what to
mind). Completed plans, superseded memory-model drafts and raw probe logs were
removed on 2026-11-24; they remain in the git history (before that, HEAD `6e7c9e8`).
