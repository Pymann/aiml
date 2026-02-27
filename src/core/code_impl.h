#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>

// ---------------------------------------------------------------------------
// CodeImpl — runtime support for the AIML built-in `Code` type.
//
// A Code object is heap-allocated and represented as an opaque i8* in AIML.
// The underlying struct (__aiml_code_t) has four fields:
//   field 0: i8*  id          — FNV-1a hex hash of code_chunks content
//   field 1: i8*  start_call  — JSON/YAML call-node string
//   field 2: i8*  code_chunks — pointer to heap-allocated list<string> (definitions)
//   field 3: i8*  data_chunks — pointer to heap-allocated list<string> (state-init)
//
// sys.bundle fills code_chunks with AIML YAML source of the selected function/class
// and all its transitive dependencies (compile-time operation).
// data_chunks carries instance-state initialisation code (decl + sys.deserialize).
//
// All runtime helpers are emitted once per module (saveIP / restoreIP pattern).
// ---------------------------------------------------------------------------
namespace CodeImpl {

// Returns (creates on first call) the __aiml_code_t struct type.
llvm::StructType* getStructType(CompilerContext& ctx);

// Allocate and initialise a new Code object.
// id:          i8*  — hash string
// start_call:  i8*  — JSON call-node (may be empty string)
// code_chunks: i8*  — pointer to heap-alloc'd list<string> struct
// data_chunks: i8*  — pointer to heap-alloc'd list<string> struct
// Returns i8* (raw pointer to __aiml_code_t).
llvm::Value* create(CompilerContext& ctx,
                    llvm::Value* id,
                    llvm::Value* start_call,
                    llvm::Value* code_chunks,
                    llvm::Value* data_chunks);

// Push a string chunk onto code_chunks list.
void push_code(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* chunk);

// Push a string chunk onto data_chunks list.
void push_data(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* chunk);

// bool contains(Code, name: string) — true iff name appears in any code_chunks element.
llvm::Value* contains(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* name);

// i64 size(Code) — strlen(toString(code)).
llvm::Value* size(CompilerContext& ctx, llvm::Value* codePtr);

// string toString(Code) — returns code_chunks + "\n" + data_chunks concatenated.
llvm::Value* toString(CompilerContext& ctx, llvm::Value* codePtr);

// Code merge(a: Code, b: Code) — new Code with a.code_chunks + b.code_chunks and
// a.data_chunks + b.data_chunks. start_call inherited from a.
llvm::Value* merge(CompilerContext& ctx, llvm::Value* aPtr, llvm::Value* bPtr);

} // namespace CodeImpl
