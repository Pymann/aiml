// spec/61 §10 — conservative, memV3-only borrow/ownership diagnostics.
//
// A SEPARATE diagnostic pass (isolated from ownership_analyzer, which plans drops and runs in
// every mode). This pass runs ONLY under AIML_MEM_V3 and only REPORTS (via LOG_ERROR); it never
// changes codegen or drop decisions, so the OFF/deployed path is completely unaffected.
//
// It is intentionally CONSERVATIVE — it flags a violation only when it is certain (no false
// positives), preferring to miss a case over rejecting valid code. Currently detects, for a
// plain `ptr<T>` LOCAL:
//   • use-after-move  — a read after the ptr was moved (rebind/adopt/return/push/consumed-arg),
//   • use-after-free  — a read after `free`,
//   • double-free     — a second `free` on the same ptr,
// with must-move/must-free merging across if/else (a state is carried past a branch only when it
// holds on every path). Loops/try reset to the pre-block state afterwards.
#pragma once

class CompilerContext;
namespace AIML { namespace AST {
class FunctionDecl;
// Emit spec/61 §10 ownership diagnostics for `fn`. No-op unless ctx.memV3 is set.
void checkBorrowsMemV3(const ::CompilerContext& ctx, const FunctionDecl& fn);
// spec/61 §10 owning-cycle lint (WARNING): a type graph whose owning edges (ptr/shared/container
// elements/nested value models) form a cycle with no ref<T> to break it can leak. Runs once over
// all registered model types. No-op unless ctx.memV3.
void checkOwningCyclesMemV3(const ::CompilerContext& ctx);
} }
