#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// sys.arg_dump runtime helper.
//
// Parses argv[1..argc-1] into a struct.
// Fields are described by three parallel arrays (names, types, offsets) of length num_fields.
//
// type codes:
//   0 = i32   — consumed as atoi(next); if next is absent/flag-like, set to 1 (boolean)
//   1 = f32   — consumed as (float)atof(next)
//   2 = string (i8*) — consumed as strdup(next)
//   3 = i64   — consumed as atoll(next)
//
// Positional arguments (not starting with '-') are silently skipped.
// Unknown flags cause an error message on stderr + exit(1).
void aiml_arg_dump_impl(void*        struct_ptr,
                        const char** names,
                        const int*   types,
                        const int*   offsets,
                        int          num_fields,
                        const char*  struct_name,
                        int          argc,
                        char**       argv);

#ifdef __cplusplus
}
#endif
