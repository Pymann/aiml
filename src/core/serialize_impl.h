#pragma once
#include "context.h"
#include <string>

// ---------------------------------------------------------------------------
// SerializeImpl — yyjson-based sys.serialize / sys.deserialize
//
// Serialisation format:
//   Primitives (i8–u64, f32, f64, bool, string) → plain string ("42","true")
//   Enums       → plain integer string ("0" for Color.Red)
//   Structs / Classes / Models → JSON with __type field:
//       {"__type":"Vec2","x":10,"y":20}
//   list<T>     → {"__type":"list<i32>","values":[1,2,3]}
//   map<string:V> → {"__type":"map<string:i32>","entries":{"k":1}}
//   Nested complex fields in structs: fully recursive sub-objects
//
// ptr<T> semantics (handled by visitSysSerializeCall, not here):
//   serialize  → auto-deref: load the pointer, serialize as T
//   deserialize → return heap i8* directly (same as struct/list/map below)
//
// Unsupported types → isSerializable() returns false → caller emits LOG_ERROR:
//   any, chan<T>, lock, thread_handle, Code, file_handle, process
// ---------------------------------------------------------------------------

namespace SerializeImpl {

/// Returns true if typeName can be (de)serialised.
/// Does NOT emit a LOG_ERROR — callers decide how to report.
bool isSerializable(CompilerContext& ctx, const std::string& typeName);

/// Serialise a value to an i8* string.
///
/// For scalar primitives / enums  → val is the loaded scalar LLVM value.
/// For structs / lists / maps     → val is the alloca / raw struct pointer.
///
/// Always returns llvm::Value* of type i8*.
llvm::Value* serialize(CompilerContext& ctx,
                       llvm::Value*        val,
                       const std::string&  typeName);

/// Deserialise a JSON/plain string to a value of typeName.
///
/// Returns:
///   i1/i8        for bool
///   i32          for i32/u32/i16/u16/i8/u8 and enums
///   i64          for i64/u64
///   float        for f32
///   double       for f64
///   i8*          for string (the string value as-is)
///   ptr          for structs (heap-allocated struct; caller memcpy's if needed)
///   ptr          for list<T> (heap-allocated list struct; caller memcpy's)
///   ptr          for map<K:V> (heap-allocated map struct; caller memcpy's)
llvm::Value* deserialize(CompilerContext& ctx,
                         llvm::Value*        jsonStr,
                         const std::string&  typeName);

} // namespace SerializeImpl
