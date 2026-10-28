#pragma once
// Identity builtins (spec/67_identity.md): base64, LDAP (OpenLDAP libldap),
// SAML 2.0 SP side (libxmlsec1 + OpenSSL + libxml2).
//
// Memory contract (spec/61, memV3): every returned char* is an aiml-managed
// string (aiml_str_alloc/aiml_str_dup) or nullptr for "absent"; every returned
// map is a memV3 map<string:string> HANDLE (aiml_map_handle_new, dense store of
// managed strings); lists are filled into a caller-provided list (handle under
// memV3, alloca otherwise) with managed string elements.  Buffers are returned
// as a fresh heap header { i32 size, i32 cap, i32 rIdx, i32 wIdx, char* data }
// that the caller copies into its buffer and frees (data moves along).
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── base64 (RFC 4648) ────────────────────────────────────────────────────────
// url = 0: standard alphabet with '=' padding; url = 1: URL-safe alphabet, no padding.
char* aiml_b64_encode(const void* data, int32_t len, int32_t url);
char* aiml_b64_encode_str(const char* s, int32_t url);
// Decode into a heap buffer header (never null; empty buffer on invalid input).
void* aiml_b64_decode_buf(const char* s, int32_t url);
// Decode into a managed string; nullptr on invalid input.  Text use only:
// a decoded NUL byte ends the string.
char* aiml_b64_decode_str(const char* s, int32_t url);

// ── LDAP ─────────────────────────────────────────────────────────────────────
void*   aiml_ldap_open(const char* url);                 // AimlLdap* or nullptr
int32_t aiml_ldap_start_tls(void* h);                    // 1 ok, -1 error
int32_t aiml_ldap_bind(void* h, const char* dn, const char* password); // 1 ok, 0 bad creds, -1 error
int32_t aiml_ldap_search_dns_fill(void* h, const char* base, const char* filter, void* list_ptr);
int32_t aiml_ldap_attr_values_fill(void* h, const char* dn, const char* attr, void* list_ptr);
char*   aiml_ldap_error(void* h);                        // managed string ("" if none)
void    aiml_ldap_set_timeout(void* h, int32_t ms);
void    aiml_ldap_close(void* h);
char*   aiml_ldap_escape_filter(const char* s);          // RFC 4515 value escaping
char*   aiml_ldap_escape_dn(const char* s);              // RFC 4514 value escaping

// ── SAML 2.0 (service provider side) ────────────────────────────────────────
char* aiml_saml_authn_request_url(const char* idp_sso_url, const char* sp_entity_id,
                                  const char* acs_url, const char* request_id,
                                  const char* relay_state);
char* aiml_saml_new_request_id(void);
// Returns a memV3 map<string:string> handle (see spec/67 §4 for the keys).
void* aiml_saml_verify_response(const char* saml_response_b64, const char* idp_cert_pem,
                                const char* sp_entity_id, const char* acs_url,
                                const char* expected_request_id);
void  aiml_saml_set_clock_skew(int32_t seconds);

#ifdef __cplusplus
}
#endif
