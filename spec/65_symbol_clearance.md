# 65 — Symbol Clearance (Levels) [PROPOSAL]

> Status: PROPOSAL, **all steps 2 to 8 built** (2026-10-17) and measured (lab
> `tests/clearance/` with `STAGE=writedown`: 12 positive, 25 negative probes green).
> **Two are outstanding**, and not because of this chapter: `clearance-create` is
> built but unreachable as long as spec/29_2 §4.3 (dynamic reloading) is missing — see §9
> step 7.
> **Two parts are opt-in and stay that way:** the flow check (F65-17) and the
> write-down rule (F65-34), both behind `AIML_STATIC_CLEARANCE`.  For write-down the
> requirement "after measuring on the corpus" is NOT met: the corpus uses `level:` nowhere,
> so a run only shows that the rule has nothing to look at.  The named
> limits are in §9 step 8, not hidden in the code.
>
>
> Derived from `doc/analysis/symbol_clearance_2026-10-16.md`, which holds the
> measurements this chapter rests on. Where this spec and the implementation disagree,
> **this spec wins** (Arbeitsprinzip 2).

## 1. The normal case has none of this

**F65-1: A program with one binary and one author has no clearance, and pays nothing for it.**
Normally one binary runs, written by one party, trusting itself completely. There a clearance
system is not merely unnecessary — it is noise. So the feature is **absent** in that case, not
switched off:

- no key has to be written, and none has to be read to understand a program;
- no analysis pass is constructed, so compile time is unchanged;
- no diagnostic can fire, so no program becomes invalid by adding this chapter;
- nothing exists at runtime — clearance is a compile-time admission rule and emits no code.

**F65-2: Clearance is a property of a BOUNDARY, not of a program.** It describes the relation
between a **host** that already runs and **foreign code entering it**. Without such a boundary
there is nothing to describe. Today exactly three boundaries exist:

| Boundary | The host | The newcomer |
|---|---|---|
| Script console (cosmos doc/spec, planned) | the running server | a script a user submits |
| `sys.bundle` → `aiml_exec_code` (spec/29_2) | the receiving process | a body that travelled |
| `sys.lib_load` (spec/29_2 §15) | the loading process | a library it does not author |

A single-binary build is none of these, which is why F65-1 is the default and not an option.

**F65-3: Clearance is not `public`/`private`/`protected`.** Those describe **module structure**
and already exist for class members (`AccessLevel`, `src/core/utils.cpp`). Clearance describes
**trust**. A symbol can be perfectly public and still be reachable by nobody from a console. The
two must not be merged, because a refactor of structure would silently move a trust boundary.

## 2. Direction, written exactly once

**F65-4: A lower level is MORE restricted.** A symbol's `level:` is an integer `0 … 255`; a
session's clearance is an integer `1 … 255`. Access is granted exactly when

```
  session_clearance  <=  symbol_level
```

So a symbol at level `1` is reachable only by the most privileged session; a symbol at level
`255` is reachable by every session. This sentence exists **once**, here. It is the kind of
ordering that gets inverted in a second place and then disagrees with itself (see this project's
history on duplicated facts), so no other document restates it — they link here.

The meaning of the individual numbers is the **host's** business; aiml enforces only the
ordering. A host with two kinds of user needs two numbers, not a scale.

**F65-22: `0` is not a valid clearance, because `0` is what a FORGOTTEN FIELD holds.** The two
sides of the scale are deliberately asymmetric:

| `0` means | on a **symbol** (`level:`) | on a **session** (clearance) |
|---|---|---|
| | the closed end — reachable by nobody | **invalid** (`invalid-clearance`) |
| so an unset field means | maximally restricted (safe) | a hard error, never "admin" |

An uninitialised integer must **fail**, not pass. Were `0` the privileged clearance, every
forgotten assignment, zeroed struct and missing column in a user record would silently be an
administrator — the single most likely way this chapter could cause the damage it exists to
prevent. The privileged end of the clearance scale is therefore `1`.

**F65-5: An unannotated symbol is level 0, which NO restricted session reaches** — not even
clearance `1`, since `1 <= 0` is false. A symbol becomes reachable across a boundary only when
somebody wrote a level on it; omission never grants. This mirrors the purity default of spec/62
F62-18, where an unlisted namespace counts as impure. The wanted consequence: adding a function
to the host does **not** silently widen what a console can reach. Trusted mode (F65-1) is
unaffected — there the host's own code calls it as it always did.

## 3. Who sets the clearance

**F65-6: The clearance of a compile session comes from the HOST, never from the source.** Source
text may carry `level:` on the symbols it *defines*; it can never state the clearance it *runs
at*. Self-certification would be a one-line escalation.

**F65-7: "No clearance" and "the most privileged clearance" are different states.** No clearance
set = **trusted mode** = F65-1 = the pass does not run at all. Clearance `1` = a restricted
session that reaches every annotated level, but in which every other rule of this chapter
(§5, §6) still applies: the dropped dialect, the `extern` admission, the builtin table. A host
must be able to say "unrestricted" without saying "the most privileged number" — and an admin
console is still a console, not the host itself.

