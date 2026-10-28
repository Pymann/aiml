// SAML 2.0, service-provider side (spec/67_identity.md §4).
//
//   saml.authn_request_url  — SP-initiated login, HTTP-Redirect binding
//   saml.verify_response    — HTTP-POST binding: verify and read a <Response>
//
// verify_response is the security boundary of SSO, so its rules are fixed
// here and not left to callers:
//   * Only the CONFIGURED IdP certificate is used to verify.  <KeyInfo> in the
//     document is never trusted (dsigCtx->signKey is set; xmlsec then ignores it).
//   * Signature wrapping: the document must contain exactly ONE <Assertion>, a
//     direct child of <Response>; every ID must be unique; the verified
//     signature must be enveloped in the element its single Reference names
//     ("#"+ID of the parent).  Values are read ONLY from the signed element
//     (the Response, which then covers its one Assertion, or the Assertion).
//   * No DTD, no entity references, no comments, no processing instructions:
//     comments in particular are dropped by exclusive C14N, so "a<!---->b"
//     in a NameID would verify while naive text extraction read "a".
//   * Reference transforms are limited to enveloped-signature + (exclusive)
//     C14N without comments; digests and signatures to SHA-256/384/512 (RSA,
//     ECDSA).  SHA-1 and XSLT are rejected.
//   * Encrypted assertions are not supported and rejected explicitly.
#include "identity_runtime.h"

#define XMLSEC_NO_SIZE_T 1
#define XMLSEC_CRYPTO_OPENSSL 1
#define XMLSEC_NO_CRYPTO_DYNAMIC_LOADING 1
#define XMLSEC_OPENSSL3_ENGINES 1
#define XMLSEC_NO_FTP 1
#define XMLSEC_NO_MD5 1
#define XMLSEC_NO_GOST 1
#define XMLSEC_NO_GOST2012 1
#ifndef __XMLSEC_FUNCTION__
#define __XMLSEC_FUNCTION__ __func__
#endif

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <xmlsec/xmlsec.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/xmldsig.h>
#include <xmlsec/crypto.h>
#include <xmlsec/errors.h>

#include <openssl/rand.h>
#include <zlib.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

extern "C" char* aiml_str_alloc(size_t len);
extern "C" char* aiml_str_dup(const char* s);
extern "C" void* aiml_map_handle_new(void);

