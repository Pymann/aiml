// zim_runtime.cpp — the ZIM entry points inside libaiml (spec/33_2 §6).
//
// These are forwarders.  The libzim code lives in libaiml_zim.so and is loaded on FIRST USE,
// for the reason spelled out in zim_runtime.h: libzim drags in libxapian, and linking it into
// libaiml made libxapian a transitive dependency of every aiml program — which broke the link
// of projects that never touch a ZIM file.  A program that never calls compress.zim_* never
// loads the module and pays nothing.
//
// Without the module every call reports a ZimError naming what is missing, rather than failing
// as a null dereference somewhere in generated code.

#include "zim_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

extern "C" char* aiml_str_dup(const char* s);

namespace {

// The reason the module could not be loaded, and the error state for calls made anyway.
// Per thread, like the module's own error state, so two readers cannot overwrite each other.
thread_local int32_t g_stub_code = AIML_ZIM_OK;
thread_local char    g_stub_msg[512];

void stub_fail(int32_t code, const char* msg) {
    g_stub_code = code;
    snprintf(g_stub_msg, sizeof(g_stub_msg), "%s", msg ? msg : "");
}

const char* g_load_error = nullptr;

// Load libaiml_zim.so once.  Returns nullptr when it is not there, and remembers why.
const AimlZimApi* module_api() {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    g_load_error = "ZIM archives need libzim, which is not available in a wasm build";
    return nullptr;
#else
    static const AimlZimApi* api = nullptr;
    static bool tried = false;
    if (tried) return api;
    tried = true;

    // Relative to libaiml itself first: dladdr gives the path of the object this code is in, so
    // the module is found whatever the caller's rpath looks like.  Two layouts: installed side
    // by side in one lib directory, and the build tree, where each config writes into its own
    // out/<name>/ folder.
    void* handle = nullptr;
    Dl_info info;
    if (dladdr((void*)&module_api, &info) && info.dli_fname) {
        const char* slash = strrchr(info.dli_fname, '/');
        if (slash) {
            size_t dir = (size_t)(slash - info.dli_fname) + 1;
            static const char* const rel[] = { "libaiml_zim.so",
                                               "../libaiml_zim/libaiml_zim.so" };
            for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]) && !handle; i++) {
                char path[4096];
                size_t n = strlen(rel[i]) + 1;
                if (dir + n > sizeof(path)) continue;
                memcpy(path, info.dli_fname, dir);
                memcpy(path + dir, rel[i], n);
                handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            }
        }
    }
    // Then the ordinary search (rpath, LD_LIBRARY_PATH, ld.so.conf).
    if (!handle) handle = dlopen("libaiml_zim.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        g_load_error = "ZIM support is not installed: libaiml_zim.so could not be loaded "
                       "(build it with `exodus build --all`, and fetch libzim with "
                       "`exodus pkg install`)";
        return nullptr;
    }

    typedef const AimlZimApi* (*api_fn)(void);
    api_fn get = (api_fn)dlsym(handle, "aiml_zim_module_api");
    if (!get) {
        g_load_error = "libaiml_zim.so does not export aiml_zim_module_api — a stale module";
        return nullptr;
    }
    const AimlZimApi* candidate = get();
    if (!candidate || candidate->version != AIML_ZIM_API_VERSION) {
        g_load_error = "libaiml_zim.so was built against a different ZIM API version";
        return nullptr;
    }
    api = candidate;
    return api;
#endif
}

// Report "no module" in the shape every caller already handles.
const AimlZimApi* api_or_fail(int32_t code) {
    const AimlZimApi* api = module_api();
    if (!api) stub_fail(code, g_load_error);
    return api;
}

} // namespace

extern "C" int32_t aiml_zim_last_code(void) {
    const AimlZimApi* api = module_api();
    return api ? api->last_code() : g_stub_code;
}

extern "C" char* aiml_zim_last_message(void) {
    const AimlZimApi* api = module_api();
    if (api) return api->last_message();
    return aiml_str_dup(g_stub_msg);
}

