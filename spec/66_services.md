# 66 — Services (`- service:`): one declaration, every transport

Status: **Design. Not implemented.**  Staged plan in §10; tests precede the compiler work.

## 1. Motivation

A server endpoint exists three times today: as an RPC handler registered with the
dispatcher (spec/44_5), as an HTTP route with request/response handles (spec/32_3), and as
a client call that has to spell the same method name and model types again.  Nothing ties
the three together: a renamed field, a forgotten role check or a route that no client calls
are all silent.  The 9rooms server registers 79 RPCs by hand, and its client re-declares
every one of them.

A **service declaration** states an endpoint once — name, request model, response model,
HTTP verb and path, required roles — and the compiler synthesizes the transports from it,
the same way `- classgroup:` synthesizes instances (spec/58) and `- cascade_class:`
synthesizes a dispatch (spec/57).  The implementation body is written once and reached
through every transport.

## 2. Declaration

```yaml
- service:
    name: LioApi
    base: "/api/v1"                  # path prefix for the HTTP transport
    transport: [rpc, rest]           # which transports to synthesize
    auth: TokenAuth                  # a class implementing the §6 contract; `none` to disable
    endpoints:
      - def:
          name: resolve_component     # RPC method name, CLI subcommand, operation id
          http: [post, "/components/resolve"]
          roles: [viewer]
          args: [[req, ResolveReq]]
          return: ResolveResp
          body:
            - decl: [out, ResolveResp]
            - set: [out.spdx, "Apache-2.0"]
            - return: out

      - def:
          name: approve_resolution
          http: [post, "/releases/:release_id/resolution"]
          roles: [approver]
          args: [[req, ApproveReq]]   # `release_id` must be a field of ApproveReq (§5.2)
          return: ApproveResp
          body: [ ... ]
```

`endpoints` holds ordinary `def` nodes with three extra keys:

| Key | Required | Meaning |
|---|---|---|
| `http` | for the `rest` transport | `[verb, path]`; the path is appended to `base`, `:name` marks a path parameter |
| `roles` | unless `auth: none` | role names the caller must hold; `[]` means "authenticated, any role" |
| `cli` | no | `[subcommand, …]` for the optional CLI transport (§4.3) |

An endpoint takes **exactly one** argument, and both it and the return type must be models
(spec/15).  That is what makes one body reachable from three transports: the models carry
the wire format, and the compiler already emits their type descriptors.

## 3. What the compiler synthesizes

Deterministic names, because they appear in diagnostics and stack traces:

| Synthesized | Name | Purpose |
|---|---|---|
| Endpoint body | `<Service>_<endpoint>` | the `def` as written, unchanged |
| RPC handler | `<Service>_<endpoint>_rpc` | decode payload → auth → body → `reply_rpc(ev, "set_result", 0, resp)` |
| HTTP handler | `<Service>_<endpoint>_http` | parse body/params → auth → body → JSON response + status |
| Server facade | class `<Service>` | `dispatch(session, ev) -> i32`, `serve_rest(server)` |
| Client stub | class `<Service>Client` | one method per endpoint over `send_and_await` |
| Description | `<Service>__describe` | the §7 reflection table |

Nothing else changes: the transport stays the one from spec/44_5 and spec/32_3.  The project
keeps its event loop and calls `dispatch(session, ev)` for each polled `rpc_event` — the
facade answers `1` when it handled the method and `0` when it did not, so a server can mix
generated endpoints with hand-written ones.  `serve_rest(server)` registers the HTTP routes
on an `http_server_handle` the project created, so listening, TLS and middleware order stay
in the project's hands.

## 4. Transports

### 4.1 RPC (spec/44_5)

`dispatch` switches on `ev.method`.  The matching handler decodes the typed payload
(`arg_kind == 1`) into the argument model, calls the body and answers with
`reply_rpc(ev, "set_result", 0, resp)`.  A failure answers with `"set_error"` carrying a
`ServiceError` (§8).  An unknown method leaves the event untouched and returns `0`; an
event whose payload does not decode into the declared model is answered `400`, never
dropped silently.

### 4.2 REST (spec/32_3)

`verb base+path` is registered with `server.route`.  The request model is filled from, in
this order: the JSON body, then the path parameters (`req.params`), then the query
parameters — a later source never overwrites an earlier one, and a field that no source
supplies keeps its model default.  The response model is serialized as JSON with
`Content-Type: application/json`; the status is `200`, or the mapping of §8.

### 4.3 CLI (optional)

With `cli:` the compiler also emits a subcommand, and the facade gets

```yaml
- call: [api.run_cli, args]     # args: list<string>, [0] = subcommand
```

`run_cli` returns `0` when it ran a subcommand and `1` when the first token matched none —
same contract as `dispatch`, so a program can mix generated subcommands with its own.  The
request model is filled from `--field value` flags with the `sys.arg_dump` naming of spec/23
(`--foo-bar` → `foo_bar`); `string` fields take the token, `i32`/`i64` parse it, a flag
without a value sets `1`.  The response is written to `io.stdout` as JSON.

The argument **list is passed in**, not read from the process: a JIT run has no `argv` at all
(spec/23), and a program that owns its command line may hand over a sub-list.  This is what
lets a scanner call its own service locally without a server.

### 4.4 Target availability