**F65-20: The host sets the clearance through the embedding API; `AIML_CLEARANCE` exists for
probes and may only make a session STRICTER.** A test lab cannot call the embedding API, so the
environment variable `AIML_CLEARANCE=<n>` starts a restricted session at level `n`. Whoever sets
it already controls the process, so it grants nothing — but to keep that true in both directions,
the variable is clamped: if the host already set a clearance, the effective one is
`max(api, env)` (the stricter, since higher = less privileged), and the variable can never remove
a restriction nor leave trusted mode once the host has left it.

**F65-8: A nested compile inherits the clearance and can never raise it.** If restricted code
reaches `sys.bundle`/`aiml_exec_code` at all (§6), the inner session runs at the outer clearance
or stricter. Otherwise the whole chapter is one indirection deep.

## 4. Privilege flow, which is the actual content

Checking the call site alone is not enough. If a function open to everyone internally calls a
restricted one, every user obtains the restricted effect by detour.

**F65-9: For every call edge `f → g`, `level(g) >= level(f)` must hold.** The same applies to
builtins a body calls and globals it touches. A violation is a compile error in the **host**
build when the check is enabled (§7) — it is a defect in the host's own API surface, not in the
script.

The data this needs is already collected per function for bundling and purity
(`src/core/context.h`): `referencedFuncs`, `referencedBuiltins`, `referencedGlobals`.

**F65-10: `grants:` marks the deliberate gateway.** A function that must offer a lower-level
effect at a higher level says so:

```yaml
- def:
    name: restart_agent
    level: 7            # a normal operator may call it
    grants: 2           # and it deliberately performs level-2 work inside
```

`grants:` relaxes F65-9 **inside this body only**, down to the named level. Its callers are
checked normally against `level:`. These functions are the audit list: a host's trust model is
exactly its set of `grants:` annotations, and that set should be short enough to read.

**F65-11: Diagnostics name the path, not just the symbol.** For an F65-9 violation the error
states the whole edge chain (`make_jobs (7) → prepare (7) → wipe_queue (2)`); for a denied access
it states symbol, its level, and the session clearance. A clearance error that only names the end
point makes the reader search the wrong end — the same failure mode spec/106 F106-13 records for
rejected sources.

## 5. The three checkpoints

**F65-12: Clearance is enforced where names are ADMITTED, not where symbols are resolved at
runtime.** It cannot be built as JIT visibility: `DynamicLibrary::AddSymbol` writes into a
**process-global** table with no per-session view, and `compileYamlChunk` exports *all* functions
of the active context into it before each bundle compile (`exec_impl.cpp`). A symbol made visible
for one session is resolvable by every JIT in the process. Hence three compile-time checkpoints:

1. **Function/global resolution** — the name is looked up in the session's context; a symbol
   failing F65-4 is reported as denied, and is **not** reported as "unknown" (a misleading
   diagnostic is worse than a refusal, and probing the difference reveals nothing a host should
   hide — see F65-16).
2. **Builtin registry** — levels for builtins live in a table shaped like `src/core/purity.h`:
   a default per namespace, exceptions per function. **Every** namespace starts at level 0 and is
   opened deliberately, never listed as dangerous — see F65-35.
3. **`extern` admission** — see F65-13.

**F65-13: In a restricted session, `- extern: "C"` is limited to the host's export list.** An
`extern` declaration bypasses every symbol table of the compiler by construction: the script
states the signature itself and the JIT resolves the name against the process. So a restricted
session admits an `extern` name only if the host published it, with the level the host gave it —
the same table row that carries its address (F65-23).

## 5a. The export table — one table, not two

**F65-23: The export table and the level table are the SAME table.** One row per symbol the host
offers: **name**, **address**, **signature**, **level**. The `extern` admission of F65-13, the
resolution check of F65-12 and the registration that makes the symbol reachable at all read the
same rows.

Two separate lists — "what is exported" next to "what level it has" — would be the failure mode
this project has paid for repeatedly: a fact in two places drifts, and the drift is silent until
the wrong one is the one that is checked. A symbol that is not a row is not reachable and has no
level, which is the same statement said once.

**F65-24: Across the boundary, TWO things are handed over, and they are not the same thing.**
Confusing them produces either "Unknown function" at compile time or an undefined symbol at
resolution time, and the two have different cures:

| | What the newcomer needs | Mechanism |
|---|---|---|
| **Signature** (compile time) | name, arguments, return type | `compileDeclarationsOnly()` with `headerOnlyMode` (spec/29_2 §15) — declarations without bodies; `- extern: "C"` on the script side |
| **Address** (resolution time) | where the function actually is | `DynamicLibrary::AddSymbol(name, addr)` |

In a JIT host the second half is automatic: `compileYamlChunk` walks the active context's
functions, fetches each address from the engine and registers it. **In an AOT host nothing does
this**, because there is no active context and no execution engine — the host must hand over both
halves itself.

**The address does not require `-rdynamic`.** A host takes a function pointer to its own `def`
(spec/12_1, `set: [fp, add]`), which is the real address inside the binary, and registers it under
the name it chose. Measured on a built AOT server (16.10.2026): of 38 statically linked aiml
functions, **0** appear in `.dynsym`, so neither `LoadLibraryPermanently(nullptr)` nor `dlsym`
finds them — while the function pointer does. Exporting through the table is therefore not only
safer than `-rdynamic`, it is the mechanism that actually works for a static build.

