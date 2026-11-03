// zim_runtime.cpp — ZIM archives, read-only (spec/33_2 §6).
//
// libzim is a C++ library that reports every failure by throwing.  Nothing may escape from
// here: an exception crossing into generated code has no handler of the right type and would
// end the process.  So every entry point catches, records the reason on this thread, and
// returns a sentinel; the compiler turns that into a ZimError with message and code
// (spec/33_2 §6.4).

#include "../src/sys/zim_runtime.h"
#include "../src/sys/compress_runtime.h"   // AimlCompressBuffer, the buffer ABI of spec/33_2

#include <zim/archive.h>
#include <zim/entry.h>
#include <zim/item.h>
#include <zim/blob.h>
#include <zim/error.h>
#include <zim/uuid.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <new>
#include <string>

extern "C" char* aiml_str_dup(const char* s);

// ── error state, per thread ────────────────────────────────────────────────────────────────
static thread_local int32_t     g_code = AIML_ZIM_OK;
static thread_local std::string g_msg;

static void zim_ok() { g_code = AIML_ZIM_OK; g_msg.clear(); }
static void zim_fail(int32_t code, const std::string& msg) { g_code = code; g_msg = msg; }

extern "C" int32_t aiml_zim_last_code(void) { return g_code; }
extern "C" char*   aiml_zim_last_message(void) { return aiml_str_dup(g_msg.c_str()); }

// A libzim throw turned into one of the codes of §6.4.  ZimFileFormatError covers both "not a
// ZIM file" and a cluster that will not decompress; the message is what libzim said, so the
// two stay distinguishable to a reader even though the code is coarser.
static void zim_fail_from_exception(const char* what, int32_t fallback) {
    zim_fail(fallback, what ? what : "unknown libzim error");
}

namespace {

// The open archive.  libzim's Archive is already a shared handle onto its FileImpl, so one
// per aiml handle is enough; §6.4 (F33-2-31) allows several handles onto the same file.
struct ZimHandle {
    zim::Archive archive;
    explicit ZimHandle(const std::string& path) : archive(path) {}
};

ZimHandle* as_handle(void* h) { return static_cast<ZimHandle*>(h); }

void clear_entry(AimlZimEntry* out) {
    if (!out) return;
    out->index = 0;
    out->size = 0;
    out->position = -1;
    out->found = 0;
    out->is_redirect = 0;
}

// Fill an entry value from a libzim Entry.  `position` is only known when the entry came out
// of a cluster-order scan; everywhere else it is -1 (F33-2-25).
void fill_entry(const zim::Entry& e, int64_t position, AimlZimEntry* out) {
    out->index       = (int64_t)e.getIndex();
    out->position    = position;
    out->found       = 1;
    out->is_redirect = e.isRedirect() ? 1 : 0;
    out->size        = 0;
    if (!e.isRedirect()) {
        try { out->size = (int64_t)e.getItem(false).getSize(); } catch (...) { out->size = 0; }
    }
}

// The libzim Entry behind a value.  Throws zim::EntryNotFound if the index is not in the file.
zim::Entry entry_of(ZimHandle* h, const AimlZimEntry* e) {
    return h->archive.getEntryByPath((zim::entry_index_type)e->index);
}

} // namespace

// ── §6.1 opening and metadata ──────────────────────────────────────────────────────────────
extern "C" void* aiml_zim_open(const char* path) {
    zim_ok();
    if (!path || !*path) { zim_fail(AIML_ZIM_E_OPEN, "sys: no path given"); return nullptr; }
    try {
        return new ZimHandle(std::string(path));
    } catch (const zim::ZimFileFormatError& e) {
        zim_fail(AIML_ZIM_E_FORMAT, std::string("not a ZIM file: ") + e.what());
    } catch (const std::bad_alloc&) {
        zim_fail(AIML_ZIM_E_OPEN, std::string("out of memory opening ") + path);
    } catch (const std::exception& e) {
        // libzim reports a missing or unreadable file as a runtime_error
        zim_fail(AIML_ZIM_E_OPEN, std::string("cannot open ") + path + ": " + e.what());
    } catch (...) {
        zim_fail(AIML_ZIM_E_OPEN, std::string("cannot open ") + path);
    }
    return nullptr;
}

extern "C" void aiml_zim_close(void* handle) {
    if (!handle) return;
    delete as_handle(handle);
}

