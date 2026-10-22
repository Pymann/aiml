# spec/66 `- service:` — Tests

Positiv: `describe` (Reflexion §7), `rpc_dispatch` (§4.1), `rest` (§4.2, echter
HTTP-Round-Trip), `rest_auth` (§6: 401/403/200 ueber den Authorization-Header),
`errors` (§8: ServiceNotFound→404, ServiceInvalid→400), `cli` (§4.3), `client_stub` (§3),
`used_single_file` (Gegenprobe zu §5.6).

Negativ (`test_service_neg_*`, alle `Expected exit code: 1`): doppelter Endpunktname und
doppelte Route (§5.1), Pfadparameter ohne Feld (§5.2), fehlende `roles:` (§5.3),
skalares Argument (§5.4), unbekannte Auth-Klasse und fehlendes `token`-Feld (§6),
unreferenzierter Service (§5.6).

Hilfsdateien beginnen mit `_` und werden von `run_all_tests.sh` uebersprungen; die
erwarteten Exit-Codes stehen im Dateikopf, der Runner liest sie von dort.