**F65-25: A restricted session in an AOT host is necessarily an ISOLATED context, hence
sequential.** `sys.exec` with `context = false` returns `-2` in an AOT binary — it looks for a
shared JIT session and there is none (spec/29_2 §3.1, §11). So a console inside an AOT host runs
`context = true`, which is `aiml_isolated_exec`, which saves and restores the process globals
(`g_active_context`, `g_active_ee`, `g_fptable_current`) around the run. Two such runs at the same
time in one process clobber each other's globals. Consequence, and it is a hard one:

> **one console per process, one process per user — not one thread per user.**

**There is a measurement, and it is worse than a clean failure.**
`tests/test_concurrent_isolated_exec.aiml` — four threads each compiling and running three
different bundles — exists precisely to answer "may several isolated compiles run at once?"
(spec/62 F62-99, which serialises compilation while the answer is unknown). It asserts exit 0.
Measured 16./17.10.2026 on clean builds: it **crashed with SIGSEGV once inside the full corpus
run** and then passed in the next full run and in six consecutive isolated runs. So the fault is
**intermittent and load-dependent**, not deterministic — which is the bad kind: a test that is
green six times out of seven invites the conclusion that concurrent isolated compiles are fine.
They are not; they are unproven, and one observed crash is enough to keep F62-99's serialisation
and the one-process rule above.

The cause is not this chapter but the compiler (`ctx.module()` is held as a reference in
601 places, `doc/analysis/symbol_clearance_2026-10-16.md` §2). It also means the open finding in
spec/29_2 §12 — the isolated object cache has no version key in the AOT case — must be fixed
before a console relies on that path.

## 5b. The write side: creating, replacing, hot-reloading

Everything up to here governs **reading** — which names a session may mention. A session can also
**write** to the host's symbol table, and that is a different question with a stricter answer.
Hot-reload (spec/29_2 §4) makes it concrete: replacing `check_auth` means every later caller in
the host runs the newcomer's body **at the host's privilege**. No read rule prevents that.

**F65-26: Three capabilities, one record — and `create_level` is about the HOST's table, not about
having a body at all.** A session policy carries exactly three things, handed over together,
because they are three answers to one question and splitting them across env vars would be the
drift of F65-23 again:

```
clearance      1 … 255     which symbols may be NAMED                     (F65-4)
create_level   0 … 255     may this session add symbols to the HOST?      (clearance <= create_level)
replace_level  0 … 255     may it REPLACE existing host symbols?          (clearance <= replace_level)
```

Both of the latter default to `0`, which under F65-22 means **nobody**.

The three go out **together on `sys.exec`**, in the order of this table (F65-36)
— not via three environment variables, because three separate levers for one decision are
the drift from F65-23.  The environment remains the process-wide default and may only
tighten (F65-32).

**An earlier draft of this requirement was wrong, and the acceptance probes are what exposed it.**
It read "may this session DEFINE new symbols", with the default `0` forbidding all definition.
Every aiml program needs a `- def: main` (spec/08), so that default forbade *every script* —
the capability as written could never be granted usefully, only switched between "no script runs"
and "everything is allowed".

The distinction that was missing: a definition in an **isolated** session (F65-31, the default and
in an AOT host the only option) is local to that session's fresh context and never reaches the
host. Writing it is not the dangerous act, and a console script defining its own helpers is as
harmless as a local variable. What `create_level` governs is a definition that **enters the host's
live symbol table** — which only happens in the shared context, where `compileYamlChunk` also
exports it into the process-global table (F65-31). `replace_level` governs supplying a body for a
name the host already has (F65-28), which is dangerous in either context.

So: an isolated session always defines its own symbols freely; it may not add to or replace the
host's.

**F65-27: A created symbol's level IS the session's clearance.** Not `0` (its creator could then
not call it), and not a value the source picks (that would be F65-6 self-certification). The
consequence is the containment, and it falls out of F65-9 rather than being bolted on: a symbol
created at clearance 7 is level 7, so by the flow rule it may not call anything below 7. Nobody
can create something more privileged than themselves.  It follows that `level:` and `grants:` in
the source of a restricted session are refused as self-certification (F65-6): they are policy, and
policy belongs to the host.

**F65-28: Replacing a symbol never rewrites its table row.** `level:` and `grants:` belong to the
host's export table (F65-23), not to the newcomer. A reload supplies a **body**; the level stays,
and the new body is re-checked against the existing level by F65-9. Otherwise a script reloads
`wipe_queue` with `level: 255` and has opened it to everyone — the shortest possible escalation in
this whole chapter.

**F65-29: A `version:` in arriving code never authorises a reload by itself.** spec/29_2 §4.2
decides reload-versus-skip by comparing the bundle's version against the host's. That number is
written by whoever wrote the bundle, so in a restricted session it is an **input**, not a
permission: a reload needs `replace_level` first, and only then does the version decide whether
anything changes.

**F65-30: In a restricted session, a name that already exists in the host is an ERROR, not a
skip.** This is the quiet trap in spec/29_2 §4.2: with an equal or lower version the arriving
definition is silently **skipped**, so a script that defines its own `helper` would quietly get
the *host's* `helper` instead — a surprise in correctness before it is one in security. A
restricted session therefore refuses the collision and says so; shadowing is not a thing a
boundary may do silently.

