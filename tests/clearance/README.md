# tests/clearance — acceptance probes for spec/65 (symbol clearance)

**Status 17.10.2026: `STAGE=writedown` is GREEN** (12 positive, 25 negative probes), i.e.
all stages up to step 8. **Two** are PENDING, and not because of this chapter:
`11_create_allowed` and `neg/n20` need spec/29_2 §4.3 (`# NEEDS: dynload`).

The probes were written **before** the code (working principle 3: spec and tests first) and
record the decisions that are easy to get wrong later — several times they uncovered a
design error before there was any code (F65-26 would have forbidden every script; the
access check first needed the export table; F65-38's criterion was impossible as
specified). Measured against the corpus: 869 tests, no regression — and that is noted
here instead of being implied by a missing test.

```
bash tests/clearance/run.sh        # all
ONLY=03 bash tests/clearance/run.sh
```

## The two session headers

A clearance only exists at a boundary (F65-2), and the boundary is set by the **host**, never by
the source (F65-6). A probe therefore declares its session in its header, and the runner turns it
into environment:

| Header | Meaning |
|---|---|
| `# CLEARANCE: none` | trusted mode — the pass does not run at all (F65-1). **Not** the same as 1. |
| `# CLEARANCE: 1` | the admin console: every annotated level reachable, still restricted (F65-7) |
| `# CLEARANCE: 7` | an ordinary restricted session (`AIML_CLEARANCE`, F65-20) |
| `# STATIC-CLEARANCE: on` | the opt-in flow rule in a trusted build (`AIML_STATIC_CLEARANCE`, F65-17) |
| `# CREATE-LEVEL: 7` | the host permits this session to DEFINE symbols (`AIML_CREATE_LEVEL`, F65-26) |
| `# REPLACE-LEVEL: 7` | …and to REPLACE existing ones, i.e. hot-reload (`AIML_REPLACE_LEVEL`, F65-26) |

## The asymmetry at `0` (F65-22), which four probes guard together

`0` is the value a forgotten field holds, so the two sides of the scale treat it oppositely:
on a **symbol** it is the closed end (reachable by nobody), as a **clearance** it is invalid.
An uninitialised integer must fail, not pass.

| Probe | Pins |
|---|---|
| `neg/n13_clearance_zero_invalid` | clearance `0` is refused outright — never "admin" |
| `neg/n14_level_zero_unreachable` | level `0` is reachable by nobody, **not even** clearance 1 |
| `neg/n02_unannotated_is_zero` | an unannotated symbol is level `0`, so omission never grants |
| `05_admin_is_not_trusted` | …while clearance 1 does reach every *annotated* level |

## What each probe pins

| Probe | Requirement | Pins |
|---|---|---|
| `01_trusted_default` | F65-1, F65-17 | the single-binary case: annotations present, **nothing** enforced — including an edge the opt-in rule would reject |
| `02_clearance_allows` | F65-4 | the ordering `clearance <= level`, equality included |
| `03_grants_gateway` | F65-10 | `grants:` relaxes the flow rule inside one body only |
| `04_flow_check_clean` | F65-17, F65-5 | a monotone chain passes with the opt-in rule on |
| `05_admin_is_not_trusted` | F65-7 | clearance 1 reaches every annotated level… |
| `neg/n12_admin_still_restricted` | F65-7, F65-14 | …and is **still** a console: the dropped dialect applies |
| `neg/n01` | F65-4 | denial above clearance |
| `neg/n02`, `neg/n13`, `neg/n14` | F65-5, F65-22 | the `0` asymmetry, see the table above |
| `neg/n03` | F65-9 | the detour: an open function reaching a restricted one |
| `neg/n04` | F65-6 | no self-certification |
| `neg/n05` | F65-13 | `extern` outside the host's export list |
| `neg/n06`, `neg/n07`, `neg/n08` | F65-14 | the three one-line bypasses: pointer cast, shell, store |
| `06_isolated_defines_freely` | F65-26, F65-27 | an isolated session defines freely (see the write side below) |
| `07_level_syntax_everywhere` | F65-19 | `level` is accepted in ALL forms |
| `neg/n09`, `neg/n10`, `neg/n11`, `neg/n17`, `neg/n18` | F65-17, F65-21 | annotation syntax is checked in **every** build, the trusted one included |

A negative probe that is rejected for the *wrong* reason counts as failed — the runner compares
the identifier from spec/65 §10 (F65-21), not merely the exit code. Otherwise a typo anywhere
would make this lab look green.

## Still missing

**F65-18** ("if nothing is annotated and no clearance is set, the pass is not constructed") is the
acceptance test that F65-1 is real, and it cannot be written as a probe — it is a statement about
the compiler, not about a program. It needs an observable: a debug counter or a log line that must
be **absent**. To be added with the implementation, together with a compile-time measurement on an
unannotated corpus file.

