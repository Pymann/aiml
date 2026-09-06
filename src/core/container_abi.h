#pragma once
// ── Runtime container-header ABI sizes ───────────────────────────────────────
// The serializer stores a NESTED container (the element of list<list<T>>,
// list<buffer<T>>, list<map<...>>) inline as its runtime *header* struct.  Those
// headers embed a raw pointer, so their byte size depends on the TARGET pointer
// width:  64-bit → 16 / 32 / 24,  wasm32 → 12 / 20 / 20.
//
// The compiler runs on the host (always 64-bit) but emits code for the target,
// so these sizes MUST be derived from the target `llvm::DataLayout` — never from
// a host `sizeof(...)`.  Hardcoding the 64-bit values (the old `esz = 16` / `24`)
// silently corrupts nested-container strides on wasm32: the serializer decodes
// with a 16-byte stride while list indexing (getTypeAllocSize) uses 12, so every
// element past index 0 is read misaligned → garbage inner-`data` pointers.
//
// These literal struct shapes MUST mirror SerList / SerMap / SerBuffer in
// src/sys/ser_runtime.cpp (whose static_asserts pin the per-arch sizes).
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>

namespace aiml {

// SerList  { i32 size, i32 capacity, void* data }
inline llvm::StructType* serListHeaderTy(llvm::LLVMContext& c) {
    auto* i32 = llvm::Type::getInt32Ty(c);
    auto* ptr = llvm::PointerType::get(c, 0);
    return llvm::StructType::get(c, {i32, i32, ptr});
}
// SerMap   { i32 size, i32 capacity, i8* occupied, void* keys, void* values }
inline llvm::StructType* serMapHeaderTy(llvm::LLVMContext& c) {
    auto* i32 = llvm::Type::getInt32Ty(c);
    auto* ptr = llvm::PointerType::get(c, 0);
    return llvm::StructType::get(c, {i32, i32, ptr, ptr, ptr});
}
// SerBuffer{ i32 size, i32 cap, i32 read_idx, i32 write_idx, void* data }
inline llvm::StructType* serBufferHeaderTy(llvm::LLVMContext& c) {
    auto* i32 = llvm::Type::getInt32Ty(c);
    auto* ptr = llvm::PointerType::get(c, 0);
    return llvm::StructType::get(c, {i32, i32, i32, i32, ptr});
}

// Target-ABI byte sizes (64-bit → 16/32/24, wasm32 → 12/20/20).
inline int32_t serListHeaderSize(llvm::LLVMContext& c, const llvm::DataLayout& dl) {
    return (int32_t)dl.getTypeAllocSize(serListHeaderTy(c));
}
inline int32_t serMapHeaderSize(llvm::LLVMContext& c, const llvm::DataLayout& dl) {
    return (int32_t)dl.getTypeAllocSize(serMapHeaderTy(c));
}
inline int32_t serBufferHeaderSize(llvm::LLVMContext& c, const llvm::DataLayout& dl) {
    return (int32_t)dl.getTypeAllocSize(serBufferHeaderTy(c));
}

} // namespace aiml