**F65-31: A restricted session runs in an ISOLATED context by default** (`context = true`). The
shared context writes into the host's live table, and — measured 16.10.2026 — `compileYamlChunk`
exports *every* function of the active context into the **process-global** `DynamicLibrary` table,
so a symbol created there becomes resolvable for every other JIT in the process, including other
users' sessions. In an AOT host the question does not arise: `context = false` returns `-2`
(spec/29_2 §3.1), so **an AOT console cannot hot-reload at all**, and the most dangerous path is
closed by construction rather than by a check.

**F65-32: The policy comes from the host; `clearance.*` is readable, never writable.** A builtin
that *sets* `create_level` would be self-certification (F65-6) — the script would be granting
itself a permission. A builtin that *reads* it is harmless and useful for a decent error message,
and is allowed, since levels are not secret (F65-16). The environment lever of F65-20 extends to
`create_level`/`replace_level` with the same restriction: it may only make a session **stricter**,
never looser, and never lift it out of a restriction the host imposed.

## 5c. How a host offers — the two missing language features

§5a says that signature and address are the second half of the handover (F65-24).  So far
only the first is built: name and level.  Two language features are missing for the second, and their
absence is the reason why the three `host` probes cannot run.

**F65-36: The clearance belongs to the EXECUTION, not to the bundle.**

```yaml
- decl: [pkg, Code, { call: [sys.bundle, "user_script", { context: true }] }]
- decl: [rc,  i32,  { call: [sys.exec, pkg, 7] }]         # clearance only: 7, write nothing
- decl: [rc2, i32,  { call: [sys.exec, pkg, 7, 7, 0] }]   # ... may create, not replace
```

The three values of the policy from F65-26 are **in the order of that table** —
`clearance`, `create_level`, `replace_level` — and they travel together, as one operation.
Missing trailing values are `0`, which under F65-22 means **nobody**: whoever says nothing may
write nothing (fail closed).  Each of the three is an ordinary expression and not a
literal, because a console forms the policy **per user at runtime**; a
policy that could only appear as a literal in the source would be useless for the actual
case.

A bundle is **code**, a session is an **action**.  The same code must be executable for two
users with different clearances — attaching the policy to the `Code` object
would forbid that, although that is exactly the case at stake.

(The first version of this requirement put the clearance on `sys.bundle`, reasoning that
the `Code` object describes the session because it already carries `context`.  That was the wrong
way round: `context` describes HOW it is compiled and is a property of the code; the
clearance describes WHO executes.  Incidentally, it would have needed a sixth field in a structure
whose layout is mirrored in three places — LLVM struct, C struct in `exec_impl`
and `__aiml_code_new` — where a mismatch silently reads wrong data.)

`AIML_CLEARANCE`, `AIML_CREATE_LEVEL` and `AIML_REPLACE_LEVEL` remain the
process default and the lever for probes (F65-20); the arguments to `sys.exec` apply to this
one session and may only be STRICTER than the default.  For the two write rights, "stricter" runs
in the OPPOSITE direction from the clearance, because the comparisons are
reversed (`clearance <= create_level`): there, stricter is the SMALLER number, and 0
is the minimum, i.e. nobody.  An unset environment variable is therefore not 0
but "no default" — otherwise the right could never be granted, only switched off.
This means `sessionClearance()` cannot keep its process-wide cache: a
clearance that is read once and remembered is not a property of a session.

**F65-39: An address comes by TWO routes, and the second is the one for a console.**
`sys.export_symbol` (F65-37) presupposes that the host already had the code at **compile time**
— it registers a `def` of its own.  A host that has **foreign** source text compiled
cannot do that: it IS the compiler and only knows the script at runtime.  For
it the following applies:

| | Name + level | Address |
|---|---|---|
| own code (F65-37) | `sys.export_symbol` | the same call |
| foreign source text | export table (`AIML_EXPORTS`) | library preloaded into the process |

Both halves from F65-24 are thus present in the second case too, only from two sources — and that
is right here, not the drift from F65-23: the **policy** is in one place (the
table), the address is a property of the process.  What matters is the direction:
admission depends on the TABLE, not on whether the symbol can be found.  Otherwise
every library a host loads for itself would automatically be its offer to every
user.

Measured 2026-10-17: `14_extern_preloaded_callable` (level 7 offered, clearance 7 → call
returns 42) and `neg/n28_preloaded_symbol_too_low` (the same library, symbol at level 2
→ `clearance-denied`, although the address is in the process).  Probe `08` only demonstrated
admission and never called — half the requirement.

**F65-40: An admitted `extern` without a resolvable symbol crashes today.**  Measured
on the same day: the same probe without `LD_PRELOAD` ends with **SIGSEGV**, not with a
diagnostic — the call goes to an unresolved address.  That is not a
clearance hole (admission decided correctly), but for a host that compiles foreign
source text it is one: a forgotten or wrongly pathed library takes
the process down instead of telling the user what is missing.  It should be checked where the
`extern` admission already decides anyway — there the name is known and `dlsym` is one
line.  **Not built yet**, and that is stated here instead of turning up in a crash report.

