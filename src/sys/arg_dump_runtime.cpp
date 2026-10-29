#include "arg_dump_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

// spec/61 §4: string struct fields must be aiml-MANAGED strings (with an AimlStrHeader) so the
// owner's teardown (fre → aiml_str_decref) can read the rc header at (s - 8). A libc strdup'd /
// string-literal value has no header → the decref reads 8 bytes out of bounds.
extern "C" char* aiml_str_dup(const char* s);
extern "C" void  aiml_str_decref(char* s);
extern "C" int32_t aiml_list_push_str_copy(void* list_ptr, const char* s, size_t len);

// A token that is a flag: '-' followed by something other than a digit ("-3" is a value).
static int is_flag(const char* s) {
    return s && s[0] == '-' && s[1] && !isdigit((unsigned char)s[1]);
}

// "--foo-bar=v" → name "foo_bar" in buf, *value = "v" (or NULL without '=').
static void split_flag(const char* arg, char* buf, int bufsz, const char** value) {
    const char* src = arg + 1;
    if (*src == '-') src++;
    int i = 0;
    *value = NULL;
    while (*src && i < bufsz - 1) {
        if (*src == '=') { *value = src + 1; break; }
        buf[i++] = (*src == '-') ? '_' : *src;
        src++;
    }
    buf[i] = '\0';
}

static void set_int(char* field, int code, long long v) {
    switch (code) {
        case ARG_I8: case ARG_BOOL: *(int8_t*)field = (int8_t)v; break;
        case ARG_I16: *(int16_t*)field = (int16_t)v; break;
        case ARG_I64: *(int64_t*)field = (int64_t)v; break;
        default: *(int32_t*)field = (int32_t)v; break;
    }
}

static void* list_of(char* field, int code) {
    return code == ARG_LIST_HANDLE ? *(void**)field : (void*)field;
}

void aiml_arg_dump_impl(void*        struct_ptr,
                        const char** names,
                        const int*   types,
                        const int*   offsets,
                        int          num_fields,
                        const char*  struct_name,
                        int          argc,
                        char**       argv) {
    char name[512];
    int positional = -1;                       // field collecting positional tokens
    for (int fi = 0; fi < num_fields; fi++)
        if ((types[fi] == ARG_LIST_HANDLE || types[fi] == ARG_LIST_INLINE) && strcmp(names[fi], "positional") == 0)
            positional = fi;
    int flags_done = 0;
    for (int argi = 1; argi < argc; argi++) {
        const char* arg = argv[argi];
        if (!arg) continue;
        if (flags_done || !is_flag(arg)) {
            if (positional >= 0) {
                char* field = (char*)struct_ptr + offsets[positional];
                void* list = list_of(field, types[positional]);
                if (list) aiml_list_push_str_copy(list, arg, strlen(arg));
            }
            continue;
        }
        if (strcmp(arg, "--") == 0) { flags_done = 1; continue; }
        const char* inline_value;
        split_flag(arg, name, (int)sizeof(name), &inline_value);
        int fi = 0;
        while (fi < num_fields && (types[fi] == ARG_SKIP || fi == positional || strcmp(name, names[fi]) != 0)) fi++;
        if (fi == num_fields) {
            fprintf(stderr, "UnknownArgumentException: unknown flag '%s'", arg);
            if (struct_name && struct_name[0]) fprintf(stderr, " (expected fields of %s)", struct_name);
            fprintf(stderr, "\n");
            exit(1);
        }
        char* field = (char*)struct_ptr + offsets[fi];
        int code = types[fi];
        const char* next = (argi + 1 < argc) ? argv[argi + 1] : NULL;
        // the value: "=v", else the next token (consumed) unless the field is a switch
        const char* value = inline_value;
        if (!value && code != ARG_BOOL && next && !is_flag(next)) { value = next; argi++; }
        switch (code) {
            case ARG_BOOL:
                set_int(field, code, value ? atoll(value) != 0 : 1);
                break;
            case ARG_I8: case ARG_I16: case ARG_I32: case ARG_I64:
                set_int(field, code, value ? atoll(value) : 1);
                break;
            case ARG_F32: if (value) *(float*)field = (float)atof(value); break;
            case ARG_F64: if (value) *(double*)field = atof(value); break;
            case ARG_STRING:
                // the field holds its default (an aiml-managed string): release it first
                aiml_str_decref(*(char**)field);
                *(char**)field = aiml_str_dup(value ? value : "");
                break;
            case ARG_LIST_HANDLE: case ARG_LIST_INLINE: {
                void* list = list_of(field, code);           // --name v repeats: each v is appended
                if (list && value) aiml_list_push_str_copy(list, value, strlen(value));
                break;
            }
            default: break;
        }
    }
}
