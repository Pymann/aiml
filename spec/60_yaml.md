# 60 - YAML Documents

## 1. Goal

AIML provides a read-only `yaml` namespace for parsing and traversing YAML
documents.  It is intended for programs that consume structured configuration,
metadata, manifests, or AIML source without treating YAML as unstructured text.

The namespace exposes document and node handles.  It does not evaluate YAML as
AIML code, mutate parsed documents, preserve formatting/comments, or infer an
application schema.

## 2. Types

`yaml_doc` and `yaml_node` are opaque language types.  A document is owned through
`ptr<yaml_doc>`; a node is a non-owning view into its document and is bound as
`ref<yaml_node>` (spec/61 §2.1 borrowed view, §5.4):

```yaml
- decl: [doc, ptr<yaml_doc>]
- decl: [node, ref<yaml_node>]
```

Binding `yaml.get` to `ptr<yaml_node>` is a compile error (spec/61 §5.4: a read yields a
borrow, never a second owner).  `yaml.root`, `yaml.item` and `yaml.value` still accept a
`ptr<yaml_node>` binding for existing code; that form is deprecated.

Nodes have one of these kinds:

| Constant | Meaning |
|---|---|
| `yaml.null` | null or absent node |
| `yaml.scalar` | one scalar value |
| `yaml.sequence` | ordered list of nodes |
| `yaml.mapping` | mapping of scalar keys to nodes |

`yaml.kind` returns these `i32` constants.  A null pointer is treated as an
absent node and has kind `yaml.null`.

## 3. Parsing

```yaml
- decl: [source, string, "name: Ada"]
- decl: [file_path, path, "config.yaml"]
- decl: [parsed_doc, ptr<yaml_doc>, { call: [yaml.parse, source] }]
- decl: [file_doc, ptr<yaml_doc>, { call: [yaml.parse_file, file_path] }]
- decl: [parse_error, string, { call: [yaml.error, parsed_doc] }]
- call: [yaml.free, parsed_doc]
- call: [yaml.free, file_doc]
```

`yaml.parse` parses the supplied UTF-8 string.  `yaml.parse_file` reads and
parses a UTF-8 file.  Both return a document handle even when parsing fails.
`yaml.error` returns an empty string on success; otherwise it returns a
human-readable parser or read error.  A failed document has no root node.

Parsing failure is data, not an implicit program termination.  AIML code decides
whether to report the error, continue, or call `sys.exit`.

`yaml.free` releases the document and invalidates its root and every node derived
from it.  It is safe to call once with a null pointer; using an invalidated
non-null node is a runtime error.

A `yaml_doc` is a unique owned resource (spec/61 §2.1 Class 4): the `ptr<yaml_doc>`
returned by `yaml.parse` / `yaml.parse_file` is released automatically when its owning
local goes out of scope (every exit, spec/61 §9.1).  `yaml.free` on a local is an early
release that nulls the local, so the scope release is then a no-op.  A `yaml_node` is a
non-owning view into its document and is never released on its own.

## 4. Root And Node Inspection

```yaml
- decl: [root, ref<yaml_node>, { call: [yaml.root, doc] }]
- decl: [kind, i32, { call: [yaml.kind, root] }]
- decl: [count, i32, { call: [yaml.size, root] }]
- decl: [text, string, { call: [yaml.scalar, root] }]
- decl: [line, i32, { call: [yaml.line, root] }]
- decl: [column, i32, { call: [yaml.column, root] }]
- decl: [normalized, string, { call: [yaml.dump, root] }]
```

`yaml.root` returns the document root, or null when the document did not parse.

`yaml.size` returns the number of entries for a sequence or mapping; for scalar,
null, and invalid nodes it returns `0`.

`yaml.scalar` returns the source scalar representation for a scalar node.  It
returns `""` for non-scalar, null, and invalid nodes.  It does not coerce values
to AIML numeric or boolean types; callers use the existing string conversion
functions when desired.

`yaml.line` and `yaml.column` return one-based source positions for the node.
They return `0` when no source position is available, including null and invalid
nodes.

`yaml.dump` returns normalized YAML for the selected subtree.  Its result is
semantically equivalent YAML, but it need not preserve comments, anchors,
aliases, whitespace, quote style, flow/block style, or key ordering beyond what
the underlying YAML document represents.  It returns `""` for null or invalid
nodes.

## 5. Sequence Access

```yaml
- decl: [child, ref<yaml_node>, { call: [yaml.item, node, index] }]
```

`yaml.item` returns the zero-based item of a sequence.  Negative or out-of-range
indexes, and calls on non-sequence nodes, return null.

```yaml
- decl: [doc, ptr<yaml_doc>, { call: [yaml.parse, "[one, two]"] }]
- decl: [root, ref<yaml_node>, { call: [yaml.root, doc] }]
- decl: [second, ref<yaml_node>, { call: [yaml.item, root, 1] }]
- print: [{ call: [yaml.scalar, second] }]  # two
- call: [yaml.free, doc]
```

## 6. Mapping Access

```yaml
- decl: [entry_key, string, { call: [yaml.key, node, index] }]
- decl: [entry_value, ref<yaml_node>, { call: [yaml.value, node, index] }]
- decl: [named_value, ref<yaml_node>, { call: [yaml.get, node, entry_key] }]
```

`yaml.key` and `yaml.value` access the same zero-based mapping entry.  Invalid
indexes and non-mapping nodes return `""` and null respectively.

`yaml.get` returns the value associated with an exact scalar key.  It returns
null when the node is not a mapping or the key is absent.  The namespace does
not coerce non-scalar YAML mapping keys; such keys are not addressable through
`yaml.get` in this version.

