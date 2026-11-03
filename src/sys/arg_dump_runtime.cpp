#include "arg_dump_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <float.h>

// spec/61 §4: string struct fields must be aiml-MANAGED strings (with an AimlStrHeader) so the
// owner's teardown (fre → aiml_str_decref) can read the rc header at (s - 8). A libc strdup'd /
// string-literal value has no header → the decref reads 8 bytes out of bounds.
extern "C" char* aiml_str_dup(const char* s);
extern "C" void  aiml_str_decref(char* s);
extern "C" int32_t aiml_list_push_str_copy(void* list_ptr, const char* s, size_t len);

// A token that is a flag: one or two dashes, then a name — a letter or '_', followed by
// letters, digits, '_' or '-' — optionally "=value".  Anything else is a value or a
// positional token: "-3" and "-0.5" (numbers), "-Flies(Tweety)" (a negated fact),
// "--5 + 1" (an expression), "-" (stdin by convention).  "--" alone is handled by the caller.
static int is_flag(const char* s) {
    if (!s || s[0] != '-') return 0;
    const char* p = s + 1;
    if (*p == '-') p++;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return 0;
    for (p++; *p && *p != '='; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) return 0;
    return 1;
}

static void fail_value(const char* arg, const char* what, const char* got) {
    if (got)
        fprintf(stderr, "InvalidArgumentException: flag '%s' expects %s, got '%s'\n", arg, what, got);
    else
        fprintf(stderr, "InvalidArgumentException: flag '%s' expects %s\n", arg, what);
    exit(1);
}

// The whole token must be a number: "12" yes; "12abc", "", " 12" and "abc" no.
static long long parse_int_or_fail(const char* arg, const char* value) {
    char* end = NULL;
    errno = 0;
    if (!*value || isspace((unsigned char)*value)) fail_value(arg, "an integer", value);
    long long v = strtoll(value, &end, 10);
    if (*end || errno == ERANGE) fail_value(arg, "an integer", value);
    return v;
}

// Unsigned: no sign, the full u64 range.
static unsigned long long parse_uint_or_fail(const char* arg, const char* value) {
    char* end = NULL;
    errno = 0;
    if (!*value || isspace((unsigned char)*value) || *value == '-') fail_value(arg, "a non-negative integer", value);
    unsigned long long v = strtoull(value, &end, 10);
    if (*end || errno == ERANGE) fail_value(arg, "a non-negative integer", value);
    return v;
}

