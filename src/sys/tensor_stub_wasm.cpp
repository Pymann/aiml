// WASM-only tensor stubs.
//
// The full tensor runtime (tensor_runtime.cpp) depends on native-only libraries (cblas, OpenSSL,
// mmap) and is therefore excluded from the WASM runtime build (exodus-wasm-runtime.json).  However
// the serializer's AIML_TD_TENSOR walkers (ser_runtime.cpp, round4 §3) reference aiml_tensor_free /
// aiml_tensor_clone unconditionally, so the WASM link needs these symbols to resolve.
//
// Tensors are never materialised in WASM targets (e.g. the 9rooms browser client), so a decoded
// tensor field never reaches these paths at runtime — safe no-op stubs suffice.  If a WASM target
// ever needs real tensors, tensor_runtime.cpp must be ported to a BLAS-free/emscripten backend and
// this stub removed.
#ifdef AIML_WASM_BUILD
extern "C" {

void  aiml_tensor_free(void* /*t*/) { /* no tensors in WASM — nothing to free */ }
void* aiml_tensor_clone(void* /*t*/) { return nullptr; }

}
#endif  // AIML_WASM_BUILD
