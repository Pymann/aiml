// spec/29_2 §14a — `ctx`: the context as a namespace of its own.
//
// A context is a live compiler instance: you give it bundles, it
// keeps what it has compiled, and later bundles see it.  The reason
// is the base cost — a fresh context costs ~96 ms, regardless of
// how little it does (§15).
//
// IMPORTANT: this is NOT a security boundary (F29_2-…e).  Process, address space
// and dynamic symbol table are shared; for foreign code the
// boundary remains a separate process (spec/65 F65-15).
#ifndef AIML_CTX_RUNTIME_H
#define AIML_CTX_RUNTIME_H

#include <cstdint>

extern "C" {

// Open a context.  The return value is the handle; nullptr = it did not work.
void*   aiml_ctx_new(void);

// Free it.  Afterwards all addresses from this context are invalid.
void    aiml_ctx_free(void* handle);

// The compiler instance behind a handle.  The handle itself is an entry
// of the table, NOT the compiler — whoever confuses the two calls methods on
// a foreign object (measured: crash in the first run).
void*   aiml_ctx_compiler(void* handle);

// Execute a bundle in the context (implemented in exec_impl.cpp, where the
// path from the code object to the source text already exists).
int32_t aiml_ctx_exec_code(void* handle, void* code_obj);
// Compile and link in, WITHOUT running (F29_2-…n).
int32_t aiml_ctx_load_code(void* handle, void* code_obj);

// After a run: write the bundle's types into the context's table.
// `exec_impl.cpp` calls it directly after `execInContext` — without that
// `aiml_ctx_symbol` only knows what the LAST bundle compiled.
void    aiml_ctx_record_symbols(void* handle);

// The names that this context carries with exactly this type or this signature
// (F29_2-…i), and the messages of the last run (F29_2-…l).  Both
// fill an aiml list that the caller created.
int32_t aiml_ctx_names_fill(void* handle, const char* want_type, void* list_ptr);
int32_t aiml_ctx_errors_fill(void* handle, void* list_ptr);
int32_t aiml_ctx_use_packages(void* handle, const char* path);

// Grab a symbol — WITH a named type (F29_2-…d).
//   0 = found, `out_addr` carries the address or value
//   1 = name unknown              → SymbolMissingException
//   2 = name present, type wrong  → SymbolTypeException
// `out_msg` then points to the finished reason (static buffer,
// valid until the next call): it names BOTH types, expected and
// found — a "type error" without the two names forces one to search.
int32_t aiml_ctx_symbol(void* handle, const char* name, const char* want_type,
                        int64_t* out_addr, const char** out_msg);

}  // extern "C"
#endif