**F65-37: `sys.export_symbol(name, fn, level)` — one line, one call.**

```yaml
- def: { name: read_status, level: 7, return: i32, body: [{ return: [3] }] }
- decl: [fpr, fn<:i32>]
- set:  [fpr, read_status]
- call: [sys.export_symbol, "read_status", fpr, 7]
```

The call does both halves in one go: it registers the **address**
(`DynamicLibrary::AddSymbol`, so that the JIT resolves the name) and enters **name and level** in
the export table (§5a).  Two separate calls would be two lists that drift — exactly what
F65-23 forbids.  The address comes from a function pointer to the own `def` (spec/12_1),
not from `dlsym`: that way no `-rdynamic` is needed, and with a static build it is the only
way it works at all (measured: 0 of 38 aiml functions are in `.dynsym`).

**Only in the trusted state.** In a restricted session
`sys.export_symbol` must be refused (`clearance-builtin`): a script that may export itself
sets its own level — self-certification in its most harmful form.  The `level`
of this call is therefore also not bound to the `level:` of the `def`: the host decides
what it offers and may offer less than the function carries.

**F65-38: A bundle carries the DECLARATION of an exported symbol, not the body.**
`sys.bundle` today takes along the source texts of the called functions (`referencedFuncs`,
spec/29_2 §16) — otherwise the receiver would report "Unknown function".  For a symbol from the
export table that is wrong, though: the script would get a **copy** and execute it in its own
context, with the session's level instead of the host's.  The access check would
then run into nothing, because there would be nothing left to refuse.

**The criterion is the `level:` on the `def`, not the export table.**  That looks like a
subtlety and is the core: `sys.bundle` runs at **COMPILE TIME**,
`sys.export_symbol` at **runtime**.  Whoever consults the table at bundling time consults it empty —
measured on 2026-10-17 on `09_host_exports_callable`: the body was bundled along, and in the
restricted session its `level:` triggered as self-certification (F65-27).  A detour
via the table is not possible here, because at this point it does not exist yet.

Therefore: if a called `def` carries a `level:`, it belongs to the offered surface, and the
host bundles its signature instead of its body.  What the host then actually offers
is decided by `sys.export_symbol` at runtime — it may offer LESS than the annotation
says (F65-37), and what it does not offer fails at the receiver at the `extern` admission.
The two points in time are thus clearly separated: the annotation says "belongs to the surface",
the table says "is reachable now, and at which level".

Original text (the criterion was wrong): if a called name is in the export table
Therefore: if a called name is in the export table, the host bundles its signature and
not its body.  That is the same separation as in F65-24 (signature via a declaration,
address via `AddSymbol`) — and it is the reason why an exported symbol is EXECUTABLE for the
caller without the caller being able to READ it.

**What does NOT come with this.** The signature still travels via a declaration, not via this
builtin — the host gives the script a file with `- extern: "C"` declarations of the offered
names, and the `extern` admission from F65-13 is exactly the gate for this.  Encoding a signature in a
string would be a second type language next to aiml; the declaration mechanism already exists
(`compileDeclarationsOnly`, spec/29_2 §15).

## 6. What a restricted dialect must drop, or this chapter is decoration

**F65-14: A restricted session rejects raw memory access and compile-time re-entry.** With
pointer arithmetic, in-process code reaches every byte regardless of any name rule, including the
host's own runtime state. So in a restricted session the following are refused: `ptr<T>`
arithmetic and casts between pointers and integers, raw buffer indexing outside the checked
forms, and — unless the host explicitly grants them — `sys.exe`, `sys.lib_load`, `sys.bundle`,
`aiml_exec_code` and `db.open_*`. Each of these is a complete bypass in one line; a level system
that admits them checks the front door of a house without walls.

**F65-15: Clearance is a compile-time check, NOT a sandbox.** It prevents mistakes, documents
intent and gives a versionable API surface. Against a hostile participant in the same process it
offers no protection, and this chapter must never be cited as if it did. The security boundary is
the **process**, and for foreign parties ultimately real isolation (rootless/userns). A host that
accepts foreign scripts in-process accepts them at its own privilege.

**F65-16: Levels are not secret.** A denied symbol's name and level may appear in a diagnostic.
Hiding the API surface from someone already permitted to compile against it buys nothing and
costs a clear error message.

## 7. Keeping annotations honest in a single-binary build

F65-1 says nothing runs by default. But annotations that are never checked rot, and a library
author wants to annotate once rather than when a console first appears.

**F65-17: The flow check (F65-9) is available as an OPT-IN static rule** under spec/49, off by
default. Enabling it (`AIML_STATIC_CLEARANCE=1`) checks a trusted build's own `level:`/`grants:` annotations for internal
consistency without restricting anything. Syntax of `level:`/`grants:` is validated whenever
present, in every build — a typo is an error, an annotation is never silently ignored.

**F65-18: If no symbol in the build carries `level:`/`grants:` and no clearance is set, the pass
is not constructed.** This is an observable property, not an intention: it is the acceptance test
that F65-1 is real.

## 8. Syntax: `level` is a MODIFIER, not a new form

