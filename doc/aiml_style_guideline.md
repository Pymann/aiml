# AIML Language & API Style Guideline

Design principles for **AIML the language** — the compiler, runtime, and the shape of the
built-in API surface. This is not a "how to write AIML code" reference; the *syntax and
semantics* of the language live in the spec (`spec/*.md`). This file captures the *design
values* that decide how the language and its builtins should grow.

When these principles conflict with a quick shortcut, the principle wins — root-cause the
design, don't paper over it.

---

## 1. Keep the API surface generic — no per-type method proliferation

When a capability needs to work on a new kind of data, do **not** add per-type methods
(`buffer.sha256()`, `buffer.md5()`, `buffer.crc32()`, …). That explodes the surface as
*one method × every algorithm × every type*.

Instead, make the **existing generic entry point** consume the new type. Example (hashing):
`hash.sha256(x)` detects a `buffer<u8>` argument and hashes its pointer+length internally,
so the single `hash.<algo>` surface covers strings *and* buffers, binary-safe. Reference:
`aiml_hash_hex_buf` + the `REG_HASH1B` macro in the builtin registry / `src/sys/`.

Equivalently: expose **one** generic accessor (pointer, plus length where needed) that any
byte consumer reuses, rather than wiring each consumer into every container per-feature.

## 2. Reusable / cross-tool logic belongs in the compiler & runtime

If a piece of functionality is needed by more than one AIML program or tool, build it
**once in the C++ compiler/runtime as a builtin** — do not copy an AIML-code helper into
every tool.

**Why:** duplicated AIML helpers drift between implementations and don't scale; a builtin
is available everywhere in one identical, tested form. Infrastructure logic is also usually
clumsy to express as AIML boilerplate, where C++ is more efficient and expressive.

**How to apply:**
- For any new cross-tool need, first check whether a builtin/runtime function already exists.
- If not, weigh building it into the compiler before writing an AIML helper.
- Pattern: C function in the relevant `src/.../*.cpp`, register via `AddSymbol` in the
  compiler, expose as a namespace function in the builtin registry.
- Exception: genuinely tool-specific logic (a domain-specific calculation) stays in AIML.
- Litmus test before writing a generic AIML helper: *"wouldn't this be better as a C++ builtin?"*

## 3. Build cross-cutting concerns into the language — then use them

Cross-cutting concerns (i18n, serialization, hashing, storage) should be first-class
language/runtime features, and AIML code should **use the built-in feature rather than
hand-roll an equivalent**.

Example — i18n/translation is built in (`spec/44_8_vocab.md`): `push_text_batch` translates
labels/buttons automatically; `decorator: [vocab]` translates formatter fragments before
concatenation. Hand-rolled `if color==1 return "Feuer"` mappings are redundant and bypass
the `vocab.json` pipeline. Keep the canonical label form English; let vocab supply the
translations.

The general rule: if the language already owns a concern, extend and reuse that path — a
hand-rolled parallel implementation is a design smell, not a shortcut.

## 4. Static checks encode design intent — fix the root, don't bypass

The AIML static checker exists to enforce good design. When a check fires, the fix is to
satisfy the intent, **not** to disable the check.

Example: when `str_cmp` fires (a variable compared against 2+ string literals = a
hand-rolled string switch), introduce an **enum** — do not set `static_off: ["str_cmp"]`.
Model wire/status string fields as `i32`-backed enums (`0` = a sensible default) and compare
`x == SlotKind.ai` instead of `x == "ai"`. Enums don't count against the model/class limits.

`static_off` is a shortcut; prefer the typed solution the check is steering you toward. The
same stance applies to every static check: treat a firing check as a design signal.

## 5. When a language feature has a bug, fix the language

If a legitimate language construct (e.g. `model` + `sync:` with inheritance in the mirror
runtime) doesn't work, treat it as a **compiler/runtime bug and fix it** — do not architect
the AIML code around the defect. Working around a language bug hides it and spreads the
workaround; fixing it keeps the language honest and benefits every caller.

---

## Related

- Language syntax & semantics: `spec/*.md` (types, models/classes, enums, inheritance,
  collections, pointers, namespaces, hot-reload, network, hash, vocab, …).
- Repo/contributor workflow (build via venv-activate, `exodus build`, never delete
  `__exodus_cache`, don't filter build warnings, spec-first) is tracked separately.
