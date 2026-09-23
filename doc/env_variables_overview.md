# Environment Variables — Overview

## Overview

The AIML compiler and its runtime read a small set of environment variables via
`getenv`. They fall into three groups:

1. **Memory model / semantics** — change the code the compiler *emits*.
2. **Tracing / diagnostics** — only change *output*, never behavior.
3. **Runtime / network** — read at runtime by the compiled program.

A variable reaches the compiler only through the process environment of the
`aiml` invocation. When building with `exodus`, put it in the `"env"` block of
the project's `exodus-*.json` — nothing else forwards it to the compiler:

```json
{
  "env": {
    "AIML_MEM_V3": "1",
    "AIML_LIFETIME_DROPS": "1"
  }
}
```

### Boolean convention

Unless noted otherwise, a boolean variable is **ON** when it is set to any value
other than `0`, `off`, `OFF`, `false`, `FALSE` (or the empty string). Some
switches (e.g. `AIML_PREFAB_LENIENT`, `AIML_SER_NOGUARD`, `AIML_DEBUG`) are
**presence-only**: merely being set — to any value, including `0` — turns them
on. Those are called out below.

---

## 1. Memory model / semantics

These alter the generated IR. They must be consistent across every compilation
unit of a program (including header-only import sub-compilers), or layout and
release code disagree → IR-verify failure / crash.

### `AIML_MEM_V3` — spec/61 memory model

- **Default: ON** (`CompilerContext::memV3 = true`).
- Enables the spec/61 (v3) memory model: Option-B refcounted **handles** for
  `list`/`map`/`buffer`, explicit `new`/`free`, and the `ptr<T>` / `ref<T>` /
  `shared<T>` ownership kinds.
- Set `AIML_MEM_V3=0` (or `off`/`false`) to fall back to the legacy inline
  value model.
- **Also gates the `free:` operator** (spec/20): the `free:` statement only
  materializes as a real AST node — enabling the §10 ownership diagnostics and
  the active early-release in codegen — under this flag. The parse-time gate and
  the codegen gate share the same env read *and default* so they always agree; a
  mismatch would silently turn `free:` into a no-op while codegen believes the
  model is active, leaking every `free`d box.

### `AIML_LIFETIME_DROPS` — automatic scope drops

- **Default: OFF** (`CompilerContext::lifetimeDropsEnabled = false`).
- Emits automatic release at scope exit for locals, managed strings and
  container/handle values, plus caller-move for `consumed` parameters. This is
  the complement to explicit `new`/`free`: without it, scope-local values that
  are not explicitly `free`d are leaked.
- memV3 projects that rely on automatic cleanup set `AIML_LIFETIME_DROPS=1`
  alongside `AIML_MEM_V3=1`.

### `AIML_MODEL_REFCOUNT` — refcounted value models

- **Default: OFF**.
- Lays out value-models as refcounted `%M*` pointers (incref/decref) instead of
  inline. Must be identical in every context from birth, since a header-only
  pass with a different setting lays out fields differently from the main
  compile.

### `AIML_ALLOW_PTR_CONTAINER` — allow `ptr<T>` in value lists

- **Default: OFF** (presence-only escape hatch).
- Suppresses the diagnostic that rejects `ptr<T>` inside a container value list.
  Intended only to work around the check deliberately.

### `AIML_STRICT_TOPLEVEL` — deterministic top-level order

- **Default: OFF**.
- Enforces a deterministic ordering of top-level statements. (`main()` order is
  already deterministic; this concerns free top-level statements.)

### `AIML_PREFAB_LENIENT` — tolerant prefab resolution

- **Default: OFF** (presence-only).
- Makes prefab resolution lenient instead of a hard error on a missing entry.

### `AIML_SER_NOGUARD` — disable serialize/free cycle guard

- **Default: OFF** (presence-only).
- Disables the cycle-visited guard (`g_fre_seen`) in the serialize / deep-free
  walker. Debugging only — with a cyclic graph this can infinite-loop or
  double-free.

---

## 2. Tracing / diagnostics

These never change generated behavior — only diagnostic output (to `stderr`).
See **spec/48** for the full trace format and matching rules.

| Variable | Effect |
|---|---|
| `AIML_TRACE` | Function-call entry tracing by name (`"*"` for all). See spec/48. |
| `AIML_SIZETRACE` | Trace struct / container size computation. See spec/48. |
| `AIML_TRACE_SYMBOL` | Trace a specific symbol. See spec/48. |
| `AIML_TRACE_FRE` | Trace string / heap releases (`fre` / decref) — leak hunting. |
| `AIML_MODEL_TRACE` | Trace model allocation / lifecycle. |
| `AIML_DEBUG` | General debug output (presence-only). |

---

## 3. Runtime / network

Read at runtime by the compiled program, not by the compiler.

| Variable | Effect |
|---|---|
| `AIML_WSS_CA` | Path to a CA certificate bundle for WSS (WebSocket TLS). See spec/32_4. |
| `AIML_WSS_INSECURE` | Disable WSS certificate verification (development only). |

---

## Recommended baseline for memV3 projects

Set both memory flags explicitly in the project's `exodus-*.json` `"env"` block:

```json
"env": { "AIML_MEM_V3": "1", "AIML_LIFETIME_DROPS": "1" }
```

`AIML_MEM_V3` is ON by default, but stating it explicitly keeps the build
unambiguous and robust against future default changes. Everything in groups 2
and 3 stays unset in normal operation.