**F65-19: `level` goes where the other qualifiers already go.** aiml has one established place for
declaration modifiers — the **4th list position** of `decl` and of a class member (spec/03) — and
one for definition keys (`access:` next to `name:`). `level` joins both; it invents nothing:

```yaml
# 1. locals and globals — 4th position, beside const/volatile/extern/lifetime (spec/03, spec/61)
- decl: [api_token, string, "", { level: 2 }]
- decl: [buf, "dyn_array<i32>", { new_array: [i32, n] }, { lifetime: persistent, level: 7 }]
- decl: [tmp, i32, null, { level: 7 }]            # no initialiser -> `null` in the 3rd slot

# 2. functions and methods — a key, beside `access:`
- def:
    name: wipe_queue
    level: 2
    grants: 0                                      # F65-10, only where privilege is dropped
    body: ...

# 3. class members — the same 4th-position map as a local
- class:
    name: Session
    level: 7                                        # the type itself
    members:
      - [user_id, i32, 0,  { access: public,  level: 7 }]
      - [secret,  string, "", { access: private, level: 2 }]
    methods:
      - def: { name: renew, access: public, level: 7, body: ... }

# 4. the other definition forms — same key, same meaning
- model:      { name: JobDef,  level: 7, fields:  [...] }
- enum:       { name: Phase,   level: 7, values: [...] }
- interface:  { name: Store,   level: 2, ... }
- classgroup: { name: Agents,  level: 2, ... }
- cascade:    { name: Build,   level: 7, ... }
```

**F65-33: A level on a BINDING restricts the binding, not the type.** A local or member at
level 2 is storage the session may not name; the type it has is governed by the type's own level.
Both are the same rule (F65-4) applied to two different symbols, and neither implies the other: a
level-7 `Session` may perfectly well hold a level-2 `secret`, which is exactly how a host offers
an object whose interesting parts stay closed.

**F65-34: A level on a LOCAL is not access control — it is the write-down rule.** A local inside a
body has no caller that could name it, so under F65-4 alone the annotation would be decoration,
and this chapter refuses to produce decoration (§6). Its one honest meaning is containment of the
VALUE: a value read from a symbol at level `L` may not be stored into a location whose level is
**higher** (less restricted) than `L`.

```yaml
- decl: [open_log, string, "", { level: 255 }]
- set: [open_log, { call: [read_secret] }]        # read_secret is level 2 -> clearance-writedown
```

Without this rule a restricted value is laundered in one assignment: read it where you are
allowed, store it where everyone is. With it, the local's level is the declaration of what the
storage is allowed to hold. This is the heaviest part of the chapter — it needs value flow inside
a body, not just the call graph of F65-9 — and it is therefore **last** in §9 and opt-in like
F65-17 until it has been measured on the corpus.

**Two meanings that must be kept apart**, otherwise the rule is bypassed in an
intermediate step:

- The `level:` of a local is the **bound for storing** — what may go in here.
- What a local **carries** is the strictest thing ever written into it, and that
  travels on.  A local WITHOUT an annotation therefore also carries a level as soon as a
  restricted value flows into it; otherwise `tmp = read_secret()` would forget the origin
  and `open_log = tmp` would be allowed.  Exactly these two lines are the
  most obvious bypass, not the assignment from the example above.

Without an annotation and without a restricted origin, a local carries **no** level — not
level 0.  That is the difference from F65-5: there 0 is the fail-closed default for
ACCESS, here 0 would mean that every literal into every annotated local would be a violation.
A rule that breaks every program protects nothing.

**F65-35: Builtins are closed from the ground up.** Every builtin namespace and function is
level `0` unless the table (§5 checkpoint 2) explicitly raises it. Earlier drafts of this chapter
named `sys`, `db`, `io`, `net` as the closed ones, which was the wrong shape: it made a list of
the dangerous names, and such a list is complete only until the next builtin is added. The
default closes, and opening is the deliberate act — the same direction as F65-5 for symbols and
F65-26 for the write side. One rule, three places, no exceptions to remember.

## 9. Order of implementation

1. This chapter, then acceptance tests (`tests/clearance/`), then code — in that order.
2. ✅ **built 2026-10-17** — Parse + validate `level:`/`grants:` everywhere; enforce nothing
   (F65-17 second sentence).  Validation in one place: `src/core/clearance.h`; carried in
   `DeclStmt`, `FunctionDecl`, `StructMember` and the seven type forms.
3. ✅ **built 2026-10-17** — flow check as an opt-in spec/49 rule, with path diagnostics
   (F65-9, F65-11): `src/core/clearance_flow.h`, a whole-program pass after `astCompiler.compile`.
   Limit: only edges WITHIN one file (imported functions are in `ctx.functions` as declarations
   without a level and are skipped, otherwise false positives).
   Across files it needs propagation as for the enum sets — open.
4. ✅ **built 2026-10-17** — the session itself: clearance from the host (`AIML_CLEARANCE`,
   F65-20), validity 1…255 with `invalid-clearance` (F65-22), and self-certification
   refused — `level:`/`grants:` in restricted source as well as a top-level `clearance:`
   (F65-6, F65-27).  That is the part that can be checked without a host.
