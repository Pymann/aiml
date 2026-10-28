#!/usr/bin/env bash
# gen.sh — regenerate the SAML test fixtures for tests/test_saml.aiml (spec/67 §4).
#
# Needs openssl, xmlsec1 and python3 at GENERATION time only; the generated files
# are committed so the test itself needs none of them.
#   bash tests/_saml/gen.sh
set -euo pipefail
cd "$(dirname "$0")"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

openssl req -x509 -newkey rsa:2048 -nodes -days 36500 -subj "/CN=test-idp" \
    -keyout idp_key.pem -out idp_cert.pem 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -days 36500 -subj "/CN=other-idp" \
    -keyout "$TMP/other_key.pem" -out other_cert.pem 2>/dev/null

python3 - "$TMP" <<'PY'
import sys, os
tmp = sys.argv[1]
def sig(ref, alg="rsa-sha256", dig="sha256"):
    sm = {"rsa-sha256": "http://www.w3.org/2001/04/xmldsig-more#rsa-sha256",
          "rsa-sha1": "http://www.w3.org/2000/09/xmldsig#rsa-sha1"}[alg]
    dm = {"sha256": "http://www.w3.org/2001/04/xmlenc#sha256",
          "sha1": "http://www.w3.org/2000/09/xmldsig#sha1"}[dig]
    return ('<ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#"><ds:SignedInfo>'
            '<ds:CanonicalizationMethod Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/>'
            f'<ds:SignatureMethod Algorithm="{sm}"/>'
            f'<ds:Reference URI="#{ref}"><ds:Transforms>'
            '<ds:Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/>'
            '<ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/></ds:Transforms>'
            f'<ds:DigestMethod Algorithm="{dm}"/><ds:DigestValue></ds:DigestValue></ds:Reference>'
            '</ds:SignedInfo><ds:SignatureValue></ds:SignatureValue>'
            '<ds:KeyInfo><ds:X509Data/></ds:KeyInfo></ds:Signature>')

def response(resp_sig="", ass_sig="", name_id="alice@example.org", expiry="2099-01-01T00:00:00Z",
             extra_before="", resp_id="_resp1", ass_id="_assert1"):
    return ('<samlp:Response xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol" '
            'xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" '
            f'ID="{resp_id}" Version="2.0" IssueInstant="2026-10-27T20:00:00Z" '
            'Destination="https://sp.example/acs" InResponseTo="_req123">'
            '<saml:Issuer>https://idp.example</saml:Issuer>' + resp_sig +
            '<samlp:Status><samlp:StatusCode Value="urn:oasis:names:tc:SAML:2.0:status:Success"/></samlp:Status>'
            + extra_before +
            f'<saml:Assertion ID="{ass_id}" Version="2.0" IssueInstant="2026-10-27T20:00:00Z">'
            '<saml:Issuer>https://idp.example</saml:Issuer>' + ass_sig +
            '<saml:Subject><saml:NameID Format="urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress">'
            + name_id + '</saml:NameID>'
            '<saml:SubjectConfirmation Method="urn:oasis:names:tc:SAML:2.0:cm:bearer">'
            f'<saml:SubjectConfirmationData InResponseTo="_req123" NotOnOrAfter="{expiry}" '
            'Recipient="https://sp.example/acs"/></saml:SubjectConfirmation></saml:Subject>'
            f'<saml:Conditions NotBefore="2020-01-01T00:00:00Z" NotOnOrAfter="{expiry}">'
            '<saml:AudienceRestriction><saml:Audience>https://sp.example/metadata</saml:Audience>'
            '</saml:AudienceRestriction></saml:Conditions>'
            '<saml:AuthnStatement AuthnInstant="2026-10-27T20:00:00Z" SessionIndex="_sess42">'
            '<saml:AuthnContext><saml:AuthnContextClassRef>urn:oasis:names:tc:SAML:2.0:ac:classes:Password'
            '</saml:AuthnContextClassRef></saml:AuthnContext></saml:AuthnStatement>'
            '<saml:AttributeStatement>'
            '<saml:Attribute Name="groups"><saml:AttributeValue>devs</saml:AttributeValue>'
            '<saml:AttributeValue>ops</saml:AttributeValue></saml:Attribute>'
            '<saml:Attribute Name="displayName"><saml:AttributeValue>Alice Example</saml:AttributeValue></saml:Attribute>'
            '</saml:AttributeStatement></saml:Assertion></samlp:Response>')

