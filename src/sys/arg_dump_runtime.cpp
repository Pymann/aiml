#include "arg_dump_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// spec/61 §4: string struct fields must be aiml-MANAGED strings (with an AimlStrHeader) so the
// owner's teardown (fre → aiml_str_decref) can read the rc header at (s - 8). A libc strdup'd /
// string-literal value has no header → the decref reads 8 bytes out of bounds.
extern "C" char* aiml_str_dup(const char* s);
extern "C" void  aiml_str_decref(char* s);

// Normalize a CLI flag: strip leading '-' or '--', replace '-' with '_'.
// Writes into buf (caller-owned, must be >= len(arg)+1 bytes).
// Returns buf on success, NULL if arg does not start with '-'.
static const char* normalize_flag(const char* arg, char* buf, int bufsz) {
    if (!arg || arg[0] != '-') return NULL;
    const char* src = arg + 1;
    if (*src == '-') src++;  // strip second -
    int i = 0;
    while (*src && i < bufsz - 1) {
        buf[i++] = (*src == '-') ? '_' : *src;
        src++;
    }
    buf[i] = '\0';
    return (i > 0) ? buf : NULL;
}

// Returns 1 if s is NULL/empty or looks like a flag (starts with '-' not followed by digit).
static int looks_like_flag_or_end(const char* s) {
    if (!s || !s[0]) return 1;
    if (s[0] == '-' && !isdigit((unsigned char)s[1])) return 1;
    return 0;
}

void aiml_arg_dump_impl(void*        struct_ptr,
                        const char** names,
                        const int*   types,
                        const int*   offsets,
                        int          num_fields,
                        const char*  struct_name,
                        int          argc,
                        char**       argv) {
    char norm_buf[512];

    for (int argi = 1; argi < argc; ) {
        const char* arg = argv[argi];
        if (!arg || !arg[0]) { argi++; continue; }

        // Positional argument — skip silently
        if (arg[0] != '-') { argi++; continue; }

        const char* norm = normalize_flag(arg, norm_buf, (int)sizeof(norm_buf));
        if (!norm) { argi++; continue; }

        int found = 0;
        for (int fi = 0; fi < num_fields; fi++) {
            if (strcmp(norm, names[fi]) != 0) continue;
            found = 1;

            char* field_ptr = (char*)struct_ptr + offsets[fi];

            switch (types[fi]) {
                case 0: {  // i32
                    const char* next = (argi + 1 < argc) ? argv[argi + 1] : NULL;
                    if (looks_like_flag_or_end(next)) {
                        *(int*)field_ptr = 1;  // boolean-style flag
                    } else {
                        *(int*)field_ptr = atoi(next);
                        argi++;
                    }
                    break;
                }
                case 1: {  // f32
                    argi++;
                    const char* next = (argi < argc) ? argv[argi] : NULL;
                    if (next && next[0]) *(float*)field_ptr = (float)atof(next);
                    break;
                }
                case 2: {  // string (i8*)
                    argi++;
                    const char* next = (argi < argc) ? argv[argi] : NULL;
                    // The field already holds the model's default value (an aiml-managed "" the compiler
                    // dup'd at construction). Decref it before overwriting, else that default leaks — the
                    // owner's later scope-drop only frees the NEW value (spec/61 §7). Null/non-aiml-safe.
                    aiml_str_decref(*(char**)field_ptr);
                    *(const char**)field_ptr = aiml_str_dup((next && next[0]) ? next : "");
                    break;
                }
                case 3: {  // i64
                    argi++;
                    const char* next = (argi < argc) ? argv[argi] : NULL;
                    if (next && next[0]) *(long long*)field_ptr = atoll(next);
                    break;
                }
            }
            break;
        }

        if (!found) {
            fprintf(stderr, "UnknownArgumentException: unknown flag '%s'", arg);
            if (struct_name && struct_name[0])
                fprintf(stderr, " (expected fields of %s)", struct_name);
            fprintf(stderr, "\n");
            exit(1);
        }
        argi++;
    }
}