| Target | rpc | rest | client stubs |
|---|---|---|---|
| native | yes | yes | yes |
| wasm | client only | no (no HTTP server in the browser) | yes |

Declaring `transport: [rest]` in a wasm build is a compile error that names the target.

## 5. Static checks

The value of putting this in the compiler is what it can refuse:

1. **Duplicate endpoint name** within a service, or duplicate `verb + path` across all
   services of the build.
2. **Path parameter without a field**: `/releases/:release_id` requires a `release_id`
   field in the request model, of a type a path segment can carry (`string`, integer).
3. **Missing roles**: an endpoint without `roles` while the service has `auth` set is an
   error.  There is no implicit "open" endpoint — `roles: []` or `auth: none` has to be
   written, so an open endpoint is visible in review.
4. **Non-model argument or return**, or a model that is not serializable.
5. **Unknown auth class**, or one that does not satisfy §6 — including a request model
   without the `token: string` field the gate reads (§6).
6. **Unreferenced service**: a declared service that nothing in the build touches —
   no `dispatch`/`serve_rest`, no client stub, no endpoint body, no `__describe`, and
   no instance of the facade.  The endpoints would be dead code.  The check asks
   "is anything of this service used", not "is it served", because that is the same
   question before the transports exist (§10 M3/M4) and after.
7. **Transport not available for the target** (§4.4).

## 6. Authentication contract

The language does not define users.  A service names a class that answers two questions:

```yaml
- class:
    name: TokenAuth
    methods:
      - def: { name: principal_of, args: [[token, string]], return: Principal }
      - def: { name: has_role, args: [[who, Principal], [role, string]], return: i32 }
```

`Principal` is any model; the compiler only passes it through.  The generated handlers
call `principal_of`, then `has_role` for each entry of `roles`, and reject with `401` /
`403` before the body runs.

**Where the token comes from** has to be nameable by the compiler, so it is fixed per
transport:

| Transport | Token source |
|---|---|
| RPC | the request model's `token: string` field |
| REST | the `Authorization: Bearer <token>` header, falling back to the same field |

The RPC rule is a field and not the session, because `peer_id` → user is the project's own
mapping and the compiler cannot see it; a field is declared, reviewable and already how
real services carry it.  A guarded service whose request model has no `token: string` is a
compile error (§5.5) — the alternative is a gate that silently never runs.

**Who owns the gate**: the auth class holds project state (a token store, a database
handle), so the compiler never constructs it.  A guarded `<Service>` gets a member `auth`
of that class and a method `set_auth(gate)`; the project builds the object and hands it in
before it serves.  A guarded facade whose `auth` was never set answers every call `401`
instead of running the body — an unset gate must not read as an open door.  A project that wants
per-object permissions does that inside the body — the declaration covers the gate, not
the policy.

## 7. Reflection

`<Service>__describe` returns a list of `ServiceEndpoint` models (name, verb, path, roles,
request type, response type).  Together with the model descriptors that already exist for
serialization, that is everything an external generator needs to write OpenAPI or a client
for another language — the compiler stays out of the document-format business.

## 8. Errors

| Body does | RPC | REST |
|---|---|---|
| returns normally | `set_result` + response model | `200` + JSON |
| throws `ServiceNotFound` | `set_error`, code `404` | `404` |
| throws `ServiceInvalid` | `set_error`, code `400` | `400` |
| auth missing/!role | `set_error`, code `401`/`403` | `401`/`403` |
| any other exception | `set_error`, code `500`, message logged not leaked | `500` |

`ServiceError` is a model (`code: i32`, `message: string`, `detail: string`), so both
transports carry the same shape.

The two exception types are synthesized with the service, next to `ServiceError`, and
they extend `Exception`.  They carry the `Service` prefix on purpose: the compiler has to
name them in the generated `catch`, and a bare `NotFound` would take a very ordinary name
away from every project that holds one of its own.  Mapping an exception to `500` requires
it to extend `Exception` — a thrown type outside that tree is not caught here and ends the
program as any uncaught exception does (spec/22).

## 9. Non-goals

Routing beyond `verb + path + :param`, content negotiation, pagination conventions, rate
limiting, OpenAPI emission (§7 hands that to a tool), websocket streaming endpoints,
and user management itself (§6).

## 10. Staged plan

**Status 2026-10-18:** M1–M4 are implemented and covered by `tests/services/` (16 tests,
8 of them negative, with real HTTP round trips).  Open: §4.4 (reject REST in the wasm target —
only checkable with a wasm build, therefore not built yet) and M5.

- **M1 — tests first.** Corpus tests that declare a service and assert the synthesized
  names exist, that a request round-trips over RPC, and one negative test per check of §5.
  The negatives come first; they are what the feature is for.
- **M2 — AST + parser.** `- service:` node, endpoint nodes with `http`/`roles`/`cli`,
  registration in the context, no codegen yet; the static checks of §5.1–§5.5 run here.
- **M3 — codegen RPC.** Synthesize `<Service>_<endpoint>_rpc`, the facade with `dispatch`,
  and the client stubs; a service with `transport: [rpc]` works end to end.
- **M4 — codegen REST + CLI.** `serve_rest`, parameter filling per §4.2, the error mapping
  of §8, then the `cli` transport.
- **M5 — reflection + adoption.** `__describe`, then migrate one real server (9rooms' auth
  handlers) and compare the generated dispatch against the hand-written one.