namespace {

const char* NS_P  = "urn:oasis:names:tc:SAML:2.0:protocol";
const char* NS_A  = "urn:oasis:names:tc:SAML:2.0:assertion";
const char* NS_DS = "http://www.w3.org/2000/09/xmldsig#";

std::atomic<int> g_skew_seconds{120};
std::once_flag   g_init_once;
bool             g_init_ok = false;

void silent_xmlsec_errors(const char*, int, const char*, const char*, const char*, int, const char*) {}
void silent_xml_errors(void*, const char*, ...) {}

void init_once() {
    std::call_once(g_init_once, [] {
        xmlInitParser();
        if (xmlSecInit() < 0) return;
        if (xmlSecCheckVersion() != 1) return;
        if (xmlSecCryptoAppInit(nullptr) < 0) return;
        if (xmlSecCryptoInit() < 0) return;
        xmlSecErrorsSetCallback(silent_xmlsec_errors);
        g_init_ok = true;
    });
}

// ── small helpers ────────────────────────────────────────────────────────────
struct Result {
    std::vector<std::pair<std::string, std::string>> kv;
    void put(const std::string& k, const std::string& v) {
        for (auto& e : kv) if (e.first == k) { e.second = v; return; }
        kv.emplace_back(k, v);
    }
};

// memV3 map<string:string> handle, dense store of managed strings (same
// representation as sys.env, mem_runtime.cpp).
struct MapRaw { int32_t size; int32_t capacity; int8_t* occupied; char** keys; char** values; };

void* to_handle(const Result& r) {
    void* handle = aiml_map_handle_new();
    if (!handle) return handle;
    MapRaw* m = static_cast<MapRaw*>(handle);
    int32_t n = static_cast<int32_t>(r.kv.size());
    if (n == 0) return handle;
    m->keys = static_cast<char**>(calloc(size_t(n), sizeof(char*)));
    m->values = static_cast<char**>(calloc(size_t(n), sizeof(char*)));
    m->occupied = nullptr;
    m->capacity = n;
    if (!m->keys || !m->values) { m->size = 0; return handle; }
    for (int32_t i = 0; i < n; ++i) {
        m->keys[i] = aiml_str_dup(r.kv[size_t(i)].first.c_str());
        m->values[i] = aiml_str_dup(r.kv[size_t(i)].second.c_str());
    }
    m->size = n;
    return handle;
}

void* fail(const std::string& why) {
    Result r;
    r.put("ok", "0");
    r.put("error", why);
    return to_handle(r);
}

bool is_el(xmlNodePtr n, const char* ns, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && n->ns && n->ns->href &&
           strcmp(reinterpret_cast<const char*>(n->ns->href), ns) == 0 &&
           strcmp(reinterpret_cast<const char*>(n->name), name) == 0;
}

xmlNodePtr child(xmlNodePtr parent, const char* ns, const char* name) {
    if (!parent) return nullptr;
    for (xmlNodePtr c = parent->children; c; c = c->next)
        if (is_el(c, ns, name)) return c;
    return nullptr;
}

std::string attr(xmlNodePtr n, const char* name) {
    if (!n) return "";
    xmlChar* v = xmlGetNoNsProp(n, reinterpret_cast<const xmlChar*>(name));
    if (!v) return "";
    std::string s(reinterpret_cast<const char*>(v));
    xmlFree(v);
    return s;
}

bool has_attr(xmlNodePtr n, const char* name) {
    return n && xmlHasNsProp(n, reinterpret_cast<const xmlChar*>(name), nullptr) != nullptr;
}

// Text of an element: ALL text children concatenated (comments are rejected
// before we get here, so nothing can hide between text nodes).
std::string text(xmlNodePtr n) {
    if (!n) return "";
    xmlChar* v = xmlNodeGetContent(n);
    if (!v) return "";
    std::string s(reinterpret_cast<const char*>(v));
    xmlFree(v);
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

int count_assertions(xmlNodePtr n) {
    int c = 0;
    for (; n; n = n->next) {
        if (is_el(n, NS_A, "Assertion")) ++c;
        if (n->type == XML_ELEMENT_NODE) c += count_assertions(n->children);
    }
    return c;
}

bool contains_el(xmlNodePtr n, const char* ns, const char* name) {
    for (; n; n = n->next) {
        if (is_el(n, ns, name)) return true;
        if (n->type == XML_ELEMENT_NODE && contains_el(n->children, ns, name)) return true;
    }
    return false;
}

// Rejects comment / PI / entity nodes anywhere; collects ID attributes and
// rejects duplicates.
bool scan_tree(xmlNodePtr n, std::set<std::string>& ids, std::string& why) {
    for (; n; n = n->next) {
        switch (n->type) {
            case XML_COMMENT_NODE: why = "comments are not allowed in a SAML response"; return false;
            case XML_PI_NODE: why = "processing instructions are not allowed"; return false;
            case XML_ENTITY_REF_NODE: case XML_ENTITY_NODE: case XML_ENTITY_DECL:
                why = "entities are not allowed"; return false;
            case XML_DTD_NODE: why = "a DTD is not allowed"; return false;
            default: break;
        }
        if (n->type == XML_ELEMENT_NODE) {
            if (has_attr(n, "ID")) {
                std::string id = attr(n, "ID");
                if (!ids.insert(id).second) { why = "duplicate ID attribute '" + id + "'"; return false; }
            }
            if (!scan_tree(n->children, ids, why)) return false;
        }
    }
    return true;
}

// xs:dateTime → epoch seconds.  "YYYY-MM-DDThh:mm:ss[.fff][Z|±hh:mm]"; -1 on error.
long long parse_time(const std::string& s) {
    int Y, M, D, h, m, sec;
    if (s.size() < 19 || sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6)
        return -1;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
    t.tm_hour = h; t.tm_min = m; t.tm_sec = sec;
    long long epoch = static_cast<long long>(timegm(&t));
    size_t p = 19;
    if (p < s.size() && s[p] == '.') { ++p; while (p < s.size() && isdigit(static_cast<unsigned char>(s[p]))) ++p; }
    if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
        int oh = 0, om = 0;
        if (sscanf(s.c_str() + p + 1, "%2d:%2d", &oh, &om) != 2) return -1;
        long long off = (oh * 3600LL + om * 60LL) * (s[p] == '+' ? 1 : -1);
        epoch -= off;
    } else if (p < s.size() && s[p] != 'Z') {
        return -1;
    }
    return epoch;
}

std::string b64_decode_lenient(const char* in) {
    static int8_t rev[256];
    static std::once_flag once;
    std::call_once(once, [] {
        memset(rev, -1, sizeof(rev));
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) rev[static_cast<unsigned char>(a[i])] = static_cast<int8_t>(i);
    });
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
        if (*p == '=' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') continue;
        int v = rev[*p];
        if (v < 0) return std::string();
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += static_cast<char>((acc >> bits) & 0xFF); }
    }
    return out;
}

