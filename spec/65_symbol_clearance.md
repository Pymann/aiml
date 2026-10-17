# 65 — Symbol Clearance (Levels) [PROPOSAL]

> Status: PROPOSAL, **alle Schritte 2 bis 8 gebaut** (17.10.2026) und gemessen (Labor
> `tests/clearance/` bei `STAGE=writedown`: 12 positive, 25 negative Proben gruen).
> **Ausstehend sind zwei**, und zwar nicht wegen dieses Kapitels: `clearance-create` ist
> gebaut, aber unerreichbar, solange spec/29_2 §4.3 (dynamisches Nachladen) fehlt — siehe §9
> Schritt 7.
> **Zwei Teile sind opt-in und bleiben es:** die Flusspruefung (F65-17) und die
> Write-down-Regel (F65-34), beide an `AIML_STATIC_CLEARANCE`.  Fuer Write-down ist die
> Auflage „nach Messung am Korpus" NICHT erfuellt: der Korpus benutzt `level:` an keiner
> Stelle, ein Lauf zeigt also nur, dass die Regel nichts zu sehen hat.  Die benannten
> Grenzen stehen in §9 Schritt 8, nicht im Code versteckt.
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

Die drei gehen **zusammen am `sys.exec`** hinaus, in der Reihenfolge dieser Tabelle (F65-36)
— nicht ueber drei Umgebungsvariablen, denn drei getrennte Hebel fuer eine Entscheidung sind
die Drift aus F65-23.  Die Umgebung bleibt die prozessweite Vorgabe und darf nur
verschaerfen (F65-32).

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

## 5c. Wie ein Wirt anbietet — die zwei fehlenden Sprachmittel

§5a sagt, dass Signatur und Adresse die zweite Haelfte der Uebergabe sind (F65-24).  Gebaut ist
bisher nur die erste: Name und Level.  Fuer die zweite fehlen zwei Sprachmittel, und ihr
Fehlen ist der Grund, warum die drei `host`-Proben nicht laufen koennen.

**F65-36: Die Freigabe gehoert an die AUSFUEHRUNG, nicht an das Buendel.**

```yaml
- decl: [pkg, Code, { call: [sys.bundle, "user_script", { context: true }] }]
- decl: [rc,  i32,  { call: [sys.exec, pkg, 7] }]         # nur Freigabe: 7, nichts schreiben
- decl: [rc2, i32,  { call: [sys.exec, pkg, 7, 7, 0] }]   # ... darf anlegen, nicht ersetzen
```

Die drei Werte der Richtlinie aus F65-26 stehen **in der Reihenfolge jener Tabelle** —
`clearance`, `create_level`, `replace_level` — und sie reisen zusammen, als ein Vorgang.
Fehlende hintere Werte sind `0`, was nach F65-22 **niemand** heisst: wer nichts sagt, darf
nichts schreiben (fail closed).  Jeder der drei ist ein gewoehnlicher Ausdruck und kein
Literal, weil eine Konsole die Richtlinie **je Nutzer zur Laufzeit** bildet; eine
Richtlinie, die nur als Literal in der Quelle stehen koennte, waere fuer den eigentlichen
Fall unbrauchbar.

Ein Buendel ist **Code**, eine Sitzung ist eine **Handlung**.  Derselbe Code muss fuer zwei
Nutzer mit verschiedenen Freigaben ausfuehrbar sein — die Richtlinie an das `Code`-Objekt zu
haengen wuerde das verbieten, obwohl genau das der Fall ist, um den es geht.

(Die erste Fassung dieser Anforderung stellte die Freigabe an `sys.bundle`, mit der Begruendung,
das `Code`-Objekt beschreibe die Sitzung, weil es schon `context` traegt.  Das war falsch
herum: `context` beschreibt, WIE uebersetzt wird, und ist eine Eigenschaft des Codes; die
Freigabe beschreibt, WER ausfuehrt.  Nebenbei haette es ein sechstes Feld in einer Struktur
gebraucht, deren Layout an drei Stellen gespiegelt ist — LLVM-Struct, C-Struct in `exec_impl`
und `__aiml_code_new` —, wo eine Abweichung still falsche Daten liest.)