## The write side (§5b) — and what the lab cannot check about it

Reading can be checked with a single probe: the clearance alone decides. **Writing
cannot**, because adding, replacing and colliding need a *host* with an export table to
write against, and `aiml file.aiml -r` has none.

What the lab covers today:

| Probe | Requirement | Pins |
|---|---|---|
| `06_isolated_defines_freely` | F65-26, F65-27 | an isolated session defines its own symbols **freely**, and can call its own creation, because its level is the clearance |
| `neg/n16_level_in_restricted_source` | F65-27, F65-6 | `level:` in restricted source is self-certification, not a declaration |

**F65-26 was wrong at first, and the probes uncovered it.** The draft required
`create_level` for every definition; since every aiml program needs a `- def: main` (spec/08),
the default `0` would have forbidden *every script*. The version of 17.10.2026 separates them:
one's own, isolated definitions are free — only what gets into the **host's table** is regulated.
The probe `n15_create_forbidden` that used to be here has been dropped; its successor needs a
host and is listed below.

**The host fixture exists since 17.10.2026** (`09_host_exports_callable`): a host in one
file that registers its own `def`s with address and level via `sys.export_symbol` and runs a bundle
against them in a restricted session. With it the requirements of the write side are
measured instead of only specified — `# NEEDS: write`:

- **F65-26 / `clearance-create`** — putting a symbol into the host's table (only possible in the shared
  context, F65-31) without `create_level`. The guard is built, but **not reachable
  today**, and that is a finding about the language means, not about the requirement:
  `sys.bundle` takes the NAME of a function that the host compiled itself, so every
  bundle carries a name the host ALREADY HAS — and ends up in the REPLACE branch. Measured
  17.10.2026 on `fresh_helper`. The two probes (`11_create_allowed`, `neg/n20`) are
  therefore on `# NEEDS: dynload` and report PENDING until spec/29_2 §4.3 exists.
- **F65-28 / `clearance-replace`** — supplying a body for a host symbol without
  `replace_level`: `neg/n21`. There `create_level` is GRANTED and `replace_level` is not — that
  is the point: the two rights are separate, and one does not grant the other.
- **F65-29** — a `version:` in incoming code is input, not permission
  (spec/29_2 §4.2): `neg/n23` with `version: 99` and without `replace_level`. The order of the
  checks IS the content: first the permission, then the version decides.
- **F65-30 / `clearance-collision`** — a name the host already has must be an error;
  the skip logic would silently give the script the *host's* function: `neg/n22`, with
  `replace_level` granted, so that the permission does not carry the statement.
- **F65-32** — the policy is readable, never writable: `12_policy_readable` (the three readers)
  and `neg/n24` (reaching for a setter names the reason instead of looking like a
  typo). The tightening rule is `neg/n25`: the environment caps a requested
  `replace_level`. That the STRICTER one always wins, whatever the source, is stated in the
  header of that probe with the second direction measured inline — a single probe
  cannot tell "capped" apart from "call ignored".

**F65-34 / `clearance-writedown`** (step 8, `# NEEDS: writedown`) has three probes:
`13_writedown_clean` for the allowed directions (equal and stricter), `neg/n26` for the
chapter's example and `neg/n27` for the most obvious bypass — an unannotated `tmp`
in the middle. `n27` is the more important of the two negatives: if a local without `level:`
forgot its origin, the rule would be defeated with one intermediate step.
The six named limits of the pass are listed in spec/65 §9 step 8 — among them that
an unannotated wrapper function launders and that a loop back edge can hide a
flow.

Two requirements remain specified and UNCHECKED, and that is stated here instead of being
implied by a missing test:

- **F65-27** (the level of a created symbol IS the session's clearance) hinges on the
  same point: without §4.3 a created symbol never becomes callable, so its level is not
  observable. Whoever builds §4.3 redeems both probes — and must redeem F65-27 there.
- **F65-31** (restricted sessions run isolated; an AOT host cannot hot-reload at
  all, because `context = false` returns `-2` there) is a property of the
  host's construction. Structurally satisfied: `aiml_exec_code` returns for the isolated context
  BEFORE the write side is reached at all — the checks sit behind this
  branch, not before it.

## STAGE: why this lab is not simply "red"

Spec-first means: the probes exist before there is code. A lab that therefore glows red
permanently says nothing any more — after three days nobody reads it. So every probe
names in its header the implementation step it needs, and the runner only evaluates the ones reached:

```
# NEEDS: step2     accept the syntax + validate `level`/`grants` (always on, F65-17/F65-21)
# NEEDS: step3     opt-in flow check (AIML_STATIC_CLEARANCE, F65-9/F65-17)
# NEEDS: session   restricted session (F65-4 … F65-22)
# NEEDS: guard     guards without a host: builtins, dialect, `extern` (F65-13/F65-14/F65-35)
# NEEDS: exports   the host's export table, the expectation is a diagnostic (F65-23)
# NEEDS: host      host with CALLABLE symbols: signature + address (F65-24/F65-37)
# NEEDS: write     the write side: create/replace/collision/readers (F65-26 … F65-32)
# NEEDS: writedown the write-down rule (F65-34), value flow INSIDE the body
# NEEDS: dynload   needs spec/29_2 §4.3 — not this chapter

STAGE=step2 bash tests/clearance/run.sh     # default
STAGE=writedown bash tests/clearance/run.sh # everything
```

Probes above `STAGE` report **PENDING** and do not count as a failure.

### What step2 checks

| Probe | Pins |
|---|---|
| `01_trusted_default` | annotations present, **nothing** enforced (F65-1) |
| `07_level_syntax_everywhere` | `level` is accepted in ALL forms: `decl` 4th position (with/without `lifetime`, with a `null` initialiser), `def` key, `class` + member, `model` + field, `enum`, `interface`, `classgroup` |
| `neg/n09`, `neg/n10`, `neg/n17` | `invalid-level` — non-integer, outside 0…255, at the 4th position of a `decl` |
| `neg/n18` | the same check on a class member (the same modifier block, spec/03) |
| `neg/n11` | `invalid-grants` — `grants:` above `level:` |

### ⚠️ The lab is in the corpus of `run_all_tests.sh`

`run_all_tests.sh` collects with `find tests/ -name "*.aiml"` and only excludes `lifetime/neg`.
`tests/clearance/` **and** `tests/closures/` therefore end up in the corpus, although both
are spec-before-code labs with their own runner — the suite cannot become green because of that, and
negative probes are judged there by return value instead of by their `EXPECT-ERROR` identifier
(`n04`/`n05` therefore appear as "OK" although they were accepted, which in the lab is a
failure). Proposal: exclude both directories like `lifetime/neg`. **Not** done
today, because that would have shifted the comparison figures of an ongoing measurement.

## How this lab relates to the main suite (17.10.2026)

The lab is a **driver** following the existing convention `tests/<dir>/run.sh`, like
`pipeline_purity/`, `pipeline_graph_neg/`, `lint_globals/` and `bundle_socket/`:

- `run_all_tests.sh` calls `tests/clearance/run.sh` **as one entry** and takes over its
  return value. Evaluation is thus by the `EXPECT-ERROR` identifier from spec/65 §10, not
  by the return value of a single probe.
- Every probe carries `# Requires: driver`; the main suite thereby skips it **visibly**
  ("runs via the run.sh of its directory") instead of silently filtering it out.

Before, the probes sat individually in the corpus. That was wrong for two reasons: negative probes
were judged by return value (an accepted probe appeared as "OK" although in the
lab it is a failure), and the stages not yet built produced permanently red lines
that nobody reads any more. **Nothing** was excluded — the probes keep running in every
corpus run, just through their own runner.

## The export table as a fixture (stage `exports`, 17.10.2026)

Six probes needed a host. On thinking it through they split into two halves, and that
was the cut for step 6:

| | needs | probes |
|---|---|---|
| `# NEEDS: exports` | only the host's **name and level** — the expectation is a diagnostic | `neg/n01`, `neg/n02`, `neg/n14`, `08_extern_admitted` |
| `# NEEDS: host` | **callable** symbols (signature + address, F65-24) | `02`, `03`, `05` |

`host_exports.txt` is the host's offer, one line per symbol (`Name|Level`), and the
runner passes it in via `# EXPORTS:` as `AIML_EXPORTS`. Signature and address are
**deliberately** missing from it: only calling needs them, and for that a real host has to register them.

That the table comes via the environment does not contradict F65-32 ("env may only
tighten"): that is about the limits of the SESSION (clearance, create_level,
replace_level). An entry in the export table is the **host's offer**, and offering
is its prerogative — whoever sets `AIML_EXPORTS` is the host, the same reasoning as for
`AIML_CLEARANCE`.

Until 17.10.2026 the three `host` probes carried the note **REWORK NEEDED**, because they
still defined their symbols themselves with `level:` — self-certification since F65-27. They were
deliberately not "repaired" earlier: a probe without a host cannot make the statement "a symbol
I may not reach", and a silent rework would have hidden the missing host.
With step 6b they run against the real fixture (`sys.export_symbol` + bundle in a
restricted session); the note is gone. That `grants:` in `03` is load-bearing has been
cross-checked: without the annotation the file fails with `clearance-flow` including the path
`user_script -> restart_agent -> kill_process`.
