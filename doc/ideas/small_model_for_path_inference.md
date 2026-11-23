# Small model for path inference

This note explores a very small specialized model whose only job is to infer
path-like intent from natural language and hand it to the next typed node.

The idea is intentionally narrow.

Instead of asking a large general model to both understand the user request
and choose concrete file-system targets, we split out a tiny semantic worker
for just one task:

- infer directories, files, path references, and workspace-relative intent

## Core intuition

A lot of developer interaction contains path intent that is only partly
explicit.

Examples:

- "in the card editor"
- "the file in the ideas folder"
- "the tests for cascade"
- "the file next to the parser"
- "the markdown under doc"

Humans resolve these phrases using:

- workspace structure
- naming conventions
- recent context
- nearby open files
- domain habits

A small specialized model could do exactly that, without trying to solve the
whole programming task.

## Why a small model makes sense here

This is a narrow semantic domain.

The model does not need:

- broad world knowledge
- creative writing ability
- deep code generation
- open-ended reasoning across many domains

It mainly needs:

- path pattern recognition
- project-structure inference
- relative/absolute path handling
- ambiguity detection
- confidence estimation

That makes it a strong candidate for a small, specialized component.

## Possible typed outputs

The model should not just emit raw strings.
It should emit typed resolution results.

For example:

```aiml
- model:
    name: ResolvedPath
    fields:
      - [path, string]
      - [kind, PathKind]
      - [confidence, f32]

- model:
    name: CandidatePaths
    fields:
      - [paths, list<ResolvedPath>]

- model:
    name: PathResolutionFailure
    fields:
      - [reason, string]
      - [needs_user_clarification, i32]

- enum:
    name: PathKind
    values: [file, directory, ambiguous, external]
```

Or more abstractly:

- `ResolvedPath`
- `CandidatePaths`
- `AmbiguousPathReference`
- `MissingWorkspaceContext`
- `ExternalPathReference`

The important point is:

- the next node should receive structured path meaning, not only text

## Example role in a cascade pipeline

A typed cascade or node system could look like this:

- user sentence
- path inference node
- path validation node
- file selection or directory scan node
- content read node
- downstream task node

Very roughly:

```text
UserText
  -> PathIntentResolver
  -> CandidatePaths
  -> PathValidator
  -> ExistingWorkspacePath
  -> FileReader
  -> FileContent
```

This is interesting because each stage becomes much narrower and more stable.

## Why this could improve context safety

A large general model often drifts because it is juggling too many things at
once:

- language interpretation
- task planning
- path guessing
- tool choice
- code reasoning

A dedicated path resolver would only do one thing.

That may improve:

- precision
- consistency
- reproducibility
- explainability
- lower hallucination rate around file locations

## Example in practice

Input sentence:

- "schau mal im card editor des 9rooms_v2"

Small path model might emit:

- `CandidatePaths([
    /.../test/9rooms_v2/client/card_editor_screen.aiml,
    /.../test/9rooms_v2/doc/backlog/card_editor_spec.md
  ])`

The next node can then:

- validate which paths exist
- rank by task intent
- ask for clarification if confidence is too low

## Better than raw string extraction

Simple regex or keyword extraction is not enough for this role.

Why:

- many references are indirect
- project naming is inconsistent
- folders and files share concepts
- user language is elliptical
- the correct path depends on active workspace context

So this is still a semantic problem.
But it is a much smaller semantic problem than general assistant reasoning.

## Strong fit with typed transitions

This path resolver fits especially well with the typed-transition cascade idea.

For example:

- node A returns `CandidatePaths`
- node B accepts `CandidatePaths` and returns `ExistingPath`
- node C accepts `ExistingPath` and returns `FileContent`

This means the system can enforce:

- no file reading before path resolution
- no path validation on raw free text
- no downstream code analysis on unresolved references

That makes the process easier to constrain.

## Why this could reduce token usage

This may also reduce token usage.

Instead of repeatedly asking a large model to restate path assumptions in text,
we can externalize that state once as a typed object.

Examples of wasted token patterns today:

- "I think you mean the card editor file under client"
- "There is also a spec file with a similar name"
- "I will inspect these candidate files first"

A typed path resolver could store this directly as machine state.
The next nodes can consume it without forcing the assistant to re-explain it
in prose every time.

## Training and implementation path

This is one of the more realistic small-model ideas because it can be trained
or tuned on relatively concrete data:

- sentence -> candidate path set
- sentence + workspace tree -> best matching path
- sentence + open files -> disambiguated path intent

It also allows hybrid implementation:

- small model proposes candidates
- deterministic filesystem and ranking logic confirms them

That is probably better than a pure neural solution.

## Short version

A tiny model for path inference is a good example of how many-small-model
architectures could work.

It has:

- a narrow responsibility
- structured outputs
- strong composability with typed nodes
- obvious practical value for developer tooling

So it is a plausible first prototype for the broader cascade-based AI idea.
