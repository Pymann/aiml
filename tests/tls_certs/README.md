# Test-only TLS keys and certificates

**These private keys are public on purpose. Never use them for anything but the
test suite.** They exist so the TLS, mTLS and WSS tests run offline and
deterministically, without generating keys at test time.

| File | What it is |
| --- | --- |
| `server.crt`, `server.key` | self-signed server certificate, `CN=localhost`, RSA 2048, valid until 2036-02-28 |
| `ca.crt`, `ca.key` | test CA, `CN=AIML-Test-CA`, RSA 2048, valid until 2036-08-22 |
| `client.crt`, `client.key` | client certificate `CN=aiml-test-client`, signed by the test CA (for mTLS) |
| `server_ip.crt`, `server_ip.key` | server cert signed by the test CA, `subjectAltName = DNS:localhost, IP:127.0.0.1` — the **verified** client paths (no `tls_insecure`) need a SAN, and an IP literal in a URL is matched against the `IP:` entry |
| `server_dns.crt`, `server_dns.key` | the same, but `subjectAltName = DNS:localhost` only — the counter-example: reaching it by IP must be refused |

`server.crt`, `ca.crt` and `client.crt` carry no extensions (no subjectAltName, no
key usage); the two SAN certs above do, because that is what they are for.

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

# the two SAN certs (signed by the test CA)
for kind in ip dns; do
    san="subjectAltName=DNS:localhost,IP:127.0.0.1"
    [ "$kind" = dns ] && san="subjectAltName=DNS:localhost"
    openssl req -newkey rsa:2048 -nodes -subj "/CN=aiml-test-$kind-server" \
        -keyout server_$kind.key -out server_$kind.csr
    openssl x509 -req -in server_$kind.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
        -days 3650 -out server_$kind.crt -extfile <(printf '%s\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n' "$san")
    rm server_$kind.csr
done
rm -f ca.srl
```

Secret scanners will flag the `.key` files; that is expected for this directory.
The SAML fixtures in `tests/_saml/` follow the same rule and have their own
generator, `gen.sh`.
