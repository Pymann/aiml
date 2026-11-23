# Test-only TLS keys and certificates

**These private keys are public on purpose. Never use them for anything but the
test suite.** They exist so the TLS, mTLS and WSS tests run offline and
deterministically, without generating keys at test time.

| File | What it is |
| --- | --- |
| `server.crt`, `server.key` | self-signed server certificate, `CN=localhost`, RSA 2048, valid until 2036-02-28 |
| `ca.crt`, `ca.key` | test CA, `CN=AIML-Test-CA`, RSA 2048, valid until 2036-08-22 |
| `client.crt`, `client.key` | client certificate `CN=aiml-test-client`, signed by the test CA (for mTLS) |

The certificates carry no extensions (no subjectAltName, no key usage).

Used by, among others, `tests/test_http_mtls.aiml`, `tests/test_mirror_wss.aiml`,
`tests/test_http_client_proxy.aiml` and `tests/test_http_tls_handshake_stall.aiml`.

## Regenerating

```bash
cd tests/tls_certs
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=localhost" \
    -keyout server.key -out server.crt
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=AIML-Test-CA" \
    -keyout ca.key -out ca.crt
openssl req -newkey rsa:2048 -nodes -subj "/CN=aiml-test-client" \
    -keyout client.key -out client.csr
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -days 3650 -out client.crt
rm client.csr ca.srl
```

Secret scanners will flag the `.key` files; that is expected for this directory.
The SAML fixtures in `tests/_saml/` follow the same rule and have their own
generator, `gen.sh`.