`AIML_CLEARANCE`, `AIML_CREATE_LEVEL` und `AIML_REPLACE_LEVEL` bleiben die Vorgabe des
Prozesses und der Hebel fuer Proben (F65-20); die Argumente an `sys.exec` gelten fuer diese
eine Sitzung und duerfen nur STRENGER sein als die Vorgabe.  „Strenger" laeuft bei den beiden
Schreibrechten in die ANDERE Richtung als bei der Freigabe, weil die Vergleiche
gegenlaeufig sind (`clearance <= create_level`): strenger ist dort die KLEINERE Zahl, und 0
ist das Minimum, also niemand.  Eine nicht gesetzte Umgebungsvariable ist deshalb keine 0,
sondern „keine Vorgabe" — sonst waere das Recht nie erteilbar, nur abschaltbar.
Damit kann `sessionClearance()` seinen prozessweiten Zwischenspeicher nicht behalten: eine
Freigabe, die einmal gelesen und gemerkt wird, ist keine Eigenschaft einer Sitzung.

**F65-39: Eine Adresse kommt auf ZWEI Wegen, und der zweite ist der fuer eine Konsole.**
`sys.export_symbol` (F65-37) setzt voraus, dass der Wirt den Code beim **Uebersetzen** schon
hatte — er meldet einen eigenen `def` an.  Ein Wirt, der **fremden** Quelltext uebersetzen
laesst, kann das nicht: er IST der Uebersetzer und kennt das Skript erst zur Laufzeit.  Fuer
ihn gilt:

| | Name + Level | Adresse |
|---|---|---|
| eigener Code (F65-37) | `sys.export_symbol` | derselbe Aufruf |
| fremder Quelltext | Export-Tabelle (`AIML_EXPORTS`) | vorgeladene Bibliothek im Prozess |

Beide Haelften aus F65-24 sind damit auch im zweiten Fall da, nur aus zwei Quellen — und das
ist hier richtig, nicht die Drift aus F65-23: die **Politik** steht an einer Stelle (der
Tabelle), die Adresse ist eine Eigenschaft des Prozesses.  Entscheidend ist die Richtung:
die Zulassung haengt an der TABELLE, nicht an der Auffindbarkeit des Symbols.  Sonst waere
jede Bibliothek, die ein Wirt fuer sich selbst laedt, automatisch sein Angebot an jeden
Nutzer.

Gemessen 17.10.2026: `14_extern_preloaded_callable` (Level 7 angeboten, Freigabe 7 → Aufruf
liefert 42) und `neg/n28_preloaded_symbol_too_low` (dieselbe Bibliothek, Symbol auf Level 2
→ `clearance-denied`, obwohl die Adresse im Prozess liegt).  Probe `08` belegte nur die
Zulassung und rief nie auf — die halbe Anforderung.

**F65-40: Ein zugelassenes `extern` ohne aufloesbares Symbol stuerzt heute ab.**  Gemessen
am selben Tag: dieselbe Probe ohne `LD_PRELOAD` endet mit **SIGSEGV**, nicht mit einer
Diagnose — der Aufruf geht auf eine nicht aufgeloeste Adresse.  Das ist kein
Clearance-Loch (die Zulassung hat korrekt entschieden), aber fuer einen Wirt, der fremden
Quelltext uebersetzt, ist es eines: eine vergessene oder falsch gepfadete Bibliothek nimmt
den Prozess mit, statt dem Nutzer zu sagen, was fehlt.  Gehoert geprueft, wo die
`extern`-Zulassung ohnehin schon entscheidet — dort ist der Name bekannt und `dlsym` eine
Zeile.  **Noch nicht gebaut**, und das steht hier, statt in einem Absturzbericht aufzutauchen.

**F65-37: `sys.export_symbol(name, fn, level)` — eine Zeile, ein Aufruf.**