5. ✅ **built 2026-10-17** — the guards that need no host:
   - **Builtin table** (F65-35) in `src/core/clearance_builtins.h`, shaped like `purity.h`
     (default per namespace, exceptions per function).  Open are the **pure** namespaces
     `math`, `hash`, `enums`, `compress`, `cascade` plus `log` — without output a console is
     not one.  Everything else is 0.  The check is made on the **namespace**, not on the individual
     function: `sys.anything` is therefore caught even if it would only be reachable via the
     mangled C name (`sys_anything`).
   - **Dialect** (F65-14) in `visitCast`: a `cast:` to a pointer, or from a pointer
     to an integer, is an error in a restricted session.
   - **`extern` admission** (F65-13) in `commitFunc`: currently the admissible set is **empty**,
     because the export table (§5a) does not exist yet — every `extern` in a
     restricted session is refused.  That becomes less strict by itself as soon as there is
     a table, and until then it is the right direction (fail closed).

   **Two limits, deliberate and not accidental:**
   (a) **Methods on values are not covered.**  The guard sits at the dispatch for
   namespace builtins (`ns.fn`); `.push`, `.len`, `.split` do not pass through there.  That is
   intentional — they are language features, not capabilities — but it is a decision, and
   whoever wants to change it must change it here.
   (b) **The dialect guard covers `cast:`, not everything from F65-14.**  Raw
   buffer indexing outside the checked forms is open.  `cast:` is the main route
   by which a value becomes a pointer — but "main route" is not "all routes".
   Builtin table (F65-35) and the `extern` admission (F65-13).
6a. ✅ **built 2026-10-17** — the export table with name and level (`AIML_EXPORTS`, one
   line per symbol).  This gives rise to the access check F65-4/F65-5 and the
   `extern` admission F65-13 — both read the same table (F65-23).  A refused
   symbol is reported as REFUSED, not as "unknown" (F65-12).
6b. ✅ **built 2026-10-17** — signature and address (F65-24), and with that a script can CALL a
   host symbol.  The two halves of the handover go out in ONE call,
   so that name, level and address cannot drift apart: `sys.export_symbol`
   (F65-37) takes name, function pointer (`fn<…>`, spec/12_1) and level, checks the level,
   registers the address via `AddSymbol` and writes the same entry into the table from
   6a — `src/sys/clearance_runtime.cpp`.
   The clearance hangs on `sys.exec(pkg, clearance)` (F65-36) and not in the environment:
   a bundle is code, a session is an operation.  That `pushSessionClearance` can only
   tighten is the rule behind it.
   An exported symbol is bundled DECLARED instead of copied as a body (F65-38) —
   otherwise the access check would run against the copy that travelled along and not against the host's
   offer.  The criterion is the `level:` on the `def` at **compile time**, not the
   export table: `sys.bundle` runs at compile time, `sys.export_symbol` only at
   runtime, so the table is still empty at bundling time.  The probes forced this.
   The three `# NEEDS: host` probes (02, 03, 05) now run against this mechanism instead of
   defining their symbols themselves with `level:`; `UMBAU NOETIG` is gone.
   Original text of this item (the order correction that the probes forced):
   one line per offered symbol with address, signature and level
   (F65-23, F65-24).  **Only here can the access check from F65-4/F65-5 come into
   being at all** — before this there are no host symbols that carry a level.

   This is a correction of the order, and the acceptance probes forced it: the
   probes for refusal (`clearance-denied`) wanted a symbol "at level 2 that I may not
   reach".  A single file has no such symbol — and since F65-27 it may not
   write one for itself either.  Building the access check without an export table would mean pointing it
   at the script's own definitions, where it protects nothing.