Mapping iteration is deterministic for one parsed document: `key(node, i)` and
`value(node, i)` remain paired and keep their order until `yaml.free(doc)`.

```yaml
- decl: [doc, ptr<yaml_doc>, { call: [yaml.parse, "name: Ada\nrank: 7"] }]
- decl: [root, ref<yaml_node>, { call: [yaml.root, doc] }]
- decl: [name_node, ref<yaml_node>, { call: [yaml.get, root, "name"] }]
- print: [{ call: [yaml.scalar, name_node] }]  # Ada
- call: [yaml.free, doc]
```

## 7. Lifetime And Aliasing

Every `yaml_node` belongs to exactly one `yaml_doc`.  Returned child nodes are
views, not copies.  Repeated calls that select the same YAML location may return
different pointer identities; programs must compare YAML content, not handle
addresses.

All node handles remain valid until their owning document is freed.  A program
must retain the document pointer whenever it retains one of its nodes.  No YAML
node is serializable, persistent, or transferable between processes.

## 8. Errors

The namespace does not throw language exceptions for ordinary absence:

- absent mapping key -> null node;
- invalid sequence or mapping index -> null node or empty key;
- node operation on the wrong kind -> null/default value;
- malformed YAML or unreadable file -> document with a non-empty `yaml.error`.

Use-after-free of a non-null document or node handle is a runtime error and must
produce a diagnostic that identifies the invalid YAML handle operation.

## 9. Non-Goals

- YAML writing or in-place modification.
- Comment, anchor, alias, or exact-style preservation.
- Automatic conversion between arbitrary YAML mappings and AIML models.
- YAMLPath/JSONPath or application-specific query syntax.
- YAML imports, schema validation, expression evaluation, or compilation.

Higher-level libraries may build traversal, querying, conversion, or validation
on these primitive document operations.

## 10. Status

Implemented (`src/sys/yaml_runtime.cpp`); tests `tests/test_yaml_documents.aiml` and
`tests/lifetime/163_yaml_doc_scope_release.aiml`.

## `yaml.tokens` — error-tolerant lexing for highlighting

Syntax highlighting cannot use `yaml.parse`: while text is being typed it is almost always
invalid, so the parser yields no document at all — and that is precisely the state in which
highlighting is most wanted.  A parser also discards **comments**, which have to be coloured.

```
yaml.tokens(text: string) → list<i32>      # 3 entries per token: start, len, kind
```

A flat `list<i32>` with triples rather than a list of models, because builtins return
`list<i32>`/`list<f32>`/`list<string>` and nothing else — the same shape as
`cascade.field_indices`.

### Token kinds

| `YamlTok` | Value | Covers |
|---|---|---|
| `text` | 0 | anything not classified below |
| `comment` | 1 | `#` to end of line |
| `key` | 2 | a scalar followed by `:` |
| `string` | 3 | `'…'` and `"…"`, including an unterminated one |
| `number` | 4 | integer and float scalars |
| `keyword` | 5 | `true`, `false`, `null`, `~`, `yes`, `no`, `on`, `off` |
| `punct` | 6 | `-`, `:`, `,`, `[`, `]`, `{`, `}`, `?` |
| `anchor` | 7 | `&name`, `*name` |
| `tag` | 8 | `!tag`, `!!tag` |
| `directive` | 9 | `%YAML`, `%TAG`, and `---` / `...` |

### Contract

- **Never fails.**  There is no error return and no exception: every input produces tokens.  An
  unterminated quote yields a `string` token to the end of the line and lexing continues on the
  next — a half-typed line is the normal case, not an error case.
- **Byte offsets**, matching the caret and selection offsets of `ui.text_area` and the prefab
  text widgets.  A caller never has to convert between two counting schemes.
- **Complete and non-overlapping.**  The triples cover the input in order; concatenating every
  `[start, start+len)` reproduces the input exactly.  Whitespace belongs to the preceding token's
  gap, not to a token, so a caller that colours only the spans leaves whitespace untouched.
- **Line-local.**  No token spans a `\n`, so a caller can colour one visible line without
  lexing the rest.
- **No semantics.**  `key` means "scalar followed by `:`", not "valid mapping key"; indentation is
  not interpreted.  Deciding what is structurally correct is the parser's job, and the parser is
  exactly what is unavailable while typing.

```yaml
- decl: [toks, list<i32>, { call: [yaml.tokens, src] }]
- decl: [idx, i32, 0]
- while:
    condition: { lt: [idx, toks.len] }
    body:
      - decl: [start, i32, { at: [toks, idx] }]
      - decl: [tlen,  i32, { at: [toks, { +: [idx, 1] }] }]
      - decl: [kind,  i32, { at: [toks, { +: [idx, 2] }] }]
      - set: [idx, { +: [idx, 3] }]
```

## 10. Availability in WASM

`yaml.tokens` is available in **both** targets, the rest of the namespace only **natively**.

The reason is not taste but a dependency: parsing, access and `dump` require
yaml-cpp, the lexer does not — it reads bytes.  `yaml.tokens` therefore lives in its own
translation unit (`src/sys/yaml_tokens_runtime.cpp`), which is built into the WASM runtime.

This was noticed because the cosmos UI does its pipeline highlighting with
`yaml.tokens` and the WASM client no longer linked with `undefined symbol: aiml_yaml_tokens`
(measured 2026-10-12) — the highlighting had only been tested natively.  Lesson: a builtin that
a **UI** uses needs a link test in the WASM target, not just a run.

