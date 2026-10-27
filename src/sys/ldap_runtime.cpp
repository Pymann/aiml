// LDAP (spec/67_identity.md §3) over OpenLDAP libldap.
//
// One ldap_handle per thread (libldap 2.6 is thread-safe, a single LDAP* is not
// meant to be shared between concurrent callers).  The handle is a plain
// resource: `close()` releases it (like ssh_session).
//
// Security rules baked in, not left to callers:
//   * bind() rejects an EMPTY password before talking to the server.  An LDAP
//     simple bind with a DN and an empty password is an "unauthenticated bind"
//     (RFC 4513 §5.1.2) that many servers answer with SUCCESS — the classic
//     login bypass.
//   * search filters and DNs built from user input must go through
//     escape_filter / escape_dn (RFC 4515 / RFC 4514).
#include "identity_runtime.h"

#define LDAP_DEPRECATED 0
#include <ldap.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" char* aiml_str_alloc(size_t len);
extern "C" char* aiml_str_dup(const char* s);
extern "C" char* aiml_str_ndup(const char* s, size_t len);

namespace {

struct AimlLdap {
    LDAP*       ld = nullptr;
    int32_t     timeout_ms = 10000;
    std::string err;
};

struct ListRaw { int32_t size; int32_t capacity; void** data; };

bool push_managed(void* list_ptr, char* s) {
    ListRaw* list = static_cast<ListRaw*>(list_ptr);
    if (!list || !s) return false;
    if (list->size >= list->capacity) {
        int newCap = list->capacity > 0 ? list->capacity * 2 : 8;
        void** nd = static_cast<void**>(realloc(list->data, size_t(newCap) * sizeof(void*)));
        if (!nd) return false;
        list->data = nd;
        list->capacity = newCap;
    }
    list->data[list->size++] = s;
    return true;
}

void apply_timeouts(AimlLdap* h) {
    struct timeval tv;
    tv.tv_sec  = h->timeout_ms / 1000;
    tv.tv_usec = (h->timeout_ms % 1000) * 1000;
    ldap_set_option(h->ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);
    ldap_set_option(h->ld, LDAP_OPT_TIMEOUT, &tv);
}

void set_err(AimlLdap* h, const char* what, int rc) {
    h->err = std::string(what) + ": " + ldap_err2string(rc);
    char* diag = nullptr;
    if (h->ld && ldap_get_option(h->ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &diag) == LDAP_OPT_SUCCESS && diag) {
        if (*diag) h->err += std::string(" (") + diag + ")";
        ldap_memfree(diag);
    }
}

}  // namespace

extern "C" void* aiml_ldap_open(const char* url) {
    if (!url || !*url) return nullptr;
    AimlLdap* h = new AimlLdap();
    int rc = ldap_initialize(&h->ld, url);
    if (rc != LDAP_SUCCESS || !h->ld) { delete h; return nullptr; }
    int version = LDAP_VERSION3;
    ldap_set_option(h->ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    ldap_set_option(h->ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);
    apply_timeouts(h);
    return h;
}

extern "C" int32_t aiml_ldap_start_tls(void* hp) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h || !h->ld) return -1;
    int rc = ldap_start_tls_s(h->ld, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) { set_err(h, "start_tls", rc); return -1; }
    return 1;
}

extern "C" int32_t aiml_ldap_bind(void* hp, const char* dn, const char* password) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h || !h->ld) return -1;
    if (!password || !*password) {
        h->err = "bind: empty password rejected (unauthenticated bind)";
        return 0;
    }
    if (!dn || !*dn) {
        h->err = "bind: empty DN rejected (anonymous bind)";
        return 0;
    }
    struct berval cred;
    cred.bv_val = const_cast<char*>(password);
    cred.bv_len = strlen(password);
    int rc = ldap_sasl_bind_s(h->ld, dn, LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc == LDAP_SUCCESS) { h->err.clear(); return 1; }
    set_err(h, "bind", rc);
    return rc == LDAP_INVALID_CREDENTIALS ? 0 : -1;
}

