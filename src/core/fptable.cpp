#include "fptable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <algorithm>
#include <set>
#include <vector>
#include <string>

namespace AIML {

void emitFptable(CompilerContext& ctx) {
    // Skip if no user functions were compiled (e.g. header-only mode)
    if (ctx.fptable_next_index == 0) return;

    auto& M   = ctx.module();
    auto& C   = ctx.context;
    auto* i8p = llvm::PointerType::get(C, 0);  // opaque pointer (LLVM 18+)
    auto* i32 = llvm::Type::getInt32Ty(C);

    int N = ctx.fptable_next_index;  // total number of fptable slots

    // Collect unique entries ordered by fptable_index.
    // ctx.functions may store the same function under two keys (qualified + unqualified);
    // we deduplicate by index.
    std::vector<std::pair<int, CompilerContext::FunctionInfo*>> entries(N, {-1, nullptr});
    std::vector<std::string> names(N);

    for (auto& [name, fi] : ctx.functions) {
        int idx = fi.fptable_index;
        if (idx < 0 || idx >= N) continue;
        if (entries[idx].first == -1) {
            entries[idx] = {idx, &fi};
            names[idx]   = name;
        }
    }

    // ── 1. @__aiml_fptable  [N x [AIML_MAX_VERSIONS x i8*]] ─────────────────
    auto* row_ty   = llvm::ArrayType::get(i8p, AIML_MAX_VERSIONS);
    auto* table_ty = llvm::ArrayType::get(row_ty, N);

    std::vector<llvm::Constant*> rows;
    for (int i = 0; i < N; i++) {
        std::vector<llvm::Constant*> slots(AIML_MAX_VERSIONS,
                                           llvm::ConstantPointerNull::get(i8p));
        llvm::Function* fn = entries[i].second ? entries[i].second->func : nullptr;
        if (fn) slots[0] = llvm::ConstantExpr::getBitCast(fn, i8p);
        rows.push_back(llvm::ConstantArray::get(row_ty, slots));
    }
    new llvm::GlobalVariable(M, table_ty, /*isConst=*/false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(table_ty, rows),
        "__aiml_fptable");

    // ── 2. @__aiml_fptable_current  [N x i8*] ───────────────────────────────
    auto* cur_ty = llvm::ArrayType::get(i8p, N);
    std::vector<llvm::Constant*> cur_ptrs;
    for (int i = 0; i < N; i++) {
        llvm::Function* fn = entries[i].second ? entries[i].second->func : nullptr;
        cur_ptrs.push_back(fn
            ? llvm::ConstantExpr::getBitCast(fn, i8p)
            : llvm::ConstantPointerNull::get(i8p));
    }
    new llvm::GlobalVariable(M, cur_ty, /*isConst=*/false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(cur_ty, cur_ptrs),
        "__aiml_fptable_current");

    // ── 3. @__aiml_fptable_versions  [N x i32] ──────────────────────────────
    auto* ver_ty = llvm::ArrayType::get(i32, N);
    std::vector<llvm::Constant*> vers;
    for (int i = 0; i < N; i++) {
        uint64_t v = entries[i].second ? entries[i].second->compiled_version : 1;
        vers.push_back(llvm::ConstantInt::get(i32, (uint32_t)v));
    }
    new llvm::GlobalVariable(M, ver_ty, /*isConst=*/false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(ver_ty, vers),
        "__aiml_fptable_versions");

    // ── 4. @__aiml_func_names  [N x i8*] ────────────────────────────────────
    auto* names_ty = llvm::ArrayType::get(i8p, N);
    std::vector<llvm::Constant*> name_ptrs;
    for (int i = 0; i < N; i++) {
        const std::string& nm = names[i];
        auto* str = llvm::ConstantDataArray::getString(C, nm, /*AddNull=*/true);
        auto* gv  = new llvm::GlobalVariable(M, str->getType(), /*isConst=*/true,
            llvm::GlobalValue::PrivateLinkage, str,
            "__aiml_fname_" + nm);
        name_ptrs.push_back(llvm::ConstantExpr::getBitCast(gv, i8p));
    }
    new llvm::GlobalVariable(M, names_ty, /*isConst=*/true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(names_ty, name_ptrs),
        "__aiml_func_names");

    // ── 5. @__aiml_func_count  i32 ──────────────────────────────────────────
    new llvm::GlobalVariable(M, i32, /*isConst=*/true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i32, N),
        "__aiml_func_count");
}

} // namespace AIML
