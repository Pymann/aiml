#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>

namespace ChanImpl {

// Channel struct layout (heap-allocated, opaque i8* in AIML):
//   [40 x i8]  mutex     (field 0) — pthread_mutex_t
//   [48 x i8]  cond_put  (field 1) — pthread_cond_t, signalled when space available
//   [48 x i8]  cond_get  (field 2) — pthread_cond_t, signalled when item available
//   i64        capacity  (field 3)
//   i64        size      (field 4)
//   i64        head      (field 5) — next read index
//   i64        tail      (field 6) — next write index
//   i8*        buffer    (field 7) — heap array of i64 elements

llvm::StructType* getStructType(CompilerContext& ctx);

// Auto-init: malloc struct + pthread_mutex_init + 2x cond_init + malloc buffer.
// chanAlloca: alloca holding the i8* channel pointer.
// capacity: i64 value (e.g. builder.getInt64(1) for unbuffered).
void init(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Value* capacity);

// Emit (once) and call __aiml_chan_send_i64(i8* chan, i64 val).
// val is widened/bitcast to i64 as needed.
void send(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Value* val, llvm::Type* elemType);

// Emit (once) and call __aiml_chan_recv_i64(i8* chan) -> i64.
// Result is truncated/bitcast back to elemType.
llvm::Value* recv(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Type* elemType);

} // namespace ChanImpl