extern "C" void* aiml_zim_open(const char* path) {
    g_stub_code = AIML_ZIM_OK;
    const AimlZimApi* api = api_or_fail(AIML_ZIM_E_OPEN);
    return api ? api->open(path) : nullptr;
}

extern "C" void aiml_zim_close(void* handle) {
    const AimlZimApi* api = module_api();
    if (api) api->close(handle);
}

extern "C" int64_t aiml_zim_entry_count(void* handle) {
    const AimlZimApi* api = module_api();
    return api ? api->entry_count(handle) : 0;
}

extern "C" int64_t aiml_zim_article_count(void* handle) {
    const AimlZimApi* api = module_api();
    return api ? api->article_count(handle) : 0;
}

extern "C" int64_t aiml_zim_scan_count(void* handle) {
    const AimlZimApi* api = module_api();
    return api ? api->scan_count(handle) : 0;
}

extern "C" char* aiml_zim_uuid(void* handle) {
    const AimlZimApi* api = module_api();
    return api ? api->uuid(handle) : aiml_str_dup("");
}

extern "C" char* aiml_zim_metadata(void* handle, const char* key) {
    const AimlZimApi* api = module_api();
    return api ? api->metadata(handle, key) : aiml_str_dup("");
}

extern "C" int32_t aiml_zim_checksum_ok(void* handle) {
    const AimlZimApi* api = module_api();
    return api ? api->checksum_ok(handle) : 0;
}

extern "C" void aiml_zim_by_path(void* handle, const char* path, AimlZimEntry* out) {
    g_stub_code = AIML_ZIM_OK;
    const AimlZimApi* api = module_api();
    if (api) { api->by_path(handle, path, out); return; }
    // No module: report it rather than letting a caller read an uninitialised entry as "found".
    stub_fail(AIML_ZIM_E_OPEN, g_load_error);
    if (out) memset(out, 0, sizeof(*out));
    if (out) out->position = -1;
}

extern "C" void aiml_zim_by_title(void* handle, const char* title, AimlZimEntry* out) {
    g_stub_code = AIML_ZIM_OK;
    const AimlZimApi* api = module_api();
    if (api) { api->by_title(handle, title, out); return; }
    stub_fail(AIML_ZIM_E_OPEN, g_load_error);
    if (out) memset(out, 0, sizeof(*out));
    if (out) out->position = -1;
}

extern "C" int32_t aiml_zim_resolve(void* handle, const AimlZimEntry* in, AimlZimEntry* out) {
    const AimlZimApi* api = api_or_fail(AIML_ZIM_E_OPEN);
    if (!api) {
        if (out) { memset(out, 0, sizeof(*out)); out->position = -1; }
        return g_stub_code;
    }
    return api->resolve(handle, in, out);
}

extern "C" char* aiml_zim_str(void* handle, const AimlZimEntry* entry, int32_t which) {
    const AimlZimApi* api = module_api();
    return api ? api->str(handle, entry, which) : aiml_str_dup("");
}

extern "C" void* aiml_zim_content(void* handle, const AimlZimEntry* entry) {
    const AimlZimApi* api = api_or_fail(AIML_ZIM_E_OPEN);
    return api ? api->content(handle, entry) : nullptr;
}

extern "C" char* aiml_zim_content_str(void* handle, const AimlZimEntry* entry) {
    const AimlZimApi* api = api_or_fail(AIML_ZIM_E_OPEN);
    return api ? api->content_str(handle, entry) : nullptr;
}

extern "C" int32_t aiml_zim_scan(void* handle, int64_t from, int32_t count, void* list_ptr) {
    const AimlZimApi* api = api_or_fail(AIML_ZIM_E_OPEN);
    return api ? api->scan(handle, from, count, list_ptr) : 0;
}

extern "C" void aiml_zim_cache(int64_t clusters) {
    // Takes effect for archives opened later, so it is worth recording even before the module
    // is loaded: the module reads the same environment variable when it opens one.
    if (clusters < 1) clusters = 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)clusters);
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
    setenv("ZIM_CLUSTERCACHE", buf, 1);
#endif
    const AimlZimApi* api = module_api();
    if (api) api->cache(clusters);
}
