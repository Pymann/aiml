#include "schema_registry.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <vector>
#include <string>

namespace AIML {

void emitSchemaRegistry(CompilerContext& ctx) {
    auto& M  = ctx.module;
    auto& C  = ctx.context;
    auto* i8p = llvm::PointerType::get(C, 0);  // opaque pointer (LLVM 18+)
    auto* i32 = llvm::Type::getInt32Ty(C);

    // Collect classes and models (non-builtins with known packed_size)
    std::vector<const CompilerContext::ClassInfo*> schemas;
    std::vector<std::string> schemaNames;

    for (auto& [name, ci] : ctx.classes) {
        if (ci.isBuiltin) continue;
        if (!ci.type)      continue;
        if (ci.padded_size <= 0) continue;
        schemas.push_back(&ci);
        schemaNames.push_back(name);
    }

    int N = (int)schemas.size();
    if (N == 0) {
        // Emit empty count so exec_impl can always read it
        new llvm::GlobalVariable(M, i32, /*isConst=*/true,
            llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantInt::get(i32, 0),
            "__aiml_schema_count");
        return;
    }

    // Build the LLVM struct type mirroring AimlFieldInfo: { i8* name }
    // (no offset/size fields — Append-Only guarantees GEP stability)
    auto* fieldinfo_ty = llvm::StructType::getTypeByName(C, "AimlFieldInfo");
    if (!fieldinfo_ty) {
        std::vector<llvm::Type*> fi_fields = {i8p};
        fieldinfo_ty = llvm::StructType::create(C, fi_fields, "AimlFieldInfo");
    }

    // AimlClassSchema: { i8* name, i32 version, i32 field_count,
    //                    i32 packed_size, i32 padded_size,
    //                    [AIML_MAX_FIELDS x AimlFieldInfo] fields }
    auto* fields_arr_ty = llvm::ArrayType::get(fieldinfo_ty, AIML_MAX_FIELDS);
    auto* schema_ty = llvm::StructType::getTypeByName(C, "AimlClassSchema");
    if (!schema_ty) {
        std::vector<llvm::Type*> sc_fields = {i8p, i32, i32, i32, i32, fields_arr_ty};
        schema_ty = llvm::StructType::create(C, sc_fields, "AimlClassSchema");
    }

    // Array of schemas
    auto* schemas_arr_ty = llvm::ArrayType::get(schema_ty, N);

    std::vector<llvm::Constant*> schema_entries;
    for (int s = 0; s < N; s++) {
        const auto& ci = *schemas[s];
        const std::string& nm = schemaNames[s];

        // name string
        auto* nm_data = llvm::ConstantDataArray::getString(C, nm, true);
        auto* nm_gv   = new llvm::GlobalVariable(M, nm_data->getType(), true,
            llvm::GlobalValue::PrivateLinkage, nm_data,
            "__aiml_sname_" + nm);
        llvm::Constant* name_ptr = llvm::ConstantExpr::getBitCast(nm_gv, i8p);

        // fields array — collect members ordered by index
        std::vector<std::pair<int,std::string>> ordered;
        for (auto& [fname, mbr] : ci.members)
            ordered.push_back({mbr.index, fname});
        std::sort(ordered.begin(), ordered.end());

        std::vector<llvm::Constant*> field_consts;
        for (int fi = 0; fi < AIML_MAX_FIELDS; fi++) {
            llvm::Constant* fname_ptr;
            if (fi < (int)ordered.size()) {
                const std::string& fn = ordered[fi].second;
                auto* fn_data = llvm::ConstantDataArray::getString(C, fn, true);
                auto* fn_gv   = new llvm::GlobalVariable(M, fn_data->getType(), true,
                    llvm::GlobalValue::PrivateLinkage, fn_data,
                    "__aiml_sfield_" + nm + "_" + fn);
                fname_ptr = llvm::ConstantExpr::getBitCast(fn_gv, i8p);
            } else {
                fname_ptr = llvm::ConstantPointerNull::get(i8p);
            }
            field_consts.push_back(llvm::ConstantStruct::get(fieldinfo_ty, {fname_ptr}));
        }

        llvm::Constant* fields_init = llvm::ConstantArray::get(fields_arr_ty, field_consts);

        llvm::Constant* schema_init = llvm::ConstantStruct::get(schema_ty, {
            name_ptr,
            llvm::ConstantInt::get(i32, (uint32_t)ci.version),
            llvm::ConstantInt::get(i32, (int32_t)ordered.size()),
            llvm::ConstantInt::get(i32, ci.packed_size),
            llvm::ConstantInt::get(i32, ci.padded_size),
            fields_init
        });
        schema_entries.push_back(schema_init);
    }

    // Emit @__aiml_schemas
    new llvm::GlobalVariable(M, schemas_arr_ty, /*isConst=*/false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(schemas_arr_ty, schema_entries),
        "__aiml_schemas");

    // Emit @__aiml_schema_count
    new llvm::GlobalVariable(M, i32, /*isConst=*/true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i32, N),
        "__aiml_schema_count");
}

} // namespace AIML