7. ✅ **built 2026-10-17** — the write side (§5b).  It sits at the one place where
   foreign code gets into the host's table: `tryHotReload` in `exec_impl.cpp`, behind
   the branch into the isolated context.  F65-31 is thus fulfilled by construction and not
   by a check — an isolated session never reaches the write side at all.
   - **The policy** as a triple (F65-26) in `clearance.h`: `pushSessionPolicy` /
     `popSessionPolicy`, and `mayCreate()`/`mayReplace()` as the two questions.  The environment
     remains the process-wide default and may only tighten (F65-32) — for the write rights
     DOWNWARDS, because the comparison is reversed.  "Not set" is therefore not 0
     but `kPolicyUnset`: if it were 0, the environment could only switch the right off and
     never grant it.
   - **The order** of the checks is the content of F65-29: first `replace_level`,
     THEN the version decides whether anything changes.  A `version:` in the bundle grants
     no right, it is input.
   - **The collision error** (F65-30) replaces the silent skip branch — but only in a
     restricted session; for code from the same source spec/29_2 §4.2 remains valid.
   - **The readers** (F65-32): `clearance.level`/`create_level`/`replace_level`.  The
     namespace `clearance` is CLOSED in the builtin table and only these three are open, so that
     reaching for a setter reports `clearance-builtin` and not "unknown function".

   **Finding 2026-10-17: `clearance-create` is built and UNREACHABLE today.**  Not because of
   a bug in the guard, but because of the language features: `sys.bundle` takes the NAME
   of a function the host compiled itself (spec/29_2 §16).  So every
   bundle carries a name the host ALREADY HAS, and ends up in the REPLACE branch — measured on
   `fresh_helper`, which was refused with `clearance-replace`, although the probe meant
   creation.  A NEW name in the bundle presupposes that a host can accept foreign code with a
   foreign name, and that is spec/29_2 §4.3 ("dynamic loading not yet
   supported"), not this chapter.
   The guard stays in place: fail closed is right for when §4.3 comes, and a gate
   that only comes into being with the feature gets forgotten when the feature is built.  The two probes
   are on `# NEEDS: dynload` and report PENDING — the requirement is specified and
   unchecked, and that is written down instead of being hinted at by a missing test.
   **F65-27** hinges on the same point: without §4.3 a created symbol never becomes callable, so its
   level is never observable.  Whoever builds §4.3 must redeem both there.
8. ✅ **built 2026-10-17** — the write-down rule (F65-34) in `src/core/clearance_writedown.h`,
   a forward pass over the AST per body.  It hangs on the SAME lever as the
   flow check (`AIML_STATIC_CLEARANCE`) and in the same place in `compiler.cpp`: both
   are the same wish ("check the privileges statically"), so one switch and not two.
   Two maps per body, because a local has two meanings: `bound` is the bound from
   `level:`, `carried` is the strictest thing that ever flowed in.

   **Six limits, named instead of hinted at** — all follow directly from the shape of the
   pass, so they are read off and not guessed:
   (a) **Only within one file**, like the flow check and for the same reason: an
   imported declaration carries no level.
   (b) **One forward pass in text order.**  A back edge can hide a flow:
   if in a `while` the read comes BEFORE the write that sets the origin,
   it is not seen.  A fixpoint over loops would be the right thing and is not built.
   (c) **An unannotated wrapper function launders.**  `exprLevel` takes the level of a function
   from the table; a function without `level:` is not in it, so its return value carries
   nothing.  And F65-9 does not apply here: for an unannotated caller (level 0)
   `level(callee) >= 0` is always true.  Closing the hole needs return levels across
   function boundaries — interprocedural, and therefore a step of its own.
   (d) **Parameters carry no level.**  Only `decl` bounds and globals.
   (e) **Member targets are not covered.**  `bound` is keyed by the name from the `decl`;
   a target with a dotted path hits no entry.
   (f) **Container elements are measured on the container**, not per element — `idx:` checks
   against the level of the container.

   **And the requirement from this item is NOT met:** "after measuring on the corpus" cannot be
   redeemed by the rule today, because the corpus uses the modifier `level:` in not a
   single place (measured 2026-10-17: the two hits in `tests/` are comment text).
   A run with the lever set therefore only shows that the rule has nothing to look at — and
   that is no evidence that it is quiet.  It therefore stays **opt-in**, and whoever wants to make it
   the default first needs a corpus that uses it.

Two prerequisites sit **outside** this chapter and belong to whoever builds the first console:

- the missing version key of the isolated object cache in an AOT host (spec/29_2 §12, finding of
  16.10.2026) — a console would otherwise cache compiled scripts under `rev_unknown`;
- one process per user (F65-25), because the compiler keeps one JIT context at a time.

(The last paragraph of this section still listed the write-down rule as open; it is
item 8 above and has been built since 2026-10-17 — opt-in, with the unmet
requirement named there.)

Neither is a clearance problem, and neither may be discovered after the fact.

## 10. Diagnostic identifiers

**F65-21: Every refusal of this chapter carries one of these identifiers**, so that acceptance
tests and implementation cannot drift apart (the identifier is stable; the prose around it is not):

| Identifier | Fires when | Requirement |
|---|---|---|
| `clearance-denied` | a restricted session names a symbol above its clearance, including an unannotated one | F65-4, F65-5 |
| `clearance-flow` | a call/builtin/global edge violates `level(g) >= level(f)` | F65-9 |
| `clearance-self-certify` | the source tries to state the clearance it runs at | F65-6 |
| `clearance-extern` | a restricted session declares an `extern` name the host did not export | F65-13 |
| `clearance-dialect` | a restricted session uses raw pointer arithmetic or a pointer/integer cast | F65-14 |
| `clearance-builtin` | a restricted session calls a builtin below its clearance and not granted | F65-12, F65-14 |
| `clearance-create` | a session adds a symbol to the HOST's table without `create_level` (its own, isolated definitions are free) | F65-26 |
| `clearance-replace` | a session supplies a body for an existing symbol without `replace_level` | F65-26, F65-28 |
| `clearance-collision` | a restricted session defines a name the host already has (never a silent skip) | F65-30 |
| `clearance-writedown` | a value from a lower level is stored into a higher-level location | F65-34 |
| `invalid-clearance` | a session clearance outside `1…255` — above all the `0` of an unset field | F65-22 |
| `invalid-level` | `level:` is not an integer `0…255` | F65-17 |
| `invalid-grants` | `grants:` is absent a `level:`, or is **greater** than it (a gateway may only drop privilege) | F65-10 |

`invalid-level` and `invalid-grants` fire in **every** build, trusted included — an annotation is
never silently ignored. `invalid-clearance` fires wherever a clearance is handed in, including
before any source is read. The remaining nine can only fire in a restricted session, except
`clearance-flow`, which also fires in a trusted build when the opt-in rule of F65-17 is enabled.
