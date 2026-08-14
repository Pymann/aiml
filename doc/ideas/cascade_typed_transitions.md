# `cascade<T>` with typed transitions

This note extends the plain `cascade<T>` idea.

The basic cascade idea is:

- one active node
- node-specific payload
- recursive substructure

This document explores a stronger version:

- nodes can describe not only payload
- but also the type of the next valid transition

That means a cascade is no longer only a typed state tree.
It becomes a typed transition system.

## Core intuition

A normal cascade says:

- "given the chosen node, these fields exist"

A transition-aware cascade says:

- "given the chosen node, these fields exist"
- "and only certain next nodes are type-compatible successors"

So the type system constrains not only the shape of state, but also the shape
of legal continuation.

## Why this matters

Many reasoning systems already behave like this informally.

A step produces something, and the next step is only valid if it accepts that
kind of output.

Examples:

- a search node returns a query result set
- the next node must accept result sets, not raw user goals
- a planner returns a plan
- the next node must accept plans, not arbitrary text
- a parser returns a typed structure
- the next node must accept that structure

In current LLM systems this matching is often implicit and fragile.
Typed transitions would make it explicit.

## Minimal abstract model

A transition-capable node can be thought of as carrying three things:

- local payload
- output type
- admissible next-step interfaces

Very abstractly:

- `Node<A, B>` consumes `A`
- it produces `B`
- it may only connect to a node that consumes `B`

This is close to typed composition.

## Cascade as typed continuation graph

A stronger cascade system could let nodes reference templates of interfaces or
node-families instead of only concrete child data.

Then a cascade definition can say:

- this node returns `SearchResults`
- the next node must implement `Accepts<SearchResults>`

That gives us a graph where type-compatibility is part of the definition.

## Sketch of the idea

Below is not proposed syntax, only a semantic sketch.

```aiml
- interface:
    name: Accepts<T>
    methods:
      - def: { name: accept, args: [[value, T]] }

- interface:
    name: Produces<T>
    methods:
      - def: { name: produce, return: T }

- cascade:
    name: ReasonNode
    nodes:
      - search:
          returns: SearchResults
          next: [Accepts<SearchResults>]
          fields:
            - [query, SearchQuery]

      - summarize:
          accepts: SearchResults
          returns: Summary
          next: [Accepts<Summary>]
          fields:
            - [style, SummaryStyle]

      - decide:
          accepts: Summary
          returns: Decision
          next: [Accepts<Decision>]
          fields:
            - [policy, DecisionPolicy]
```

The important idea is not the syntax.
The important idea is:

- next-step legality is checked through type compatibility
- the output of one node determines the admissible space of successors

## Stronger than plain tree structure

A plain cascade captures nested meaning.
A transition-aware cascade captures legal semantic motion.

That is a much stronger concept.

Instead of representing only:

- what a node means

it also represents:

- what may meaningfully follow from it

## Why interfaces and templates are interesting

If next nodes had to be named concretely, the system would become rigid.

Interfaces keep it open.
A node can say:

- "I do not care which concrete node follows"
- "I only require that it accepts `Result<X>`"

Templates make this reusable.
The type parameter of one node becomes the type argument of the next contract.

That allows patterns like:

- `Planner<TGoal> -> Plan<TGoal>`
- `Executor<TPlan> -> Result<TPlan>`
- `Reducer<TResult> -> Summary<TResult>`

This is much closer to typed pipelines than to plain dropdown trees.

## Relevance for AI systems

This gets interesting for AI because many failures in LLM agents are really
transition failures.

The model produces something that sounds plausible, but the next step is not
actually compatible with it.

Examples:

- wrong tool argument type
- wrong abstraction level
- step skipped too early
- result treated as if it were already validated
- plan treated as if it were an execution result

Typed transitions could prevent a class of these errors by construction.

## Potential benefit for LLM agents

If a model reasons in a space of typed transitions, it may need less free-form
meta-text like:

- "now I should summarize this"
- "next I need a tool that can accept these results"
- "this output must be converted before the next step"

Those relations would already exist in the transition graph.

That could improve:

- reliability
- token efficiency
- debuggability
- replayability
- compositional training

## Important distinction

This is not just static typing for data.
It is typing for process.

That is the key leap.

A plain cascade is a typed value.
A transition-aware cascade is a typed evolution of values.

## What this resembles

Conceptually this touches ideas from:

- typed continuations
- session types
- compositional IRs
- workflow type systems
- category-style composition
- neuro-symbolic control graphs

It is not identical to any one of them, but it rhymes with all of them.

## Biggest open question

The biggest design question is where transition typing should live.

Possible answers:

1. inside `cascade` itself
2. in a separate transition layer over ordinary cascades
3. in interfaces implemented by nodes generated from cascades

My instinct is that option 2 may be the cleanest.

Why:

- plain `cascade` stays simple
- transition logic can be added only where needed
- data semantics and process semantics stay separable

## A useful framing

A good framing may be:

- `cascade<T>` models semantic state
- transition contracts model admissible semantic continuation

Then a reasoning system can use both:

- cascades for explicit structured state
- typed transition rules for what may come next

## Short version

If plain cascades are typed branching values,
then cascades with interface-template successors could become typed branching
processes.

That would be a much more powerful substrate for agents and reasoning systems
than free-form token continuation alone.