std::string b64_encode(const unsigned char* p, size_t n) {
    const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
    }
    if (i < n) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < n) v |= uint32_t(p[i + 1]) << 8;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string url_encode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string xml_escape(const char* s) {
    std::string out;
    for (; s && *s; ++s) {
        switch (*s) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += *s;
        }
    }
    return out;
}

std::string random_id() {
    unsigned char raw[20];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        for (auto& b : raw) b = static_cast<unsigned char>(rand());
    }
    static const char hex[] = "0123456789abcdef";
    std::string id = "_";
    for (unsigned char b : raw) { id += hex[b >> 4]; id += hex[b & 15]; }
    return id;
}

std::string now_iso() {
    time_t t = time(nullptr);
    struct tm g;
    gmtime_r(&t, &g);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

// Verifies the <ds:Signature> that is a DIRECT child of `signed_el`.
// Returns "" on success, else the reason.
std::string verify_enveloped(xmlDocPtr doc, xmlNodePtr signed_el, xmlNodePtr sig, const char* cert_pem) {
    std::string id = attr(signed_el, "ID");
    if (id.empty()) return "signed element has no ID";

    xmlSecKeyPtr key = xmlSecCryptoAppKeyLoadMemory(
        reinterpret_cast<const xmlSecByte*>(cert_pem), static_cast<xmlSecSize>(strlen(cert_pem)),
        xmlSecKeyDataFormatCertPem, nullptr, nullptr, nullptr);
    if (!key) return "IdP certificate could not be loaded (PEM X.509 expected)";

    xmlSecDSigCtxPtr ctx = xmlSecDSigCtxCreate(nullptr);
    if (!ctx) { xmlSecKeyDestroy(key); return "xmlsec context failed"; }
    ctx->signKey = key;                                     // owned by ctx from here
    ctx->enabledReferenceUris = xmlSecTransformUriTypeSameDocument;

    // Allowed transforms (anything else fails verification).
    xmlSecTransformId refT[] = { xmlSecTransformEnvelopedId, xmlSecTransformExclC14NId,
                                 xmlSecTransformInclC14NId, xmlSecTransformSha256Id,
                                 xmlSecTransformSha384Id, xmlSecTransformSha512Id };
    for (auto t : refT) xmlSecDSigCtxEnableReferenceTransform(ctx, t);
    xmlSecTransformId sigT[] = { xmlSecTransformExclC14NId, xmlSecTransformInclC14NId,
                                 xmlSecTransformRsaSha256Id, xmlSecTransformRsaSha384Id,
                                 xmlSecTransformRsaSha512Id, xmlSecTransformEcdsaSha256Id,
                                 xmlSecTransformEcdsaSha384Id, xmlSecTransformEcdsaSha512Id };
    for (auto t : sigT) xmlSecDSigCtxEnableSignatureTransform(ctx, t);

    std::string why;
    if (xmlSecDSigCtxVerify(ctx, sig) < 0) {
        why = "signature could not be processed (unsupported algorithm or malformed)";
    } else if (ctx->status != xmlSecDSigStatusSucceeded) {
        why = "signature is invalid";
    } else if (xmlSecPtrListGetSize(&ctx->signedInfoReferences) != 1) {
        why = "signature must have exactly one reference";
    } else {
        xmlSecDSigReferenceCtxPtr ref = static_cast<xmlSecDSigReferenceCtxPtr>(
            xmlSecPtrListGetItem(&ctx->signedInfoReferences, 0));
        std::string uri = (ref && ref->uri) ? reinterpret_cast<const char*>(ref->uri) : "";
        if (!ref || ref->status != xmlSecDSigStatusSucceeded) why = "reference digest mismatch";
        else if (uri != "#" + id) why = "signature does not reference its enclosing element";
        else {
            xmlAttrPtr idAttr = xmlGetID(doc, reinterpret_cast<const xmlChar*>(id.c_str()));
            if (!idAttr || idAttr->parent != signed_el) why = "signed ID resolves to a different element";
        }
    }
    xmlSecDSigCtxDestroy(ctx);
    (void)doc;
    return why;
}

}  // namespace

