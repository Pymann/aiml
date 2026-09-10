#pragma once
// spec/61 §4 — intraprocedural ownership analyzer (Increment B). Produces an OwnershipPlan
// for one function that the codegen (Increment E) consults to decide whether a registered
// scope-drop is provably safe. This first cut is deliberately CONSERVATIVE and fail-safe:
// a slot is `Owned` only when it is declared exactly like the drop-registration cases in
// visitDeclStmt (a fresh heap local) AND is never rebound from a non-fresh source; any doubt
// downgrades it to `Unknown`, which makes mayDrop() return false → the drop is suppressed
// (a leak, never a double-free). It changes NO lowering; it only gates existing drops.
#include "ownership_flow.h"

// context.h defines CompilerContext in the GLOBAL namespace (Symbol/CompilerContext are not
// inside AIML::AST). Forward-declare it there so this header does not shadow the real type.
class CompilerContext;

namespace AIML {
namespace AST {

class FunctionDecl;

// Build the ownership plan for `fn`. `ctx` supplies type info (classes / heap-fields).
OwnershipPlan buildOwnershipPlan(const ::CompilerContext& ctx, const FunctionDecl& fn);

} // namespace AST
} // namespace AIML
