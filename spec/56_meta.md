# meta — Declaration Metadata

`meta:` attaches a `list<string>` of free-form tags to a `def` (free function
or class/interface method).  Tags the compiler does not recognise are ignored,
so `meta:` is forward-compatible — new tags can be added over time without
breaking older code.

Today exactly one tag is recognised: `deprecated`.  Future tags (e.g.
`experimental`, `internal`, versioning hints) attach here too.

```yaml
- def:
    name: <fn_name>
    args: [...]
    return: <type>
    meta: [deprecated, ...]       # list<string>
    body: [...]
```

## Recognised tags

| Tag          | Effect                                                          |
|--------------|----------------------------------------------------------------|
| `deprecated` | Every **call site** of this function/method emits a compile-time WARNING (not an error). Compilation and runtime are unaffected. |

## `deprecated` — compile-time warning at every use

Marking an old API `meta: [deprecated]` makes the compiler point at every
remaining caller — the intended workflow for migrating code off a symbol
before deleting it.

```
[WARNING] ... src.aiml:831: 'CardService.get' is deprecated — { call: [cards_ref.get, req_email, card_id] }
```

Each warning carries:
- the source **file:line** of the call,
- the fully-qualified name (`fn` for free functions, `Class.method` for methods),
- the **full call node reconstructed as single-line AIML** (`{ call: [...] }`),
  so the output is greppable and can drive a `sed`/Python parse-and-replace.

One warning is emitted **per call site** (per usage location), so the count of
warnings equals the number of places still to migrate.

## Scope

- Free functions: warned via the normal call path.
- Concrete class methods (`obj.method()` where `obj: ptr<Class>`): warned via
  `Class_method` lookup.
- Interface **virtual** dispatch is not yet covered (the concrete target is
  only known at runtime); mark the concrete implementations if you need those
  sites flagged.

## Implementation

- Parsed into `FunctionDecl::meta` (`function_builder.cpp`).
- `FunctionInfo::deprecated` is set in `visitFunction` when `meta` contains
  `"deprecated"`.
- Warnings emitted in `visitCall` (free) and `visitObjectMethodCall` (methods);
  the call node is rendered by `exprToAiml` (AST → single-line flow YAML,
  because event-parsed bodies keep only a source Mark, not a DOM).
- Repro / regression: `tests/test_deprecated_meta_warning.aiml`.