// A real: overflow is an error, underflow to a subnormal or 0 is a valid value.
static double parse_real_or_fail(const char* arg, const char* value) {
    char* end = NULL;
    errno = 0;
    if (!*value || isspace((unsigned char)*value)) fail_value(arg, "a number", value);
    double v = strtod(value, &end);
    if (*end || (errno == ERANGE && (v == HUGE_VAL || v == -HUGE_VAL))) fail_value(arg, "a number", value);
    return v;
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

static void set_uint(char* field, int code, unsigned long long v) {
    switch (code) {
        case ARG_U8: *(uint8_t*)field = (uint8_t)v; break;
        case ARG_U16: *(uint16_t*)field = (uint16_t)v; break;
        case ARG_U64: *(uint64_t*)field = (uint64_t)v; break;
        default: *(uint32_t*)field = (uint32_t)v; break;
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
    aiml_arg_dump_impl2(struct_ptr, names, types, offsets, num_fields, struct_name, argc, argv, NULL);
}

void aiml_arg_dump_impl2(void*        struct_ptr,
                         const char** names,
                         const int*   types,
                         const int*   offsets,
                         int          num_fields,
                         const char*  struct_name,
                         int          argc,
                         char**       argv,
                         const char** real_names) {
    char name[512];
    int positional = -1;                       // field collecting positional tokens
    int given = -1;                            // field collecting the names of the fields set (F23-64)
    for (int fi = 0; fi < num_fields; fi++) {
        int is_list = types[fi] == ARG_LIST_HANDLE || types[fi] == ARG_LIST_INLINE;
        if (is_list && strcmp(names[fi], "positional") == 0)
            positional = fi;
        if (is_list && real_names && real_names[fi] && strcmp(real_names[fi], "given") == 0)
            given = fi;
    }
    char* recorded = (char*)calloc(num_fields > 0 ? (size_t)num_fields : 1, 1);
    int flags_done = 0;
    for (int argi = 1; argi < argc; argi++) {
        const char* arg = argv[argi];
        if (!arg) continue;
        if (!flags_done && strcmp(arg, "--") == 0) { flags_done = 1; continue; }
        if (flags_done || !is_flag(arg)) {
            if (positional >= 0) {
                char* field = (char*)struct_ptr + offsets[positional];
                void* list = list_of(field, types[positional]);
                if (list) aiml_list_push_str_copy(list, arg, strlen(arg));
            }
            continue;
        }
        const char* inline_value;
        split_flag(arg, name, (int)sizeof(name), &inline_value);
        int fi = 0;
        while (fi < num_fields && (types[fi] == ARG_SKIP || fi == positional || fi == given || strcmp(name, names[fi]) != 0)) fi++;
        if (fi == num_fields) {
            fprintf(stderr, "UnknownArgumentException: unknown flag '%s'", arg);
            if (struct_name && struct_name[0]) fprintf(stderr, " (expected fields of %s)", struct_name);
            fprintf(stderr, "\n");
            exit(1);
        }
        char* field = (char*)struct_ptr + offsets[fi];
        int code = types[fi];
        const char* next = (argi + 1 < argc) ? argv[argi + 1] : NULL;
        // the value: "=v", else the next token (consumed) unless the field is a switch.
        // Only a bool is a switch; every other field needs its value — a missing one is an
        // error, never a silent 1 or "" (F23-61), so a forgotten number cannot slip through.
        const char* value = inline_value;
        // "--" is never a value: it ends the flags, so a flag right before it lacks its value
        if (!value && code != ARG_BOOL && next && strcmp(next, "--") != 0 && !is_flag(next)) { value = next; argi++; }
        switch (code) {
            case ARG_BOOL: {
                int on = 1;
                if (value) {
                    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) on = 1;
                    else if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0) on = 0;
                    else fail_value(arg, "0, 1, true or false", value);
                }
                set_int(field, code, on);
                break;
            }
            case ARG_I8: case ARG_I16: case ARG_I32: case ARG_I64: {
                if (!value) fail_value(arg, "an integer", NULL);
                long long v = parse_int_or_fail(arg, value);
                if ((code == ARG_I8  && (v < INT8_MIN  || v > INT8_MAX)) ||
                    (code == ARG_I16 && (v < INT16_MIN || v > INT16_MAX)) ||
                    (code == ARG_I32 && (v < INT32_MIN || v > INT32_MAX)))
                    fail_value(arg, "an integer in range of the field", value);
                set_int(field, code, v);
                break;
            }
            case ARG_U8: case ARG_U16: case ARG_U32: case ARG_U64: {
                if (!value) fail_value(arg, "a non-negative integer", NULL);
                unsigned long long v = parse_uint_or_fail(arg, value);
                if ((code == ARG_U8  && v > UINT8_MAX) ||
                    (code == ARG_U16 && v > UINT16_MAX) ||
                    (code == ARG_U32 && v > UINT32_MAX))
                    fail_value(arg, "an integer in range of the field", value);
                set_uint(field, code, v);
                break;
            }
            case ARG_F32: {
                if (!value) fail_value(arg, "a number", NULL);
                double v = parse_real_or_fail(arg, value);
                // finite but beyond float: an error, not inf (the cast would be undefined)
                if (isfinite(v) && fabs(v) > FLT_MAX) fail_value(arg, "a number in range of the field", value);
                *(float*)field = (float)v;
                break;
            }
            case ARG_F64:
                if (!value) fail_value(arg, "a number", NULL);
                *(double*)field = parse_real_or_fail(arg, value);
                break;
            case ARG_STRING:
                if (!value) fail_value(arg, "a value", NULL);
                // the field holds its default (an aiml-managed string): release it first
                aiml_str_decref(*(char**)field);
                *(char**)field = aiml_str_dup(value);
                break;
            case ARG_LIST_HANDLE: case ARG_LIST_INLINE: {
                if (!value) fail_value(arg, "a value", NULL);
                void* list = list_of(field, code);           // --name v repeats: each v is appended
                if (list) aiml_list_push_str_copy(list, value, strlen(value));
                break;
            }
            default: break;
        }
        // F23-64: the field was set from the command line (a malformed value exited above)
        if (given >= 0 && recorded && !recorded[fi]) {
            recorded[fi] = 1;
            char* gfield = (char*)struct_ptr + offsets[given];
            void* glist = list_of(gfield, types[given]);
            const char* real = (real_names && real_names[fi]) ? real_names[fi] : names[fi];
            if (glist) aiml_list_push_str_copy(glist, real, strlen(real));
        }
    }
    free(recorded);
}