```yaml
- def: { name: read_status, level: 7, return: i32, body: [{ return: [3] }] }
- decl: [fpr, fn<:i32>]
- set:  [fpr, read_status]
- call: [sys.export_symbol, "read_status", fpr, 7]
```

Der Aufruf macht beide Haelften in einem Zug: er meldet die **Adresse** an
(`DynamicLibrary::AddSymbol`, damit der JIT den Namen aufloest) und traegt **Name und Level** in
die Export-Tabelle (§5a).  Zwei getrennte Aufrufe waeren zwei Listen, die driften — genau das
verbietet F65-23.  Die Adresse kommt aus einem Funktionszeiger auf den eigenen `def` (spec/12_1),
nicht aus `dlsym`: so braucht es kein `-rdynamic`, und bei einem statischen Build funktioniert es
ueberhaupt nur so (gemessen: 0 von 38 aiml-Funktionen stehen in `.dynsym`).

**Nur im vertrauenswuerdigen Zustand.** In einer eingeschraenkten Sitzung ist
`sys.export_symbol` abzulehnen (`clearance-builtin`): ein Skript, das sich selbst exportieren
darf, setzt sein eigenes Level — Selbstbeglaubigung in ihrer schaedlichsten Form.  Das `level`
dieses Aufrufs ist deshalb auch nicht an das `level:` des `def` gebunden: der Wirt entscheidet,
was er anbietet, und darf weniger anbieten als die Funktion traegt.

**F65-38: Ein Buendel traegt von einem exportierten Symbol die DEKLARATION, nicht den Rumpf.**
`sys.bundle` nimmt heute die Quelltexte der gerufenen Funktionen mit (`referencedFuncs`,
spec/29_2 §16) — sonst meldete der Empfaenger „Unknown function".  Fuer ein Symbol aus der
Export-Tabelle ist das aber falsch: das Skript bekaeme eine **Kopie** und wuerde sie im eigenen
Kontext ausfuehren, mit dem Level der Sitzung statt dem des Wirts.  Die Zugriffspruefung liefe
dann ins Leere, weil es nichts mehr zu verweigern gibt.

**Das Kriterium ist das `level:` am `def`, nicht die Export-Tabelle.**  Das sieht nach einer
Feinheit aus und ist der Kern: `sys.bundle` laeuft zur **UEBERSETZUNGSZEIT**,
`sys.export_symbol` zur **Laufzeit**.  Wer beim Buendeln die Tabelle befragt, befragt sie leer —
gemessen am 17.10.2026 an `09_host_exports_callable`: der Rumpf wurde mitgebuendelt, und in der
eingeschraenkten Sitzung schlug sein `level:` als Selbstbeglaubigung an (F65-27).  Ein Umweg
ueber die Tabelle ist hier nicht moeglich, weil sie zu diesem Zeitpunkt noch nicht existiert.

Darum: traegt ein gerufener `def` ein `level:`, gehoert er zur angebotenen Oberfläche, und der
Wirt buendelt seine Signatur statt seines Rumpfs.  Was der Wirt dann tatsaechlich anbietet,
entscheidet `sys.export_symbol` zur Laufzeit — er darf WENIGER anbieten als die Annotation
sagt (F65-37), und was er nicht anbietet, scheitert beim Empfaenger an der `extern`-Zulassung.
Die beiden Zeitpunkte sind damit klar getrennt: die Annotation sagt „gehoert zur Oberfläche",
die Tabelle sagt „ist jetzt erreichbar, und auf welchem Level".

Urspruenglicher Text (das Kriterium war falsch): steht ein gerufener Name in der Export-Tabelle
Darum: steht ein gerufener Name in der Export-Tabelle, buendelt der Wirt seine Signatur und
nicht seinen Rumpf.  Das ist dieselbe Trennung wie in F65-24 (Signatur ueber eine Deklaration,
Adresse ueber `AddSymbol`) — und es ist der Grund, warum ein exportiertes Symbol beim
Aufrufer AUSFUEHRBAR ist, ohne dass er es LESEN kann.