extern "C" int64_t aiml_zim_entry_count(void* handle) {
    if (!handle) return 0;
    try { return (int64_t)as_handle(handle)->archive.getAllEntryCount(); } catch (...) { return 0; }
}

extern "C" int64_t aiml_zim_article_count(void* handle) {
    if (!handle) return 0;
    try { return (int64_t)as_handle(handle)->archive.getArticleCount(); } catch (...) { return 0; }
}

// §6.3: how many positions the cluster order has.  This is NOT entry_count: that one counts
// every entry including the metadata and the internal ones (Title, Language, the counter, the
// title index), which live outside the cluster order.  A scan walks the user entries — items
// and redirects — so this is the bound a resumable reader loops to.
extern "C" int64_t aiml_zim_scan_count(void* handle) {
    if (!handle) return 0;
    try { return (int64_t)as_handle(handle)->archive.getEntryCount(); } catch (...) { return 0; }
}

extern "C" char* aiml_zim_uuid(void* handle) {
    if (!handle) return aiml_str_dup("");
    try {
        // libzim renders the canonical, hyphenated form (8-4-4-4-12 = 36 characters).  §6.1
        // promises the 32 hex digits, which is the same value without the separators and is
        // what a caller compares or stores.  Raw bytes are also accepted, for robustness.
        std::string text = static_cast<std::string>(as_handle(handle)->archive.getUuid());
        static const char* digits = "0123456789abcdef";
        bool printable = true;
        for (unsigned char ch : text)
            if (!(isxdigit(ch) || ch == '-')) { printable = false; break; }
        std::string out;
        out.reserve(32);
        if (printable) {
            for (char ch : text) if (ch != '-') out.push_back((char)tolower((unsigned char)ch));
        } else {
            for (unsigned char byte : text) {
                out.push_back(digits[(byte >> 4) & 0xF]);
                out.push_back(digits[byte & 0xF]);
            }
        }
        return aiml_str_dup(out.c_str());
    } catch (...) { return aiml_str_dup(""); }
}

extern "C" char* aiml_zim_metadata(void* handle, const char* key) {
    if (!handle || !key) return aiml_str_dup("");
    try {
        return aiml_str_dup(as_handle(handle)->archive.getMetadata(std::string(key)).c_str());
    } catch (...) {
        return aiml_str_dup("");          // §6.1: a missing key is "" and not an error
    }
}

extern "C" int32_t aiml_zim_checksum_ok(void* handle) {
    if (!handle) return 0;
    try { return as_handle(handle)->archive.check() ? 1 : 0; } catch (...) { return 0; }
}

// ── §6.2 entries by path or title ──────────────────────────────────────────────────────────
extern "C" void aiml_zim_by_path(void* handle, const char* path, AimlZimEntry* out) {
    zim_ok();
    clear_entry(out);
    if (!handle || !path || !out) return;
    ZimHandle* h = as_handle(handle);
    try {
        fill_entry(h->archive.getEntryByPath(std::string(path)), -1, out);
        return;
    } catch (const zim::EntryNotFound&) {
        // fall through to the old-scheme attempt
    } catch (...) {
        return;
    }
    // F33-2-22: in an old-scheme file the stored path carries the namespace ("A/Kupfer"), so a
    // caller writing the bare path would never find an article.
    if (!h->archive.hasNewNamespaceScheme()) {
        try {
            fill_entry(h->archive.getEntryByPath("A/" + std::string(path)), -1, out);
        } catch (...) { /* a miss is not an error (§6.4) */ }
    }
}

extern "C" void aiml_zim_by_title(void* handle, const char* title, AimlZimEntry* out) {
    zim_ok();
    clear_entry(out);
    if (!handle || !title || !out) return;
    try {
        fill_entry(as_handle(handle)->archive.getEntryByTitle(std::string(title)), -1, out);
    } catch (...) { /* a miss is not an error (§6.4) */ }
}