extern "C" int32_t aiml_ldap_search_dns_fill(void* hp, const char* base, const char* filter, void* list_ptr) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h || !h->ld || !list_ptr) return -1;
    char noattrs[] = LDAP_NO_ATTRS;                       // "1.1": DNs only
    char* attrs[] = { noattrs, nullptr };
    struct timeval tv;
    tv.tv_sec = h->timeout_ms / 1000; tv.tv_usec = (h->timeout_ms % 1000) * 1000;
    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(h->ld, base ? base : "", LDAP_SCOPE_SUBTREE,
                               (filter && *filter) ? filter : "(objectClass=*)",
                               attrs, 0, nullptr, nullptr, &tv, 1000, &res);
    if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
        set_err(h, "search", rc);
        if (res) ldap_msgfree(res);
        return -1;
    }
    int n = 0;
    for (LDAPMessage* e = ldap_first_entry(h->ld, res); e; e = ldap_next_entry(h->ld, e)) {
        char* dn = ldap_get_dn(h->ld, e);
        if (dn) { if (push_managed(list_ptr, aiml_str_dup(dn))) ++n; ldap_memfree(dn); }
    }
    ldap_msgfree(res);
    h->err.clear();
    return n;
}

extern "C" int32_t aiml_ldap_attr_values_fill(void* hp, const char* dn, const char* attr, void* list_ptr) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h || !h->ld || !list_ptr || !dn || !*dn || !attr || !*attr) return -1;
    char* attrs[] = { const_cast<char*>(attr), nullptr };
    struct timeval tv;
    tv.tv_sec = h->timeout_ms / 1000; tv.tv_usec = (h->timeout_ms % 1000) * 1000;
    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(h->ld, dn, LDAP_SCOPE_BASE, "(objectClass=*)",
                               attrs, 0, nullptr, nullptr, &tv, 1, &res);
    if (rc != LDAP_SUCCESS) {
        set_err(h, "attr_values", rc);
        if (res) ldap_msgfree(res);
        return -1;
    }
    int n = 0;
    LDAPMessage* e = ldap_first_entry(h->ld, res);
    if (e) {
        struct berval** vals = ldap_get_values_len(h->ld, e, attr);
        if (vals) {
            for (int i = 0; vals[i]; ++i)
                if (push_managed(list_ptr, aiml_str_ndup(vals[i]->bv_val, vals[i]->bv_len))) ++n;
            ldap_value_free_len(vals);
        }
    }
    ldap_msgfree(res);
    h->err.clear();
    return n;
}

extern "C" char* aiml_ldap_error(void* hp) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    return aiml_str_dup(h ? h->err.c_str() : "invalid ldap handle");
}

extern "C" void aiml_ldap_set_timeout(void* hp, int32_t ms) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h || !h->ld || ms <= 0) return;
    h->timeout_ms = ms;
    apply_timeouts(h);
}

extern "C" void aiml_ldap_close(void* hp) {
    AimlLdap* h = static_cast<AimlLdap*>(hp);
    if (!h) return;
    if (h->ld) ldap_unbind_ext_s(h->ld, nullptr, nullptr);
    delete h;
}

// RFC 4515 §3: '*', '(', ')', '\' and NUL are written as \XX in assertion values.
extern "C" char* aiml_ldap_escape_filter(const char* s) {
    if (!s) return aiml_str_alloc(0);
    std::string out;
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        unsigned char c = *p;
        if (c == '*' || c == '(' || c == ')' || c == '\\') {
            out += '\\'; out += hex[c >> 4]; out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    return aiml_str_dup(out.c_str());
}

// RFC 4514 §2.4: escape , + " \ < > ; = with a backslash, a leading '#' or
// space and a trailing space likewise; control bytes as \XX.
extern "C" char* aiml_ldap_escape_dn(const char* s) {
    if (!s) return aiml_str_alloc(0);
    std::string out;
    static const char hex[] = "0123456789abcdef";
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        bool special = c == ',' || c == '+' || c == '"' || c == '\\' || c == '<' || c == '>' ||
                       c == ';' || c == '=';
        bool edge = (i == 0 && (c == ' ' || c == '#')) || (i == n - 1 && c == ' ');
        if (c < 0x20 || c == 0x7f) {
            out += '\\'; out += hex[c >> 4]; out += hex[c & 15];
        } else if (special || edge) {
            out += '\\'; out += static_cast<char>(c);
        } else {
            out += static_cast<char>(c);
        }
    }
    return aiml_str_dup(out.c_str());
}