evil = ('<saml:Assertion ID="_evil" Version="2.0" IssueInstant="2026-10-27T20:00:00Z">'
        '<saml:Issuer>https://idp.example</saml:Issuer><saml:Subject>'
        '<saml:NameID>admin@example.org</saml:NameID></saml:Subject></saml:Assertion>')

T = {
  "assertion_signed": response(ass_sig=sig("_assert1")),
  "response_signed":  response(resp_sig=sig("_resp1")),
  "both_signed":      response(resp_sig=sig("_resp1"), ass_sig=sig("_assert1")),
  "expired":          response(ass_sig=sig("_assert1"), expiry="2001-01-01T00:00:00Z"),
  "comment_nameid":   response(ass_sig=sig("_assert1"), name_id="alice@example.org<!---->.evil.example"),
  "sha1":             response(ass_sig=sig("_assert1", "rsa-sha1", "sha1")),
  "foreign_ref":      response(ass_sig=sig("_resp1")),
}
for k, v in T.items():
    open(os.path.join(tmp, k + ".tmpl.xml"), "w").write(v)
# unsigned / structural cases need no signing
open(os.path.join(tmp, "unsigned.xml"), "w").write(response())
open(os.path.join(tmp, "duplicate_id.xml"), "w").write(response(ass_id="_resp1"))
open(os.path.join(tmp, "encrypted.xml"), "w").write(
    response().replace("<saml:Assertion ", '<saml:EncryptedAssertion><xenc:EncryptedData xmlns:xenc="http://www.w3.org/2001/04/xmlenc#"/></saml:EncryptedAssertion><saml:Assertion '))
open(os.path.join(tmp, "dtd.xml"), "w").write(
    '<?xml version="1.0"?><!DOCTYPE r [<!ENTITY x "alice@example.org">]>' + response(name_id="&x;"))
open(os.path.join(tmp, "evil.txt"), "w").write(evil)
PY

sign() {  # sign <in> <out> [<xpath-of-signature>]
    local args=(--sign --privkey-pem idp_key.pem,idp_cert.pem
                --id-attr:ID urn:oasis:names:tc:SAML:2.0:protocol:Response
                --id-attr:ID urn:oasis:names:tc:SAML:2.0:assertion:Assertion)
    [ $# -ge 3 ] && args+=(--node-xpath "$3")
    xmlsec1 "${args[@]}" --output "$2" "$1" 2>/dev/null
}
ASSIG="//*[local-name()='Assertion']/*[local-name()='Signature']"
RESSIG="/*[local-name()='Response']/*[local-name()='Signature']"

sign "$TMP/assertion_signed.tmpl.xml" "$TMP/assertion_signed.xml"
sign "$TMP/response_signed.tmpl.xml"  "$TMP/response_signed.xml"
sign "$TMP/both_signed.tmpl.xml" "$TMP/both_a.xml" "$ASSIG"
sign "$TMP/both_a.xml" "$TMP/both_signed.xml" "$RESSIG"
sign "$TMP/expired.tmpl.xml" "$TMP/expired.xml"
sign "$TMP/comment_nameid.tmpl.xml" "$TMP/comment_nameid.xml"
sign "$TMP/sha1.tmpl.xml" "$TMP/sha1.xml"
sign "$TMP/foreign_ref.tmpl.xml" "$TMP/foreign_ref.xml"

# tampered: NameID changed AFTER signing
sed 's/alice@example.org</mallory@example.org</' "$TMP/assertion_signed.xml" > "$TMP/tampered.xml"
# wrapping: an extra unsigned Assertion in front of the signed one
python3 - "$TMP" <<'PY'
import sys, os
t = sys.argv[1]
s = open(os.path.join(t, "assertion_signed.xml")).read()
evil = open(os.path.join(t, "evil.txt")).read()
i = s.index("<saml:Assertion ")
open(os.path.join(t, "wrapping.xml"), "w").write(s[:i] + evil + s[i:])
PY

for f in assertion_signed response_signed both_signed expired comment_nameid sha1 foreign_ref \
         tampered wrapping unsigned duplicate_id encrypted dtd; do
    base64 -w 76 "$TMP/$f.xml" > "$f.b64"
done
rm -f idp_key.pem        # the private key is not needed after signing
echo "fixtures written to $(pwd)"