extern "C" int32_t aiml_zim_resolve(void* handle, const AimlZimEntry* in, AimlZimEntry* out) {
    zim_ok();
    clear_entry(out);
    if (!handle || !in || !out) { zim_fail(AIML_ZIM_E_NO_ENTRY, "resolve: no entry"); return g_code; }
    if (!in->found) { zim_fail(AIML_ZIM_E_NO_ENTRY, "resolve: the entry was not found"); return g_code; }
    ZimHandle* h = as_handle(handle);
    try {
        zim::Entry cur = entry_of(h, in);
        // F33-2-23: at most 16 hops, and a repeat is a loop.  A ZIM file may point a redirect
        // at itself or in a ring; without the counter this would spin forever.
        int64_t seen_first = (int64_t)cur.getIndex();
        for (int hop = 0; cur.isRedirect(); ++hop) {
            if (hop >= 16) {
                zim_fail(AIML_ZIM_E_LOOP, "redirect chain longer than 16 hops");
                clear_entry(out);
                return g_code;
            }
            cur = cur.getRedirectEntry();
            if ((int64_t)cur.getIndex() == seen_first) {
                zim_fail(AIML_ZIM_E_LOOP, "redirect points back at where the chain started");
                clear_entry(out);
                return g_code;
            }
        }
        fill_entry(cur, in->position, out);
        return AIML_ZIM_OK;
    } catch (const zim::EntryNotFound& e) {
        zim_fail(AIML_ZIM_E_NO_ENTRY, std::string("resolve: ") + e.what());
    } catch (const zim::ZimFileFormatError& e) {
        zim_fail(AIML_ZIM_E_CORRUPT, std::string("resolve: ") + e.what());
    } catch (const std::exception& e) {
        zim_fail_from_exception(e.what(), AIML_ZIM_E_NO_ENTRY);
    } catch (...) {
        zim_fail(AIML_ZIM_E_NO_ENTRY, "resolve failed");
    }
    clear_entry(out);
    return g_code;
}

extern "C" char* aiml_zim_str(void* handle, const AimlZimEntry* entry, int32_t which) {
    zim_ok();
    if (!handle || !entry || !entry->found) return aiml_str_dup("");
    try {
        zim::Entry e = entry_of(as_handle(handle), entry);
        switch (which) {
            case AIML_ZIM_STR_PATH:  return aiml_str_dup(e.getPath().c_str());
            case AIML_ZIM_STR_TITLE: return aiml_str_dup(e.getTitle().c_str());
            case AIML_ZIM_STR_MIMETYPE:
                if (e.isRedirect()) return aiml_str_dup("");       // §6.2: "" for a redirect
                return aiml_str_dup(e.getItem(false).getMimetype().c_str());
            case AIML_ZIM_STR_REDIRECT_PATH:
                if (!e.isRedirect()) return aiml_str_dup("");      // §6.2: "" when not one
                return aiml_str_dup(e.getRedirectEntry().getPath().c_str());
            default: break;
        }
    } catch (...) { /* a string of an entry that is gone reads as "" */ }
    return aiml_str_dup("");
}

extern "C" void* aiml_zim_content(void* handle, const AimlZimEntry* entry) {
    zim_ok();
    if (!handle || !entry || !entry->found) {
        zim_fail(AIML_ZIM_E_NO_ENTRY, "content: the entry was not found");
        return nullptr;
    }
    if (entry->is_redirect) {
        // F33-2-24: a redirect has no content.  Say so rather than handing back an empty
        // buffer that a caller would read as an empty article.
        zim_fail(AIML_ZIM_E_IS_REDIRECT, "content: the entry is a redirect — resolve it first");
        return nullptr;
    }
    try {
        zim::Blob blob = entry_of(as_handle(handle), entry).getItem(false).getData();
        size_t n = blob.size();
        AimlCompressBuffer* buf = (AimlCompressBuffer*)malloc(sizeof(AimlCompressBuffer));
        if (!buf) { zim_fail(AIML_ZIM_E_CORRUPT, "content: out of memory"); return nullptr; }
        buf->data = (uint8_t*)malloc(n > 0 ? n : 1);
        if (!buf->data) { free(buf); zim_fail(AIML_ZIM_E_CORRUPT, "content: out of memory"); return nullptr; }
        if (n > 0) memcpy(buf->data, blob.data(), n);
        buf->size       = (int32_t)n;
        buf->capacity   = (int32_t)(n > 0 ? n : 1);
        buf->read_idx   = 0;
        buf->write_idx  = (int32_t)n;
        return buf;
    } catch (const zim::EntryNotFound& e) {
        zim_fail(AIML_ZIM_E_NO_ENTRY, std::string("content: ") + e.what());
    } catch (const zim::ZimFileFormatError& e) {
        zim_fail(AIML_ZIM_E_CORRUPT, std::string("content: ") + e.what());
    } catch (const std::exception& e) {
        zim_fail_from_exception(e.what(), AIML_ZIM_E_CORRUPT);
    } catch (...) {
        zim_fail(AIML_ZIM_E_CORRUPT, "content failed");
    }
    return nullptr;
}

