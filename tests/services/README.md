# spec/66 `- service:` — tests

Positive: `describe` (reflection §7), `rpc_dispatch` (§4.1), `rest` (§4.2, a real
HTTP round trip), `rest_auth` (§6: 401/403/200 via the Authorization header),
`errors` (§8: ServiceNotFound→404, ServiceInvalid→400), `cli` (§4.3), `client_stub` (§3),
`used_single_file` (counter-probe for §5.6).

Negative (`test_service_neg_*`, all `Expected exit code: 1`): duplicate endpoint name and
duplicate route (§5.1), path parameter without a field (§5.2), missing `roles:` (§5.3),
scalar argument (§5.4), unknown auth class and missing `token` field (§6),
unreferenced service (§5.6).

Helper files start with `_` and are skipped by `run_all_tests.sh`; the
expected exit codes are in the file header, and the runner reads them from there.