extern "C" void aiml_saml_set_clock_skew(int32_t seconds) {
    if (seconds >= 0 && seconds <= 3600) g_skew_seconds = seconds;
}

extern "C" char* aiml_saml_new_request_id(void) {
    return aiml_str_dup(random_id().c_str());
}

extern "C" char* aiml_saml_authn_request_url(const char* idp_sso_url, const char* sp_entity_id,
                                             const char* acs_url, const char* request_id,
                                             const char* relay_state) {
    if (!idp_sso_url || !*idp_sso_url) return aiml_str_alloc(0);
    std::string id = (request_id && *request_id) ? request_id : random_id();
    std::string xml =
        "<samlp:AuthnRequest xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\" "
        "xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\" ID=\"" + xml_escape(id.c_str()) +
        "\" Version=\"2.0\" IssueInstant=\"" + now_iso() + "\" Destination=\"" + xml_escape(idp_sso_url) +
        "\" AssertionConsumerServiceURL=\"" + xml_escape(acs_url) +
        "\" ProtocolBinding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\">"
        "<saml:Issuer>" + xml_escape(sp_entity_id) + "</saml:Issuer>"
        "<samlp:NameIDPolicy AllowCreate=\"true\"/></samlp:AuthnRequest>";

    // HTTP-Redirect binding: raw DEFLATE (no zlib header), base64, URL-encode.
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return aiml_str_alloc(0);
    std::vector<unsigned char> out(xml.size() + 64);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(xml.data()));
    zs.avail_in = static_cast<uInt>(xml.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    int rc = deflate(&zs, Z_FINISH);
    size_t n = out.size() - zs.avail_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) return aiml_str_alloc(0);

    std::string url = idp_sso_url;
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += "SAMLRequest=" + url_encode(b64_encode(out.data(), n));
    if (relay_state && *relay_state) url += "&RelayState=" + url_encode(relay_state);
    return aiml_str_dup(url.c_str());
}