**Was damit NICHT kommt.** Die Signatur reist weiter ueber eine Deklaration, nicht ueber dieses
Builtin — der Wirt gibt dem Skript eine Datei mit `- extern: "C"`-Deklarationen der angebotenen
Namen, und die `extern`-Zulassung aus F65-13 ist genau das Tor dafuer.  Eine Signatur in einen
String zu kodieren waere eine zweite Typsprache neben aiml; das Deklarationsmittel gibt es schon
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

**Zwei Bedeutungen, die auseinandergehalten werden muessen**, sonst ist die Regel in einem
Zwischenschritt umgangen:

- Das `level:` einer Lokalen ist die **Schranke fuer das Speichern** — was darf hier hinein.
- Was eine Lokale **traegt**, ist das Strengste, was je hineingeschrieben wurde, und das
  reist weiter.  Eine Lokale OHNE Annotation traegt darum ebenfalls ein Level, sobald ein
  eingeschraenkter Wert hineinlaeuft; sonst wuerde `tmp = read_secret()` die Herkunft
  vergessen und `open_log = tmp` waere erlaubt.  Genau diese zwei Zeilen sind die
  naheliegendste Umgehung, nicht die Zuweisung aus dem Beispiel oben.

Ohne Annotation und ohne eingeschraenkte Herkunft traegt eine Lokale **kein** Level — nicht
Level 0.  Das ist der Unterschied zu F65-5: dort ist 0 die fail-closed-Vorgabe fuer den
ZUGRIFF, hier wuerde 0 bedeuten, dass jedes Literal in jede annotierte Lokale ein Verstoss
waere.  Eine Regel, die jedes Programm bricht, schuetzt nichts.

**F65-35: Builtins are closed from the ground up.** Every builtin namespace and function is
level `0` unless the table (§5 checkpoint 2) explicitly raises it. Earlier drafts of this chapter
named `sys`, `db`, `io`, `net` as the closed ones, which was the wrong shape: it made a list of
the dangerous names, and such a list is complete only until the next builtin is added. The
default closes, and opening is the deliberate act — the same direction as F65-5 for symbols and
F65-26 for the write side. One rule, three places, no exceptions to remember.

## 9. Order of implementation

1. This chapter, then acceptance tests (`tests/clearance/`), then code — in that order.
2. ✅ **gebaut 17.10.2026** — Parse + validate `level:`/`grants:` everywhere; enforce nothing
   (F65-17 second sentence).  Validierung an einer Stelle: `src/core/clearance.h`; getragen in
   `DeclStmt`, `FunctionDecl`, `StructMember` und den sieben Typformen.
3. ✅ **gebaut 17.10.2026** — flow check as an opt-in spec/49 rule, with path diagnostics
   (F65-9, F65-11): `src/core/clearance_flow.h`, Gesamtlauf nach `astCompiler.compile`.
   Grenze: nur Kanten INNERHALB einer Datei (importierte Funktionen stehen als Deklaration
   ohne Level in `ctx.functions` und werden uebersprungen, sonst Falsch-Positive).
   Datei-uebergreifend braucht die Propagation wie bei den Enum-Mengen — offen.
4. ✅ **gebaut 17.10.2026** — die Sitzung selbst: Freigabe aus dem Wirt (`AIML_CLEARANCE`,
   F65-20), Gueltigkeit 1…255 mit `invalid-clearance` (F65-22), und Selbstbeglaubigung
   abgelehnt — `level:`/`grants:` in eingeschraenkter Quelle sowie ein Top-Level `clearance:`
   (F65-6, F65-27).  Das ist der Teil, der ohne Wirt pruefbar ist.
