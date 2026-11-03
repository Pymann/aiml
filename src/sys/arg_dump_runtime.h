#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// sys.arg_dump runtime helper (spec/23 §3, F23-61).
//
// Parses argv[1..argc-1] into a struct.  Fields are described by three parallel
// arrays (names, types, offsets) of length num_fields; `types` holds the codes below.
// Unknown flags, a missing or malformed value cause an error message on stderr + exit(1).
// A field name is matched by its spec/15 alias when it has one; real_names holds the field
// names themselves.  A list<string> field whose real name is `given` is never set by a flag:
// it collects the real name of every field the command line set, each once, in order (F23-64),
// so a default and an explicitly given value that happens to equal it stay distinguishable.
enum {
    ARG_I32 = 0, ARG_F32 = 1, ARG_STRING = 2, ARG_I64 = 3, ARG_F64 = 4,
    ARG_I8 = 5, ARG_I16 = 6, ARG_BOOL = 7,
    ARG_LIST_HANDLE = 10,   // list<string> field holding a memV3 list handle
    ARG_LIST_INLINE = 11,   // list<string> field holding the list struct itself
    ARG_U8 = 12, ARG_U16 = 13, ARG_U32 = 14, ARG_U64 = 15,   // unsigned: own range, no sign
    ARG_SKIP = 99           // any other field type: never matched
};

// Emitted by the compiler since F23-64.  aiml_arg_dump_impl (without real_names) stays for
// objects built by an older compiler: it behaves the same, only without the `given` collector.
void aiml_arg_dump_impl2(void*        struct_ptr,
                         const char** names,
                         const int*   types,
                         const int*   offsets,
                         int          num_fields,
                         const char*  struct_name,
                         int          argc,
                         char**       argv,
                         const char** real_names);
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