extern "C" void* aiml_saml_verify_response(const char* saml_response_b64, const char* idp_cert_pem,
                                           const char* sp_entity_id, const char* acs_url,
                                           const char* expected_request_id) {
    init_once();
    if (!g_init_ok) return fail("xmlsec could not be initialised");
    if (!saml_response_b64 || !*saml_response_b64) return fail("empty SAMLResponse");
    if (!idp_cert_pem || !*idp_cert_pem) return fail("no IdP certificate configured");
    if (!sp_entity_id || !*sp_entity_id) return fail("no SP entity id configured");
    std::string acs = acs_url ? acs_url : "";
    std::string want_req = expected_request_id ? expected_request_id : "";

    std::string xml = b64_decode_lenient(saml_response_b64);
    if (xml.empty()) return fail("SAMLResponse is not valid base64");

    xmlSetGenericErrorFunc(nullptr, silent_xml_errors);
    // XML_PARSE_NONET: no network fetches.  No XML_PARSE_NOENT / DTDLOAD: entities stay unexpanded
    // references, which scan_tree rejects.
    xmlDocPtr doc = xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "saml-response.xml", nullptr,
                                  XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
    if (!doc) return fail("SAMLResponse is not well-formed XML");
    struct DocGuard { xmlDocPtr d; ~DocGuard() { xmlFreeDoc(d); } } guard{doc};

    if (doc->intSubset || doc->extSubset) return fail("a DTD is not allowed");
    std::set<std::string> ids;
    std::string why;
    if (!scan_tree(doc->children, ids, why)) return fail(why);

    xmlNodePtr resp = xmlDocGetRootElement(doc);
    if (!is_el(resp, NS_P, "Response")) return fail("root element is not samlp:Response");
    if (contains_el(resp, NS_A, "EncryptedAssertion")) return fail("encrypted assertions are not supported");
    if (count_assertions(resp->children) != 1) return fail("expected exactly one Assertion");
    xmlNodePtr assertion = child(resp, NS_A, "Assertion");
    if (!assertion) return fail("the Assertion must be a direct child of Response");

    // Register ID attributes so xmlsec can resolve "#id" references.
    const xmlChar* idAttrs[] = { BAD_CAST "ID", nullptr };
    xmlSecAddIDs(doc, resp, idAttrs);

    xmlNodePtr respSig = child(resp, NS_DS, "Signature");
    xmlNodePtr assSig = child(assertion, NS_DS, "Signature");
    if (!respSig && !assSig) return fail("the response is not signed");
    bool respOk = false, assOk = false;
    std::string respWhy, assWhy;
    if (respSig) { respWhy = verify_enveloped(doc, resp, respSig, idp_cert_pem); respOk = respWhy.empty(); }
    if (assSig)  { assWhy  = verify_enveloped(doc, assertion, assSig, idp_cert_pem); assOk = assWhy.empty(); }
    // A present but invalid signature always fails — never fall back to the other one.
    if (respSig && !respOk) return fail("Response " + respWhy);
    if (assSig && !assOk) return fail("Assertion " + assWhy);

    // ── protocol checks ──────────────────────────────────────────────────────
    xmlNodePtr status = child(resp, NS_P, "Status");
    xmlNodePtr code = status ? child(status, NS_P, "StatusCode") : nullptr;
    if (attr(code, "Value") != "urn:oasis:names:tc:SAML:2.0:status:Success")
        return fail("IdP status is not Success");
    if (has_attr(resp, "Destination") && attr(resp, "Destination") != acs)
        return fail("Destination does not match the ACS URL");
    std::string respIRT = attr(resp, "InResponseTo");
    if (!want_req.empty() && has_attr(resp, "InResponseTo") && respIRT != want_req)
        return fail("InResponseTo does not match the request");

    long long now = static_cast<long long>(time(nullptr));
    long long skew = g_skew_seconds.load();

    xmlNodePtr conds = child(assertion, NS_A, "Conditions");
    if (!conds) return fail("Assertion has no Conditions");
    if (has_attr(conds, "NotBefore")) {
        long long nb = parse_time(attr(conds, "NotBefore"));
        if (nb < 0) return fail("invalid NotBefore");
        if (now + skew < nb) return fail("Assertion is not yet valid");
    }
    if (has_attr(conds, "NotOnOrAfter")) {
        long long na = parse_time(attr(conds, "NotOnOrAfter"));
        if (na < 0) return fail("invalid NotOnOrAfter");
        if (now - skew >= na) return fail("Assertion has expired");
    }
    bool audienceOk = false, sawRestriction = false;
    for (xmlNodePtr ar = conds->children; ar; ar = ar->next) {
        if (!is_el(ar, NS_A, "AudienceRestriction")) continue;
        sawRestriction = true;
        bool thisOk = false;
        for (xmlNodePtr au = ar->children; au; au = au->next)
            if (is_el(au, NS_A, "Audience") && text(au) == sp_entity_id) thisOk = true;
        audienceOk = thisOk;                        // every restriction must admit us
        if (!thisOk) break;
    }
    if (!sawRestriction || !audienceOk) return fail("Audience does not match the SP entity id");

    xmlNodePtr subject = child(assertion, NS_A, "Subject");
    xmlNodePtr nameId = subject ? child(subject, NS_A, "NameID") : nullptr;
    if (!nameId) return fail("Assertion has no Subject/NameID");
    bool bearerOk = false;
    std::string subjIRT;
    for (xmlNodePtr sc = subject->children; sc; sc = sc->next) {
        if (!is_el(sc, NS_A, "SubjectConfirmation")) continue;
        if (attr(sc, "Method") != "urn:oasis:names:tc:SAML:2.0:cm:bearer") continue;
        xmlNodePtr scd = child(sc, NS_A, "SubjectConfirmationData");
        if (!scd) continue;
        if (has_attr(scd, "Recipient") && attr(scd, "Recipient") != acs) continue;
        if (has_attr(scd, "NotOnOrAfter")) {
            long long na = parse_time(attr(scd, "NotOnOrAfter"));
            if (na < 0 || now - skew >= na) continue;
        }
        if (has_attr(scd, "InResponseTo")) {
            subjIRT = attr(scd, "InResponseTo");
            if (!want_req.empty() && subjIRT != want_req) continue;
        }
        bearerOk = true;
        break;
    }
    if (!bearerOk) return fail("no valid bearer SubjectConfirmation (recipient, expiry or InResponseTo)");
    if (!want_req.empty() && respIRT.empty() && subjIRT.empty())
        return fail("InResponseTo is missing (expected a reply to our request)");

    // ── result: values only from the signed Assertion ────────────────────────
    Result r;
    r.put("ok", "1");
    r.put("error", "");
    r.put("signed", respOk && assOk ? "both" : (respOk ? "response" : "assertion"));
    xmlNodePtr issuer = child(assertion, NS_A, "Issuer");
    r.put("issuer", text(issuer));
    r.put("name_id", text(nameId));
    r.put("name_id_format", attr(nameId, "Format"));
    r.put("request_id", !respIRT.empty() ? respIRT : subjIRT);
    r.put("not_on_or_after", attr(conds, "NotOnOrAfter"));
    xmlNodePtr authn = child(assertion, NS_A, "AuthnStatement");
    r.put("session_index", attr(authn, "SessionIndex"));
    for (xmlNodePtr st = assertion->children; st; st = st->next) {
        if (!is_el(st, NS_A, "AttributeStatement")) continue;
        for (xmlNodePtr a = st->children; a; a = a->next) {
            if (!is_el(a, NS_A, "Attribute")) continue;
            std::string name = attr(a, "Name");
            if (name.empty()) continue;
            std::string joined;
            for (xmlNodePtr v = a->children; v; v = v->next) {
                if (!is_el(v, NS_A, "AttributeValue")) continue;
                if (!joined.empty()) joined += "\n";
                joined += text(v);
            }
            std::string key = "attr." + name;
            std::string prev;
            for (auto& e : r.kv) if (e.first == key) prev = e.second;
            r.put(key, prev.empty() ? joined : prev + "\n" + joined);
        }
    }
    return to_handle(r);
}