5. ✅ **gebaut 17.10.2026** — die Waechter, die keinen Wirt brauchen:
   - **Builtin-Tabelle** (F65-35) in `src/core/clearance_builtins.h`, Form wie `purity.h`
     (Vorgabe je Namensraum, Ausnahmen je Funktion).  Offen sind die **reinen** Namensraeume
     `math`, `hash`, `enums`, `compress`, `cascade` plus `log` — ohne Ausgabe ist eine Konsole
     keine.  Alles andere ist 0.  Geprueft wird am **Namensraum**, nicht an der einzelnen
     Funktion: `sys.irgendwas` wird daher auch dann abgefangen, wenn es nur ueber den
     gemangelten C-Namen (`sys_irgendwas`) erreichbar waere.
   - **Dialekt** (F65-14) in `visitCast`: ein `cast:` auf einen Zeiger, oder von einem Zeiger
     auf eine Ganzzahl, ist in einer eingeschraenkten Sitzung ein Fehler.
   - **`extern`-Zulassung** (F65-13) in `commitFunc`: derzeit ist die zulaessige Menge **leer**,
     weil die Export-Tabelle (§5a) noch nicht existiert — jedes `extern` in einer
     eingeschraenkten Sitzung wird abgelehnt.  Das wird von selbst weniger streng, sobald es
     eine Tabelle gibt, und ist bis dahin die richtige Richtung (fail closed).

   **Zwei Grenzen, bewusst und nicht zufaellig:**
   (a) **Methoden auf Werten sind nicht erfasst.**  Der Waechter sitzt am Dispatch fuer
   Namensraum-Builtins (`ns.fn`); `.push`, `.len`, `.split` gehen dort nicht durch.  Das ist
   Absicht — sie sind Sprachmittel, keine Faehigkeiten — aber es ist eine Entscheidung, und
   wer sie aendern will, muss sie hier aendern.
   (b) **Der Dialekt-Waechter deckt `cast:` ab, nicht alles aus F65-14.**  Rohes
   Buffer-Indizieren ausserhalb der geprueften Formen ist offen.  `cast:` ist der Hauptweg,
   ueber den ein Wert zum Zeiger wird — „Hauptweg" ist aber nicht „alle".
   Builtin-Tabelle (F65-35) und die `extern`-Zulassung (F65-13).
6a. ✅ **gebaut 17.10.2026** — die Export-Tabelle mit Name und Level (`AIML_EXPORTS`, eine
   Zeile je Symbol).  Damit entstehen die Zugriffspruefung F65-4/F65-5 und die
   `extern`-Zulassung F65-13 — beide lesen dieselbe Tabelle (F65-23).  Ein verweigertes
   Symbol wird als VERWEIGERT gemeldet, nicht als „unbekannt" (F65-12).
6b. ✅ **gebaut 17.10.2026** — Signatur und Adresse (F65-24), und damit kann ein Skript ein
   Wirts-Symbol AUFRUFEN.  Die beiden Haelften der Uebergabe gehen in EINEM Aufruf hinaus,
   damit Name, Level und Adresse nicht auseinanderlaufen koennen: `sys.export_symbol`
   (F65-37) nimmt Name, Funktionszeiger (`fn<…>`, spec/12_1) und Level, prueft das Level,
   meldet die Adresse per `AddSymbol` an und schreibt denselben Eintrag in die Tabelle aus
   6a — `src/sys/clearance_runtime.cpp`.
   Die Freigabe haengt am `sys.exec(pkg, clearance)` (F65-36) und nicht in der Umgebung:
   ein Buendel ist Code, eine Sitzung ist ein Vorgang.  Dass `pushSessionClearance` nur
   verschaerfen kann, ist die Regel dahinter.
   Ein exportiertes Symbol wird DEKLARIERT gebuendelt statt als Rumpf kopiert (F65-38) —
   sonst liefe die Zugriffspruefung gegen die mitgereiste Kopie und nicht gegen das Angebot
   des Wirts.  Kriterium ist das `level:` am `def` zur **Uebersetzungszeit**, nicht die
   Export-Tabelle: `sys.bundle` laeuft beim Uebersetzen, `sys.export_symbol` erst zur
   Laufzeit, die Tabelle ist beim Buendeln also noch leer.  Die Proben haben das erzwungen.
   Die drei `# NEEDS: host`-Proben (02, 03, 05) laufen jetzt gegen diese Vorrichtung statt
   ihre Symbole selbst mit `level:` zu definieren; `UMBAU NOETIG` ist weg.
   Urspruenglicher Text dieses Punktes (die Reihenfolge-Korrektur, die die Proben erzwungen
   haben): eine Zeile je angebotenem Symbol mit Adresse, Signatur und Level
   (F65-23, F65-24).  **Erst hier kann die Zugriffspruefung aus F65-4/F65-5 ueberhaupt
   entstehen** — vorher gibt es keine Wirts-Symbole, die ein Level tragen.

   Das ist eine Korrektur der Reihenfolge, und die Abnahmeproben haben sie erzwungen: die
   Proben zur Verweigerung (`clearance-denied`) wollten ein Symbol „auf Level 2, das ich nicht
   erreichen darf".  Eine einzelne Datei hat kein solches Symbol — und seit F65-27 darf sie
   sich auch keines schreiben.  Die Zugriffspruefung ohne Export-Tabelle zu bauen hiesse, sie
   gegen die eigenen Definitionen des Skripts zu richten, wo sie nichts schuetzt.