extern "C" char* aiml_zim_content_str(void* handle, const AimlZimEntry* entry) {
    AimlCompressBuffer* buf = (AimlCompressBuffer*)aiml_zim_content(handle, entry);
    if (!buf) return nullptr;                      // the error is already recorded
    // §6.2: the bytes are kept as they are, no validation; a NUL inside would end the string,
    // so content with embedded NULs belongs in zim.content.
    std::string text((const char*)buf->data, (size_t)buf->size);
    free(buf->data);
    free(buf);
    return aiml_str_dup(text.c_str());
}

// ── §6.3 scanning in cluster order ─────────────────────────────────────────────────────────
// Must match AimlListRaw (string_runtime.cpp): the handle points AT this struct.
struct AimlZimListRaw {
    int32_t size;
    int32_t capacity;
    void*   data;
};

extern "C" int32_t aiml_zim_scan(void* handle, int64_t from, int32_t count, void* list_ptr) {
    zim_ok();
    if (!handle || !list_ptr || count <= 0) return 0;
    ZimHandle* h = as_handle(handle);
    AimlZimListRaw* list = static_cast<AimlZimListRaw*>(list_ptr);

    int64_t total = 0;
    try { total = (int64_t)h->archive.getEntryCount(); } catch (...) { return 0; }
    if (from < 0 || from >= total) return 0;            // F33-2-25: empty list at the end
    int64_t want = count;
    if (from + want > total) want = total - from;

    AimlZimEntry* rows = (AimlZimEntry*)calloc((size_t)want, sizeof(AimlZimEntry));
    if (!rows) { zim_fail(AIML_ZIM_E_CORRUPT, "scan: out of memory"); return 0; }

    int32_t filled = 0;
    for (int64_t i = 0; i < want; i++) {
        try {
            // getEntryByClusterOrder indexes the cluster order directly, so any `from` costs
            // the same — no iterator has to be restarted and no directory entry skipped.
            zim::Entry e = h->archive.getEntryByClusterOrder((zim::entry_index_type)(from + i));
            fill_entry(e, from + i, &rows[filled]);
            filled++;
        } catch (...) {
            // One unreadable entry must not lose the batch; it is simply left out.
        }
    }

    // Replace the list's buffer with ours — the same swap compress.zip_entries does.  The
    // elements are scalars only, so there is nothing to release per element.
    free(list->data);
    list->data     = rows;
    list->size     = filled;
    list->capacity = (int32_t)want;
    return filled;
}

extern "C" void aiml_zim_cache(int64_t clusters) {
    // libzim 8 has no setter for the cluster cache; it reads ZIM_CLUSTERCACHE (a NUMBER OF
    // CLUSTERS) while opening an archive.  So this reaches archives opened AFTERWARDS — which
    // is also the only moment at which a cache size can still matter.
    if (clusters < 1) clusters = 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)clusters);
    setenv("ZIM_CLUSTERCACHE", buf, 1);
}

// ── The one exported symbol (spec/33_2 §6.4) ────────────────────────────────────────────────
// libaiml opens this object on demand and reads this table; nothing else here is called by
// name.  Entry points are matched by POSITION, so a field is only ever appended.
// The ONLY symbol this object exports (everything else is hidden, see exodus-zim.json's
// -fvisibility=hidden).  That is not tidiness: libaiml defines the same aiml_zim_* names as
// forwarders and the main binary is linked -rdynamic, so with default visibility the dynamic
// linker resolved this table's entries to the FORWARDERS instead of the implementations below
// — aiml_zim_open called itself until the stack ran out (measured 2026-11-03, SIGSEGV with an
// aiml_zim_open frame repeated to the bottom).
extern "C" __attribute__((visibility("default"))) const AimlZimApi* aiml_zim_module_api(void) {
    static const AimlZimApi api = {
        AIML_ZIM_API_VERSION,
        aiml_zim_open,
        aiml_zim_close,
        aiml_zim_entry_count,
        aiml_zim_article_count,
        aiml_zim_scan_count,
        aiml_zim_uuid,
        aiml_zim_metadata,
        aiml_zim_checksum_ok,
        aiml_zim_by_path,
        aiml_zim_by_title,
        aiml_zim_resolve,
        aiml_zim_str,
        aiml_zim_content,
        aiml_zim_content_str,
        aiml_zim_scan,
        aiml_zim_cache,
        aiml_zim_last_code,
        aiml_zim_last_message,
    };
    return &api;
}