7. ✅ **gebaut 17.10.2026** — die Schreibseite (§5b).  Sie sitzt an der einen Stelle, an der
   fremder Code in die Tabelle des Wirts gelangt: `tryHotReload` in `exec_impl.cpp`, hinter
   der Abzweigung in den isolierten Kontext.  Damit ist F65-31 baulich erfuellt und nicht
   durch eine Pruefung — eine isolierte Sitzung erreicht die Schreibseite gar nicht.
   - **Die Richtlinie** als Dreier (F65-26) in `clearance.h`: `pushSessionPolicy` /
     `popSessionPolicy`, und `mayCreate()`/`mayReplace()` als die zwei Fragen.  Die Umgebung
     bleibt prozessweite Vorgabe und darf nur verschaerfen (F65-32) — bei den Schreibrechten
     nach UNTEN, weil der Vergleich gegenlaeufig ist.  „Nicht gesetzt" ist deshalb nicht 0,
     sondern `kPolicyUnset`: waere es 0, koennte die Umgebung das Recht nur abschalten und
     nie erteilen.
   - **Die Reihenfolge** der Pruefungen ist der Inhalt von F65-29: zuerst `replace_level`,
     DANN entscheidet die Version, ob sich etwas aendert.  Ein `version:` im Buendel erteilt
     kein Recht, es ist Eingabe.
   - **Der Kollisionsfehler** (F65-30) ersetzt den stillen Skip-Zweig — aber nur in einer
     eingeschraenkten Sitzung; fuer Code aus derselben Quelle bleibt spec/29_2 §4.2 gueltig.
   - **Die Leser** (F65-32): `clearance.level`/`create_level`/`replace_level`.  Der
     Namensraum `clearance` ist in der Builtin-Tabelle ZU und nur diese drei sind offen, damit
     ein Griff nach einem Setzer `clearance-builtin` nennt und nicht „unbekannte Funktion".

   **Befund 17.10.2026: `clearance-create` ist gebaut und heute UNERREICHBAR.**  Nicht wegen
   eines Fehlers im Waechter, sondern wegen der Sprachmittel: `sys.bundle` nimmt den NAMEN
   einer Funktion, die der Wirt selbst uebersetzt hat (spec/29_2 §16).  Damit traegt jedes
   Buendel einen Namen, den der Wirt SCHON HAT, und landet im ERSETZEN-Zweig — gemessen an
   `fresh_helper`, das mit `clearance-replace` abgelehnt wurde, obwohl die Probe das Anlegen
   meinte.  Ein NEUER Name im Buendel setzt voraus, dass ein Wirt fremden Code mit einem
   fremden Namen annehmen kann, und das ist spec/29_2 §4.3 („dynamic loading not yet
   supported"), nicht dieses Kapitel.
   Der Waechter bleibt stehen: fail closed ist dann richtig, wenn §4.3 kommt, und ein Gate,
   das erst mit dem Feature entsteht, wird beim Bau des Features vergessen.  Die beiden Proben
   stehen auf `# NEEDS: dynload` und melden AUSSTEHEND — die Anforderung ist spezifiziert und
   ungeprueft, und das steht da, statt von einem fehlenden Test angedeutet zu werden.
   **F65-27** haengt am selben Punkt: ohne §4.3 wird ein angelegtes Symbol nie aufrufbar, sein
   Level also nie beobachtbar.  Wer §4.3 baut, muss beides dort einloesen.
8. ✅ **gebaut 17.10.2026** — die Write-down-Regel (F65-34) in `src/core/clearance_writedown.h`,
   ein Vorwaertsdurchlauf ueber den AST je Rumpf.  Sie haengt am GLEICHEN Hebel wie die
   Flusspruefung (`AIML_STATIC_CLEARANCE`) und an derselben Stelle in `compiler.cpp`: beides
   ist derselbe Wunsch („pruefe die Privilegien statisch"), also ein Schalter und nicht zwei.
   Zwei Karten je Rumpf, weil eine Lokale zwei Bedeutungen hat: `bound` ist die Schranke aus
   `level:`, `carried` ist das Strengste, was je hineinlief.

   **Sechs Grenzen, benannt statt angedeutet** — alle folgen unmittelbar aus der Form des
   Durchlaufs, sind also gelesen und nicht vermutet:
   (a) **Nur innerhalb einer Datei**, wie die Flusspruefung und aus demselben Grund: eine
   importierte Deklaration traegt kein Level.
   (b) **Ein Vorwaertsdurchlauf in Textreihenfolge.**  Eine Rueckwaertskante kann einen Fluss
   verdecken: steht in einem `while` das Lesen VOR dem Schreiben, das die Herkunft setzt, wird
   es nicht gesehen.  Ein Fixpunkt ueber Schleifen waere das Richtige und ist nicht gebaut.
   (c) **Eine unannotierte Huellfunktion wascht.**  `exprLevel` nimmt das Level einer Funktion
   aus der Tabelle; eine Funktion ohne `level:` steht nicht darin, ihr Rueckgabewert traegt also
   nichts.  Und F65-9 greift hier nicht: fuer einen unannotierten Aufrufer (Level 0) ist
   `level(callee) >= 0` immer wahr.  Das Loch zu schliessen braucht Rueckgabe-Level ueber
   Funktionsgrenzen — interprozedural, und damit ein eigener Schritt.
   (d) **Parameter tragen kein Level.**  Nur `decl`-Schranken und Globale.
   (e) **Member-Ziele sind nicht erfasst.**  `bound` ist nach dem Namen aus der `decl`
   geschluesselt; ein Ziel mit Punktpfad trifft keinen Eintrag.
   (f) **Container-Elemente werden am Behaelter gemessen**, nicht je Element — `idx:` prueft
   gegen das Level des Behaelters.

   **Und die Auflage aus diesem Punkt ist NICHT erfuellt:** „nach Messung am Korpus" kann die
   Regel heute nicht einloesen, denn der Korpus benutzt den Modifier `level:` an keiner
   einzigen Stelle (gemessen 17.10.2026: die zwei Treffer in `tests/` sind Kommentartext).
   Ein Durchlauf mit gesetztem Hebel zeigt also nur, dass die Regel nichts zu sehen hat — und
   das ist kein Beleg, dass sie leise ist.  Sie bleibt darum **opt-in**, und wer sie zur
   Vorgabe machen will, braucht zuerst einen Korpus, der sie benutzt.

Two prerequisites sit **outside** this chapter and belong to whoever builds the first console:

- the missing version key of the isolated object cache in an AOT host (spec/29_2 §12, finding of
  16.10.2026) — a console would otherwise cache compiled scripts under `rev_unknown`;
- one process per user (F65-25), because the compiler keeps one JIT context at a time.

(Der letzte Absatz dieses Abschnitts nannte die Write-down-Regel noch als offen; sie ist
Punkt 8 oben und seit 17.10.2026 gebaut — opt-in, mit der dort benannten unerfuellten
Auflage.)

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
