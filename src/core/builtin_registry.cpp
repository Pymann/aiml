#define DEBUG_MODE
#include "builtin_registry.h"
#include "ast_nodes.h"
#include "buffer_impl.h"
#include "map_impl.h"
#include "list_impl.h"
#include "code_impl.h"
#include "ir_generator.h"
#include "path_runtime.h"
#include "../sys/yaml_runtime.h"
#include "../../src/sys/net_runtime.h"
#include "../../src/sys/db_runtime.h"
#include "asset_scanner.h"
#include "../../src/sys/http_runtime.h"
#include "../../src/sys/compress_runtime.h"
#include "../../src/sys/gltf_runtime.h"
#include "../../src/sys/gl_runtime.h"
#include "../../src/sys/engine_runtime.h"
#include "../../src/sys/input_runtime.h"
#include "../../src/sys/ui_runtime.h"
#include "../../src/sys/tensor_runtime.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

using IRValue = AIML::IRValue;

// ---------------------------------------------------------------------------
// Helpers used by multiple handlers
// ---------------------------------------------------------------------------
static std::string extractTypeName(AIML::AST::Expr* expr) {
    if (auto* v = dynamic_cast<AIML::AST::VariableExpr*>(expr)) return v->name;
    if (auto* l = dynamic_cast<AIML::AST::LiteralExpr*>(expr))   return l->value;
    return "unknown";
}

// Emit an unconditional __cxa_throw at the current insert point.
// The current basic block MUST be terminated after this call.
static void emitThrow(CompilerContext& ctx) {
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocFn = ctx.module.getFunction("__cxa_allocate_exception");
    if (!allocFn) {
        auto* fty = llvm::FunctionType::get(ptrTy, {ctx.builder.getInt64Ty()}, false);
        allocFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_allocate_exception", ctx.module);
    }
    llvm::Function* throwFn = ctx.module.getFunction("__cxa_throw");
    if (!throwFn) {
        auto* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy}, false);
        throwFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_throw", ctx.module);
    }

    llvm::Value* excAlloc = ctx.builder.CreateCall(allocFn, {ctx.builder.getInt64(8)}, "exc_alloc");
    ctx.builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy), excAlloc);
    llvm::Value* null = llvm::ConstantPointerNull::get(ptrTy);

    if (ctx.catchBlock) {
        llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachBB = llvm::BasicBlock::Create(ctx.context, "throw_unreach", fn);
        ctx.builder.CreateInvoke(throwFn, unreachBB, ctx.catchBlock, {excAlloc, null, null});
        ctx.builder.SetInsertPoint(unreachBB);
    } else {
        ctx.builder.CreateCall(throwFn, {excAlloc, null, null});
    }
    ctx.builder.CreateUnreachable();
}

// Emit a conditional check: if `flag` (i8) != 0, throw; else continue in `continueBB`.
// Returns the new insert-point BB (continueBB) with builder already set.
static llvm::BasicBlock* emitGuard(CompilerContext& ctx, llvm::Value* flag,
                                    const std::string& throwLabel,
                                    const std::string& continueLabel) {
    llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
    llvm::Value* isSet = ctx.builder.CreateICmpNE(flag, ctx.builder.getInt8(0), throwLabel + "_cmp");
    llvm::BasicBlock* throwBB    = llvm::BasicBlock::Create(ctx.context, throwLabel, fn);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(ctx.context, continueLabel, fn);
    ctx.builder.CreateCondBr(isSet, throwBB, continueBB);
    ctx.builder.SetInsertPoint(throwBB);
    emitThrow(ctx);
    ctx.builder.SetInsertPoint(continueBB);
    return continueBB;
}

// ---------------------------------------------------------------------------
// registerBuiltins
// ---------------------------------------------------------------------------
void registerBuiltins(CompilerContext& ctx) {
    ctx.enums["yaml"] = {{"null", 0}, {"scalar", 1}, {"sequence", 2}, {"mapping", 3}};

    // -----------------------------------------------------------------------
    // mirror.RpcHandler builtin interface
    //
    // Stand-in for the very common pattern in mirror-based servers:
    //   - class:
    //       name: LoginHandler
    //       implements: [mirror.RpcHandler]
    //       models:     [LoginReq]
    //       methods:
    //         - def:
    //             name: run
    //             args: [[rpc, rpc_event], [session, mirror_session]]
    //             body: [...]
    //
    // The interface contract is `run(rpc_event, mirror_session) -> void`.
    // A user-defined `RpcDispatcher` class then holds a
    // `map<string:ptr<mirror.RpcHandler>>` and dispatches per `rpc.method`.
    //
    // Registering it here saves every project from re-defining the same
    // interface block.  The vtable signature-check + missing-method
    // hard-fail (ast_compiler.cpp visitTopLevel) work unchanged because
    // they look up `cls->interfaces[i]` in `ctx.interfaces` by name.
    // -----------------------------------------------------------------------
    {
        CompilerContext::InterfaceInfo iface;
        iface.name = "mirror.RpcHandler";
        CompilerContext::InterfaceMethod run;
        run.name = "run";
        run.returnType = "void";
        run.args = {{"rpc", "rpc_event"}, {"session", "mirror_session"}};
        iface.methods.push_back(std::move(run));
        ctx.interfaces["mirror.RpcHandler"] = std::move(iface);
    }

    // -----------------------------------------------------------------------
    // VecResult class — real LLVM struct {i8*, f32} so that r.id / r.score
    // work via resolveProperty's GEP path. Must be registered before db namespace.
    // -----------------------------------------------------------------------
    {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* vrTy = llvm::StructType::getTypeByName(ctx.context, "VecResult");
        if (!vrTy) {
            vrTy = llvm::StructType::create(ctx.context,
                {i8p, ctx.builder.getFloatTy()}, "VecResult");
        }
        CompilerContext::ClassInfo vrInfo;
        vrInfo.name      = "VecResult";
        vrInfo.type      = vrTy;
        vrInfo.isBuiltin = false;

        CompilerContext::StructMember idMember;
        idMember.index    = 0;
        idMember.type     = i8p;
        idMember.typeName = "string";

        CompilerContext::StructMember scoreMember;
        scoreMember.index    = 1;
        scoreMember.type     = ctx.builder.getFloatTy();
        scoreMember.typeName = "f32";

        vrInfo.members["id"]    = idMember;
        vrInfo.members["score"] = scoreMember;
        ctx.classes["VecResult"] = std::move(vrInfo);
    }

    // -----------------------------------------------------------------------
    // JsonStyle builtin class for sys.serialize formatting
    // Layout: { i32 pretty, ptr indent, ptr newline }
    // -----------------------------------------------------------------------
    {
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::StructType* jsTy = llvm::StructType::getTypeByName(ctx.context, "JsonStyle");
        if (!jsTy) {
            jsTy = llvm::StructType::create(ctx.context, {i32, p, p}, "JsonStyle");
        }
        CompilerContext::ClassInfo jsInfo;
        jsInfo.name = "JsonStyle"; jsInfo.type = jsTy; jsInfo.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        jsInfo.members["pretty"]  = mk(i32, 0, "i32");
        jsInfo.members["indent"]  = mk(p,   1, "string");
        jsInfo.members["newline"] = mk(p,   2, "string");
        // spec/61 §7/§6.1: JsonStyle was already registered (context.h) and registerTypes built its
        // heap-field RELEASE metadata (hasHeapFields + serNodes) before compile→registerBuiltins runs.
        // Overwriting the ClassInfo wholesale dropped that metadata (new entry defaults
        // hasHeapFields=false, serNodesVar=null) → the model scope-drop for its owned `indent`/
        // `newline` string fields was never registered → 21 B leaked per `set style.indent/.newline`
        // (leak_probes/round6 §2). Preserve the built descriptors when re-registering.
        if (auto ex = ctx.classes.find("JsonStyle"); ex != ctx.classes.end()) {
            jsInfo.hasHeapFields = ex->second.hasHeapFields;
            jsInfo.serNodesVar   = ex->second.serNodesVar;
            jsInfo.serNodesCount = ex->second.serNodesCount;
        }
        ctx.classes["JsonStyle"] = std::move(jsInfo);
    }

    // -----------------------------------------------------------------------
    // zip_entry — archive entry struct for compress.zip_entries
    // Layout: { ptr name, i64 size, i64 compressed_size, i32 is_dir, i32 crc32, i32 method, i32 _pad }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::StructType* zeTy = llvm::StructType::getTypeByName(ctx.context, "zip_entry");
        if (!zeTy) zeTy = llvm::StructType::create(ctx.context, {p, i64, i64, i32, i32, i32, i32}, "zip_entry");

        CompilerContext::ClassInfo zi;
        zi.name = "zip_entry"; zi.type = zeTy; zi.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        zi.members["name"]            = mk(p,   0, "string");
        zi.members["size"]            = mk(i64, 1, "i64");
        zi.members["compressed_size"] = mk(i64, 2, "i64");
        zi.members["is_dir"]          = mk(i32, 3, "i32");
        zi.members["crc32"]           = mk(i32, 4, "i32");
        zi.members["method"]          = mk(i32, 5, "i32");
        ctx.classes["zip_entry"] = std::move(zi);
    }

    // -----------------------------------------------------------------------
    // tar_entry — archive entry struct for compress.tar_entries
    // Layout: { ptr name, i64 size, i32 is_dir, i32 mode }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::StructType* teTy = llvm::StructType::getTypeByName(ctx.context, "tar_entry");
        if (!teTy) teTy = llvm::StructType::create(ctx.context, {p, i64, i32, i32}, "tar_entry");

        CompilerContext::ClassInfo ti;
        ti.name = "tar_entry"; ti.type = teTy; ti.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        ti.members["name"]   = mk(p,   0, "string");
        ti.members["size"]   = mk(i64, 1, "i64");
        ti.members["is_dir"] = mk(i32, 2, "i32");
        ti.members["mode"]   = mk(i32, 3, "i32");
        ctx.classes["tar_entry"] = std::move(ti);
    }

    // -----------------------------------------------------------------------
    // cascade namespace  —  unit-testable helpers for cascade wire format
    // -----------------------------------------------------------------------
    {
        auto& cas = ctx.namespaces["cascade"];
        // cascade.persist_row(outer: list<list<i32>>, bound_index: i32,
        //                     tag: i32,               values: list<i32>)
        // Grows the row at bound_index to tag + n_values slots, then writes:
        //     row[0]      = tag
        //     row[1..N]   = values[0..N-1]
        // Standalone from the widget runtime so it can be exercised headless.
        cas.builtinFuncs["persist_row"] = [](CompilerContext& ctx,
                                             const std::vector<IRValue*>& args) -> IRValue* {
            auto* vd  = ctx.builder.getVoidTy();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 4) return ctx.ir_gen->getInt32(0);
            llvm::Value* outer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* tag   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* vals  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            /* args[3] may arrive as either a struct-by-value (loaded from
             * the local list<i32> alloca) or already a pointer.  If we got
             * a struct, spill it to a fresh alloca first so we can GEP on
             * a pointer.  outer (args[0]) is a list<list<i32>> which reads
             * as a struct pointer; also normalise the same way. */
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            auto normalise = [&](llvm::Value* v, const char* name) -> llvm::Value* {
                if (v->getType()->isPointerTy()) return v;
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(v->getType(), nullptr, name);
                ctx.builder.CreateStore(v, al);
                return al;
            };
            llvm::Value* outerP = normalise(outer, "outer_spill");
            llvm::Value* valsP  = normalise(vals,  "vals_spill");
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, valsP, 0, "vals_size_gep");
            llvm::Value* nVals   = ctx.builder.CreateLoad(i32, sizeGep, "vals_size");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, valsP, 2, "vals_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p,  dataGep, "vals_data");
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_persist_row", vd,
                                {i8p, i32, i32, i8p, i32}),
                {outerP, idx, tag, dataP, nVals});
            return ctx.ir_gen->getInt32(0);
        };

        // cascade.validate(name: string, row: list<i32>) → i32
        //   -1 valid | >=0 first bad index | -2 unknown cascade.
        // cascade.explain(name: string, row: list<i32>) → string  (reason / "").
        // Both extract row's (data_ptr, size) from the list struct (size@0,
        // data@2) and forward to the aiml_cascade_* runtime.
        auto cascadeCheck = [](CompilerContext& ctx, const std::vector<IRValue*>& args,
                               const char* fnName, llvm::Type* retTy) -> llvm::Value* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));   // string → i8p
            llvm::Value* row  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));   // list<i32>
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* rowP = row;
            if (!row->getType()->isPointerTy()) {
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(row->getType(), nullptr, "row_spill");
                ctx.builder.CreateStore(row, al);
                rowP = al;
            }
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, rowP, 0, "row_size_gep");
            llvm::Value* len     = ctx.builder.CreateLoad(i32, sizeGep, "row_len");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, rowP, 2, "row_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p, dataGep, "row_data");
            return ctx.builder.CreateCall(ctx.getFunction(fnName, retTy, {i8p, i8p, i32}),
                                          {name, dataP, len});
        };

        cas.builtinFuncs["validate"] = [cascadeCheck](CompilerContext& ctx,
                                        const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return ctx.ir_gen->getInt32(-2);
            return ctx.ir_gen->wrap(cascadeCheck(ctx, args, "aiml_cascade_validate",
                                                 ctx.builder.getInt32Ty()));
        };
        cas.builtinFuncs["explain"] = [cascadeCheck](CompilerContext& ctx,
                                       const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            return ctx.ir_gen->wrap(cascadeCheck(ctx, args, "aiml_cascade_explain", i8p));
        };

        // cascade.field_indices(name: string, row: list<i32>, field_type: string)
        //     → list<i32>
        //   Row indices of every payload field of type `field_type` in the node
        //   `row` selects (e.g. "TargetType" in an Effect row — a type may recur).
        //   Empty list = the field does not occur in that node.  Allocates a fresh
        //   list<i32>, fills it via the runtime, returns it (io.list_dir pattern).
        cas.builtinFuncs["field_indices"] = [](CompilerContext& ctx,
                                              const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // Fresh list<i32> for the result. spec/61 §5 (memV3): a `list<i32>` binding is a
            // refcounted HEAP HANDLE freed via aiml_list_handle_release — returning a pointer to a
            // STACK alloca made the caller's local own stack memory, so its scope-drop released a
            // stack pointer (garbage rc → crash: do_deal_damage's eff_pos → field_indices, the
            // test_creature_damage/effect rc134 cluster). Mirror the `.split` fix.
            llvm::Value* outAlloca;
            if (ctx.memV3) {
                outAlloca = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_fidx_h");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            } else {
                outAlloca = ctx.createEntryAlloca(listTy, "casc_fidx_out");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            }
            if (args.size() < 3) return ctx.ir_gen->wrap(outAlloca);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* row  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* ftyp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            // Extract row's (data_ptr, size).
            llvm::Value* rowP = row;
            if (!row->getType()->isPointerTy()) {
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(row->getType(), nullptr, "row_spill");
                ctx.builder.CreateStore(row, al);
                rowP = al;
            }
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, rowP, 0, "row_size_gep");
            llvm::Value* len     = ctx.builder.CreateLoad(i32, sizeGep, "row_len");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, rowP, 2, "row_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p, dataGep, "row_data");
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_field_indices", i32, {i8p, i8p, i32, i8p, ptrTy}),
                {name, dataP, len, ftyp, outAlloca});
            return ctx.ir_gen->wrap(outAlloca);
        };

        // cascade.count(name: string, start_op?: i32, end_op?: i32) → i64
        //   Total number of enumerable rows: opcodes × enum-members × nested
        //   sub-opcodes (value/alias fields = 1 representative).  Optional
        //   start_op/end_op bound the TOP-LEVEL opcode ids (defaults = all).
        //   Saturates at i64 max instead of wrapping.
        cas.builtinFuncs["count"] = [](CompilerContext& ctx,
                                       const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i64 = ctx.builder.getInt64Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto opArg = [&](size_t i, int32_t dflt) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(dflt);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType() != i32) v = ctx.builder.CreateSExtOrTrunc(v, i32);
                return v;
            };
            llvm::Value* lo = opArg(1, (-2147483647 - 1));
            llvm::Value* hi = opArg(2, 2147483647);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_row_count", i64, {i8p, i32, i32}),
                {name, lo, hi}));
        };

        // cascade.row_at(name: string, k: i64, start_op?: i32, end_op?: i32)
        //     → list<i32>   (the k-th enumerated row; empty if k out of range).
        //   Mixed-radix decode — O(depth), no materialisation.  Loop k in
        //   [0, cascade.count(...)) to stream every row.
        cas.builtinFuncs["row_at"] = [](CompilerContext& ctx,
                                        const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i64   = ctx.builder.getInt64Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* outAlloca;
            if (ctx.memV3) {
                outAlloca = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_row_h");
                ListImpl::initList(ctx, outAlloca, i32, 8);
            } else {
                outAlloca = ctx.createEntryAlloca(listTy, "casc_row_out");
                ListImpl::initList(ctx, outAlloca, i32, 8);
            }
            if (args.size() < 2) return ctx.ir_gen->wrap(outAlloca);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* k    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (k->getType() != i64) k = ctx.builder.CreateSExtOrTrunc(k, i64);
            auto opArg = [&](size_t i, int32_t dflt) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(dflt);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType() != i32) v = ctx.builder.CreateSExtOrTrunc(v, i32);
                return v;
            };
            llvm::Value* lo = opArg(2, (-2147483647 - 1));
            llvm::Value* hi = opArg(3, 2147483647);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_row_at", i32, {i8p, i64, i32, i32, ptrTy}),
                {name, k, lo, hi, outAlloca});
            return ctx.ir_gen->wrap(outAlloca);
        };

        // cascade.type_range(type: string) → list<i32>
        //   Mögliche Werte eines Feld-Typs: enum→Member-Werte, cascade→Sub-Node-
        //   IDs, value/alias→leer.  Serialisierbar (sys.serialize).
        cas.builtinFuncs["type_range"] = [](CompilerContext& ctx,
                                            const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* out;
            if (ctx.memV3) {
                out = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_range_h");
                ListImpl::initList(ctx, out, i32, 8);
            } else {
                out = ctx.createEntryAlloca(listTy, "casc_range_out");
                ListImpl::initList(ctx, out, i32, 8);
            }
            if (args.empty()) return ctx.ir_gen->wrap(out);
            llvm::Value* ty = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_type_range", i32, {i8p, ptrTy}), {ty, out});
            return ctx.ir_gen->wrap(out);
        };

        // cascade.describe(cascade: string, opcodes?: list<i32>) → list<string>
        //   Datentyp-Kombinationen der Opcodes (opcodes leer/fehlt → ALLE).
        //   Element 0 = Opcode-Enum-Name (z.B. "EffectCode"); danach je Opcode
        //   eine Zeile "[id]node [range]Type …".
        cas.builtinFuncs["describe"] = [](CompilerContext& ctx,
                                          const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* out;
            if (ctx.memV3) {
                out = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_desc_h");
                ListImpl::initList(ctx, out, i8p, 8);
            } else {
                out = ctx.createEntryAlloca(listTy, "casc_desc_out");
                ListImpl::initList(ctx, out, i8p, 8);
            }
            if (args.empty()) return ctx.ir_gen->wrap(out);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // Optionale Opcode-list<i32> → (data, len); fehlt → (null, 0) = alle.
            llvm::Value* opD = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* opN = ctx.builder.getInt32(0);
            if (args.size() >= 2) {
                llvm::Value* ol = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                llvm::Value* olP = ol;
                if (!ol->getType()->isPointerTy()) {
                    llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                          ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                    llvm::AllocaInst* al = tmp.CreateAlloca(ol->getType(), nullptr, "opc_spill");
                    ctx.builder.CreateStore(ol, al);
                    olP = al;
                }
                llvm::Value* szGep = ctx.builder.CreateStructGEP(listTy, olP, 0, "opc_size_gep");
                opN = ctx.builder.CreateLoad(i32, szGep, "opc_len");
                llvm::Value* dGep = ctx.builder.CreateStructGEP(listTy, olP, 2, "opc_data_gep");
                opD = ctx.builder.CreateLoad(i8p, dGep, "opc_data");
            }
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_describe", i32, {i8p, i8p, i32, ptrTy}),
                {name, opD, opN, out});
            return ctx.ir_gen->wrap(out);
        };
    }

    // -----------------------------------------------------------------------
    // enums namespace — compile-time enum reflection (spec/14_enums.md)
    //   enums.name(type: string-literal, value: i32)      → string  (value → member name)
    //   enums.value(type: string-literal, member: string) → i32     (member name → value)
    // Resolved from CompilerContext at compile time; enums.name lowers to a
    // select-chain (no runtime table).  enums.name(T,…) marks the WHOLE enum T
    // referenced for the unused-enumerator analysis (spec/49 §18).
    // -----------------------------------------------------------------------
    {
        auto& en = ctx.namespaces["enums"];

        // Extract a compile-time string literal from an evaluated arg (a global
        // string constant), or "" if the value is not a literal.
        auto litStr = [](llvm::Value* v) -> std::string {
            if (!v) return {};
            v = v->stripPointerCasts();
            if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(v)) {
                if (gv->hasInitializer()) {
                    if (auto* cda = llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer())) {
                        std::string s = cda->getAsString().str();
                        if (!s.empty() && s.back() == '\0') s.pop_back();
                        return s;
                    }
                }
            }
            return {};
        };

        // Resolve an enum name to its (registry key, member-map); tries the bare
        // name and the caller-qualified name.  nullptr if unknown.
        auto findEnum = [](CompilerContext& ctx, const std::string& typeName,
                           std::string& outKey) -> std::map<std::string,int>* {
            auto it = ctx.enums.find(typeName);
            if (it != ctx.enums.end()) { outKey = it->first; return &it->second; }
            it = ctx.enums.find(ctx.qualify(typeName));
            if (it != ctx.enums.end()) { outKey = it->first; return &it->second; }
            return nullptr;
        };

        en.builtinFuncs["name"] = [litStr, findEnum](CompilerContext& ctx,
                                    const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: expected (type, value)");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            std::string typeName = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            if (typeName.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: 'type' argument must be a string literal");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            std::string key;
            auto* em = findEnum(ctx, typeName, key);
            if (!em) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: unknown enum type '" + typeName + "'");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            // Reflective whole-enum use (spec/49 §18): mark every member referenced.
            for (const auto& kv : *em) ctx.enumUsedMembers[key].insert(kv.first);
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (val->getType() != ctx.builder.getInt32Ty())
                val = ctx.builder.CreateZExtOrTrunc(val, ctx.builder.getInt32Ty());
            // Select-chain: value → member name, default "".
            llvm::Value* result = ctx.builder.CreateGlobalStringPtr("");
            for (const auto& kv : *em) {
                llvm::Value* cond = ctx.builder.CreateICmpEQ(val, ctx.builder.getInt32(kv.second));
                llvm::Value* nm   = ctx.builder.CreateGlobalStringPtr(kv.first);
                result = ctx.builder.CreateSelect(cond, nm, result);
            }
            return ctx.ir_gen->wrap(result);
        };

        en.builtinFuncs["value"] = [litStr, findEnum](CompilerContext& ctx,
                                     const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: expected (type, member)");
                return ctx.ir_gen->getInt32(0);
            }
            std::string typeName = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            std::string member   = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])));
            if (typeName.empty() || member.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: 'type' and 'member' must be string literals");
                return ctx.ir_gen->getInt32(0);
            }
            std::string key;
            auto* em = findEnum(ctx, typeName, key);
            if (!em) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: unknown enum type '" + typeName + "'");
                return ctx.ir_gen->getInt32(0);
            }
            auto mi = em->find(member);
            if (mi == em->end()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: enum '" + typeName + "' has no member '" + member + "'");
                return ctx.ir_gen->getInt32(0);
            }
            ctx.enumUsedMembers[key].insert(member);
            return ctx.ir_gen->getInt32(mi->second);
        };
    }

    // -----------------------------------------------------------------------
    // sys namespace
    // -----------------------------------------------------------------------
    auto& sys = ctx.namespaces["sys"];

    // null_ptr() → ptr  —  returns a typed null pointer (i8*); useful for FFI
    // calls that require a NULL argument (e.g. glfwCreateWindow monitor/share).
    sys.builtinFuncs["null_ptr"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        auto* i8p = static_cast<llvm::PointerType*>(llvm::PointerType::get(ctx.context, 0));
        return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
    };

    sys.builtinFuncs["exit"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Function* f = ctx.getFunction("exit", ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()});
        llvm::Value* code = args.empty() ? ctx.builder.getInt32(0)
                                         : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        ctx.builder.CreateCall(f, {code});
        return ctx.ir_gen->getInt32(0);
    };

    // sys.mem_limit(bytes_i64) — set process virtual memory limit (RLIMIT_AS).
    // Process is terminated when exceeded instead of freezing the machine.
    sys.builtinFuncs["mem_limit"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_limit",
            ctx.builder.getVoidTy(), {ctx.builder.getInt64Ty()});
        llvm::Value* bytes = args.empty() ? ctx.builder.getInt64(0)
                                          : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        // Widen i32 to i64 if needed
        if (bytes->getType()->isIntegerTy(32))
            bytes = ctx.builder.CreateSExt(bytes, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(f, {bytes});
        return ctx.ir_gen->getInt32(0);
    };

    // sys.mem_usage() → i64 — current RSS (resident set size) in bytes.
    sys.builtinFuncs["mem_usage"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_usage",
            ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    // sys.mem_diff() → i64 — AIML_MEM_DIFF: print the live-allocation diff (top sites by growth
    // since the last call, symbolized by function) to stderr and return total live bytes. No-op
    // returning 0 when the AIML_MEM_DIFF env var is unset. Call it from the periodic [mem] tick to
    // watch what accumulates. See spec/48.
    sys.builtinFuncs["mem_diff"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_diff",
            ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    sys.builtinFuncs["env"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        // Delegate to the runtime, which populates a map<string:string> from
        // `environ`.  The returned AimlHttpMap* shares map<K:V>'s memory layout,
        // so AIML map methods (get/contains/…) operate on it transparently.
        llvm::Type* strTy = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
            ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(),
            ctx.builder.getInt8Ty()->getPointerTo(),
            strTy->getPointerTo(), strTy->getPointerTo()
        });
        llvm::Function* f = ctx.getFunction("aiml_sys_env",
            ctx.builder.getInt8Ty()->getPointerTo(), {});
        llvm::Value* raw = ctx.builder.CreateCall(f);
        llvm::Value* mapPtr = ctx.builder.CreateBitCast(raw, mapTy->getPointerTo(), "sys_env_map");
        return ctx.ir_gen->wrap(mapPtr);
    };

    sys.builtinFuncs["size"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(args[0]->getType()));
        llvm::DataLayout dl(&ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.getInt64(dl.getTypeAllocSize(ty)));
    };

    sys.builtinFuncs["exe"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Value* argvList = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (argvList->getType()->isStructTy()) {
            llvm::AllocaInst* tmp = ctx.createEntryAlloca(argvList->getType());
            ctx.builder.CreateStore(argvList, tmp);
            argvList = ctx.builder.CreateBitCast(tmp, i8p);
        } else if (argvList->getType()->isPointerTy()) {
            argvList = ctx.builder.CreateBitCast(argvList, i8p);
        } else {
            return ctx.ir_gen->getInt32(0);
        }
        llvm::Value* nul  = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Function* f = ctx.getFunction("sys_exe_runtime", i8p,
            {i8p, i8p, i8p, ctx.builder.getInt1Ty(), ctx.builder.getInt32Ty(), ctx.builder.getInt1Ty()});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
            {argvList, nul, nul, ctx.builder.getInt1(false), ctx.builder.getInt32(-1), ctx.builder.getInt1(false)}));
    };

    // sys.get_arg uses a special outlined helper — registered separately because
    // it needs the __argc/__argv globals (eagerly evaluated index passed as arg).
    sys.builtinFuncs["get_arg"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->createGlobalStringPtr("");
        llvm::Value* indexVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
        llvm::GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
        if (!argvVar || !argcVar) return ctx.ir_gen->createGlobalStringPtr("");

        llvm::Function* impl = ctx.module.getFunction("__aiml_sys_get_arg");
        if (!impl) {
            llvm::PointerType* i8p    = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::PointerType* i8pp   = i8p->getPointerTo();
            llvm::Type* i32           = ctx.builder.getInt32Ty();
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8p, {i32}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_sys_get_arg", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(ctx.context, "entry",     impl);
            llvm::BasicBlock* safeBB  = llvm::BasicBlock::Create(ctx.context, "arg_safe",  impl);
            llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(ctx.context, "arg_error", impl);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "arg_merge", impl);
            ctx.builder.SetInsertPoint(entryBB);
            llvm::Value* idxArg  = impl->getArg(0);
            llvm::Value* argvPtr = ctx.builder.CreateLoad(i8pp, argvVar);
            llvm::Value* argcVal = ctx.builder.CreateLoad(i32, argcVar);
            llvm::Value* argvIsNull = ctx.builder.CreateICmpEQ(argvPtr, llvm::ConstantPointerNull::get(i8pp));
            llvm::Value* idxOk   = ctx.builder.CreateICmpSLT(idxArg, argcVal);
            llvm::Value* isSafe  = ctx.builder.CreateAnd(ctx.builder.CreateNot(argvIsNull), idxOk);
            ctx.builder.CreateCondBr(isSafe, safeBB, errorBB);
            ctx.builder.SetInsertPoint(safeBB);
            llvm::Value* argStrPtr = ctx.builder.CreateInBoundsGEP(i8p, argvPtr, idxArg);
            llvm::Value* argStr    = ctx.builder.CreateLoad(i8p, argStrPtr);
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(errorBB);
            llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* result = ctx.builder.CreatePHI(i8p, 2);
            result->addIncoming(argStr, safeBB);
            result->addIncoming(emptyStr, errorBB);
            ctx.builder.CreateRet(result);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(impl, {indexVal}));
    };

    // sys.load_lib(path_str) → ptr   (dlopen wrapper)
    sys.builtinFuncs["load_lib"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p  = llvm::PointerType::get(ctx.context, 0);
        auto* i32  = ctx.builder.getInt32Ty();
        // Declare dlopen if needed: void* dlopen(const char*, int)
        llvm::Function* dlopen_fn = ctx.module.getFunction("dlopen");
        if (!dlopen_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i32}, false);
            dlopen_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlopen", ctx.module);
        }
        llvm::Value* pathArg = args.empty()
            ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        // RTLD_LAZY = 1
        llvm::Value* handle = ctx.builder.CreateCall(dlopen_fn, {pathArg, ctx.builder.getInt32(1)});
        return ctx.ir_gen->wrap(handle);
    };

    // sys.load_sym(handle: ptr, name: string) → ptr   (dlsym wrapper)
    sys.builtinFuncs["load_sym"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        // Declare dlsym if needed: void* dlsym(void*, const char*)
        llvm::Function* dlsym_fn = ctx.module.getFunction("dlsym");
        if (!dlsym_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i8p}, false);
            dlsym_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlsym", ctx.module);
        }
        llvm::Value* handle = args.size() > 0
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Value* name = args.size() > 1
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            : ctx.builder.CreateGlobalStringPtr("");
        llvm::Value* sym = ctx.builder.CreateCall(dlsym_fn, {handle, name});
        return ctx.ir_gen->wrap(sym);
    };

    // -----------------------------------------------------------------------
    // io namespace
    // -----------------------------------------------------------------------
    auto& io = ctx.namespaces["io"];

    io.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Function* f = ctx.getFunction("fopen", i8p, {i8p, i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
    };

    io.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* f = ctx.getFunction("fclose",
            ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
            {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
    };

    io.builtinFuncs["write"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* text   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_write_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_write_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_write",          parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* strlenF = ctx.getFunction("strlen", ctx.getSizeType(), {i8p});
        llvm::Value* len = ctx.builder.CreateCall(strlenF, {text});
        llvm::Function* fwF = ctx.getFunction("fwrite", ctx.getSizeType(),
            {i8p, ctx.getSizeType(), ctx.getSizeType(), i8p});
        llvm::Value* written = ctx.builder.CreateCall(fwF, {text, ctx.getSizeConst(1), len, handle});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.getSizeType(), 2, "write_res");
        phi->addIncoming(ctx.getSizeConst(0), invalidBB);
        phi->addIncoming(written, validBB);
        return ctx.ir_gen->wrap(ctx.builder.CreateZExtOrTrunc(phi, ctx.builder.getInt32Ty()));
    };

    io.builtinFuncs["read"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read",     parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Value* size = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (size->getType()->isIntegerTy(32)) size = ctx.builder.CreateZExt(size, ctx.builder.getInt64Ty());
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.builder.getInt64(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.builder.getInt64Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.builder.getInt64(1), size, handle});
        ctx.builder.CreateStore(ctx.builder.getInt8(0),
            ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buf, nread));
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(i8p, 2, "read_res");
        phi->addIncoming(emptyStr, invalidBB);
        phi->addIncoming(buf, doReadBB);
        return ctx.ir_gen->wrap(phi);
    };

    io.builtinFuncs["read_all"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read_all", parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* fseekF = ctx.getFunction("fseek", ctx.builder.getInt32Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt32Ty()});
        llvm::Function* ftellF = ctx.getFunction("ftell", ctx.builder.getInt64Ty(), {i8p});
        llvm::Function* rewindF = ctx.getFunction("rewind", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(fseekF, {handle, ctx.builder.getInt64(0), ctx.builder.getInt32(2)});
        llvm::Value* size = ctx.builder.CreateCall(ftellF, {handle});
        ctx.builder.CreateCall(rewindF, {handle});
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read_all", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.builder.getInt64(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.builder.getInt64Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.builder.getInt64(1), size, handle});
        ctx.builder.CreateStore(ctx.builder.getInt8(0),
            ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buf, nread));
        // spec/61 §4: return an aiml-MANAGED string (with an AimlStrHeader), not a bare malloc buffer —
        // the caller binds the result to a managed string local whose assign/drop calls
        // aiml_str_incref/decref, which read the rc header at (s - 8). A header-less malloc buffer makes
        // those touch 8 bytes out of bounds (valgrind Invalid read; a chance magic match would corrupt).
        // Copy into an aiml string, then free the raw buffer.
        llvm::Function* strDupF = ctx.getFunction("aiml_str_dup", i8p, {i8p});
        llvm::Function* freeF   = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
        llvm::Value* managed = ctx.builder.CreateCall(strDupF, {buf}, "read_all_managed");
        ctx.builder.CreateCall(freeF, {buf});
        ctx.builder.CreateBr(mergeBB);
        // Managed empty string for the invalid/empty path (same header contract as `managed`).
        ctx.builder.SetInsertPoint(invalidBB->getTerminator());  // (invalidBB already branches to merge)
        llvm::Value* emptyManaged = ctx.builder.CreateCall(strDupF, {emptyStr}, "read_all_empty_managed");
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(i8p, 2, "read_all_res");
        phi->addIncoming(emptyManaged, invalidBB);
        phi->addIncoming(managed, doReadBB);
        // spec/61 §7: the result is a FRESH owned aiml string → register as an owned stmt-temp so a
        // binding local MOVES it (freed by its scope-drop) instead of the decl-init treating it as a
        // borrow and increfing it (→ leak). Same as the yaml/str string builtins.
        if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
            ctx.stmtTempStack.back().push_back(phi);
        return ctx.ir_gen->wrap(phi);
    };

    io.builtinFuncs["open_binary"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Function* fn = ctx.getFunction("aiml_io_open_binary", i8p, {i8p});
        llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {path}));
    };

    io.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_close", ctx.builder.getVoidTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        ctx.builder.CreateCall(fn, {f});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["tell"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_tell", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["seek"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_seek", ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt64Ty()});
        llvm::Value* f   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* pos = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (pos->getType()->isIntegerTy(32)) pos = ctx.builder.CreateSExt(pos, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(fn, {f, pos});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["skip"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_skip", ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt64Ty()});
        llvm::Value* f     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* delta = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (delta->getType()->isIntegerTy(32)) delta = ctx.builder.CreateSExt(delta, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(fn, {f, delta});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["eof"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(1);
        llvm::Function* fn = ctx.getFunction("aiml_io_eof", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_u8"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_u8", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_bool"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_bool", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_i32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_i32_le", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_u32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_u32_le", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_i64_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_i64_le", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_f32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_f32_le", ctx.builder.getFloatTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_f64_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getDoubleTy(), 0.0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_f64_le", ctx.builder.getDoubleTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_cstring"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_cstring", i8p, {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    // io.read_bytes(f, n) → buffer<u8>  (heap AimlBinBuffer* → stack alloca via memcpy)
    io.builtinFuncs["read_bytes"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* bt  = ctx.getLLVMType("buffer");
        if (args.size() < 2) {
            auto* z = ctx.createEntryAlloca(bt, "binbuf_empty");
            ctx.builder.CreateMemSet(z, ctx.builder.getInt8(0), ctx.module.getDataLayout().getTypeAllocSize(bt), llvm::MaybeAlign());
            return ctx.ir_gen->wrap(z);
        }
        llvm::Function* fn = ctx.getFunction("aiml_io_read_bytes", i8p, {i8p, i32});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* n = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (n->getType()->isIntegerTy(64)) n = ctx.builder.CreateTrunc(n, i32);
        llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {f, n});
        llvm::Function* mcf  = ctx.getFunction("memcpy", i8p, {i8p, i8p, sz});
        auto* out = ctx.createEntryAlloca(bt, "binbuf_alloca");
        uint64_t szBytes = ctx.module.getDataLayout().getTypeAllocSize(bt);
        ctx.builder.CreateCall(mcf, {out, heapPtr, ctx.getSizeConst(szBytes)});
        return ctx.ir_gen->wrap(out);
    };

    // io.read_at(f, offset, n) → buffer<u8>
    io.builtinFuncs["read_at"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* bt  = ctx.getLLVMType("buffer");
        if (args.size() < 3) {
            auto* z = ctx.createEntryAlloca(bt, "binbuf_empty");
            ctx.builder.CreateMemSet(z, ctx.builder.getInt8(0), ctx.module.getDataLayout().getTypeAllocSize(bt), llvm::MaybeAlign());
            return ctx.ir_gen->wrap(z);
        }
        llvm::Function* fn = ctx.getFunction("aiml_io_read_at", i8p, {i8p, i64, i32});
        llvm::Value* f      = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* offset = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Value* n      = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
        if (offset->getType()->isIntegerTy(32)) offset = ctx.builder.CreateSExt(offset, i64);
        if (n->getType()->isIntegerTy(64))      n      = ctx.builder.CreateTrunc(n, i32);
        llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {f, offset, n});
        llvm::Function* mcf  = ctx.getFunction("memcpy", i8p, {i8p, i8p, sz});
        auto* out = ctx.createEntryAlloca(bt, "binat_alloca");
        uint64_t szBytes = ctx.module.getDataLayout().getTypeAllocSize(bt);
        ctx.builder.CreateCall(mcf, {out, heapPtr, ctx.getSizeConst(szBytes)});
        return ctx.ir_gen->wrap(out);
    };

    // io.read_f32_array(f, n) → list<f32>  (bulk single-call fread into list alloca)
    io.builtinFuncs["read_f32_array"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32   = ctx.builder.getInt32Ty();
        llvm::Type* voidT = llvm::Type::getVoidTy(ctx.module.getContext());
        llvm::StructType* listTy = ListImpl::getListStructType(ctx);
        auto* listAlloca = ctx.createEntryAlloca(listTy, "f32arr_alloca");
        llvm::Value* nVal = (args.size() >= 2) ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : ctx.builder.getInt32(0);
        if (nVal->getType()->isIntegerTy(64)) nVal = ctx.builder.CreateTrunc(nVal, i32);
        ListImpl::initList(ctx, listAlloca, ctx.builder.getFloatTy(), 1);
        if (args.size() >= 2) {
            llvm::Value* fVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fillFn = ctx.getFunction("aiml_io_read_f32_array_fill", voidT, {i8p, i32, i8p});
            llvm::Value* listRaw = ctx.builder.CreateBitCast(listAlloca, i8p);
            ctx.builder.CreateCall(fillFn, {fVal, nVal, listRaw});
        }
        return ctx.ir_gen->wrap(listAlloca);
    };

    // -----------------------------------------------------------------------
    // time namespace
    // -----------------------------------------------------------------------
    auto& time = ctx.namespaces["time"];

    time.builtinFuncs["now"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("time_now_ms", ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    time.builtinFuncs["format_utc"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
        llvm::Function* f = ctx.getFunction("time_format_utc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* ms = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (ms->getType()->isIntegerTy(32)) ms = ctx.builder.CreateSExt(ms, ctx.builder.getInt64Ty());
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {ms}));
    };

    time.builtinFuncs["sleep_ms"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* f = ctx.getFunction("time_sleep_ms", ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()});
        llvm::Value* ms = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (ms->getType()->isIntegerTy(64)) ms = ctx.builder.CreateTrunc(ms, ctx.builder.getInt32Ty());
        ctx.builder.CreateCall(f, {ms});
        return ctx.ir_gen->getInt32(0);
    };

    // -----------------------------------------------------------------------
    // storage namespace — cross-platform key/value persistence (localStorage
    // on web, mirrored to ${XDG_DATA_HOME}/aiml/storage.json natively).
    // -----------------------------------------------------------------------
    auto& storage = ctx.namespaces["storage"];
    {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* vd  = ctx.builder.getVoidTy();

        storage.builtinFuncs["get"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
            llvm::Function* f = ctx.getFunction("aiml_storage_get", i8p, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {k}));
        };

        storage.builtinFuncs["set"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_set", vd, {i8p, i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(f, {k, v});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["remove"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_remove", vd, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {k});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["has"] = [i8p, i32](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_has", i32, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {k}));
        };

        storage.builtinFuncs["clear"] = [vd](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_clear", vd, {});
            ctx.builder.CreateCall(f, {});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["host"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_host", i8p, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };
        storage.builtinFuncs["is_secure"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_is_secure", ctx.builder.getInt32Ty(), {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };
    }

    // -----------------------------------------------------------------------
    // buffer builtin class
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo bufInfo;
    bufInfo.name      = "buffer";
    bufInfo.isBuiltin = true;
    // type is resolved lazily via ctx.getLLVMType("buffer") — leave nullptr here,
    // it will be filled on first getLLVMType("buffer") call.

    bufInfo.builtinMethods["alloc"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.size() < 1) return ctx.ir_gen->getInt32(0);
        IRValue* sizeVal = evalExpr(call->args[0].get());
        if (sizeVal) BufferImpl::alloc(ctx, baseAlloca, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sizeVal)));
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.size() < 2) return ctx.ir_gen->getInt32(0);
        IRValue* val = evalExpr(call->args[0].get());
        std::string typeName = extractTypeName(call->args[1].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (ty && val) BufferImpl::write(ctx, baseAlloca, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), ty);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["read"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        std::string typeName = extractTypeName(call->args[0].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (ty) return ctx.ir_gen->wrap(BufferImpl::read(ctx, baseAlloca, ty));
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["seek"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        IRValue* rawPos = evalExpr(call->args[0].get());
        if (!rawPos) return ctx.ir_gen->getInt32(0);
        llvm::Value* pos32 = ctx.builder.CreateZExtOrTrunc(
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPos)), ctx.builder.getInt32Ty());
        BufferImpl::seek(ctx, baseAlloca, pos32, false);
        BufferImpl::seek(ctx, baseAlloca, pos32, true);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["len"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        auto* sizePtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), baseAlloca, 0, "buf_len_ptr");
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "buf_len"));
    };

    // buffer.zero() — memset(data, 0, capacity), then size = capacity, writeIndex = capacity
    bufInfo.builtinMethods["zero"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        auto* bufType = ctx.getLLVMType("buffer");
        // Get capacity and data pointer
        auto* capPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 1, "buf_cap_ptr");
        auto* cap = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), capPtr, "buf_cap");
        auto* dataPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4, "buf_data_ptr");
        auto* data = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), dataPtr, "buf_data");
        // memset(data, 0, capacity)
        auto* memsetFn = ctx.getFunction("memset",
            ctx.builder.getInt8Ty()->getPointerTo(),
            {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty()});
        auto* cap64 = ctx.builder.CreateZExt(cap, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(memsetFn, {data, ctx.builder.getInt32(0), cap64});
        // size = capacity, writeIndex = capacity
        auto* sizePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 0);
        ctx.builder.CreateStore(cap, sizePtr);
        auto* writePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 3);
        ctx.builder.CreateStore(cap, writePtr);
        return ctx.ir_gen->getInt32(0);
    };

    // buffer.resize(new_size_bytes) — realloc + zero-fill new portion
    // Preserves existing data, grows buffer in-place. Updates size, cap, writeIndex.
    bufInfo.builtinMethods["resize"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        IRValue* newSizeVal = evalExpr(call->args[0].get());
        if (!newSizeVal) return ctx.ir_gen->getInt32(0);
        auto* i8p     = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32     = ctx.builder.getInt32Ty();
        auto* i64     = ctx.builder.getInt64Ty();
        auto* sz      = ctx.getSizeType();  // target libc size_t
        auto* bufType = ctx.getLLVMType("buffer");
        llvm::Value* newSize = ctx.builder.CreateZExtOrTrunc(
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newSizeVal)), i32);
        auto* sizePtr  = ctx.builder.CreateStructGEP(bufType, baseAlloca, 0);
        auto* capPtr   = ctx.builder.CreateStructGEP(bufType, baseAlloca, 1);
        auto* writePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 3);
        auto* dataPtr  = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4);
        auto* oldSize  = ctx.builder.CreateLoad(i32, sizePtr, "old_size");
        auto* oldData  = ctx.builder.CreateLoad(i8p, dataPtr, "old_data");
        auto* reallocFn = ctx.getFunction("realloc", i8p, {i8p, sz});
        auto* newSizeSz = ctx.builder.CreateZExtOrTrunc(newSize, sz);
        auto* newData   = ctx.builder.CreateCall(reallocFn, {oldData, newSizeSz}, "new_data");
        auto* oldSize64  = ctx.builder.CreateZExt(oldSize, i64);
        auto* extraStart = ctx.builder.CreateGEP(ctx.builder.getInt8Ty(), newData, oldSize64, "extra_start");
        auto* extraBytes = ctx.builder.CreateZExtOrTrunc(ctx.builder.CreateSub(newSize, oldSize), sz);
        auto* memsetFn   = ctx.getFunction("memset", i8p, {i8p, i32, sz});
        ctx.builder.CreateCall(memsetFn, {extraStart, ctx.builder.getInt32(0), extraBytes});
        ctx.builder.CreateStore(newData, dataPtr);
        ctx.builder.CreateStore(newSize, sizePtr);
        ctx.builder.CreateStore(newSize, capPtr);
        ctx.builder.CreateStore(newSize, writePtr);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["read_versioned"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        std::string typeName = extractTypeName(call->args[0].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (!ty) return ctx.ir_gen->getInt32(0);
        auto realEval = [&](CompilerContext& c, std::shared_ptr<AIML::AST::Expr> e) -> llvm::Value* {
            IRValue* v = evalExpr(e.get());
            if (!v) return llvm::Constant::getNullValue(ctx.getLLVMType("buffer"));
            return static_cast<llvm::Value*>(c.ir_gen->unwrap(v));
        };
        llvm::Value* explicitVer = nullptr;
        if (call->args.size() >= 2) {
            IRValue* verVal = evalExpr(call->args[1].get());
            if (verVal) explicitVer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(verVal));
        }
        return ctx.ir_gen->wrap(BufferImpl::read_versioned(ctx, baseAlloca, ty, realEval, explicitVer));
    };

    // buffer.ws_recv(conn) → i32 — receive the next WebSocket frame straight into
    // THIS buffer (baseAlloca is the real buffer, so size/wIdx/rIdx are written
    // back).  Returns bytes received, 0 on close, -1 error, -2 overflow.
    bufInfo.builtinMethods["ws_recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_recv_binary", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_send(conn) → i32 — send THIS buffer's written bytes as one binary frame.
    bufInfo.builtinMethods["ws_send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_send_binary", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_recv_append(conn) → i32 — receive the next frame APPENDED at this
    // buffer's write index (reassemble N chunk frames: alloc once, recv_append N×).
    bufInfo.builtinMethods["ws_recv_append"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_recv_append", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_send_range(conn, offset, len) → i32 — send data[offset..offset+len]
    // as one binary frame (zero-copy from the buffer's data pointer).  For chunked
    // uploads: slice this buffer into fixed-size frames without copying.
    bufInfo.builtinMethods["ws_send_range"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 3) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        IRValue* offV  = evalExpr(call->args[1].get());
        IRValue* lenV  = evalExpr(call->args[2].get());
        if (!connV || !offV || !lenV) return ctx.ir_gen->getInt32(-1);
        auto* bufType = ctx.getLLVMType("buffer");
        llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4, "buf_data_ptr");
        llvm::Value* dataPtr = ctx.builder.CreateLoad(i8p, dataPtrPtr, "buf_data");
        llvm::Value* off = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(offV)), i32);
        llvm::Value* len = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lenV)), i32);
        llvm::Value* atPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, off, "buf_at");
        llvm::Function* f = ctx.getFunction("aiml_ws_send_raw", i32, {i8p, i8p, i32});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), atPtr, len }));
    };

    // buffer.decode_image(src: buffer) → i64  — decode PNG/JPG/WebP bytes in `src`
    // into THIS buffer as tightly-packed RGBA.  Returns ((w<<32)|h), 0 on failure.
    bufInfo.builtinMethods["decode_image"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        IRValue* srcV = evalExpr(call->args[0].get());
        if (!srcV) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = ctx.builder.CreateExtractValue(srcVal, {4}, "src_data");
        llvm::Value* srcLen  = ctx.builder.CreateExtractValue(srcVal, {0}, "src_len");
        llvm::Function* f = ctx.getFunction("aiml_img_decode", i64, {i8p, i8p, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen}));
    };

    // buffer.encode_webp(src: buffer, w: i32, h: i32, quality: i32) → i32 — encode the
    // RGBA bytes in `src` (w*h*4) to WebP into THIS buffer.  quality<0 = lossless.
    bufInfo.builtinMethods["encode_webp"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 4) return ctx.ir_gen->getInt32(0);
        IRValue* srcV = evalExpr(call->args[0].get());
        IRValue* wV   = evalExpr(call->args[1].get());
        IRValue* hV   = evalExpr(call->args[2].get());
        IRValue* qV   = evalExpr(call->args[3].get());
        if (!srcV || !wV || !hV || !qV) return ctx.ir_gen->getInt32(0);
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = ctx.builder.CreateExtractValue(srcVal, {4}, "src_data");
        llvm::Value* srcLen  = ctx.builder.CreateExtractValue(srcVal, {0}, "src_len");
        llvm::Value* wVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(wV)), i32);
        llvm::Value* hVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(hV)), i32);
        llvm::Value* qVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(qV)), i32);
        llvm::Function* f = ctx.getFunction("aiml_img_encode_webp", i32, {i8p, i8p, i32, i32, i32, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen, wVal, hVal, qVal}));
    };

    // buffer.resize_crop(src, sw, sh, dw, dh, mode) → i32 — resize RGBA in `src`
    // (sw×sh) to EXACTLY dw×dh into THIS buffer.  mode 0 = cover-crop (aspect kept),
    // 1 = stretch (fill exactly, may distort).  Enforces the canonical size.
    bufInfo.builtinMethods["resize_crop"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 6) return ctx.ir_gen->getInt32(0);
        IRValue* srcV = evalExpr(call->args[0].get());
        IRValue* swV  = evalExpr(call->args[1].get());
        IRValue* shV  = evalExpr(call->args[2].get());
        IRValue* dwV  = evalExpr(call->args[3].get());
        IRValue* dhV  = evalExpr(call->args[4].get());
        IRValue* mdV  = evalExpr(call->args[5].get());
        if (!srcV || !swV || !shV || !dwV || !dhV || !mdV) return ctx.ir_gen->getInt32(0);
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = ctx.builder.CreateExtractValue(srcVal, {4}, "rc_src_data");
        llvm::Value* srcLen  = ctx.builder.CreateExtractValue(srcVal, {0}, "rc_src_len");
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Function* f = ctx.getFunction("aiml_img_resize_crop", i32, {i8p, i8p, i32, i32, i32, i32, i32, i32, f32, f32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        llvm::Value* half = llvm::ConstantFP::get(f32, 0.5);   // centered crop (API unchanged)
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen, tr(swV), tr(shV), tr(dwV), tr(dhV), tr(mdV), half, half}));
    };

    // buffer.pick_webp(dw, dh, mode) → i32 — open a file picker, convert the chosen
    // image to a dw×dh WebP and write it into THIS buffer.  mode 0 = cover-crop,
    // 1 = stretch.  Returns bytes written (0 = cancelled / error).  Native uses a
    // zenity/kdialog dialog + stb/libwebp; WASM uses a browser file input + canvas.
    bufInfo.builtinMethods["pick_webp"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 3) return ctx.ir_gen->getInt32(0);
        IRValue* dwV = evalExpr(call->args[0].get());
        IRValue* dhV = evalExpr(call->args[1].get());
        IRValue* mdV = evalExpr(call->args[2].get());
        if (!dwV || !dhV || !mdV) return ctx.ir_gen->getInt32(0);
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        llvm::Function* f = ctx.getFunction("aiml_art_pick_webp", i32, {i8p, i32, i32, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, tr(dwV), tr(dhV), tr(mdV)}));
    };

    // buffer.encode_source(dw, dh, mode, panx, pany) → i32 — crop the stashed pan
    // source (from gl.pick_source) at pan (0..1 per axis) and WebP-encode it into
    // THIS buffer.  Returns bytes written (0 = no source / error).  The final-save
    // counterpart to the live gl.source_texture preview.
    bufInfo.builtinMethods["encode_source"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        if (call->args.size() < 5) return ctx.ir_gen->getInt32(0);
        IRValue* dwV = evalExpr(call->args[0].get());
        IRValue* dhV = evalExpr(call->args[1].get());
        IRValue* mdV = evalExpr(call->args[2].get());
        IRValue* pxV = evalExpr(call->args[3].get());
        IRValue* pyV = evalExpr(call->args[4].get());
        if (!dwV || !dhV || !mdV || !pxV || !pyV) return ctx.ir_gen->getInt32(0);
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        auto trf = [&](IRValue* v) -> llvm::Value* {
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
            if (x->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(x, f32);
            if (x->getType()->isDoubleTy())  return ctx.builder.CreateFPCast(x, f32);
            return x;
        };
        llvm::Function* f = ctx.getFunction("aiml_art_encode_source", i32, {i8p, i32, i32, i32, f32, f32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, tr(dwV), tr(dhV), tr(mdV), trf(pxV), trf(pyV)}));
    };

    ctx.classes["buffer"] = std::move(bufInfo);

    // -----------------------------------------------------------------------
    // process builtin class
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo procInfo;
    procInfo.name      = "process";
    procInfo.isBuiltin = true;

    procInfo.builtinMethods["wait"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
        llvm::Function* f = ctx.getFunction("process_wait", ctx.builder.getInt32Ty(), {i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {procPtr}));
    };

    procInfo.builtinMethods["kill"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
        llvm::Function* f = ctx.getFunction("process_kill", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(f, {procPtr});
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["process"] = std::move(procInfo);

    // -----------------------------------------------------------------------
    // thread_handle builtin class: { i64 tid, i8 joined, i8 detached }
    // Stored as thread_handle* (pointer to heap-allocated struct).
    // -----------------------------------------------------------------------
    llvm::StructType* handleTy = llvm::StructType::getTypeByName(ctx.context, "thread_handle");
    if (!handleTy) {
        handleTy = llvm::StructType::create(ctx.context,
            {ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty(), ctx.builder.getInt8Ty()},
            "thread_handle");
    }

    CompilerContext::ClassInfo threadInfo;
    threadInfo.name      = "thread_handle";
    threadInfo.isBuiltin = true;
    threadInfo.type      = handleTy;

    // Member "id" = tid at struct index 0
    CompilerContext::StructMember idMember;
    idMember.typeName = "i64";
    idMember.type     = ctx.builder.getInt64Ty();
    idMember.index    = 0;
    idMember.access   = AccessLevel::Public;
    threadInfo.members["id"] = idMember;

    threadInfo.builtinMethods["join"] = [handleTy](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* handlePtrTy = handleTy->getPointerTo();
        llvm::Value* handlePtr  = ctx.builder.CreateLoad(handlePtrTy, baseAlloca, "handle_ptr");

        // Guard: throw InvalidThreadStateException if already detached
        llvm::Value* detachedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr");
        llvm::Value* detachedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), detachedGEP, "detached");
        emitGuard(ctx, detachedVal, "join_detached_throw", "join_check_joined");

        // Guard: throw InvalidThreadStateException if already joined
        llvm::Value* joinedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr");
        llvm::Value* joinedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), joinedGEP, "joined");
        emitGuard(ctx, joinedVal, "join_already_throw", "join_do");

        // pthread_join(tid, NULL)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Type* i8pp = ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo();
        llvm::Function* f = ctx.getFunction("pthread_join",
            ctx.builder.getInt32Ty(), {ctx.builder.getInt64Ty(), i8pp});
        ctx.builder.CreateCall(f, {tid,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8pp))});
        ctx.builder.CreateStore(ctx.builder.getInt8(1), joinedGEP);
        return ctx.ir_gen->getInt32(0);
    };

    threadInfo.builtinMethods["detach"] = [handleTy](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* handlePtrTy = handleTy->getPointerTo();
        llvm::Value* handlePtr  = ctx.builder.CreateLoad(handlePtrTy, baseAlloca, "handle_ptr");

        // Guard: throw InvalidThreadStateException if already joined
        llvm::Value* joinedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr");
        llvm::Value* joinedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), joinedGEP, "joined");
        emitGuard(ctx, joinedVal, "detach_joined_throw", "detach_check_detached");

        // Guard: throw InvalidThreadStateException if already detached
        llvm::Value* detachedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr");
        llvm::Value* detachedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), detachedGEP, "detached");
        emitGuard(ctx, detachedVal, "detach_already_throw", "detach_do");

        // pthread_detach(tid)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Function* f = ctx.getFunction("pthread_detach",
            ctx.builder.getInt32Ty(), {ctx.getSizeType()});
        ctx.builder.CreateCall(f, {tid});
        ctx.builder.CreateStore(ctx.builder.getInt8(1), detachedGEP);
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["thread_handle"] = std::move(threadInfo);

    // -----------------------------------------------------------------------
    // lock builtin class: opaque i8* pointing to heap-allocated pthread_mutex_t
    // sizeof(pthread_mutex_t) == 40 on Linux x86_64
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo lockInfo;
    lockInfo.name      = "lock";
    lockInfo.isBuiltin = true;

    lockInfo.builtinMethods["init"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* mutexMem = ctx.builder.CreateCall(mallocFn,
            {ctx.builder.getInt64(40)}, "mutex_mem");
        ctx.builder.CreateStore(mutexMem, baseAlloca);
        llvm::Function* initFn = ctx.getFunction("pthread_mutex_init",
            ctx.builder.getInt32Ty(), {i8p, i8p});
        ctx.builder.CreateCall(initFn, {mutexMem,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))});
        return ctx.ir_gen->getInt32(0);
    };

    lockInfo.builtinMethods["acquire"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* mutexPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "mutex_ptr");
        llvm::Function* f = ctx.getFunction("pthread_mutex_lock",
            ctx.builder.getInt32Ty(), {i8p});
        ctx.builder.CreateCall(f, {mutexPtr});
        return ctx.ir_gen->getInt32(0);
    };

    lockInfo.builtinMethods["release"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* mutexPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "mutex_ptr");
        llvm::Function* f = ctx.getFunction("pthread_mutex_unlock",
            ctx.builder.getInt32Ty(), {i8p});
        ctx.builder.CreateCall(f, {mutexPtr});
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["lock"] = std::move(lockInfo);

    // -----------------------------------------------------------------------
    // Code builtin class: opaque i8* → heap-allocated __aiml_code_t
    // { id: i8*, source: i8*, src_len: i64, entry: i8* }
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo codeInfo;
    codeInfo.name      = "Code";
    codeInfo.isBuiltin = true;

    codeInfo.builtinMethods["contains"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt8(0));
        IRValue* nameVal = evalExpr(call->args[0].get());
        if (!nameVal) return ctx.ir_gen->wrap(ctx.builder.getInt8(0));
        llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(nameVal));
        return ctx.ir_gen->wrap(CodeImpl::contains(ctx, codePtr, name));
    };

    codeInfo.builtinMethods["size"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        return ctx.ir_gen->wrap(CodeImpl::size(ctx, codePtr));
    };

    codeInfo.builtinMethods["toString"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        return ctx.ir_gen->wrap(CodeImpl::toString(ctx, codePtr));
    };

    codeInfo.builtinMethods["merge"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* aPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_a");
        if (call->args.empty()) return ctx.ir_gen->wrap(aPtr);
        IRValue* bVal = evalExpr(call->args[0].get());
        if (!bVal) return ctx.ir_gen->wrap(aPtr);
        llvm::Value* bPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bVal));
        return ctx.ir_gen->wrap(CodeImpl::merge(ctx, aPtr, bPtr));
    };

    ctx.classes["Code"] = std::move(codeInfo);

    // ── path builtin class (instance methods) ────────────────────────────────
    // path values are i8* pointers into the global intern table.
    // The base alloca holds an i8* — we load it before every call.
    {
        CompilerContext::ClassInfo pi;
        pi.name      = "path";
        pi.isBuiltin = true;

        llvm::Type* i8p_ty = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(ctx.context);
        llvm::Type* i8_ty  = llvm::Type::getInt8Ty(ctx.context);
        llvm::Type* i64_ty = llvm::Type::getInt64Ty(ctx.context);

        // Helper: load i8* from the alloca
        auto loadPath = [](CompilerContext& ctx, llvm::Value* alloca) -> llvm::Value* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.builder.CreateLoad(i8p, alloca, "pv");
        };
        // Helper: emit call to path runtime fn(i8*) -> i8*
        auto callP2P = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
                                  const char* fn_name) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction(fn_name, i8p, {i8p});
            llvm::Value* pv   = loadPath(ctx, alloca);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv}));
        };
        // Helper: emit call to path runtime fn(i8*) -> i32
        auto callP2I = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
                                  const char* fn_name) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction(fn_name, i32, {i8p});
            llvm::Value* pv   = loadPath(ctx, alloca);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv}));
        };

        // join(seg...) — sequential joins for up to N args
        pi.builtinMethods["join"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* joinFn = ctx.getFunction("aiml_path_join", i8p, {i8p, i8p});
            llvm::Value* acc = loadPath(ctx, alloca);
            for (auto& arg : call->args) {
                IRValue* av = eval(arg.get());
                if (!av) continue;
                llvm::Value* seg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av));
                acc = ctx.builder.CreateCall(joinFn, {acc, seg}, "joined");
            }
            return ctx.ir_gen->wrap(acc);
        };

        // safe_join(rhs)
        pi.builtinMethods["safe_join"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_safe_join", i8p, {i8p, i8p});
            llvm::Value* base = loadPath(ctx, alloca);
            if (call->args.empty()) return ctx.ir_gen->wrap(base);
            IRValue* rv = eval(call->args[0].get());
            llvm::Value* rhs = rv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rv))
                                  : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {base, rhs}));
        };

        pi.builtinMethods["parent"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_parent");
        };
        pi.builtinMethods["name"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_name");
        };
        pi.builtinMethods["stem"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_stem");
        };
        pi.builtinMethods["extension"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_extension");
        };
        pi.builtinMethods["root"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_root");
        };
        pi.builtinMethods["to_string"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_to_string");
        };
        pi.builtinMethods["normalize"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            // Already normalized at intern time; intern again for safety
            return callP2P(ctx, alloca, "aiml_path_intern");
        };

        // relative_to(base: path)
        pi.builtinMethods["relative_to"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_relative_to", i8p, {i8p, i8p});
            llvm::Value* pv = loadPath(ctx, alloca);
            if (call->args.empty()) return ctx.ir_gen->wrap(pv);
            IRValue* bv = eval(call->args[0].get());
            llvm::Value* base = bv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bv))
                                   : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv, base}));
        };

        pi.builtinMethods["is_absolute"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_absolute");
        };
        pi.builtinMethods["is_relative"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_relative");
        };
        pi.builtinMethods["is_empty"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_empty");
        };
        pi.builtinMethods["depth"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_depth");
        };

        // Filesystem predicates on the path itself
        pi.builtinMethods["exists"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_exists");
        };
        pi.builtinMethods["is_dir"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_is_dir");
        };
        pi.builtinMethods["is_file"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_is_file");
        };

        ctx.classes["path"] = pi;
    }

    // ── path namespace (static / constructor functions) ───────────────────────
    {
        auto& pathNs = ctx.namespaces["path"];

        auto internLit = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_intern", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        pathNs.builtinFuncs["from_string"] = internLit;

        auto noArgPathFn = [](const char* fn_name) {
            return [fn_name](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* f = ctx.getFunction(fn_name, i8p, {});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
            };
        };

        pathNs.builtinFuncs["home"]        = noArgPathFn("aiml_path_home");
        pathNs.builtinFuncs["temp_dir"]    = noArgPathFn("aiml_path_temp_dir");
        pathNs.builtinFuncs["cwd"]         = noArgPathFn("aiml_path_cwd");
        pathNs.builtinFuncs["current_exe"] = noArgPathFn("aiml_path_current_exe");

        pathNs.builtinFuncs["config_dir"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_config_dir", i8p, {i8p});
            llvm::Value* app = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {app}));
        };
    }

    // ── yaml namespace: parsed document navigation ──────────────────────────
    {
        auto& yaml = ctx.namespaces["yaml"];
        auto unaryPtr = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
        };
        auto unaryI32 = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                auto* function = ctx.getFunction(name, i32, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
        };
        // spec/61 §7: a unary yaml accessor that returns a FRESH owned aiml string (scalar/dump/error,
        // all via copyString → aiml_str_ndup, rc=1, no other owner). Register the result as an owned
        // stmt-temp so a binding local MOVES it (single rc ref, decref'd by its scope-drop) and an
        // unbound transient is decref'd at statement end. Without this the decl-init normalisation
        // treats it as a BORROW and increfs it (rc 1→2, slot drops to 1) → the original ref leaks
        // (the grai yaml-grep string leak).
        auto unaryStrOwned = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p});
                llvm::Value* res = ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                    ctx.stmtTempStack.back().push_back(res);
                return ctx.ir_gen->wrap(res);
            };
        };
        yaml.builtinFuncs["parse"] = unaryPtr("aiml_yaml_parse");
        yaml.builtinFuncs["parse_file"] = unaryPtr("aiml_yaml_parse_file");
        yaml.builtinFuncs["error"] = unaryStrOwned("aiml_yaml_error");
        yaml.builtinFuncs["root"] = unaryPtr("aiml_yaml_root");
        yaml.builtinFuncs["kind"] = unaryI32("aiml_yaml_kind");
        yaml.builtinFuncs["size"] = unaryI32("aiml_yaml_size");
        yaml.builtinFuncs["line"] = unaryI32("aiml_yaml_line");
        yaml.builtinFuncs["column"] = unaryI32("aiml_yaml_column");
        yaml.builtinFuncs["scalar"] = unaryStrOwned("aiml_yaml_scalar");
        yaml.builtinFuncs["dump"] = unaryStrOwned("aiml_yaml_dump");
        yaml.builtinFuncs["free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* function = ctx.getFunction("aiml_yaml_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(function, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };
        auto indexedPtr = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p, i32});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
        };
        yaml.builtinFuncs["item"] = indexedPtr("aiml_yaml_item");
        yaml.builtinFuncs["value"] = indexedPtr("aiml_yaml_value");
        yaml.builtinFuncs["key"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            auto* function = ctx.getFunction("aiml_yaml_key", i8p, {i8p, i32});
            // yaml.key returns a FRESH owned aiml string (copyString) → own it as a stmt-temp (see
            // unaryStrOwned) so the binding local moves it / an unbound transient is decref'd.
            llvm::Value* res = ctx.builder.CreateCall(function, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                ctx.stmtTempStack.back().push_back(res);
            return ctx.ir_gen->wrap(res);
        };
        yaml.builtinFuncs["get"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
            auto* function = ctx.getFunction("aiml_yaml_get", i8p, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };
    }

    // ── io namespace: path-aware filesystem operations ────────────────────────
    {
        auto& io = ctx.namespaces["io"];

        auto ioP2I = [](const char* fn_name) {
            return [fn_name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* f = ctx.getFunction(fn_name, i32, {i8p});
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
            };
        };

        io.builtinFuncs["exists"]   = ioP2I("aiml_io_exists");
        io.builtinFuncs["is_dir"]   = ioP2I("aiml_io_is_dir");
        io.builtinFuncs["is_file"]  = ioP2I("aiml_io_is_file");
        io.builtinFuncs["mkdir"]    = ioP2I("aiml_io_mkdir");
        io.builtinFuncs["mkdir_all"]= ioP2I("aiml_io_mkdir_all");
        io.builtinFuncs["remove"]   = ioP2I("aiml_io_remove");
        io.builtinFuncs["can_read"] = ioP2I("aiml_io_can_read");
        io.builtinFuncs["can_write"]= ioP2I("aiml_io_can_write");
        io.builtinFuncs["can_exec"] = ioP2I("aiml_io_can_exec");

        io.builtinFuncs["file_size"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Function* f = ctx.getFunction("aiml_io_file_size", i64, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(-1));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        io.builtinFuncs["remove_all"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_remove_all", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        io.builtinFuncs["rename"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_rename", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {src, dst}));
        };

        io.builtinFuncs["read_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_io_read_file", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        // io.read_asset(stem) → string content from data-Kind asset store
        // (spec/44_7).  Compile-time validation when arg is a string literal.
        io.builtinFuncs["read_asset"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (!args.empty() && ctx.currentCall && !ctx.currentCall->args.empty()) {
                if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                        ctx.currentCall->args[0].get())) {
                    if (lit->type == "string" &&
                        !AIML::AssetScanner::instance().has_stem(
                            AIML::AssetKind::Data, lit->value)) {
                        LOG_ERROR(std::string("io.read_asset: stem '") +
                                  lit->value + "' not in data store");
                    }
                }
            }
            llvm::Function* f = ctx.getFunction("aiml_asset_read_data", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        // io.write_file(path, content) -> i32  (0=ok, -1=error)
        io.builtinFuncs["write_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_write_file", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* p = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* c = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, c}));
        };

        // io.list_dir(path) -> list<path>
        // Allocates a GenericList on the stack, inits it, calls aiml_io_list_dir_fill, returns list ptr.
        io.builtinFuncs["list_dir"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "listdir_alloca");
            ListImpl::initList(ctx, listAlloca, i8p, 8);

            llvm::Function* fillFn = ctx.getFunction("aiml_io_list_dir_fill", i32ty, {i8p, ptrTy});
            llvm::Value* pathVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(fillFn, {pathVal, listAlloca});

            return ctx.ir_gen->wrap(listAlloca);
        };
    }

    // ── socket_handle builtin class ──────────────────────────────────────────
    {
        CompilerContext::ClassInfo sockInfo;
        sockInfo.name      = "socket_handle";
        sockInfo.isBuiltin = true;

        // Helper: load the AimlSocket* from the i8* alloca
        // All methods: loadSock = CreateLoad(i8p, baseAlloca) → call aiml_net_*(loadSock, ...)

        sockInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr}));
        };

        sockInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr}));
        };

        sockInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_send", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* bufVal = evalExpr(call->args[0].get());
            llvm::Value* bufPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bufVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, bufPtr}));
        };

        sockInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_recv", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* bufVal = evalExpr(call->args[0].get());
            llvm::Value* bufPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bufVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, bufPtr}));
        };

        sockInfo.builtinMethods["send_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_send_string", ctx.builder.getInt32Ty(), {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* strVal = evalExpr(call->args[0].get());
            llvm::Value* strPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(strVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, strPtr}));
        };

        sockInfo.builtinMethods["recv_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_recv_string", i8p, {i8p, i32});
            llvm::Value* maxBytes = call->args.empty()
                ? ctx.builder.getInt32(4096)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, maxBytes}));
        };

        sockInfo.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_set_timeout", i32, {i8p, i32});
            llvm::Value* ms = call->args.empty()
                ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, ms}));
        };

        ctx.classes["socket_handle"] = std::move(sockInfo);
    }

    // ── net namespace ────────────────────────────────────────────────────────
    {
        auto& net = ctx.namespaces["net"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // net.socket(proto) → socket_handle (i8*)
        net.builtinFuncs["socket"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_net_socket", i8p, {i8p});
            llvm::Value* proto = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("tcp")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {proto}));
        };

        // net.connect(sock, host, port) → i32
        net.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_connect", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // net.bind(sock, host, port) → i32
        net.builtinFuncs["bind"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_bind", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // net.listen(sock [,backlog]) → i32
        net.builtinFuncs["listen"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_listen", i32, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* backlog = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                backlog
            }));
        };

        // net.accept(sock) → socket_handle (i8*)
        net.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_net_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.send(sock, buf) → i32
        net.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_net_send", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // net.recv(sock, buf) → i32
        net.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_net_recv", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // net.send_string(sock, str) → i32
        net.builtinFuncs["send_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_send_string", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // net.recv_string(sock, max_bytes) → string (i8*)
        net.builtinFuncs["recv_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_recv_string", i8p, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* maxBytes = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(4096);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                maxBytes
            }));
        };

        // net.close(sock) → i32
        net.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.fd(sock) → i32
        net.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.set_timeout(sock, ms) → i32
        net.builtinFuncs["set_timeout"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_set_timeout", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)ptrTy; // suppress unused warning
    }

    // ── tls_socket_handle builtin class ──────────────────────────────────────
    {
        CompilerContext::ClassInfo tlsInfo;
        tlsInfo.name      = "tls_socket_handle";
        tlsInfo.isBuiltin = true;

        tlsInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_send", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* buf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, buf}));
        };

        tlsInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* buf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, buf}));
        };

        tlsInfo.builtinMethods["send_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_send_string", ctx.builder.getInt32Ty(), {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* str = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, str}));
        };

        tlsInfo.builtinMethods["recv_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv_string", i8p, {i8p, i32});
            llvm::Value* maxBytes = call->args.empty()
                ? ctx.builder.getInt32(4096)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, maxBytes}));
        };

        tlsInfo.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_set_timeout", i32, {i8p, i32});
            llvm::Value* ms = call->args.empty()
                ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, ms}));
        };

        tlsInfo.builtinMethods["peer_cert_cn"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_peer_cert_cn", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["peer_cert_valid"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_peer_cert_valid", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        ctx.classes["tls_socket_handle"] = std::move(tlsInfo);
    }

    // ── tls namespace ─────────────────────────────────────────────────────────
    {
        auto& tls = ctx.namespaces["tls"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // tls.socket() → tls_socket_handle (i8*)
        tls.builtinFuncs["socket"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tls_socket", i8p, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };

        // tls.connect(sock, host, port) → i32
        tls.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_connect", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.bind(sock, host, port) → i32
        tls.builtinFuncs["bind"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_bind", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.listen(sock [,backlog]) → i32
        tls.builtinFuncs["listen"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_listen", i32, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* backlog = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                backlog
            }));
        };

        // tls.accept(sock) → tls_socket_handle (i8*)
        tls.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tls_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.send(sock, buf) → i32
        tls.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_tls_send", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.recv(sock, buf) → i32
        tls.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.send_string(sock, str) → i32
        tls.builtinFuncs["send_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_send_string", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.recv_string(sock, max_bytes) → string (i8*)
        tls.builtinFuncs["recv_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_recv_string", i8p, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* maxBytes = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(4096);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                maxBytes
            }));
        };

        // tls.close(sock) → i32
        tls.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.fd(sock) → i32
        tls.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.set_timeout(sock, ms) → i32
        tls.builtinFuncs["set_timeout"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_timeout", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.set_cert(sock, cert, key) → i32
        tls.builtinFuncs["set_cert"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_cert", i32, {i8p, i8p, i8p});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.set_ca(sock, ca) → i32
        tls.builtinFuncs["set_ca"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_ca", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.set_verify(sock, mode) → i32
        tls.builtinFuncs["set_verify"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_verify", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)ptrTy; // suppress unused warning
    }

    // ── ws_handle builtin class ───────────────────────────────────────────────
    {
        CompilerContext::ClassInfo wsInfo;
        wsInfo.name      = "ws_handle";
        wsInfo.isBuiltin = true;

        // Helper lambda that loads the AimlWs* from the i8* alloca
        wsInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_send", i32, {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* text = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, text}));
        };

        wsInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_recv", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["ping"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_ping", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["poll"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_poll", i32, {i8p, i32});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* timeout = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, timeout}));
        };

        ctx.classes["ws_handle"] = std::move(wsInfo);
    }

    // ── ws namespace ──────────────────────────────────────────────────────────
    {
        auto& ws = ctx.namespaces["ws"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // ws.connect(host, port, path) → ws_handle
        ws.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_connect", i8p, {i8p, i32, i8p});
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // ws.server(host, port) → ws_handle
        ws.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_server", i8p, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.accept(srv) → ws_handle  (also handles wss servers)
        ws.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.send(ws, text) → i32
        ws.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_send", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.send_binary(ws, buf) → i32
        ws.builtinFuncs["send_binary"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_ws_send_binary", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.send_kv_blob(ws, kv, key) → i32  (zero-copy: stream a KV blob straight from mmap)
        ws.builtinFuncs["send_kv_blob"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_send_kv_blob", i32, {i8p, i8p, i8p});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // ws.peer_ip(conn) → string — dotted peer IP of an accepted connection.
        // strdup so AIML owns the result (the runtime returns a struct-internal ptr).
        ws.builtinFuncs["peer_ip"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* fip = ctx.getFunction("aiml_ws_peer_ip", i8p, {i8p});
            llvm::Value* raw = ctx.builder.CreateCall(fip, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) });
            llvm::Function* fdup = ctx.getFunction("strdup", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdup, {raw}));
        };

        // ws.recv(ws) → string
        ws.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_recv", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.ping(ws) → i32
        ws.builtinFuncs["ping"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_ping", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.close(ws) → i32
        ws.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.fd(ws) → i32
        ws.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.poll(ws, timeout_ms) → i32  (1=data ready, 0=timeout, -1=error)
        ws.builtinFuncs["poll"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_poll", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.poll_listener(srv, timeout_ms) → i32 — 1 if a client is waiting to be
        // accepted, 0 on timeout, -1 on error.  Lets a server accept without
        // blocking, so it can multiplex many client connections in one loop.
        ws.builtinFuncs["poll_listener"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_poll_listener", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)i32; (void)ptrTy;
    }

    // ── wss namespace ─────────────────────────────────────────────────────────
    {
        auto& wss = ctx.namespaces["wss"];
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();

        // wss.connect(host, port, path) → ws_handle  (TLS)
        wss.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_wss_connect", i8p, {i8p, i32, i8p});
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // wss.server(host, port, cert, key) → ws_handle  (TLS listener)
        wss.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_wss_server", i8p, {i8p, i32, i8p, i8p});
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
            }));
        };

        // wss.accept(srv) → ws_handle  (delegates to aiml_ws_accept — it's TLS-aware)
        wss.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        (void)i32;
    }

    // ── db namespace ─────────────────────────────────────────────────────────
    // db.open("name")      → Store<T>  (reads ctx.deserializeTargetType for elem type)
    // db.open_kv("name")   → Kv<K:V>   (reads ctx.deserializeTargetType for val type)
    // db.open_vec("n", d)  → VecStore
    // db.begin/commit/rollback/checkpoint → stubs (i32 0)
    {
        auto& db = ctx.namespaces["db"];

        // db.open("name") → Store<T>
        // Options pre-parsed into ctx.dbOpenPersist / ctx.dbOpenColumnLayout by visitCall.
        db.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* i64   = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("Store<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(6, targetType.size() - 7);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module);
                    elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                } else if (ci.packed_size > 0) {
                    elemSize = static_cast<int64_t>(ci.packed_size);
                }
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* tnVal = ctx.builder.CreateGlobalStringPtr(
                elemName.empty() ? "?" : elemName);

            // ── Column Layout ─────────────────────────────────────────────────
            if (ctx.dbOpenColumnLayout && !elemName.empty() &&
                ctx.classes.count(elemName) && ctx.classes[elemName].type) {
                auto& ci = ctx.classes[elemName];
                llvm::DataLayout dl(&ctx.module);
                int n = (int)ci.members.size();
                std::vector<std::pair<int,int>> flds;
                for (auto& [fname, fm] : ci.members)
                    if (fm.type) flds.push_back({fm.index, (int)dl.getTypeAllocSize(fm.type)});
                std::sort(flds.begin(), flds.end());

                auto* arrTy = llvm::ArrayType::get(i32, (size_t)n);
                std::vector<llvm::Constant*> cs;
                for (auto& [idx, sz] : flds) cs.push_back(ctx.builder.getInt32(sz));
                auto* gv = new llvm::GlobalVariable(ctx.module, arrTy, true,
                    llvm::GlobalValue::InternalLinkage,
                    llvm::ConstantArray::get(arrTy, cs), ".col_fsizes");

                llvm::Function* f = ctx.getFunction("aiml_db_store_open_col",
                    i8p, {i8p, i8p, i32, ptrTy, i64});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt32(n), gv,
                     ctx.builder.getInt64(elemSize)}));
            }

            bool hasHeap = elemName != "string"
                && ctx.classes.count(elemName) && ctx.classes[elemName].hasHeapFields
                && ctx.classes[elemName].serNodesVar;

            // ── Persistent (WAL + mmap / typed WAL) ──────────────────────────
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                if (hasHeap) {
                    auto& ci = ctx.classes[elemName];
                    llvm::Value* nodesPtr = ci.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci.serNodesCount);
                    llvm::Function* f = ctx.getFunction("aiml_db_store_open_typed_persist",
                        i8p, {i8p, i8p, i64, i8p, i8p, i32});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                        {nameVal, tnVal, ctx.builder.getInt64(elemSize), pathVal, nodesPtr, nNodes}));
                }
                llvm::Function* f = ctx.getFunction("aiml_db_store_open_persist",
                    i8p, {i8p, i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt64(elemSize), pathVal}));
            }

            // ── Default: in-memory ────────────────────────────────────────────
            // A heap-bearing element type opens a TYPED in-memory store so insert can retain the
            // element's strings/containers (§7 share) — else reusing the source model after insert
            // dangles the stored record's strings (test_model).
            if (hasHeap) {
                auto& ci = ctx.classes[elemName];
                llvm::Function* ft = ctx.getFunction("aiml_db_store_open_typed",
                    i8p, {i8p, i8p, i64, i8p, i32});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ft,
                    {nameVal, tnVal, ctx.builder.getInt64(elemSize),
                     ci.serNodesVar, ctx.builder.getInt32((uint32_t)ci.serNodesCount)}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_store_open", i8p, {i8p, i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {nameVal, tnVal, ctx.builder.getInt64(elemSize)}));
        };

        // db.open_kv("name") → Kv<K:V>
        db.builtinFuncs["open_kv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Type* i32 = ctx.builder.getInt32Ty();

            // Extract value type from deserializeTargetType "Kv<string:ValueType>"
            std::string targetType = ctx.deserializeTargetType;
            std::string vName = "string";
            if (targetType.rfind("Kv<", 0) == 0 && targetType.back() == '>') {
                std::string inner = targetType.substr(3, targetType.size() - 4);
                size_t colon = inner.find(':');
                if (colon != std::string::npos) vName = inner.substr(colon + 1);
            }

            int64_t valSize = 0; // 0 = string
            if      (vName == "string") valSize = 0;
            else if (vName == "i32")    valSize = 4;
            else if (vName == "i64")    valSize = 8;
            else if (vName == "f32")    valSize = 4;
            else if (vName == "f64")    valSize = 8;
            else if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                llvm::DataLayout dl(&ctx.module);
                valSize = static_cast<int64_t>(dl.getTypeAllocSize(ctx.classes[vName].type));
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            // Spec 45: typed Kv — use WAL-safe open when value type has heap fields.
            bool hasHeap = vName != "string"
                && ctx.classes.count(vName) && ctx.classes[vName].hasHeapFields
                && ctx.classes[vName].serNodesVar;
            llvm::Value* kvHandle = nullptr;
            if (hasHeap) {
                auto& ci = ctx.classes[vName];
                llvm::Value* nodesPtr = ci.serNodesVar;
                llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci.serNodesCount);
                if (ctx.dbOpenPersist) {
                    llvm::Value* pathVal = ctx.dbOpenBasePathVal
                        ? ctx.dbOpenBasePathVal
                        : llvm::ConstantPointerNull::get(
                              llvm::PointerType::getUnqual(ctx.context));
                    llvm::Function* f = ctx.getFunction("aiml_db_kv_open_typed_persist",
                        i8p, {i8p, i64, i8p, i8p, i32});
                    kvHandle = ctx.builder.CreateCall(f,
                        {nameVal, ctx.builder.getInt64(valSize), pathVal, nodesPtr, nNodes});
                } else {
                    llvm::Function* f = ctx.getFunction("aiml_db_kv_open_typed", i8p, {i8p, i64, i8p, i32});
                    kvHandle = ctx.builder.CreateCall(f,
                        {nameVal, ctx.builder.getInt64(valSize), nodesPtr, nNodes});
                }
            } else if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                llvm::Function* f = ctx.getFunction("aiml_db_kv_open_persist", i8p, {i8p, i64, i8p});
                kvHandle = ctx.builder.CreateCall(f,
                    {nameVal, ctx.builder.getInt64(valSize), pathVal});
            } else {
                llvm::Function* f = ctx.getFunction("aiml_db_kv_open", i8p, {i8p, i64});
                kvHandle = ctx.builder.CreateCall(f, {nameVal, ctx.builder.getInt64(valSize)});
            }

            // Secondary indexes: for each db_indexed field on the value type,
            // emit a call to aiml_db_index_open_for_kv (registers the index
            // handle for later findings_by/finding_by lookups) and then a
            // backfill call so the index is consistent with any existing kv
            // entries (cross-process reopen, or first-time enabling the
            // modifier on an already-populated Kv).
            if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                auto& vci = ctx.classes[vName];
                llvm::StructType* vStructTy = vci.type;
                llvm::DataLayout dl(&ctx.module);
                const llvm::StructLayout* sl = dl.getStructLayout(vStructTy);
                for (auto& [fname, mem] : vci.members) {
                    if (!mem.db_indexed) continue;

                    llvm::Value* fNameVal = ctx.builder.CreateGlobalStringPtr(fname);
                    llvm::Value* modeVal  = ctx.builder.CreateGlobalStringPtr(mem.db_indexed_mode);
                    llvm::Value* uniqVal  = ctx.builder.getInt32(mem.db_indexed_unique ? 1 : 0);
                    llvm::Function* idxOpen = ctx.getFunction(
                        "aiml_db_index_open_for_kv", i8p, {i8p, i8p, i32, i8p});
                    llvm::Value* idxHandle = ctx.builder.CreateCall(
                        idxOpen, {kvHandle, fNameVal, uniqVal, modeVal});

                    // Backfill tag — must match AimlIndexFieldTag in db_runtime.cpp.
                    int32_t tag = -1;
                    const std::string& fTy = mem.typeName;
                    if      (fTy == "string")             tag = 0;
                    else if (fTy == "i32")                tag = 1;
                    else if (fTy == "i64")                tag = 2;
                    // spec/61 §5: under memV3 a list<string> FIELD is a HANDLE pointer (tag 4), not the
                    // inline {size,cap,data} struct (tag 3) — the backfill must deref it.
                    else if (fTy == "list<string>")       tag = ctx.memV3 ? 4 : 3;
                    if (tag < 0) continue;  // unsupported type: open only, no backfill

                    int32_t off = (int32_t)sl->getElementOffset((unsigned)mem.index);
                    llvm::Function* idxBackfill = ctx.getFunction(
                        "aiml_db_index_backfill",
                        ctx.builder.getVoidTy(), {i8p, i8p, i32, i32});
                    ctx.builder.CreateCall(idxBackfill,
                        {kvHandle, idxHandle, ctx.builder.getInt32(off), ctx.builder.getInt32(tag)});
                }
            }
            return ctx.ir_gen->wrap(kvHandle);
        };

        // db.open_vec("name", dims) → VecStore
        db.builtinFuncs["open_vec"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dimsVal = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                llvm::Function* f = ctx.getFunction("aiml_db_vec_open_persist", i8p, {i8p, i32, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, dimsVal, pathVal}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_vec_open", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, dimsVal}));
        };

        // db.open_timeseries("name") → TimeSeries<T>
        db.builtinFuncs["open_timeseries"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("TimeSeries<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(11, targetType.size() - 12);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module);
                    elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                } else if (ci.packed_size > 0) {
                    elemSize = static_cast<int64_t>(ci.packed_size);
                }
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                llvm::Function* f = ctx.getFunction("aiml_db_ts_open_persist", i8p, {i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, ctx.builder.getInt64(elemSize), pathVal}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_ts_open", i8p, {i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {nameVal, ctx.builder.getInt64(elemSize)}));
        };

        // Stubs: begin / commit / rollback → return i32(0)
        auto stubFn = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            return ctx.ir_gen->getInt32(0);
        };
        db.builtinFuncs["begin"]    = stubFn;
        db.builtinFuncs["commit"]   = stubFn;
        db.builtinFuncs["rollback"] = stubFn;

        // db.checkpoint → checkpoints all persistent stores
        db.builtinFuncs["checkpoint"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_db_checkpoint_all", i32, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };

        // db.reload_kv(kv_handle) → void
        // Re-reads the WAL from disk so cross-process writes become visible.
        db.builtinFuncs["reload_kv"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_kv_reload", llvm::Type::getVoidTy(ctx.context), {i8p});
            llvm::Value* kv_ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {kv_ptr});
            return ctx.ir_gen->getInt32(0);
        };

        // db.set_path(path) → void
        // Setzt den globalen AionDB-Root. Nachfolgende open_* Calls mit
        // relativem `path:` haengen ihren Sub-Pfad an diesen Root an.
        // Wird durch AIONDB_PATH env-var ueberschrieben.
        db.builtinFuncs["set_path"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_set_path",
                                                llvm::Type::getVoidTy(ctx.context),
                                                {i8p});
            llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {path});
            return ctx.ir_gen->getInt32(0);
        };

        // db.get_path() → string
        // Gibt den aktuell aufgeloesten AionDB-Root mit Trailing-Slash zurueck.
        // Single Source of Truth — verwendet die gleiche Resolution wie jeder
        // open_*-Call (env > set_path > "./aiondb/").
        db.builtinFuncs["get_path"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& /*args*/) -> IRValue* {
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_get_path", i8p, {});
            llvm::Value* result = ctx.builder.CreateCall(f, {});
            return ctx.ir_gen->wrap(result);
        };

        // db.kv_evict(kv_handle) → void
        // Free in-memory map but keep WAL on disk. Use reload_kv to restore.
        db.builtinFuncs["kv_evict"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_kv_evict", llvm::Type::getVoidTy(ctx.context), {i8p});
            llvm::Value* kv_ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {kv_ptr});
            return ctx.ir_gen->getInt32(0);
        };

        // db.query(store, {where: fn}, {order_by: "field"}, {limit: N}, {offset: N})
        // → list<ptr<T>>
        // Option bags are pre-parsed into ctx.dbQuery* by visitCall before this handler runs.
        db.builtinFuncs["query"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);

            llvm::Type* i8p   = llvm::PointerType::getUnqual(ctx.context);
            llvm::Type* i32t  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            // Predicate function pointer (NULL if no where clause)
            llvm::Value* predPtr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(i8p));
            if (!ctx.dbQueryWhere.empty()) {
                llvm::Function* predFn = ctx.module.getFunction(ctx.dbQueryWhere);
                if (predFn) {
                    predPtr = ctx.builder.CreateBitCast(predFn, i8p, "pred_ptr");
                } else {
                    LOG_ERROR(ctx.fmtLoc() + "db.query: predicate function '" + ctx.dbQueryWhere + "' not found");
                }
            }

            // order_by: compute field offset + size from Store element type
            // (extracted from ctx.dbQueryStoreType "Store<ElemType>", set by visitCall pre-parse)
            int32_t ordOff = -1, ordSz = 0;
            if (!ctx.dbQueryOrderBy.empty() && !ctx.dbQueryStoreType.empty()) {
                std::string tgt = ctx.dbQueryStoreType;
                if (tgt.rfind("Store<", 0) == 0 && tgt.back() == '>') {
                    std::string elemN = tgt.substr(6, tgt.size() - 7);
                    if (ctx.classes.count(elemN) && ctx.classes[elemN].type) {
                        auto& ci = ctx.classes[elemN];
                        llvm::DataLayout dl(&ctx.module);
                        const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
                        auto mit = ci.members.find(ctx.dbQueryOrderBy);
                        if (mit != ci.members.end()) {
                            ordOff = (int32_t)sl->getElementOffset(
                                (unsigned)mit->second.index);
                            ordSz  = (int32_t)dl.getTypeAllocSize(mit->second.type);
                        }
                    }
                }
            }

            // Allocate result list
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "query_list");
            ListImpl::initList(ctx, listAlloca, ptrTy, 8);

            // Call C runtime
            llvm::Function* f = ctx.getFunction("aiml_db_store_query",
                ctx.builder.getVoidTy(),
                {i8p, i8p, i32t, i32t, i32t, i32t, i32t, ptrTy});
            ctx.builder.CreateCall(f, {
                storeVal, predPtr,
                ctx.builder.getInt32(ordOff),
                ctx.builder.getInt32(ordSz),
                ctx.builder.getInt32(ctx.dbQueryOrderDir),
                ctx.builder.getInt32(ctx.dbQueryLimit),
                ctx.builder.getInt32(ctx.dbQueryOffset),
                listAlloca
            });
            return ctx.ir_gen->wrap(listAlloca);
        };
    }

    // -----------------------------------------------------------------------
    // HTTP namespace + handle types
    // -----------------------------------------------------------------------
    {
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32t = ctx.builder.getInt32Ty();
        llvm::Type* i8t  = ctx.builder.getInt8Ty();
        llvm::Type* voidT = ctx.builder.getVoidTy();

        // Helper: call a void* → i8* C function with a base ptr
        auto callGet = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8p, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI32 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI8 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };

        // ── url ──────────────────────────────────────────────────────────
        // Convention: builtinMethod handlers receive the alloca (base); they load from
        // it to obtain the actual pointer before calling C runtime functions.
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "url";
            ci.isBuiltin = true;
            // preLoadBase = false (default): URL handlers load from alloca internally.
#define URL_GETTER_STR(mName, cFn) \
            ci.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* { \
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr"); \
                llvm::Function* fn = ctx.module.getFunction(cFn); \
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr})); \
            }
            URL_GETTER_STR("protocol",  "aiml_url_protocol");
            URL_GETTER_STR("host",      "aiml_url_host");
            URL_GETTER_STR("path",      "aiml_url_path");
            URL_GETTER_STR("fragment",  "aiml_url_fragment");
            URL_GETTER_STR("get_param", "aiml_url_get_param");
            URL_GETTER_STR("to_string", "aiml_url_to_string");
#undef URL_GETTER_STR
            ci.builtinMethods["port"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module.getFunction("aiml_url_port");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_port", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ci.builtinMethods["is_https"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module.getFunction("aiml_url_is_https");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_is_https", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ctx.classes["url"] = std::move(ci);
        }

        // ── http namespace functions ──────────────────────────────────────
        {
            auto& http = ctx.namespaces["http"];
            // http.fetch_text(url) → string  (one-shot GET body; WASM uses browser fetch)
            http.builtinFuncs["fetch_text"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fetch_text");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_fetch_text", ctx.module);
                llvm::Value* url = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {url}));
            };
            // http.get(url) → http_response_handle
            http.builtinFuncs["get"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_get");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_get", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.post(url, body) → http_response_handle
            http.builtinFuncs["post"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_post");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_post", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.put(url, body) → http_response_handle
            http.builtinFuncs["put"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_put");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_put", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.delete(url) → http_response_handle
            http.builtinFuncs["delete"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_delete");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_delete", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.head(url) → http_response_handle
            http.builtinFuncs["head"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_head");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_head", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.new_request(method, url) → http_client_handle
            http.builtinFuncs["new_request"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_new_request");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_new_request", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.parse_url(str) → url
            http.builtinFuncs["parse_url"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_url_parse");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_parse", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.server(host, port) → http_server_handle
            http.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_server", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.server_tls(host, port, cert, key) → http_server_handle
            http.builtinFuncs["server_tls"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 4) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_tls");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty(), i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_tls", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                }));
            };
            // http.fs_options() → fs_options
            http.builtinFuncs["fs_options"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_new");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_new", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
            };
        }

        // ── http_server_handle ────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_server_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // srv.route(method, path, handler)
            ci.builtinMethods["route"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_route");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_route", ctx.module);
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                // arg[2] is a function name → get its LLVM Function* and bitcast to i8*
                llvm::Value* handlerPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get())) {
                    llvm::Function* hf = ctx.module.getFunction(ve->name);
                    if (hf) handlerPtr = ctx.builder.CreateBitCast(hf, i8p);
                }
                if (!handlerPtr) {
                    IRValue* a2 = evalExpr(call->args[2].get());
                    handlerPtr = a2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2)) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                }
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)),
                    handlerPtr});
                return nullptr;
            };
            // srv.use(middleware)
            ci.builtinMethods["use"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_use");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_use", ctx.module);
                if (!call || call->args.empty()) return nullptr;
                llvm::Value* mwPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                    if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                        mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                if (!mwPtr) { IRValue* a = evalExpr(call->args[0].get()); mwPtr = a ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)); }
                ctx.builder.CreateCall(fn, {base, mwPtr});
                return nullptr;
            };
            // srv.group(prefix, middleware?) → http_route_handle
            ci.builtinMethods["group"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_group", ctx.module);
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                            mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                    if (mwPtr == llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))) {
                        IRValue* a = evalExpr(call->args[1].get());
                        if (a) mwPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                    }
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(prefix)), mwPtr}));
            };
            // srv.set_header(name, val)
            ci.builtinMethods["set_header"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_set_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_header", ctx.module);
                if (!call || call->args.size() < 2) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1))});
                return nullptr;
            };
            // srv.set_timeout(secs) — request-read timeout (slowloris defence)
            ci.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_set_timeout");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_timeout", ctx.module);
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return nullptr;
            };
            // srv.secure_headers(hsts) — enable nosniff/X-Frame-Options/Referrer-Policy (+HSTS if hsts!=0)
            ci.builtinMethods["secure_headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_secure_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_secure_headers", ctx.module);
                llvm::Value* hsts = call && !call->args.empty()
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())))
                    : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(fn, {base, hsts});
                return nullptr;
            };
            // srv.set_max_connections(n) — cap concurrent handler threads (0 = unlimited)
            ci.builtinMethods["set_max_connections"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_set_max_connections");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_max_connections", ctx.module);
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return nullptr;
            };
            // srv.set_rate_limit(max_per_window, window_secs) — per-IP fixed-window limit
            ci.builtinMethods["set_rate_limit"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_set_rate_limit");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_rate_limit", ctx.module);
                if (!call || call->args.size() < 2) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1))});
                return nullptr;
            };
            // srv.require_client_cert(ca_path) — enable mTLS: require+verify a client
            // cert against the CA bundle PEM. Returns i32 (0 ok, -1 error). TLS server only.
            ci.builtinMethods["require_client_cert"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_require_client_cert");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i32, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_require_client_cert", ctx.module);
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                llvm::Value* r = ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return ctx.ir_gen->wrap(r);
            };
            // srv.filesystem(prefix, dirs, opts) — serve static files
            ci.builtinMethods["filesystem"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_filesystem");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_filesystem", ctx.module);
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                // dirs is a list<path>. build_fs_route reads it as an AimlList {size,cap,data}.
                // spec/61 §5 (memV3): the list local is a HANDLE — its slot holds a POINTER to the
                // {size,cap,data} part, so the VALUE of the variable (evalExpr) IS the AimlList*.
                // Passing the alloca ADDRESS here (as the OFF path does) would hand build_fs_route a
                // pointer-to-pointer → it reads the handle pointer's bytes as size/data → empty/garbage
                // fs_dirs → every request 404s (test_http_range/traversal). OFF keeps the alloca path
                // (there the alloca IS the inline {size,cap,data} struct).
                llvm::Value* dirsPtr = nullptr;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (sym) {
                        llvm::Value* av = sym->alloca_inst
                            ? static_cast<llvm::Value*>(sym->alloca_inst)
                            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                        if (ctx.memV3) {
                            // memV3: the slot holds a POINTER to the {size,cap,data} part — LOAD it so
                            // build_fs_route receives the AimlList*, not the slot address (which it would
                            // misread as size/data → empty fs_dirs → 404). OFF: the alloca IS the struct.
                            dirsPtr = ctx.builder.CreateLoad(i8p, av, "fs_dirs_handle");
                        } else {
                            dirsPtr = ctx.builder.CreateBitCast(av, i8p);
                        }
                    }
                }
                if (!dirsPtr) {
                    IRValue* a1 = evalExpr(call->args[1].get());
                    dirsPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1));
                }
                IRValue* a2 = evalExpr(call->args[2].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    dirsPtr,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2))});
                return nullptr;
            };
            // srv.start() — blocking
            ci.builtinMethods["start"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_start");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start", ctx.module);
                ctx.builder.CreateCall(fn, {base});
                return nullptr;
            };
            // srv.start_async() → thread_handle
            ci.builtinMethods["start_async"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_start_async");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start_async", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // srv.stop()
            ci.builtinMethods["stop"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_stop");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_stop", ctx.module);
                ctx.builder.CreateCall(fn, {base});
                return nullptr;
            };
            ctx.classes["http_server_handle"] = std::move(ci);
        }

        // ── http_route_handle ─────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_route_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;

            auto makeRouteMethod = [](const char* cFn, bool hasPrefix) {
                return [cFn, hasPrefix](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Function* fn = ctx.module.getFunction(cFn);
                    if (hasPrefix) {
                        // route(method, path, handler)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module);
                        if (!call || call->args.size() < 3) return nullptr;
                        IRValue* a0 = evalExpr(call->args[0].get());
                        IRValue* a1 = evalExpr(call->args[1].get());
                        llvm::Value* hPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get()))
                            if (llvm::Function* hf = ctx.module.getFunction(ve->name))
                                hPtr = ctx.builder.CreateBitCast(hf, i8p);
                        ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)), hPtr});
                    } else {
                        // use(middleware)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module);
                        if (!call || call->args.empty()) return nullptr;
                        llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                            if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                                mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                        ctx.builder.CreateCall(fn, {base, mwPtr});
                    }
                    return nullptr;
                };
            };

            ci.builtinMethods["route"] = makeRouteMethod("aiml_http_route_route", true);
            ci.builtinMethods["use"]   = makeRouteMethod("aiml_http_route_use",   false);
            // group(prefix, middleware?) → http_route_handle
            ci.builtinMethods["group"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_route_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_route_group", ctx.module);
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                            mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(prefix)), mwPtr}));
            };
            // filesystem(prefix, dirs, opts) — static files under a group
            ci.builtinMethods["filesystem"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_route_filesystem");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_route_filesystem", ctx.module);
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                IRValue* a2 = evalExpr(call->args[2].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2))});
                return nullptr;
            };
            ctx.classes["http_route_handle"] = std::move(ci);
        }

        // ── http_response_handle ──────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_response_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // Property getters (zero-arg, called via resolveProperty → builtinMethods)
            ci.builtinMethods["status"]  = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_status", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["ok"]      = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_ok");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_ok", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"]    = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_body", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // headers field (returns AimlHttpMap* for chained dispatch)
            ci.builtinMethods["headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_get_headers", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // Methods: send, write, write_bytes, flush, close, free
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_send", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_write");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_write", ctx.module);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a))});
                }
                return nullptr;
            };
            ci.builtinMethods["flush"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_flush");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_flush", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_close");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_close", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_free");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_free", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            // resp.set_cookie(name, value, max_age, same_site) — Set-Cookie with secure
            // defaults (Path=/, HttpOnly, Secure, SameSite). max_age<0 → session cookie;
            // same_site "" → "Lax". Multiple calls emit multiple Set-Cookie headers.
            ci.builtinMethods["set_cookie"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_set_cookie");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i32, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_cookie", ctx.module);
                if (!call || call->args.size() < 2) return nullptr;
                llvm::Value* nameA = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* valA  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* ageA  = call->args.size() > 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())))
                    : llvm::ConstantInt::getSigned(i32, -1);
                if (ageA->getType()->isPointerTy()) ageA = ctx.builder.CreatePtrToInt(ageA, i32);
                llvm::Value* ssA = call->args.size() > 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[3].get())))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                ctx.builder.CreateCall(fn, {base, nameA, valA, ageA, ssA});
                return nullptr;
            };
            // Property setters (called via visitSetStmt → builtinSetters)
            ci.builtinSetters["status"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_set_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_status", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_body", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ctx.classes["http_response_handle"] = std::move(ci);
        }

        // ── http_request_handle ───────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_request_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // Property getters
            ci.builtinMethods["method"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_method");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_method", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["path"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_path");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_path", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_body", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["url"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_url");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_url", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // params / cookies — AimlHttpMap* for chained dispatch (req.params.get(k))
            ci.builtinMethods["params"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_params");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_params", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["cookies"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_cookies");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookies", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // req.header(name) — method with arg
            ci.builtinMethods["header"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_header", ctx.module);
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
            };
            // req.cookie(name) — method with arg
            ci.builtinMethods["cookie"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_cookie");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookie", ctx.module);
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
            };
            // req.client_ip() — trusted socket peer IP (ignores X-Forwarded-For)
            ci.builtinMethods["client_ip"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_client_ip");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_client_ip", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ctx.classes["http_request_handle"] = std::move(ci);
        }

        // ── http_client_handle ────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_client_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // headers field → AimlHttpMap* for chained dispatch
            ci.builtinMethods["headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_get_headers", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // client.send() → http_response_handle
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_send", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // Property setters
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_body", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_set_timeout");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_timeout", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ctx.classes["http_client_handle"] = std::move(ci);
        }

        // ── fs_options ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "fs_options";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            ci.builtinSetters["index"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_index");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_index", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["spa"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_spa");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt8Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_spa", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (!v->getType()->isIntegerTy(8)) v = ctx.builder.CreateTrunc(v, ctx.builder.getInt8Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["cache_max_age"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_cache_max_age");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_cache_max_age", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ctx.classes["fs_options"] = std::move(ci);
        }
    }

    // -----------------------------------------------------------------------
    // math namespace + math value types (vec2/vec3/vec4/quat/mat4/ivec2/ivec3)
    // -----------------------------------------------------------------------
    {
        llvm::Type* f32  = llvm::Type::getFloatTy(ctx.context);
        llvm::Type* i32  = llvm::Type::getInt32Ty(ctx.context);
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* voidT = ctx.builder.getVoidTy();

        // Ensure struct types exist (getLLVMType has the same logic, call it to be safe)
        llvm::StructType* vec2Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
        llvm::StructType* vec3Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec3"));
        llvm::StructType* vec4Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec4"));
        llvm::StructType* quatTy  = static_cast<llvm::StructType*>(ctx.getLLVMType("quat"));
        llvm::StructType* mat4Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("mat4"));
        llvm::StructType* ivec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("ivec2"));
        llvm::StructType* ivec3Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("ivec3"));

        // ── Helper lambdas ────────────────────────────────────────────────

        // Store a loaded struct value to a temp alloca and return the alloca ptr
        auto storeToTmp = [](CompilerContext& ctx, llvm::Value* val, llvm::Type* ty) -> llvm::Value* {
            llvm::AllocaInst* a = ctx.createEntryAlloca(ty, "mtmp");
            ctx.builder.CreateStore(val, a);
            return a;
        };

        // Get or declare a void C function with given pointer arg types
        auto getVoidFn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(voidT, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        auto getF32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(f32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        auto getI32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };

        // Pointer types (opaque for passing structs by reference)
        llvm::Type* pVec2  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pVec3  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pVec4  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pQuat  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pMat4  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pIVec2 = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pIVec3 = llvm::PointerType::get(ctx.context, 0);
        (void)pVec2; (void)pVec3; (void)pVec4; (void)pQuat; (void)pMat4;
        (void)pIVec2; (void)pIVec3;

        // ── Register ClassInfo for each math value type ───────────────────
        // Members are x→0, y→1, z→2, w→3 for vec types.
        // Registering as non-builtin with a real StructType so resolveProperty
        // uses the GEP path for member access (v.x, v.y, ...).
        // mat4 uses builtinMethods for get/set (no plain member access).

        auto addMember = [&](CompilerContext::ClassInfo& ci, const std::string& nm, int idx, llvm::Type* ty, const std::string& typNm) {
            CompilerContext::StructMember m;
            m.index    = idx;
            m.type     = ty;
            m.typeName = typNm;
            m.access   = AccessLevel::Public;
            ci.members[nm] = m;
        };

        // vec2 {f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec2"; ci.type = vec2Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            ctx.classes["vec2"] = std::move(ci);
        }
        // vec3 {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec3"; ci.type = vec3Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            ctx.classes["vec3"] = std::move(ci);
        }
        // vec4 {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec4"; ci.type = vec4Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            addMember(ci, "w", 3, f32, "f32");
            ctx.classes["vec4"] = std::move(ci);
        }
        // quat {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "quat"; ci.type = quatTy; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            addMember(ci, "w", 3, f32, "f32");
            ctx.classes["quat"] = std::move(ci);
        }
        // mat4 {[16 x f32]} — get/set methods only, no plain member access
        {
            CompilerContext::ClassInfo ci;
            ci.name = "mat4"; ci.type = mat4Ty; ci.isBuiltin = true;
            ci.preLoadBase = false;

            ci.builtinMethods["get"] = [mat4Ty](CompilerContext& ctx, llvm::Value* baseAlloca,
                                                  AIML::AST::CallExpr* call,
                                                  std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                // args: row (i32), col (i32)
                if (!call || call->args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getFloatTy(), baseAlloca, "m_get_err"));
                AIML::IRValue* rowV = evalExpr(call->args[0].get());
                AIML::IRValue* colV = evalExpr(call->args[1].get());
                llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rowV));
                llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(colV));
                llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_get");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_get", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {baseAlloca, row, col}, "mat4_get"));
            };

            ci.builtinMethods["set"] = [mat4Ty](CompilerContext& ctx, llvm::Value* baseAlloca,
                                                  AIML::AST::CallExpr* call,
                                                  std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                // args: row (i32), col (i32), val (f32)
                if (!call || call->args.size() < 3) return ctx.ir_gen->getInt32(0);
                AIML::IRValue* rowV = evalExpr(call->args[0].get());
                AIML::IRValue* colV = evalExpr(call->args[1].get());
                AIML::IRValue* valV = evalExpr(call->args[2].get());
                llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rowV));
                llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(colV));
                llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valV));
                if (!val->getType()->isFloatTy())
                    val = ctx.builder.CreateSIToFP(val, ctx.builder.getFloatTy());
                llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_set");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getVoidTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(), ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_set", ctx.module);
                ctx.builder.CreateCall(fn, {baseAlloca, row, col, val});
                return ctx.ir_gen->getInt32(0);
            };

            ctx.classes["mat4"] = std::move(ci);
        }
        // ivec2 {i32, i32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ivec2"; ci.type = ivec2Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, i32, "i32");
            addMember(ci, "y", 1, i32, "i32");
            ctx.classes["ivec2"] = std::move(ci);
        }
        // ivec3 {i32, i32, i32, i32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ivec3"; ci.type = ivec3Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, i32, "i32");
            addMember(ci, "y", 1, i32, "i32");
            addMember(ci, "z", 2, i32, "i32");
            ctx.classes["ivec3"] = std::move(ci);
        }

        // ── math namespace functions ──────────────────────────────────────
        auto& math = ctx.namespaces["math"];

        // Helper: cast a loaded value to f32 (from f64 or integer literals)
        auto toF32 = [](CompilerContext& ctx, llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isFloatTy()) return v;
            if (v->getType()->isDoubleTy()) return ctx.builder.CreateFPTrunc(v, ctx.builder.getFloatTy());
            if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, ctx.builder.getFloatTy());
            return v;
        };

        // Constructors ─────────────────────────────────────────────────────
        math.builtinFuncs["vec2"] = [vec2Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : ctx.builder.CreateFPCast(llvm::ConstantFP::get(ctx.context, llvm::APFloat(0.0f)), ctx.builder.getFloatTy());
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : ctx.builder.CreateFPCast(llvm::ConstantFP::get(ctx.context, llvm::APFloat(0.0f)), ctx.builder.getFloatTy());
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["vec3"] = [vec3Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto zero = [&]{ return llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0); };
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : zero();
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : zero();
            llvm::Value* z = args.size()>2 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))) : zero();
            llvm::Value* v = llvm::UndefValue::get(vec3Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            v = ctx.builder.CreateInsertValue(v, z, 2);
            v = ctx.builder.CreateInsertValue(v, zero(), 3);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["vec4"] = [vec4Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto zero = [&]{ return llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0); };
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : zero();
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : zero();
            llvm::Value* z = args.size()>2 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))) : zero();
            llvm::Value* w = args.size()>3 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))) : zero();
            llvm::Value* v = llvm::UndefValue::get(vec4Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            v = ctx.builder.CreateInsertValue(v, z, 2);
            v = ctx.builder.CreateInsertValue(v, w, 3);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["quat_identity"] = [quatTy](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("aiml_math_quat_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_quat_identity", ctx.module);
            llvm::AllocaInst* out = ctx.createEntryAlloca(quatTy, "qi");
            ctx.builder.CreateCall(fn, {out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy, out, "qi_val"));
        };
        math.builtinFuncs["mat4_identity"] = [mat4Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_mat4_identity", ctx.module);
            llvm::AllocaInst* out = ctx.createEntryAlloca(mat4Ty, "mi");
            ctx.builder.CreateCall(fn, {out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty, out, "mi_val"));
        };
        math.builtinFuncs["ivec2"] = [ivec2Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto getI32 = [&](int idx) -> llvm::Value* {
                if ((int)args.size() <= idx) return ctx.builder.getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[idx]));
                if (!v->getType()->isIntegerTy(32)) v = ctx.builder.CreateSExtOrTrunc(v, ctx.builder.getInt32Ty());
                return v;
            };
            llvm::Value* v = llvm::UndefValue::get(ivec2Ty);
            v = ctx.builder.CreateInsertValue(v, getI32(0), 0);
            v = ctx.builder.CreateInsertValue(v, getI32(1), 1);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["ivec3"] = [ivec3Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto getI32 = [&](int idx) -> llvm::Value* {
                if ((int)args.size() <= idx) return ctx.builder.getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[idx]));
                if (!v->getType()->isIntegerTy(32)) v = ctx.builder.CreateSExtOrTrunc(v, ctx.builder.getInt32Ty());
                return v;
            };
            llvm::Value* v = llvm::UndefValue::get(ivec3Ty);
            v = ctx.builder.CreateInsertValue(v, getI32(0), 0);
            v = ctx.builder.CreateInsertValue(v, getI32(1), 1);
            v = ctx.builder.CreateInsertValue(v, getI32(2), 2);
            v = ctx.builder.CreateInsertValue(v, ctx.builder.getInt32(0), 3);
            return ctx.ir_gen->wrap(v);
        };

        // ── Scalar math (libm wrappers or LLVM intrinsics) ────────────────
        // to_radians / to_degrees
        math.builtinFuncs["to_radians"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pi = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.14159265358979323846f);
            llvm::Value* d180 = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 180.0f);
            return ctx.ir_gen->wrap(ctx.builder.CreateFMul(v, ctx.builder.CreateFDiv(pi, d180)));
        };
        math.builtinFuncs["to_degrees"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pi = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.14159265358979323846f);
            llvm::Value* d180 = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 180.0f);
            return ctx.ir_gen->wrap(ctx.builder.CreateFMul(v, ctx.builder.CreateFDiv(d180, pi)));
        };

        // Declare libm f32 functions once
        auto libmF1 = [&](const char* aimlName, const char* cName) {
            math.builtinFuncs[aimlName] = [cName, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Function* fn = ctx.module.getFunction(cName);
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(), {ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, cName, ctx.module);
                llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                             : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}));
            };
        };
        libmF1("sin",   "sinf");
        libmF1("cos",   "cosf");
        libmF1("tan",   "tanf");
        libmF1("asin",  "asinf");
        libmF1("acos",  "acosf");
        libmF1("sqrt",  "sqrtf");
        libmF1("exp",   "expf");
        libmF1("log",   "logf");
        libmF1("log2",  "log2f");
        libmF1("floor", "floorf");
        libmF1("ceil",  "ceilf");
        libmF1("round", "roundf");
        libmF1("abs",   "fabsf");

        // atan2 (2-arg)
        math.builtinFuncs["atan2"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("atan2f");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "atan2f", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // pow (2-arg)
        math.builtinFuncs["pow"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("powf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "powf", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // fmod
        math.builtinFuncs["fmod"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("fmodf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "fmodf", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // math.random(lo, hi) -> f32 — uniform in [lo, hi). Defaults lo=0, hi=1.
        math.builtinFuncs["random"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("aiml_math_random_f32");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "aiml_math_random_f32", ctx.module);
            llvm::Value* lo = args.size() > 0 ? toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                              : llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
            llvm::Value* hi = args.size() > 1 ? toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
                                              : llvm::ConstantFP::get(ctx.builder.getFloatTy(), 1.0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {lo, hi}));
        };

        // math.random_int(lo, hi) -> i32 — uniform integer in [lo, hi). Defaults lo=0.
        math.builtinFuncs["random_int"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_math_random_int");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {i32t, i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_math_random_int", ctx.module);
            auto toI32 = [&](llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isIntegerTy(32)) return v;
                if (v->getType()->isIntegerTy(64)) return ctx.builder.CreateTrunc(v, i32t);
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i32t);
                return v;
            };
            llvm::Value* lo = args.size() > 0 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                              : llvm::ConstantInt::get(i32t, 0);
            llvm::Value* hi = args.size() > 1 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
                                              : llvm::ConstantInt::get(i32t, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {lo, hi}));
        };

        // math.random_hex(nbytes: i32) → string (hex of cryptographically-secure random bytes, RAND_bytes)
        math.builtinFuncs["random_hex"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_math_random_hex");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(p, {i32t}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_math_random_hex", ctx.module);
            auto toI32 = [&](llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isIntegerTy(32)) return v;
                if (v->getType()->isIntegerTy(64)) return ctx.builder.CreateTrunc(v, i32t);
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i32t);
                return v;
            };
            llvm::Value* n = args.size() > 0 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                             : llvm::ConstantInt::get(i32t, 16);
            // spec/61 §7: aiml_math_random_hex returns an OWNED refcounted string (to_hex_rc →
            // aiml_str_alloc). Register it as an owned string temp so a consuming decl/set MOVES it
            // (one owned ref, one drop) and a transient is decref'd at stmt end — else the initial
            // reference leaks (auth hash_password_new salt: 41 B/call, leak_probes/round3 §2).
            llvm::Value* rhres = ctx.builder.CreateCall(fn, {n});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                ctx.stmtTempStack.back().push_back(rhres);
            return ctx.ir_gen->wrap(rhres);
        };

        // fract = x - floor(x)
        math.builtinFuncs["fract"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Function* flFn = ctx.module.getFunction("floorf");
            if (!flFn) flFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{ctx.builder.getFloatTy()},false),
                llvm::Function::ExternalLinkage,"floorf",ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateFSub(v, ctx.builder.CreateCall(flFn,{v})));
        };

        // sign
        math.builtinFuncs["sign"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pos = ctx.builder.CreateFCmpOGT(v, llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* neg = ctx.builder.CreateFCmpOLT(v, llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* p = ctx.builder.CreateUIToFP(pos, ctx.builder.getFloatTy());
            llvm::Value* n = ctx.builder.CreateUIToFP(neg, ctx.builder.getFloatTy());
            return ctx.ir_gen->wrap(ctx.builder.CreateFSub(p, n));
        };

        // min / max (f32)
        // Helper: emit call to aiml_math_fmin_list / aiml_math_fmax_list(float* data, i32 len)
        auto callListReduce = [](CompilerContext& ctx, const char* cName,
                                  llvm::Value* dataPtr, llvm::Value* len) -> llvm::Value* {
            auto* op = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction(cName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {op, ctx.builder.getInt32Ty()}, false),
                llvm::Function::ExternalLinkage, cName, ctx.module);
            return ctx.builder.CreateCall(fn, {dataPtr, len});
        };
        // Helper: detect if a loaded value is a list struct {i32, i32, i8*}
        auto isListStruct = [](llvm::Type* ty) -> bool {
            auto* st = llvm::dyn_cast<llvm::StructType>(ty);
            return st && st->getNumElements() == 3 &&
                   st->getElementType(0)->isIntegerTy(32) &&
                   st->getElementType(1)->isIntegerTy(32) &&
                   st->getElementType(2)->isPointerTy();
        };

        math.builtinFuncs["min"] = [toF32, callListReduce, isListStruct]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // list<f32>: single struct arg {i32, i32, i8*}
            if (args.size() == 1 && isListStruct(a0->getType())) {
                llvm::Value* len  = ctx.builder.CreateExtractValue(a0, {0u});
                llvm::Value* data = ctx.builder.CreateExtractValue(a0, {2u});
                return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmin_list", data, len));
            }
            // ptr+len: (i8*, i32)
            if (args.size() == 2) {
                llvm::Value* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                if (a0->getType()->isPointerTy() && a1->getType()->isIntegerTy(32))
                    return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmin_list", a0, a1));
            }
            // 2-scalar fallback
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* fa=g(0), *fb=g(1);
            return ctx.ir_gen->wrap(ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(fa,fb), fa, fb));
        };
        math.builtinFuncs["max"] = [toF32, callListReduce, isListStruct]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // list<f32>: single struct arg {i32, i32, i8*}
            if (args.size() == 1 && isListStruct(a0->getType())) {
                llvm::Value* len  = ctx.builder.CreateExtractValue(a0, {0u});
                llvm::Value* data = ctx.builder.CreateExtractValue(a0, {2u});
                return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmax_list", data, len));
            }
            // ptr+len: (i8*, i32)
            if (args.size() == 2) {
                llvm::Value* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                if (a0->getType()->isPointerTy() && a1->getType()->isIntegerTy(32))
                    return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmax_list", a0, a1));
            }
            // 2-scalar fallback
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* fa=g(0), *fb=g(1);
            return ctx.ir_gen->wrap(ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(fa,fb), fa, fb));
        };

        // clamp(x, lo, hi)
        math.builtinFuncs["clamp"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* x=g(0), *lo=g(1), *hi=g(2);
            llvm::Value* clamped = ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(x,lo), lo,
                                   ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(x,hi), hi, x));
            return ctx.ir_gen->wrap(clamped);
        };

        // smoothstep(edge0, edge1, x)
        math.builtinFuncs["smoothstep"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* e0=g(0), *e1=g(1), *x=g(2);
            // t = clamp((x-e0)/(e1-e0), 0, 1)
            llvm::Value* range = ctx.builder.CreateFSub(e1, e0);
            llvm::Value* t = ctx.builder.CreateFDiv(ctx.builder.CreateFSub(x, e0), range);
            llvm::Value* zero = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
            llvm::Value* one  = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 1.0);
            llvm::Value* two  = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 2.0);
            llvm::Value* three= llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.0);
            t = ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(t,zero),zero,
                ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(t,one),one,t));
            // t*t*(3 - 2*t)
            llvm::Value* result = ctx.builder.CreateFMul(t, ctx.builder.CreateFMul(t,
                ctx.builder.CreateFSub(three, ctx.builder.CreateFMul(two, t))));
            return ctx.ir_gen->wrap(result);
        };

        // lerp scalar: lerp(a, b, t) = a + (b-a)*t
        math.builtinFuncs["lerp"] = [toF32, vec2Ty, vec3Ty, vec4Ty, quatTy, storeToTmp]
                                    (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 3) return ctx.ir_gen->getInt32(0);
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* t = toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])));
            llvm::Type*  ty = a->getType();

            // Scalar f32
            if (ty->isFloatTy() || ty->isDoubleTy()) {
                llvm::Value* av = toF32(ctx,a); llvm::Value* bv = toF32(ctx,b);
                return ctx.ir_gen->wrap(ctx.builder.CreateFAdd(av, ctx.builder.CreateFMul(ctx.builder.CreateFSub(bv,av), t)));
            }
            // Vec types — call C runtime
            const char* fnName = nullptr;
            llvm::Type* structTy = nullptr;
            if (ty == vec2Ty) { fnName="aiml_math_vec2_lerp"; structTy=vec2Ty; }
            else if (ty == vec3Ty) { fnName="aiml_math_vec3_lerp"; structTy=vec3Ty; }
            else if (ty == vec4Ty) { fnName="aiml_math_vec4_lerp"; structTy=vec4Ty; }
            else if (ty == quatTy) { fnName="aiml_math_quat_lerp"; structTy=quatTy; }
            if (!fnName) return ctx.ir_gen->getInt32(0);
            auto opty = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{opty,opty,ctx.builder.getFloatTy(),opty},false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            llvm::Value* pa = storeToTmp(ctx,a,structTy);
            llvm::Value* pb = storeToTmp(ctx,b,structTy);
            llvm::AllocaInst* out = ctx.createEntryAlloca(structTy);
            ctx.builder.CreateCall(fn,{pa,pb,t,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(structTy,out));
        };

        // ── Vector functions ──────────────────────────────────────────────

        // dot (works for vec2, vec3)
        math.builtinFuncs["dot"] = [vec2Ty, vec3Ty, storeToTmp]
                                   (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Type* ty = a->getType();
            const char* nm = (ty == vec2Ty) ? "aiml_math_vec2_dot" : "aiml_math_vec3_dot";
            llvm::Type* st = (ty == vec2Ty) ? vec2Ty : vec3Ty;
            auto op = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module.getFunction(nm);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{op,op},false),
                llvm::Function::ExternalLinkage, nm, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),storeToTmp(ctx,b,st)}));
        };

        // cross
        math.builtinFuncs["cross"] = [vec3Ty, storeToTmp]
                                     (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module.getFunction("aiml_math_vec3_cross");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage, "aiml_math_vec3_cross", ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // length / length_sq / normalize / distance
        auto makeVecF = [&](const std::string& mathName, const char* c2, const char* c3, bool returnsVec,
                            llvm::StructType* fallbackSt, bool isScalar2arg = false) {
            math.builtinFuncs[mathName] = [c2,c3,returnsVec,fallbackSt,vec2Ty,vec3Ty,storeToTmp,isScalar2arg]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
                llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Type* ty=a->getType();
                bool isV2 = (ty==vec2Ty);
                const char* nm = isV2 ? c2 : c3;
                llvm::Type* st = isV2 ? (llvm::Type*)vec2Ty : (llvm::Type*)vec3Ty;
                if (!nm) { nm=c3; st=fallbackSt; }
                auto op = llvm::PointerType::get(ctx.context,0);
                if (returnsVec) {
                    llvm::Function* fn=ctx.module.getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts={op,op};
                        if (isScalar2arg) pts={op,ctx.builder.getFloatTy(),op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module);
                    }
                    llvm::AllocaInst* out=ctx.createEntryAlloca(st);
                    if (isScalar2arg && args.size()>=2) {
                        llvm::Value* t2=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                        if (!t2->getType()->isFloatTy()) t2=ctx.builder.CreateSIToFP(t2,ctx.builder.getFloatTy());
                        ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),t2,out});
                    } else {
                        llvm::Value* b=args.size()>1?storeToTmp(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),st):nullptr;
                        if (b) ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),b,out});
                        else   ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),out});
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateLoad(static_cast<llvm::Type*>(st),out));
                } else {
                    llvm::Function* fn=ctx.module.getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts=(args.size()>1)?std::vector<llvm::Type*>{op,op}:std::vector<llvm::Type*>{op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module);
                    }
                    if (args.size()>1){
                        llvm::Value* b=storeToTmp(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),st);
                        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),b}));
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st)}));
                }
            };
        };

        makeVecF("length",    "aiml_math_vec2_length",    "aiml_math_vec3_length",    false, vec3Ty);
        makeVecF("length_sq", "aiml_math_vec2_length_sq", "aiml_math_vec3_length_sq", false, vec3Ty);
        makeVecF("normalize", "aiml_math_vec2_normalize", "aiml_math_vec3_normalize", true,  vec3Ty);
        makeVecF("distance",  "aiml_math_vec2_distance",  "aiml_math_vec3_distance",  false, vec3Ty);

        // angle (vec2 only)
        math.builtinFuncs["angle"] = [vec2Ty, storeToTmp]
                                     (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_angle",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty)}));
        };
        // perp (vec2 only)
        math.builtinFuncs["perp"] = [vec2Ty, storeToTmp]
                                    (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec2Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_perp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_perp",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };

        // reflect / refract
        math.builtinFuncs["reflect"] = [vec3Ty, storeToTmp]
                                       (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec3_reflect");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_reflect",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };
        math.builtinFuncs["refract"] = [vec3Ty, storeToTmp, toF32]
                                        (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<3) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec3_refract");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_refract",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* ior=toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),ior,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // ── Matrix functions ──────────────────────────────────────────────
        auto mathMat1V3Out = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, vec3Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                if (!args.empty()) {
                    llvm::Value* v=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    ctx.builder.CreateCall(fn,{storeToTmp(ctx,v,vec3Ty),out});
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMat1V3Out("mat4_translate", "aiml_math_mat4_translate");
        mathMat1V3Out("mat4_scale",     "aiml_math_mat4_scale");

        auto mathMatRotXYZ = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, toF32]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{ctx.builder.getFloatTy(),op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                llvm::Value* a=args.empty()?llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0):toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                ctx.builder.CreateCall(fn,{a,out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMatRotXYZ("mat4_rotate_x", "aiml_math_mat4_rotate_x");
        mathMatRotXYZ("mat4_rotate_y", "aiml_math_mat4_rotate_y");
        mathMatRotXYZ("mat4_rotate_z", "aiml_math_mat4_rotate_z");

        // mat4_rotate(axis, angle)
        math.builtinFuncs["mat4_rotate"] = [mat4Ty, vec3Ty, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_rotate");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_rotate",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_perspective(fov, aspect, near, far)
        math.builtinFuncs["mat4_perspective"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_perspective");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_perspective",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_ortho(l, r, b, t, near, far)
        math.builtinFuncs["mat4_ortho"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_ortho");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_ortho",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),g(4),g(5),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_lookat(eye, center, up)
        math.builtinFuncs["mat4_lookat"] = [mat4Ty, vec3Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_lookat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_lookat",ctx.module);
            auto gv=[&](int i)->llvm::Value*{return args.size()>(size_t)i?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])):llvm::UndefValue::get(vec3Ty);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,gv(0),vec3Ty),storeToTmp(ctx,gv(1),vec3Ty),storeToTmp(ctx,gv(2),vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_inverse / mat4_transpose
        auto mathMat1MatOut = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                if (!args.empty()) {
                    llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty),out});
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMat1MatOut("mat4_inverse",   "aiml_math_mat4_inverse");
        mathMat1MatOut("mat4_transpose", "aiml_math_mat4_transpose");

        // mat4_to_buffer
        math.builtinFuncs["mat4_to_buffer"] = [mat4Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_to_buffer");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(op,{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_buffer",ctx.module);
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(op)));
            llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty)}));
        };

        // mat4_to_quat
        math.builtinFuncs["mat4_to_quat"] = [mat4Ty, quatTy, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_to_quat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_quat",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            if (!args.empty()) {
                llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty),out});
            }
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // ── Quaternion functions ──────────────────────────────────────────
        // quat_axis_angle(axis, angle)
        math.builtinFuncs["quat_axis_angle"] = [quatTy, vec3Ty, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_axis_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_axis_angle",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_euler(x, y, z)
        math.builtinFuncs["quat_euler"] = [quatTy, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_euler");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_euler",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_mul / quat_normalize / quat_inverse / quat_to_mat4
        auto quatBin = [&](const std::string& nm, const char* cn) {
            math.builtinFuncs[nm] = [cn, quatTy, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module);
                llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
                llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
                llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),storeToTmp(ctx,b,quatTy),out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
            };
        };
        auto quatUn = [&](const std::string& nm, const char* cn) {
            math.builtinFuncs[nm] = [cn, quatTy, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module);
                llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
                llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
            };
        };
        quatBin("quat_mul",  "aiml_math_quat_mul");
        quatUn ("quat_normalize", "aiml_math_quat_normalize");
        quatUn ("quat_inverse",   "aiml_math_quat_inverse");

        // quat_rotate_vec3(q, v)
        math.builtinFuncs["quat_rotate_vec3"] = [quatTy, vec3Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_rotate_vec3");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_rotate_vec3",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            llvm::Value* v=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):llvm::UndefValue::get(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),storeToTmp(ctx,v,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // quat_to_mat4
        math.builtinFuncs["quat_to_mat4"] = [quatTy, mat4Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_to_mat4");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_to_mat4",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // quat_slerp
        math.builtinFuncs["quat_slerp"] = [quatTy, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_slerp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_slerp",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
            llvm::Value* t=args.size()>2?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.5);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),storeToTmp(ctx,b,quatTy),t,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // ── Integer vector ops ────────────────────────────────────────────
        math.builtinFuncs["idiv"] = [ivec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_div");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getInt32Ty(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_div",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* s=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):ctx.builder.getInt32(1);
            if (s->getType()->isFloatTy()||s->getType()->isDoubleTy()) s=ctx.builder.CreateFPToSI(s,ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),s,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["iadd"] = [ivec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_add");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_add",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),storeToTmp(ctx,b,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["ivec2_to_vec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_to_vec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_to_vec2",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };
        math.builtinFuncs["vec2_to_ivec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_to_ivec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_to_ivec2",ctx.module);
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
    } // end math namespace

    // -----------------------------------------------------------------------
    // hash namespace
    // -----------------------------------------------------------------------
    {
        auto& hash = ctx.namespaces["hash"];
        auto* i32t = ctx.builder.getInt32Ty();

        /* Helper: declare or get (ptr→ptr) function for 1-arg hash functions */
        auto declHash1 = [&](const char* name) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::PointerType::get(ctx.context,0),
                                            {llvm::PointerType::get(ctx.context,0)}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        /* Helper: declare or get (ptr,ptr→ptr) function for 2-arg hash functions */
        auto declHash2 = [&](const char* name) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            auto* p = llvm::PointerType::get(ctx.context,0);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(p, {p, p}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };

#define REG_HASH1(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash1](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash1(cName); \
            auto* p = llvm::PointerType::get(ctx.context,0); \
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            /* spec/61 §7: owned rc-string result → register as owned string temp (moved by a consuming */ \
            /* decl/set, decref'd at stmt end if transient) — else the initial ref leaks (hash.* surface). */ \
            llvm::Value* hres = ctx.builder.CreateCall(fn, {a}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) ctx.stmtTempStack.back().push_back(hres); \
            return ctx.ir_gen->wrap(hres); \
        };

/* Buffer-aware hex digest: if the argument is a buffer<u8>, hash its raw bytes
 * (data ptr + size, binary-safe) via aiml_hash_hex_buf; otherwise the normal
 * null-terminated string path.  Keeps a single hash.<algo> surface — no per-type
 * or per-class hash methods. */
#define REG_HASH1B(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash1](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            if (a && a->getType() == ctx.getLLVMType("buffer")) { \
                llvm::Value* dataP = ctx.builder.CreateExtractValue(a, {4}, "hash_buf_data"); \
                llvm::Value* lenV  = ctx.builder.CreateExtractValue(a, {0}, "hash_buf_len"); \
                llvm::Function* bf = ctx.module.getFunction("aiml_hash_hex_buf"); \
                if (!bf) bf = llvm::Function::Create( \
                    llvm::FunctionType::get(p, {p, p, i32}, false), \
                    llvm::Function::ExternalLinkage, "aiml_hash_hex_buf", ctx.module); \
                llvm::Value* algo = ctx.builder.CreateGlobalStringPtr(aimlName); \
                llvm::Value* hbres = ctx.builder.CreateCall(bf, {algo, dataP, lenV}); \
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) ctx.stmtTempStack.back().push_back(hbres); \
                return ctx.ir_gen->wrap(hbres); \
            } \
            auto* fn = declHash1(cName); \
            /* spec/61 §7: owned rc-string → register as owned string temp (round3 §1/§2 leak class). */ \
            llvm::Value* hsres = ctx.builder.CreateCall(fn, {a}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) ctx.stmtTempStack.back().push_back(hsres); \
            return ctx.ir_gen->wrap(hsres); \
        };

#define REG_HASH2(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash2](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash2(cName); \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                               : llvm::Constant::getNullValue(p); \
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                               : llvm::Constant::getNullValue(p); \
            /* spec/61 §7: owned rc-string → register as owned string temp (round3 §1/§2 leak class). */ \
            llvm::Value* h2res = ctx.builder.CreateCall(fn, {a0, a1}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) ctx.stmtTempStack.back().push_back(h2res); \
            return ctx.ir_gen->wrap(h2res); \
        };

        REG_HASH1B("sha256",       "aiml_hash_sha256")
        REG_HASH1B("sha512",       "aiml_hash_sha512")
        REG_HASH1B("sha3_256",     "aiml_hash_sha3_256")
        REG_HASH1("keccak256",     "aiml_hash_keccak256")
        REG_HASH1("ripemd160",     "aiml_hash_ripemd160")
        REG_HASH1B("blake2b",      "aiml_hash_blake2b")
        REG_HASH1B("blake2s",      "aiml_hash_blake2s")
        REG_HASH1B("md5",          "aiml_hash_md5")
        REG_HASH1("sha256_raw",    "aiml_hash_sha256_raw")
        REG_HASH1("sha512_raw",    "aiml_hash_sha512_raw")
        REG_HASH1("sha3_256_raw",  "aiml_hash_sha3_256_raw")
        REG_HASH1("keccak256_raw", "aiml_hash_keccak256_raw")
        REG_HASH1("ripemd160_raw", "aiml_hash_ripemd160_raw")
        REG_HASH1("blake2b_raw",   "aiml_hash_blake2b_raw")
        REG_HASH2("hmac_sha256",      "aiml_hash_hmac_sha256")
        REG_HASH2("hmac_sha512",      "aiml_hash_hmac_sha512")
        REG_HASH2("hmac_sha256_raw",  "aiml_hash_hmac_sha256_raw")

        // hash.const_time_eq(a: string, b: string) → i32 (1 = equal, constant-time)
        hash.builtinFuncs["const_time_eq"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_hash_const_time_eq");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i32, {p, p}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_hash_const_time_eq", ctx.module);
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::Constant::getNullValue(p);
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : llvm::Constant::getNullValue(p);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a0, a1}));
        };
        // hash.pbkdf2(password: string, salt: string, iterations: i32) → string (hex)
        hash.builtinFuncs["pbkdf2"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_hash_pbkdf2");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(p, {p, p, i32}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_hash_pbkdf2", ctx.module);
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::Constant::getNullValue(p);
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : llvm::Constant::getNullValue(p);
            llvm::Value* a2 = args.size() > 2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])) : ctx.builder.getInt32(0);
            // spec/61 §7: aiml_hash_pbkdf2 returns an OWNED refcounted string (to_hex_rc). Register it
            // as an owned string temp (see math.random_hex) so a consuming decl/set MOVES it — else the
            // derived-key reference leaks (auth hash_password_new/verify_password: 73 B/call, round3 §2).
            llvm::Value* pkres = ctx.builder.CreateCall(fn, {a0, a1, a2});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                ctx.stmtTempStack.back().push_back(pkres);
            return ctx.ir_gen->wrap(pkres);
        };

#undef REG_HASH1
#undef REG_HASH2

        /* crc32: returns i32 */
        hash.builtinFuncs["crc32"] = [i32t](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction("aiml_hash_crc32");
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(i32t, {p}, false),
                    llvm::Function::ExternalLinkage, "aiml_hash_crc32", ctx.module);
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                             : llvm::Constant::getNullValue(p);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a}));
        };
    } // end hash namespace

    // ── compress namespace ────────────────────────────────────────────────
    // NOTE: Handler lambdas must NOT capture local variables by reference —
    // they are stored and called later after registerBuiltins() has returned.
    // All LLVM types must be recomputed from the ctx parameter inside the lambda.
    {
        auto& compress = ctx.namespaces["compress"];

        /* Helper: memcpy a heap AimlCompressBuffer* into a fresh stack alloca and return it.
         * visitDeclStmt will then memcpy the alloca content into the target buffer<u8> alloca. */
#define COMP_HEAP_TO_ALLOCA(heapPtr) \
        ([&]() -> llvm::Value* { \
            auto* _p   = llvm::PointerType::get(ctx.context, 0); \
            auto* _i64 = ctx.builder.getInt64Ty(); \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            auto* _out = ctx.createEntryAlloca(_bt, "comp_alloca"); \
            uint64_t _sz = ctx.module.getDataLayout().getTypeAllocSize(_bt); \
            llvm::Function* _mcf = ctx.getFunction("memcpy", _p, {_p, _p, _i64}); \
            ctx.builder.CreateCall(_mcf, {_out, (heapPtr), ctx.getSizeConst(_sz)}); \
            return _out; \
        }())

        /* Helper: store input buffer<u8> struct value into a temp alloca → AimlCompressBuffer* */
#define COMP_IN_ALLOCA(argVal) \
        ([&]() -> llvm::Value* { \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            auto* _tmp = ctx.createEntryAlloca(_bt, "decomp_in"); \
            ctx.builder.CreateStore((argVal), _tmp); \
            return _tmp; \
        }())

        /* compress.X(data, [level]): string/buffer<u8> → buffer<u8>
         * Always passes len=-1 so the C runtime uses strlen for string inputs. */
#define REG_COMP(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p, i32, i32}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* data  = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                                 : llvm::Constant::getNullValue(p); \
            llvm::Value* level = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                                 : ctx.builder.getInt32(0); \
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, ctx.builder.getInt32(-1), level}); \
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr)); \
        };

        /* compress.X_decompress_str(buf): buffer<u8> → string (char*) */
#define REG_DECOMP_STR(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* inPtr = (args.size() > 0) \
                ? COMP_IN_ALLOCA(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) \
                : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {inPtr})); \
        };

        /* compress.X_decompress(buf): buffer<u8> → buffer<u8> */
#define REG_DECOMP_BUF(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* inPtr = (args.size() > 0) \
                ? COMP_IN_ALLOCA(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) \
                : llvm::Constant::getNullValue(p); \
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {inPtr}); \
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr)); \
        };

        /* compress.tar_gz / zip: (i8* src, i8* dst) → i32 */
#define REG_ARCHIVE(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(i32, {p, p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                              : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a0, a1})); \
        };

        REG_COMP("gzip",    "aiml_compress_gzip")
        REG_COMP("deflate", "aiml_compress_deflate")
        REG_COMP("zlib",    "aiml_compress_zlib")
        REG_COMP("brotli",  "aiml_compress_brotli")
        REG_COMP("zstd",    "aiml_compress_zstd")

        REG_DECOMP_BUF("gunzip",                "aiml_compress_gunzip")
        REG_DECOMP_STR("gunzip_str",            "aiml_compress_gunzip_str")
        REG_DECOMP_BUF("inflate",               "aiml_compress_inflate")
        REG_DECOMP_STR("inflate_str",           "aiml_compress_inflate_str")
        REG_DECOMP_BUF("zlib_decompress",       "aiml_compress_zlib_decompress")
        REG_DECOMP_STR("zlib_decompress_str",   "aiml_compress_zlib_decompress_str")
        REG_DECOMP_BUF("brotli_decompress",     "aiml_compress_brotli_decompress")
        REG_DECOMP_STR("brotli_decompress_str", "aiml_compress_brotli_decompress_str")
        REG_DECOMP_BUF("zstd_decompress",       "aiml_compress_zstd_decompress")
        REG_DECOMP_STR("zstd_decompress_str",   "aiml_compress_zstd_decompress_str")
        REG_DECOMP_BUF("lz4_decompress",        "aiml_compress_lz4_decompress")
        REG_DECOMP_STR("lz4_decompress_str",    "aiml_compress_lz4_decompress_str")

        /* lz4 compress: (i8* data, i32 len) → buffer<u8>  — no level parameter */
        compress.builtinFuncs["lz4"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_compress_lz4");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p, {p, i32}, false),
                llvm::Function::ExternalLinkage, "aiml_compress_lz4", ctx.module);
            llvm::Value* data = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : llvm::Constant::getNullValue(p);
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, ctx.builder.getInt32(-1)});
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr));
        };

        REG_ARCHIVE("tar_gz",   "aiml_compress_tar_gz")
        REG_ARCHIVE("untar_gz", "aiml_compress_untar_gz")
        REG_ARCHIVE("zip",      "aiml_compress_zip")
        REG_ARCHIVE("unzip",    "aiml_compress_unzip")

        /* compress.zip_entries(path) → list<ptr<zip_entry>> */
        compress.builtinFuncs["zip_entries"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "zip_entries_list");
            ListImpl::initList(ctx, listAlloca, p, 8);
            llvm::Value* pathStr = args.empty() ? llvm::Constant::getNullValue(p)
                                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fn = ctx.getFunction("aiml_compress_zip_entries_fill",
                ctx.builder.getVoidTy(), {p, p});
            ctx.builder.CreateCall(fn, {pathStr, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);
        };

        /* compress.tar_entries(path) → list<ptr<tar_entry>> */
        compress.builtinFuncs["tar_entries"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "tar_entries_list");
            ListImpl::initList(ctx, listAlloca, p, 8);
            llvm::Value* pathStr = args.empty() ? llvm::Constant::getNullValue(p)
                                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fn = ctx.getFunction("aiml_compress_tar_entries_fill",
                ctx.builder.getVoidTy(), {p, p});
            ctx.builder.CreateCall(fn, {pathStr, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);
        };

#undef REG_COMP
#undef REG_DECOMP_STR
#undef REG_DECOMP_BUF
#undef REG_ARCHIVE
#undef COMP_HEAP_TO_ALLOCA
#undef COMP_IN_ALLOCA
    } // end compress namespace

    // =======================================================================
    // ai — native ML acceleration (Spec 51)
    // =======================================================================
    {
        auto& ai = ctx.namespaces["ai"];

        // Helpers: extract struct fields from either a loaded struct (extractvalue)
        // or a pointer to a struct (GEP + load). Args can arrive as either depending
        // on whether they come from local variables (struct) or model/class fields (ptr).
        auto getBufferData = [](CompilerContext& ctx, llvm::Value* val) -> llvm::Value* {
            if (val->getType()->isAggregateType()) {
                return ctx.builder.CreateExtractValue(val, {4}, "buf_data");
            }
            // Pointer to buffer struct — GEP field 4 + load
            auto* bufTy = static_cast<llvm::StructType*>(ctx.getLLVMType("buffer"));
            auto* gep = ctx.builder.CreateStructGEP(bufTy, val, 4, "buf_data_gep");
            return ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), gep, "buf_data");
        };
        auto getListField = [](CompilerContext& ctx, llvm::Value* val, unsigned idx,
                               const char* name) -> llvm::Value* {
            if (val->getType()->isAggregateType()) {
                return ctx.builder.CreateExtractValue(val, {idx}, name);
            }
            // Pointer to list struct — GEP + load
            auto* listTy = ListImpl::getListStructType(ctx);
            auto* gep = ctx.builder.CreateStructGEP(listTy, val, idx, (std::string(name) + "_gep").c_str());
            llvm::Type* fieldTy = listTy->getElementType(idx);
            return ctx.builder.CreateLoad(fieldTy, gep, name);
        };

        // ai.embedding_lookup(buffer_var, token_id, dim) → list<f32>
        ai.builtinFuncs["embedding_lookup"] = [getBufferData](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* i64ty = ctx.builder.getInt64Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* bufVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* tokenId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* dim     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            // Allocate result list<f32>: {size=dim, capacity=dim, data=malloc(dim*4)}
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca = ctx.createEntryAlloca(listTy, "emb_lookup_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.builder.CreateZExt(dim, i64ty), ctx.builder.getInt64(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {i64ty}), {totalBytes}, "emb_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* lookupFn = ctx.getFunction("aiml_ai_embedding_lookup",
                ctx.builder.getVoidTy(), {i8p, i32ty, i32ty, ptrTy});
            ctx.builder.CreateCall(lookupFn, {flatPtr, tokenId, dim, dataPtr});

            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "emb_lookup_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.embedding_update(buffer_var, token_id, gradient_list, lr, dim)
        ai.builtinFuncs["embedding_update"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* bufVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* tokenId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));

            llvm::Value* gradVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* gradPtr = getListField(ctx, gradVal, 2, "grad_data");

            llvm::Value* lr  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            if (lr->getType()->isDoubleTy())
                lr = ctx.builder.CreateFPTrunc(lr, f32ty);
            else if (lr->getType()->isIntegerTy())
                lr = ctx.builder.CreateSIToFP(lr, f32ty);

            llvm::Function* updateFn = ctx.getFunction("aiml_ai_embedding_update",
                ctx.builder.getVoidTy(), {i8p, i32ty, ptrTy, f32ty, i32ty});
            ctx.builder.CreateCall(updateFn, {flatPtr, tokenId, gradPtr, lr, dim});
            return nullptr;
        };

        // ai.dot(list_a, list_b, dim) → f32
        ai.builtinFuncs["dot"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* aVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* aPtr = getListField(ctx, aVal, 2, "dot_a");

            llvm::Value* bVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* bPtr = getListField(ctx, bVal, 2, "dot_b");

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::Function* dotFn = ctx.getFunction("aiml_ai_dot", f32ty, {ptrTy, ptrTy, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(dotFn, {aPtr, bPtr, dim}, "dot_result"));
        };

        // ai.train_bpe_merges(texts_list, max_merges) → opaque ptr (AimlBpeMergeResult*)
        ai.builtinFuncs["train_bpe_merges"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTexts  = getListField(ctx, listVal, 0, "n_texts");
            llvm::Value* textsPtr = getListField(ctx, listVal, 2, "texts_ptr");

            llvm::Value* maxMerges = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));

            llvm::Function* trainFn = ctx.getFunction("aiml_bpe_train_merges",
                ptrTy, {ptrTy, i32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(trainFn, {textsPtr, nTexts, maxMerges}, "bpe_result"));
        };

        // ai.bpe_merge_count(result_ptr) → i32
        ai.builtinFuncs["bpe_merge_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* countGep = ctx.builder.CreateStructGEP(resTy, ptr, 2, "merge_count");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i32ty, countGep));
        };

        // ai.bpe_merge_left(result_ptr, index) → string
        ai.builtinFuncs["bpe_merge_left"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* leftArrGep = ctx.builder.CreateStructGEP(resTy, ptr, 0);
            llvm::Value* leftArr = ctx.builder.CreateLoad(ptrTy, leftArrGep);
            llvm::Value* elemGep = ctx.builder.CreateGEP(ptrTy, leftArr, {idx}, "left_elem");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ptrTy, elemGep, "left_str"));
        };

        // ai.bpe_merge_right(result_ptr, index) → string
        ai.builtinFuncs["bpe_merge_right"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* rightArrGep = ctx.builder.CreateStructGEP(resTy, ptr, 1);
            llvm::Value* rightArr = ctx.builder.CreateLoad(ptrTy, rightArrGep);
            llvm::Value* elemGep = ctx.builder.CreateGEP(ptrTy, rightArr, {idx}, "right_elem");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ptrTy, elemGep, "right_str"));
        };

        // ai.bpe_merge_free(result_ptr)
        ai.builtinFuncs["bpe_merge_free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* freeFn = ctx.getFunction("aiml_bpe_merge_result_free",
                ctx.builder.getVoidTy(), {ptrTy});
            ctx.builder.CreateCall(freeFn, {ptr});
            return nullptr;
        };

        // ai.encode_dense(token_ids_list, buffer_var, dim) → list<f32>
        ai.builtinFuncs["encode_dense"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* i64ty = ctx.builder.getInt64Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();

            llvm::Value* idsVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTokens = getListField(ctx, idsVal, 0, "enc_n");
            llvm::Value* idsPtr  = getListField(ctx, idsVal, 2, "enc_ids");

            llvm::Value* bufVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5: a list-returning builtin must hand back a HEAP HANDLE under memV3 (a value
            // local/global adopts the returned rc), not an inline struct — else the adopt strides the
            // struct bytes as a handle pointer → crash (test_ai_dense `enc.len` on a global list<f32>).
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "enc_dense_h"))
                : ctx.createEntryAlloca(listTy, "enc_dense_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.builder.CreateZExt(dim, i64ty), ctx.builder.getInt64(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {i64ty}), {totalBytes}, "enc_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_encode_dense",
                ctx.builder.getVoidTy(), {ptrTy, i32ty, i8p, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {idsPtr, nTokens, flatPtr, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer (adopted by caller)
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "enc_dense_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.encode_dense_weighted(token_ids_list, buffer_var, dim) → list<f32>
        ai.builtinFuncs["encode_dense_weighted"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* i64ty = ctx.builder.getInt64Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();

            llvm::Value* idsVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTokens = getListField(ctx, idsVal, 0, "encw_n");
            llvm::Value* idsPtr  = getListField(ctx, idsVal, 2, "encw_ids");

            llvm::Value* bufVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // §5: heap handle under memV3 (see encode_dense) so the caller adopts an rc, not a struct.
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "encw_h"))
                : ctx.createEntryAlloca(listTy, "encw_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.builder.CreateZExt(dim, i64ty), ctx.builder.getInt64(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {i64ty}), {totalBytes}, "encw_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_encode_dense_weighted",
                ctx.builder.getVoidTy(), {ptrTy, i32ty, i8p, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {idsPtr, nTokens, flatPtr, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "encw_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.triplet_loss(q_list, pos_list, neg_list, margin, dim) → f32
        ai.builtinFuncs["triplet_loss"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* qVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* qPtr = getListField(ctx, qVal, 2, "tl_q");

            llvm::Value* posVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* posPtr = getListField(ctx, posVal, 2, "tl_pos");

            llvm::Value* negVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* negPtr = getListField(ctx, negVal, 2, "tl_neg");

            llvm::Value* margin = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (margin->getType()->isDoubleTy())
                margin = ctx.builder.CreateFPTrunc(margin, f32ty);
            else if (margin->getType()->isIntegerTy())
                margin = ctx.builder.CreateSIToFP(margin, f32ty);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            llvm::Function* fn = ctx.getFunction("aiml_ai_triplet_loss",
                f32ty, {ptrTy, ptrTy, ptrTy, f32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {qPtr, posPtr, negPtr, margin, dim}, "triplet_loss"));
        };

        // ai.triplet_grad(q_list, pos_list, neg_list, margin, dim) → list<f32>
        ai.builtinFuncs["triplet_grad"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* i64ty = ctx.builder.getInt64Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* qVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* qPtr = getListField(ctx, qVal, 2, "tg_q");
            llvm::Value* qDim = getListField(ctx, qVal, 0, "tg_dim");

            llvm::Value* posVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* posPtr = getListField(ctx, posVal, 2, "tg_pos");

            llvm::Value* negVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* negPtr = getListField(ctx, negVal, 2, "tg_neg");

            llvm::Value* margin = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (margin->getType()->isDoubleTy())
                margin = ctx.builder.CreateFPTrunc(margin, f32ty);
            else if (margin->getType()->isIntegerTy())
                margin = ctx.builder.CreateSIToFP(margin, f32ty);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // §5: heap handle under memV3 (see encode_dense) so the caller adopts an rc, not a struct.
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", ptrTy, {}), {}, "tg_h"))
                : ctx.createEntryAlloca(listTy, "tg_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.builder.CreateZExt(dim, i64ty), ctx.builder.getInt64(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {i64ty}), {totalBytes}, "tg_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_triplet_grad",
                ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy, f32ty, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {qPtr, posPtr, negPtr, margin, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "tg_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.sample_logits(tensor, temperature, top_k) → i32
        ai.builtinFuncs["sample_logits"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* tensorPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            llvm::Value* temperature = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (temperature->getType()->isDoubleTy())
                temperature = ctx.builder.CreateFPTrunc(temperature, f32ty);
            else if (temperature->getType()->isIntegerTy())
                temperature = ctx.builder.CreateSIToFP(temperature, f32ty);

            llvm::Value* topK = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::Function* fn = ctx.getFunction("aiml_ai_sample_logits",
                i32ty, {ptrTy, f32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {tensorPtr, temperature, topK}, "sampled_id"));
        };

    } // end ai namespace

    // =======================================================================
    // gltf — 3D asset loader (cgltf + stb_image backend)
    // All handle types are opaque i8* pointers with preLoadBase = true.
    // =======================================================================
    {
        // ── Helper: memcpy a heap AimlGltfBuffer* into a stack alloca ──────
#define GLTF_HEAP_TO_ALLOCA(heapPtr) \
        ([&]() -> llvm::Value* { \
            auto* _p  = llvm::PointerType::get(ctx.context, 0); \
            auto* _i64= ctx.builder.getInt64Ty(); \
            auto* _bt = ctx.getLLVMType("buffer"); \
            auto* _out= ctx.createEntryAlloca(_bt, "gltf_buf"); \
            uint64_t _sz = ctx.module.getDataLayout().getTypeAllocSize(_bt); \
            ctx.builder.CreateCall(ctx.getFunction("memcpy", _p, {_p, _p, _i64}), \
                {_out, (heapPtr), ctx.builder.getInt64(_sz)}); \
            return _out; \
        }())

        // ── Simple method macros ────────────────────────────────────────────
        // ci: ClassInfo&, aimlName: string literal, cName: C symbol string literal
#define GLTF_STR(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base})); \
        };

#define GLTF_I32M(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {p}), {base})); \
        };

#define GLTF_F32M(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* f32 = ctx.builder.getFloatTy(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, f32, {p}), {base})); \
        };

#define GLTF_PTRM(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base})); \
        };

        // Buffer-returning method: C fn returns heap AimlGltfBuffer* → memcpy to alloca
#define GLTF_BUFM(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* hp = ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base}); \
            return ctx.ir_gen->wrap(GLTF_HEAP_TO_ALLOCA(hp)); \
        };

        // List-fill method: C fn takes (scene/node/skin, list*) → void
#define GLTF_LIST(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p  = llvm::PointerType::get(ctx.context, 0); \
            auto* vt = ctx.builder.getVoidTy(); \
            llvm::StructType* lt = ListImpl::getListStructType(ctx); \
            llvm::Value* la = ctx.createEntryAlloca(lt, "gltf_list"); \
            ListImpl::initList(ctx, la, p, 8); \
            ctx.builder.CreateCall(ctx.getFunction(cName, vt, {p, p}), {base, la}); \
            return ctx.ir_gen->wrap(la); \
        };

        // ── gltf_scene ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_scene";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_I32M(ci, "valid",  "aiml_gltf_scene_valid")
            GLTF_STR (ci, "error",  "aiml_gltf_scene_error")
            GLTF_LIST(ci, "nodes",       "aiml_gltf_scene_nodes_fill")
            GLTF_LIST(ci, "meshes",      "aiml_gltf_scene_meshes_fill")
            GLTF_LIST(ci, "materials",   "aiml_gltf_scene_materials_fill")
            GLTF_LIST(ci, "animations",  "aiml_gltf_scene_animations_fill")
            GLTF_LIST(ci, "skins",       "aiml_gltf_scene_skins_fill")
            GLTF_LIST(ci, "images",      "aiml_gltf_scene_images_fill")

            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gltf_scene_free",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };

            ctx.classes["gltf_scene"] = std::move(ci);
        }

        // ── gltf_node ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_node";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",          "aiml_gltf_node_name")
            GLTF_F32M(ci, "translation_x", "aiml_gltf_node_translation_x")
            GLTF_F32M(ci, "translation_y", "aiml_gltf_node_translation_y")
            GLTF_F32M(ci, "translation_z", "aiml_gltf_node_translation_z")
            GLTF_F32M(ci, "rotation_x",    "aiml_gltf_node_rotation_x")
            GLTF_F32M(ci, "rotation_y",    "aiml_gltf_node_rotation_y")
            GLTF_F32M(ci, "rotation_z",    "aiml_gltf_node_rotation_z")
            GLTF_F32M(ci, "rotation_w",    "aiml_gltf_node_rotation_w")
            GLTF_F32M(ci, "scale_x",       "aiml_gltf_node_scale_x")
            GLTF_F32M(ci, "scale_y",       "aiml_gltf_node_scale_y")
            GLTF_F32M(ci, "scale_z",       "aiml_gltf_node_scale_z")
            GLTF_PTRM(ci, "mesh",          "aiml_gltf_node_mesh")
            GLTF_PTRM(ci, "skin",          "aiml_gltf_node_skin")
            GLTF_LIST(ci, "children",      "aiml_gltf_node_children_fill")
            GLTF_BUFM(ci, "world_matrix",  "aiml_gltf_node_world_matrix")

            ctx.classes["gltf_node"] = std::move(ci);
        }

        // ── gltf_mesh ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_mesh";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",            "aiml_gltf_mesh_name")
            GLTF_I32M(ci, "primitive_count", "aiml_gltf_mesh_primitive_count")

            ci.builtinMethods["primitive"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* idx = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_mesh_primitive", p, {p, i32}), {base, idx}));
            };

            ctx.classes["gltf_mesh"] = std::move(ci);
        }

        // ── gltf_primitive ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_primitive";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_BUFM(ci, "positions", "aiml_gltf_prim_positions")
            GLTF_BUFM(ci, "normals",   "aiml_gltf_prim_normals")
            GLTF_BUFM(ci, "tangents",  "aiml_gltf_prim_tangents")
            GLTF_BUFM(ci, "indices",   "aiml_gltf_prim_indices")
            GLTF_I32M(ci, "vertex_count", "aiml_gltf_prim_vertex_count")
            GLTF_I32M(ci, "index_count",  "aiml_gltf_prim_index_count")
            GLTF_PTRM(ci, "material",     "aiml_gltf_prim_material")
            GLTF_PTRM(ci, "to_gl_mesh",   "aiml_gltf_prim_to_gl_mesh")

            // uvs(set), colors(set), joints(set), weights(set): buffer return + 1 i32 arg
#define GLTF_BUF1(ci, aimlName, cName) \
            (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                llvm::Value* set = ctx.builder.getInt32(0); \
                if (call && !call->args.empty()) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) set = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                llvm::Value* hp = ctx.builder.CreateCall( \
                    ctx.getFunction(cName, p, {p, i32}), {base, set}); \
                return ctx.ir_gen->wrap(GLTF_HEAP_TO_ALLOCA(hp)); \
            };

            GLTF_BUF1(ci, "uvs",     "aiml_gltf_prim_uvs")
            GLTF_BUF1(ci, "colors",  "aiml_gltf_prim_colors")
            GLTF_BUF1(ci, "joints",  "aiml_gltf_prim_joints")
            GLTF_BUF1(ci, "weights", "aiml_gltf_prim_weights")
#undef GLTF_BUF1

            ctx.classes["gltf_primitive"] = std::move(ci);
        }

        // ── gltf_material ───────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_material";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",          "aiml_gltf_mat_name")
            GLTF_F32M(ci, "base_color_r",  "aiml_gltf_mat_base_color_r")
            GLTF_F32M(ci, "base_color_g",  "aiml_gltf_mat_base_color_g")
            GLTF_F32M(ci, "base_color_b",  "aiml_gltf_mat_base_color_b")
            GLTF_F32M(ci, "base_color_a",  "aiml_gltf_mat_base_color_a")
            GLTF_F32M(ci, "metallic",      "aiml_gltf_mat_metallic")
            GLTF_F32M(ci, "roughness",     "aiml_gltf_mat_roughness")
            GLTF_F32M(ci, "emissive_r",    "aiml_gltf_mat_emissive_r")
            GLTF_F32M(ci, "emissive_g",    "aiml_gltf_mat_emissive_g")
            GLTF_F32M(ci, "emissive_b",    "aiml_gltf_mat_emissive_b")
            GLTF_PTRM(ci, "base_color_texture",          "aiml_gltf_mat_base_color_texture")
            GLTF_PTRM(ci, "normal_texture",               "aiml_gltf_mat_normal_texture")
            GLTF_PTRM(ci, "metallic_roughness_texture",   "aiml_gltf_mat_metallic_roughness_texture")
            GLTF_PTRM(ci, "emissive_texture",             "aiml_gltf_mat_emissive_texture")
            GLTF_PTRM(ci, "occlusion_texture",            "aiml_gltf_mat_occlusion_texture")
            GLTF_STR (ci, "alpha_mode",    "aiml_gltf_mat_alpha_mode")
            GLTF_F32M(ci, "alpha_cutoff",  "aiml_gltf_mat_alpha_cutoff")
            GLTF_I32M(ci, "double_sided",  "aiml_gltf_mat_double_sided")

            ctx.classes["gltf_material"] = std::move(ci);
        }

        // ── gltf_texture ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_texture";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_PTRM(ci, "image",      "aiml_gltf_tex_image")
            GLTF_STR (ci, "wrap_s",     "aiml_gltf_tex_wrap_s")
            GLTF_STR (ci, "wrap_t",     "aiml_gltf_tex_wrap_t")
            GLTF_STR (ci, "min_filter", "aiml_gltf_tex_min_filter")
            GLTF_STR (ci, "mag_filter", "aiml_gltf_tex_mag_filter")

            ctx.classes["gltf_texture"] = std::move(ci);
        }

        // ── gltf_image ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_image";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_BUFM(ci, "pixels_rgba",    "aiml_gltf_img_pixels_rgba")
            GLTF_I32M(ci, "width",          "aiml_gltf_img_width")
            GLTF_I32M(ci, "height",         "aiml_gltf_img_height")
            GLTF_PTRM(ci, "to_gl_texture",  "aiml_gltf_img_to_gl_texture")

            ctx.classes["gltf_image"] = std::move(ci);
        }

        // ── gltf_animation ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_animation";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",     "aiml_gltf_anim_name")
            GLTF_F32M(ci, "duration", "aiml_gltf_anim_duration")

            // sample_translation_x/y/z and sample_rotation_x/y/z/w: (anim, node, t) → f32
#define GLTF_SAMPLE(ci, aimlName, cName) \
            (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* f32 = ctx.builder.getFloatTy(); \
                llvm::Value* nodeArg = llvm::ConstantPointerNull::get( \
                    llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* tArg = llvm::ConstantFP::get(f32, 0.0); \
                if (call && call->args.size() >= 1) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) nodeArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                if (call && call->args.size() >= 2) { \
                    IRValue* a = evalExpr(call->args[1].get()); \
                    if (a) tArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, f32, {p, p, f32}), {base, nodeArg, tArg})); \
            };

            GLTF_SAMPLE(ci, "sample_translation_x", "aiml_gltf_anim_sample_translation_x")
            GLTF_SAMPLE(ci, "sample_translation_y", "aiml_gltf_anim_sample_translation_y")
            GLTF_SAMPLE(ci, "sample_translation_z", "aiml_gltf_anim_sample_translation_z")
            GLTF_SAMPLE(ci, "sample_rotation_x",    "aiml_gltf_anim_sample_rotation_x")
            GLTF_SAMPLE(ci, "sample_rotation_y",    "aiml_gltf_anim_sample_rotation_y")
            GLTF_SAMPLE(ci, "sample_rotation_z",    "aiml_gltf_anim_sample_rotation_z")
            GLTF_SAMPLE(ci, "sample_rotation_w",    "aiml_gltf_anim_sample_rotation_w")
#undef GLTF_SAMPLE

            ctx.classes["gltf_animation"] = std::move(ci);
        }

        // ── gltf_skin ────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_skin";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_I32M(ci, "joint_count",             "aiml_gltf_skin_joint_count")
            GLTF_BUFM(ci, "inverse_bind_matrices",   "aiml_gltf_skin_inverse_bind_matrices")
            GLTF_LIST(ci, "joints",                  "aiml_gltf_skin_joints_fill")

            ctx.classes["gltf_skin"] = std::move(ci);
        }

#undef GLTF_HEAP_TO_ALLOCA
#undef GLTF_STR
#undef GLTF_I32M
#undef GLTF_F32M
#undef GLTF_PTRM
#undef GLTF_BUFM
#undef GLTF_LIST

        // ── gltf namespace: gltf.load(path) → gltf_scene ───────────────────
        {
            auto& gltf = ctx.namespaces["gltf"];
            gltf.builtinFuncs["load"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* pathArg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* scenePtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_load", p, {p}), {pathArg});
                return ctx.ir_gen->wrap(scenePtr);
            };
        }
    } // end gltf

    // =======================================================================
    // gl — OpenGL 3.3+ Core (GLEW backend)
    // Handle types: gl_buffer, gl_vao, gl_shader, gl_texture, gl_framebuffer
    // All handles are opaque i8* with preLoadBase = true.
    // =======================================================================
    {
        // ── Helper: store a loaded struct value to a temp alloca ──────────
#define GL_STORE_TMP(val, ty) \
        ([&]() -> llvm::Value* { \
            llvm::AllocaInst* _a = ctx.createEntryAlloca((ty), "gl_tmp"); \
            ctx.builder.CreateStore((val), _a); \
            return _a; \
        }())

        // ── Simple method macros (same style as gltf section) ─────────────
#define GL_VOID0(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p}), {base}); \
            return nullptr; \
        };

#define GL_PTR0(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base})); \
        };

        // ── gl_buffer ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_buffer"; ci.isBuiltin = true; ci.preLoadBase = true;

            ci.builtinMethods["update"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                auto* bt = ctx.getLLVMType("buffer");
                llvm::Value* dataArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        if (v->getType()->isPointerTy()) dataArg = v;
                        else dataArg = GL_STORE_TMP(v, bt);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_buffer_update",
                    ctx.builder.getVoidTy(), {p, p}), {base, dataArg});
                return nullptr;
            };
            GL_VOID0(ci, "delete", "aiml_gl_buffer_delete")

            ctx.classes["gl_buffer"] = std::move(ci);
        }

        // ── gl_vao ──────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_vao"; ci.isBuiltin = true; ci.preLoadBase = true;

            // bind_vbo / bind_ibo: (vao*, vbo*) → void
#define GL_VAO_BIND(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p = llvm::PointerType::get(ctx.context, 0); \
                llvm::Value* arg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (call && !call->args.empty()) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) arg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, p}), \
                    {base, arg}); \
                return nullptr; \
            };
            GL_VAO_BIND("bind_vbo", "aiml_gl_vao_bind_vbo")
            GL_VAO_BIND("bind_ibo", "aiml_gl_vao_bind_ibo")
#undef GL_VAO_BIND

            // attrib / attrib_i: (vao*, idx, components, stride, offset) → void
#define GL_VAO_ATTRIB(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                auto g = [&](int i) -> llvm::Value* { \
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0); \
                    IRValue* a = evalExpr(call->args[i].get()); \
                    if (!a) return ctx.builder.getInt32(0); \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                    return ctx.builder.CreateZExtOrTrunc(v, i32); \
                }; \
                ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, i32, i32, i32, i32}), \
                    {base, g(0), g(1), g(2), g(3)}); \
                return nullptr; \
            };
            GL_VAO_ATTRIB("attrib",   "aiml_gl_vao_attrib")
            GL_VAO_ATTRIB("attrib_i", "aiml_gl_vao_attrib_i")
#undef GL_VAO_ATTRIB

            GL_VOID0(ci, "delete", "aiml_gl_vao_delete")

            ctx.classes["gl_vao"] = std::move(ci);
        }

        // ── gl_shader ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_shader"; ci.isBuiltin = true; ci.preLoadBase = true;

            GL_VOID0(ci, "use", "aiml_gl_shader_use")

            // uniform_f32(name, v)
            ci.builtinMethods["uniform_f32"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* f32 = ctx.builder.getFloatTy();
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vArg    = llvm::ConstantFP::get(f32, 0.0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        vArg = v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_f32",
                    ctx.builder.getVoidTy(), {p, p, f32}), {base, nameArg, vArg});
                return nullptr;
            };

            // uniform_i32(name, v)
            ci.builtinMethods["uniform_i32"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vArg    = ctx.builder.getInt32(0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        vArg = ctx.builder.CreateZExtOrTrunc(v, i32);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_i32",
                    ctx.builder.getVoidTy(), {p, p, i32}), {base, nameArg, vArg});
                return nullptr;
            };

            // uniform_vec2/vec3/vec4: (name, struct_val) — struct stored to temp alloca
#define GL_UNI_VEC(aimlName, cName, structTyName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* sTy = ctx.getLLVMType(structTyName); \
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* vPtr    = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (call && call->args.size() >= 1) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                if (call && call->args.size() >= 2) { \
                    IRValue* a = evalExpr(call->args[1].get()); \
                    if (a) { \
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                        vPtr = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, sTy); \
                    } \
                } \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, p, p}), \
                    {base, nameArg, vPtr}); \
                return nullptr; \
            };
            GL_UNI_VEC("uniform_vec2", "aiml_gl_shader_uniform_vec2", "vec2")
            GL_UNI_VEC("uniform_vec3", "aiml_gl_shader_uniform_vec3", "vec3")
            GL_UNI_VEC("uniform_vec4", "aiml_gl_shader_uniform_vec4", "vec4")
#undef GL_UNI_VEC

            // uniform_mat4: (name, mat4_val)
            ci.builtinMethods["uniform_mat4"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p    = llvm::PointerType::get(ctx.context, 0);
                auto* m4Ty = ctx.getLLVMType("mat4");
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* mPtr    = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        mPtr = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, m4Ty);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_mat4",
                    ctx.builder.getVoidTy(), {p, p, p}), {base, nameArg, mPtr});
                return nullptr;
            };

            // uniform_mat4_array: (name, buffer<f32>, count)
            ci.builtinMethods["uniform_mat4_array"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* bt  = ctx.getLLVMType("buffer");
                llvm::Value* nameArg  = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* bufArg   = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* cntArg   = ctx.builder.getInt32(0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        bufArg = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, bt);
                    }
                }
                if (call && call->args.size() >= 3) {
                    IRValue* a = evalExpr(call->args[2].get());
                    if (a) cntArg = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_mat4_array",
                    ctx.builder.getVoidTy(), {p, p, p, i32}), {base, nameArg, bufArg, cntArg});
                return nullptr;
            };

            GL_VOID0(ci, "delete", "aiml_gl_shader_delete")

            ctx.classes["gl_shader"] = std::move(ci);
        }

        // ── gl_texture ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_texture"; ci.isBuiltin = true; ci.preLoadBase = true;

            // set_wrap(s, t)
            ci.builtinMethods["set_wrap"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0x2901 /*GL_REPEAT*/);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0x2901);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_texture_set_wrap",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };

            // set_filter(min, mag)
            ci.builtinMethods["set_filter"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0x2601 /*GL_LINEAR*/);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0x2601);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_texture_set_filter",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };

            GL_VOID0(ci, "gen_mipmaps", "aiml_gl_texture_gen_mipmaps")
            GL_VOID0(ci, "delete",      "aiml_gl_texture_delete")

            ctx.classes["gl_texture"] = std::move(ci);
        }

        // ── gl_framebuffer ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_framebuffer"; ci.isBuiltin = true; ci.preLoadBase = true;

            GL_PTR0(ci, "color_texture", "aiml_gl_framebuffer_color_texture")

            ci.builtinMethods["resize"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_framebuffer_resize",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };
            GL_VOID0(ci, "delete", "aiml_gl_framebuffer_delete")

            ctx.classes["gl_framebuffer"] = std::move(ci);
        }

#undef GL_STORE_TMP
#undef GL_VOID0
#undef GL_PTR0

        // ── gl namespace functions ──────────────────────────────────────────
        {
            auto& gl = ctx.namespaces["gl"];

            // Zero-arg void functions
#define GL_NS_VOID0(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {}), {}); \
                return nullptr; \
            };
            GL_NS_VOID0("init",               "aiml_gl_init")
            GL_NS_VOID0("clear_depth",         "aiml_gl_clear_depth")
            GL_NS_VOID0("enable_depth_test",   "aiml_gl_enable_depth_test")
            GL_NS_VOID0("disable_depth_test",  "aiml_gl_disable_depth_test")
            GL_NS_VOID0("enable_blend",        "aiml_gl_enable_blend")
            GL_NS_VOID0("disable_blend",       "aiml_gl_disable_blend")
            GL_NS_VOID0("enable_cull_face",    "aiml_gl_enable_cull_face")
            GL_NS_VOID0("cull_back",           "aiml_gl_cull_back")
            GL_NS_VOID0("cull_front",          "aiml_gl_cull_front")
            GL_NS_VOID0("bind_default_framebuffer", "aiml_gl_bind_default_framebuffer")
            GL_NS_VOID0("create_vao",          "aiml_gl_create_vao")  /* overridden below */
#undef GL_NS_VOID0

            // gl.viewport(x, y, w, h: i32) → void
            gl.builtinFuncs["viewport"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_viewport",
                    ctx.builder.getVoidTy(), {i32, i32, i32, i32}), {g(0), g(1), g(2), g(3)});
                return nullptr;
            };

            // gl.clear(r, g, b, a: f32) / gl.clear_all(r, g, b, a)
#define GL_NS_CLEAR(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* f32 = ctx.builder.getFloatTy(); \
                auto g = [&](int i) -> llvm::Value* { \
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0); \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])); \
                    return v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32); \
                }; \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {f32,f32,f32,f32}), \
                    {g(0), g(1), g(2), g(3)}); \
                return nullptr; \
            };
            GL_NS_CLEAR("clear",     "aiml_gl_clear")
            GL_NS_CLEAR("clear_all", "aiml_gl_clear_all")
#undef GL_NS_CLEAR

            // gl.wireframe(on: i8) → void
            gl.builtinFuncs["wireframe"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Value* on = args.empty() ? ctx.builder.getInt8(0)
                    : ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                        ctx.builder.getInt8Ty());
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_wireframe",
                    ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()}), {on});
                return nullptr;
            };

            // gl.polygon_offset(factor, units: f32) → void
            gl.builtinFuncs["polygon_offset"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* f32 = ctx.builder.getFloatTy();
                auto g = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    return v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_polygon_offset",
                    ctx.builder.getVoidTy(), {f32, f32}), {g(0), g(1)});
                return nullptr;
            };
            // gl.enable_polygon_offset_fill() / disable_polygon_offset_fill()
#define GL_NS_VOID0_INLINE(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
                ctx.builder.CreateCall(ctx.getFunction(cName, \
                    ctx.builder.getVoidTy(), {}), {}); \
                return nullptr; \
            };
            GL_NS_VOID0_INLINE("enable_polygon_offset_fill",  "aiml_gl_enable_polygon_offset_fill")
            GL_NS_VOID0_INLINE("disable_polygon_offset_fill", "aiml_gl_disable_polygon_offset_fill")
#undef GL_NS_VOID0_INLINE

            // gl.create_buffer / create_buffer_dynamic / create_index_buffer: (data_buf) → gl_buffer
#define GL_NS_MKBUF(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* p  = llvm::PointerType::get(ctx.context, 0); \
                auto* bt = ctx.getLLVMType("buffer"); \
                llvm::Value* arg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (!args.empty()) { \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
                    arg = v->getType()->isPointerTy() ? v : \
                          ([&]() -> llvm::Value* { \
                              auto* _a = ctx.createEntryAlloca(bt, "gl_buf_in"); \
                              ctx.builder.CreateStore(v, _a); return _a; }()); \
                } \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, p, {p}), {arg})); \
            };
            GL_NS_MKBUF("create_buffer",         "aiml_gl_create_buffer")
            GL_NS_MKBUF("create_buffer_dynamic",  "aiml_gl_create_buffer_dynamic")
            GL_NS_MKBUF("create_index_buffer",    "aiml_gl_create_index_buffer")
#undef GL_NS_MKBUF

            // gl.create_vao() → gl_vao
            gl.builtinFuncs["create_vao"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_vao", p, {}), {}));
            };

            // gl.compile_shader(vert_src, frag_src) → gl_shader
            gl.builtinFuncs["compile_shader"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vs = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null;
                llvm::Value* fs = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : null;
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_compile_shader", p, {p, p}), {vs, fs}));
            };

            // gl.load_shader(vert_path, frag_path) → gl_shader
            gl.builtinFuncs["load_shader"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vp = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null;
                llvm::Value* fp = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : null;
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_load_shader", p, {p, p}), {vp, fp}));
            };

            // gl.load_texture(path) → gl_texture.  Escape-Hatch for runtime-
            // generated paths.  Spec/22: when arg is a string literal AND the
            // path resolves under an `asset_directory`, emit a hard error
            // forcing the caller to switch to gl.tex(stem).
            gl.builtinFuncs["load_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (!args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall ? ctx.currentCall->args[0].get() : nullptr)) {
                        if (lit->type == "string" &&
                            AIML::AssetScanner::instance().path_under_root(lit->value)) {
                            // Suggest the stem for the user's existing path.
                            std::string suggest;
                            auto dot = lit->value.find_last_of('.');
                            auto slash = lit->value.find_last_of('/');
                            size_t a = (slash == std::string::npos) ? 0 : slash + 1;
                            size_t b = (dot == std::string::npos || dot < a)
                                       ? lit->value.size() : dot;
                            suggest = lit->value.substr(a, b - a);
                            LOG_ERROR(std::string("gl.load_texture: literal path '") +
                                      lit->value + "' is in the asset bundle; use gl.tex(\"" +
                                      suggest + "\") instead");
                        }
                    }
                }
                llvm::Value* arg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_load_texture", p, {p}), {arg}));
            };

            // gl.tex(stem) → gl_texture (Asset Registry, spec/44_7).  String-
            // literal arg gets compile-time validation against the texture
            // store; dynamic args fall through and runtime-error on miss.
            gl.builtinFuncs["tex"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (!args.empty() && ctx.currentCall &&
                    !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            !AIML::AssetScanner::instance().has_stem(
                                AIML::AssetKind::Texture, lit->value)) {
                            LOG_ERROR(std::string("gl.tex: stem '") +
                                      lit->value + "' not in texture store");
                        }
                    }
                }
                llvm::Value* arg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_get_texture", p, {p}), {arg}));
            };

            // gl.font(stem, size) → ui_font (spec/44_7).  Stem-literal-check
            // wie gl.tex; size darf dynamisch sein.  Backend: aiml_ui_load_font
            // via Asset-Resolver mit per-(stem,size) Cache.
            gl.builtinFuncs["font"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p    = llvm::PointerType::get(ctx.context, 0);
                auto* f32  = ctx.builder.getFloatTy();
                if (!args.empty() && ctx.currentCall &&
                    !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            !AIML::AssetScanner::instance().has_stem(
                                AIML::AssetKind::Font, lit->value)) {
                            LOG_ERROR(std::string("gl.font: stem '") +
                                      lit->value + "' not in font store");
                        }
                    }
                }
                llvm::Value* stem = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* sz   = (args.size() < 2)
                    ? llvm::ConstantFP::get(f32, 12.0)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                // Promote double → float if needed (literals come as f64).
                if (sz->getType()->isDoubleTy()) sz = ctx.builder.CreateFPTrunc(sz, f32);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_get_font", p, {p, f32}), {stem, sz}));
            };

            // gl.create_texture(w, h, px_buf) → gl_texture
            gl.builtinFuncs["create_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* bt  = ctx.getLLVMType("buffer");
                auto g32 = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                llvm::Value* pxArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (args.size() >= 3) {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
                    pxArg = v->getType()->isPointerTy() ? v :
                            ([&]() -> llvm::Value* {
                                auto* _a = ctx.createEntryAlloca(bt, "gl_px_in");
                                ctx.builder.CreateStore(v, _a); return _a; }());
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_texture", p, {i32, i32, p}),
                    {g32(0), g32(1), pxArg}));
            };

            // gl.register_texture(stem, tex) → void — register a decoded texture
            // under a synthetic stem ("@art:<hash>") so a sprite's texture_path
            // resolves to it via the asset registry / load_texture_cached.
            gl.builtinFuncs["register_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_register_texture", ctx.builder.getVoidTy(), {p, p}),
                    { static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                      static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) });
                return ctx.ir_gen->getInt32(0);
            };

            // gl.has_texture(stem) → i32 — 1 if a texture is cached under this stem.
            gl.builtinFuncs["has_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_has_texture", i32, {p}),
                    { static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) }));
            };

            // gl.decode_texture(webp: buffer) → gl_texture — decode encoded image
            // bytes (WebP/PNG/JPG) in `webp` into a GPU texture.  Native uses the
            // stb/libwebp path; WASM decodes in the browser (createImageBitmap).
            // Returns a gl_texture handle (null on failure).  Needs a live GL
            // context (client render path, not headless).
            gl.builtinFuncs["decode_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                auto* bt = ctx.getLLVMType("buffer");
                if (args.empty())
                    return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)));
                // aiml_art_decode_texture reads ->data/->size, so it needs the
                // buffer's ADDRESS — pass an incoming pointer through, else spill
                // the by-value struct to a temp alloca (mirrors gl.create_texture).
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* bufPtr = v->getType()->isPointerTy() ? v :
                    ([&]() -> llvm::Value* {
                        auto* _a = ctx.createEntryAlloca(bt, "art_dec_in");
                        ctx.builder.CreateStore(v, _a); return _a; }());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_decode_texture", p, {p}), {bufPtr}));
            };

            // gl.pick_source() → i32 — open a file picker + decode + stash the full
            // image as the pan-crop source (for source_texture / buffer.encode_source).
            // 1 = ok, 0 = cancelled/failed.  Needs a display (client path).
            gl.builtinFuncs["pick_source"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                auto* i32 = ctx.builder.getInt32Ty();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_pick_source", i32, {}), {}));
            };

            // gl.source_texture(dw, dh, mode, panx, pany) → gl_texture — crop the
            // stashed source (mode 0 cover / 1 stretch) at pan (0..1 per axis) into a
            // dw×dh preview texture.  For live drag-to-position.  null on failure.
            gl.builtinFuncs["source_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* f32 = ctx.builder.getFloatTy();
                auto gi = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                auto gf = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.5);
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, f32);
                    if (v->getType()->isDoubleTy())  return ctx.builder.CreateFPCast(v, f32);
                    return v;
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_source_texture", p, {i32, i32, i32, f32, f32}),
                    {gi(0), gi(1), gi(2), gf(3), gf(4)}));
            };

            // gl.bind_texture(tex, slot) → void
            gl.builtinFuncs["bind_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* tex  = args.size() > 0
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* slot = args.size() > 1
                    ? ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32)
                    : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_bind_texture",
                    ctx.builder.getVoidTy(), {p, i32}), {tex, slot});
                return nullptr;
            };

            // gl.draw_arrays(vao, count) / gl.draw_elements(vao, count)
#define GL_NS_DRAW(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                llvm::Value* vao = args.size() > 0 \
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* cnt = args.size() > 1 \
                    ? ctx.builder.CreateZExtOrTrunc( \
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32) \
                    : ctx.builder.getInt32(0); \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, i32}), \
                    {vao, cnt}); \
                return nullptr; \
            };
            GL_NS_DRAW("draw_arrays",   "aiml_gl_draw_arrays")
            GL_NS_DRAW("draw_elements", "aiml_gl_draw_elements")
            GL_NS_DRAW("draw_lines",    "aiml_gl_draw_lines")
#undef GL_NS_DRAW

            // gl.draw_arrays_mode(vao, mode, count) → void
            gl.builtinFuncs["draw_arrays_mode"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* vao  = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* mode = args.size() > 1 ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32) : ctx.builder.getInt32(0x0004);
                llvm::Value* cnt  = args.size() > 2 ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])), i32) : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_draw_arrays_mode",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {vao, mode, cnt});
                return nullptr;
            };

            // gl.create_framebuffer(w, h) → gl_framebuffer
            gl.builtinFuncs["create_framebuffer"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g32 = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_framebuffer", p, {i32, i32}), {g32(0), g32(1)}));
            };

            // gl.bind_framebuffer(fbo) → void
            gl.builtinFuncs["bind_framebuffer"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* fb = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_bind_framebuffer",
                    ctx.builder.getVoidTy(), {p}), {fb});
                return nullptr;
            };
        }

        // ── gl_mesh ─────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_mesh"; ci.isBuiltin = true; ci.preLoadBase = true;

            // mesh.draw() → void
            ci.builtinMethods["draw"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_draw",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };
            // mesh.draw_count(n: i32) → void
            ci.builtinMethods["draw_count"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* cnt = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) cnt = ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_draw_count",
                    ctx.builder.getVoidTy(), {p, i32}), {base, cnt});
                return nullptr;
            };
            // mesh.index_count() → i32
            ci.builtinMethods["index_count"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_mesh_index_count",
                        ctx.builder.getInt32Ty(), {p}), {base}));
            };
            // mesh.delete() → void
            ci.builtinMethods["delete"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_delete",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };
            ctx.classes["gl_mesh"] = std::move(ci);
        }

        // ── gl.create_mesh ──────────────────────────────────────────────────
        {
            auto& gl = ctx.namespaces["gl"];
            gl.builtinFuncs["create_mesh"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                auto getArg = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return null;
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    if (v->getType()->isPointerTy()) return v;
                    /* buffer<T> struct — store to alloca and pass as pointer */
                    auto* tmp = ctx.createEntryAlloca(v->getType(), "mesh_buf");
                    ctx.builder.CreateStore(v, tmp); return tmp;
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_mesh", p, {p, p, p, p}),
                    {getArg(0), getArg(1), getArg(2), getArg(3)}));
            };
        }
    } // end gl

    // ==========================================================================
    // engine — cross-platform window + game loop
    // ==========================================================================
    {
        // ── window_handle type ───────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "window_handle"; ci.isBuiltin = true; ci.preLoadBase = true;
            // No methods needed on window_handle directly — use engine.* namespace
            ctx.classes["window_handle"] = std::move(ci);
        }

        auto& eng = ctx.namespaces["engine"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* i32 = ctx.builder.getInt32Ty();
        auto* f32 = ctx.builder.getFloatTy();
        auto* f64 = ctx.builder.getDoubleTy();
        auto* i64 = ctx.builder.getInt64Ty();
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

#define ENG_WIN_RET_PTR(name, cName) \
        eng.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
            auto g = [&](int i) -> llvm::Value* { \
                if ((int)args.size() <= i) return null; \
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])); \
            }; \
            (void)i32; (void)i8p; (void)null; (void)g; \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {i32, i32, i8p}), \
                {ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32), \
                 ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32), \
                 args.size() > 2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])) \
                                 : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))})); \
        };

        // engine.create_window(w, h, title) → window_handle
        ENG_WIN_RET_PTR("create_window", "aiml_engine_create_window")
#undef ENG_WIN_RET_PTR

        // engine.create_window_fullscreen(title) → window_handle
        eng.builtinFuncs["create_window_fullscreen"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* title = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_create_window_fullscreen", p, {i8p}), {title}));
        };

        // engine.destroy_window(win) → void
        eng.builtinFuncs["destroy_window"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_destroy_window",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.set_title(win, title)
        eng.builtinFuncs["set_title"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* w = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_set_title",
                ctx.builder.getVoidTy(), {p, i8p}), {w, t});
            return nullptr;
        };

        // engine.maximize(win) — System-Maximize-State setzen.
        eng.builtinFuncs["maximize"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_maximize_window",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.window_size(win) → ivec2  (return {w, h} as i32 pair)
        // We expose as two functions here; ivec2 construction requires math types
        // For now: engine.window_size_x / engine.window_size_y returning i32
        eng.builtinFuncs["window_size_x"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_x", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["window_size_y"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_y", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        // engine.window_size_xf / _yf — same as above but cast to f32. Useful
        // when the value participates in float math (scaling, ratios). AIML
        // doesn't auto-promote i32→f32 in arithmetic, so a dedicated f32
        // accessor is the cleanest workaround.
        eng.builtinFuncs["window_size_xf"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* iv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_x", ctx.builder.getInt32Ty(), {p}), {w});
            return ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(iv, ctx.builder.getFloatTy()));
        };
        eng.builtinFuncs["window_size_yf"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* iv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_y", ctx.builder.getInt32Ty(), {p}), {w});
            return ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(iv, ctx.builder.getFloatTy()));
        };

        // engine.make_current(win), engine.set_vsync(on)
        eng.builtinFuncs["make_current"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_make_current",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };
        eng.builtinFuncs["set_vsync"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* on = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_set_vsync",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {on});
            return nullptr;
        };

        // engine.run(win, update_fn, render_fn) — passes fn pointers as i8*
        eng.builtinFuncs["run"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto g = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                return v->getType()->isPointerTy() ? v
                    : ctx.builder.CreateIntToPtr(v, p);
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_run",
                ctx.builder.getVoidTy(), {p, p, p}), {g(0), g(1), g(2)});
            return nullptr;
        };

        // engine.running(win) → bool(i32)
        eng.builtinFuncs["running"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_running", ctx.builder.getInt32Ty(), {p}), {w}));
        };

        // engine.is_iconified/is_focused/is_visible(win) → i32
        eng.builtinFuncs["is_iconified"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_iconified", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["is_focused"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_focused", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["is_visible"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_visible", ctx.builder.getInt32Ty(), {p}), {w}));
        };

        // engine.swap(win), engine.quit(win)
        eng.builtinFuncs["swap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_swap",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };
        eng.builtinFuncs["quit"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_quit",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.dt() → f32, engine.time() → f64, engine.fps() → f32, engine.frame() → i64
        eng.builtinFuncs["dt"]    = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_dt", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["time"]  = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_time", ctx.builder.getDoubleTy(), {}), {})); };
        eng.builtinFuncs["fps"]   = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_fps", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["frame"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_frame", ctx.builder.getInt64Ty(), {}), {})); };

        // engine.target_fps(fps: i32)
        eng.builtinFuncs["target_fps"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* fps = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_target_fps",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {fps});
            return nullptr;
        };

        // engine.platform() → string
        eng.builtinFuncs["platform"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_platform", i8p, {}), {})); };

        // engine.screen_size_x/y → i32
        eng.builtinFuncs["screen_size_x"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_screen_size_x", ctx.builder.getInt32Ty(), {}), {})); };
        eng.builtinFuncs["screen_size_y"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_screen_size_y", ctx.builder.getInt32Ty(), {}), {})); };

        // engine.dpi() → f32, engine.is_mobile/is_web → i32
        eng.builtinFuncs["dpi"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_dpi", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["is_mobile"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_mobile", ctx.builder.getInt32Ty(), {}), {})); };
        eng.builtinFuncs["is_web"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_web", ctx.builder.getInt32Ty(), {}), {})); };
        // ── Prefab functions on engine namespace ────────────────────────────
        // engine.instantiate(name) → prefab_instance
        eng.builtinFuncs["instantiate"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_instantiate", i8p, {i8p}), {name}));
        };
        // engine.destroy(inst)
        eng.builtinFuncs["destroy"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* inst = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_destroy",
                ctx.builder.getVoidTy(), {i8p}), {inst});
            return nullptr;
        };
        // engine.is_alive(inst) → i32  (1 = noch in der Szene, 0 = zerstört/null;
        // safe mit stale handle, dereferenziert nicht)
        eng.builtinFuncs["is_alive"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* inst = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_is_alive", i32, {i8p}), {inst}));
        };
        // engine.connect(from, to) → connector  (cable between two instances)
        eng.builtinFuncs["connect"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* from = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* to   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_connect", i8p, {i8p, i8p}), {from, to}));
        };
        // engine.draw_scene()
        eng.builtinFuncs["draw_scene"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_draw_scene",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
        // engine.hit_test(mx, my) → prefab_instance
        eng.builtinFuncs["hit_test"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_hit_test", i8p, {f32, f32}), {mx, my}));
        };
        // engine.get_dragged() → prefab_instance
        eng.builtinFuncs["get_dragged"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_dragged", i8p, {}), {}));
        };
        // engine.mouse_design_x() / engine.mouse_design_y() → f32
        // Cursor-Position in DESIGN-Koordinaten (= Pixel / rel_scale).  set_pos et al.
        // arbeiten in Design-Koords, input.mouse_pos_* liefert World/Pixel — dies
        // kapselt die Umrechnung an einer Stelle (Runtime kennt rel_scale).
        eng.builtinFuncs["mouse_design_x"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_mouse_design_x", f32, {}), {}));
        };
        eng.builtinFuncs["mouse_design_y"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_mouse_design_y", f32, {}), {}));
        };
        // engine.update_hover(mx, my)
        eng.builtinFuncs["update_hover"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_update_hover",
                ctx.builder.getVoidTy(), {f32, f32}), {mx, my});
            return nullptr;
        };
        // engine.get_hovered() → prefab_instance
        eng.builtinFuncs["get_hovered"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_hovered", i8p, {}), {}));
        };
        // engine.layout_hflow(items: list<prefab_instance>, x, y, gap)
        // engine.layout_vflow(items: list<prefab_instance>, x, y, gap)
        // engine.layout_stack(items: list<prefab_instance>, x, y, dx, dy, z_step)
        // Spec 44_6 §Layout — sibling auto-arrange.  items wird als
        // AimlList* (i8*) durchgereicht; das Runtime liest size/data.
        // Helper: list<prefab_instance>-Struct → (data_ptr i8*, count i32).
        // Akzeptiert sowohl Struct-Value (locale list-Variable) als auch
        // Pointer-to-Struct (class-member list, durch `<.X` ref-loaded).
        auto extractListDataCount = [](CompilerContext& ctx, AIML::IRValue* listArg,
                                       llvm::Value*& out_data, llvm::Value*& out_count) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(listArg));
            llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
            llvm::Value* sizePtrOrField;
            llvm::Value* dataPtrOrField;
            if (listVal->getType()->isPointerTy()) {
                // Pointer to AimlList → GEP into the struct in-place
                llvm::Value* sizeGEP = ctx.builder.CreateStructGEP(listStructTy, listVal, 0, "list_size_gep");
                out_count = ctx.builder.CreateLoad(i32, sizeGEP, "list_size");
                llvm::Value* dataGEP = ctx.builder.CreateStructGEP(listStructTy, listVal, 2, "list_data_gep");
                out_data = ctx.builder.CreateLoad(i8p, dataGEP, "list_data");
            } else {
                // Struct value → ExtractValue
                out_count = ctx.builder.CreateExtractValue(listVal, {0});
                if (out_count->getType() != i32) out_count = ctx.builder.CreateZExtOrTrunc(out_count, i32);
                llvm::Value* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
                if (dataPtr->getType() != i8p) dataPtr = ctx.builder.CreateBitCast(dataPtr, i8p);
                out_data = dataPtr;
            }
        };

        auto makeFlowFn = [extractListDataCount](const char* c_name) {
            return [c_name, extractListDataCount](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* f32 = ctx.builder.getFloatTy();
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* data_v = nullptr;
                llvm::Value* count_v = nullptr;
                extractListDataCount(ctx, args[0], data_v, count_v);
                auto toF32 = [&](AIML::IRValue* a) -> llvm::Value* {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                    if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
                    return v;
                };
                ctx.builder.CreateCall(ctx.getFunction(c_name,
                    ctx.builder.getVoidTy(), {i8p, i32, f32, f32, f32}),
                    {data_v, count_v, toF32(args[1]), toF32(args[2]), toF32(args[3])});
                return nullptr;
            };
        };
        eng.builtinFuncs["layout_hflow"] = makeFlowFn("aiml_engine_layout_hflow");
        eng.builtinFuncs["layout_vflow"] = makeFlowFn("aiml_engine_layout_vflow");
        eng.builtinFuncs["layout_stack"] = [extractListDataCount](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* data_v = nullptr;
            llvm::Value* count_v = nullptr;
            extractListDataCount(ctx, args[0], data_v, count_v);
            auto toF32 = [&](AIML::IRValue* a) -> llvm::Value* {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
                return v;
            };
            llvm::Value* zs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5]));
            if (zs->getType() != i32) zs = ctx.builder.CreateZExtOrTrunc(zs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_layout_stack",
                ctx.builder.getVoidTy(), {i8p, i32, f32, f32, f32, f32, i32}),
                {data_v, count_v, toF32(args[1]), toF32(args[2]), toF32(args[3]), toF32(args[4]), zs});
            return nullptr;
        };
        // engine.update_drag(mx, my, btn_down, btn_released)
        eng.builtinFuncs["update_drag"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* bd = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* br = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            if (bd->getType() != i32) bd = ctx.builder.CreateZExtOrTrunc(bd, i32);
            if (br->getType() != i32) br = ctx.builder.CreateZExtOrTrunc(br, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_update_drag",
                ctx.builder.getVoidTy(), {f32, f32, i32, i32}), {mx, my, bd, br});
            return nullptr;
        };
        // engine.prefab_scene_init(w, h)
        eng.builtinFuncs["prefab_scene_init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* w = ctx.builder.CreateZExtOrTrunc(
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            llvm::Value* h = ctx.builder.CreateZExtOrTrunc(
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_scene_init",
                ctx.builder.getVoidTy(), {i32, i32}), {w, h});
            return nullptr;
        };
        // engine.prefab_set_font(font)
        eng.builtinFuncs["prefab_set_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_font",
                ctx.builder.getVoidTy(), {i8p}), {f});
            return nullptr;
        };
    } // end engine

    // ==========================================================================
    // prefab_instance — instance methods
    // ==========================================================================
    {
        CompilerContext::ClassInfo ci;
        ci.name = "prefab_instance"; ci.isBuiltin = true;
        ci.preLoadBase = true;

        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* f32 = ctx.builder.getFloatTy();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* vd  = ctx.builder.getVoidTy();

        // set_pos(x, y)
        ci.builtinMethods["set_pos"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_pos",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };

        // set_pos_rel(rx, ry) — relative position (0.0-1.0 of screen)
        ci.builtinMethods["set_pos_rel"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_pos_rel",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };

#define PI_F32_GETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getFloatTy(), {i8p}), {base})); \
        };
#define PI_I32_GETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {base})); \
        };
#define PI_F32_SETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))); \
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p, f32}), {base, v}); \
            return nullptr; \
        };
#define PI_I32_SETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))); \
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v}); \
            return nullptr; \
        };

        PI_F32_GETTER("get_x", "aiml_prefab_get_x")
        PI_F32_GETTER("get_y", "aiml_prefab_get_y")
        PI_F32_GETTER("get_scale", "aiml_prefab_get_scale")
        PI_F32_GETTER("get_rotation", "aiml_prefab_get_rotation")
        PI_F32_GETTER("get_alpha", "aiml_prefab_get_alpha")
        PI_F32_GETTER("get_width", "aiml_prefab_get_width")
        PI_F32_GETTER("get_height", "aiml_prefab_get_height")
        PI_I32_GETTER("get_z", "aiml_prefab_get_z")
        PI_I32_GETTER("get_visible", "aiml_prefab_get_visible")

        PI_F32_SETTER("set_scale", "aiml_prefab_set_scale")
        PI_F32_SETTER("set_rotation", "aiml_prefab_set_rotation")
        PI_F32_SETTER("set_alpha", "aiml_prefab_set_alpha")
        PI_I32_SETTER("set_z", "aiml_prefab_set_z")
        PI_I32_SETTER("set_visible", "aiml_prefab_set_visible")
        PI_I32_SETTER("set_draggable", "aiml_prefab_set_draggable")
        PI_I32_GETTER("is_hovered", "aiml_prefab_is_hovered")

        // cancel_drag() — restore position to before drag started, clear dragged state
        ci.builtinMethods["cancel_drag"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_cancel_drag", ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };

        // was_clicked(btn) → i32
        ci.builtinMethods["was_clicked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* btn = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (btn->getType() != i32) btn = ctx.builder.CreateZExtOrTrunc(btn, i32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_was_clicked", i32, {i8p, i32}), {base, btn}));
        };

#undef PI_F32_GETTER
#undef PI_I32_GETTER
#undef PI_F32_SETTER
#undef PI_I32_SETTER

        // dock_x(col, colspan) / dock_y(row, rowspan)
        ci.builtinMethods["dock_x"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* cs  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (col->getType() != i32) col = ctx.builder.CreateZExtOrTrunc(col, i32);
            if (cs->getType()  != i32) cs  = ctx.builder.CreateZExtOrTrunc(cs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_dock_x",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, col, cs});
            return nullptr;
        };
        ci.builtinMethods["dock_y"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* rs  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (row->getType() != i32) row = ctx.builder.CreateZExtOrTrunc(row, i32);
            if (rs->getType()  != i32) rs  = ctx.builder.CreateZExtOrTrunc(rs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_dock_y",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, row, rs});
            return nullptr;
        };

        // set_text(child_name, text)
        ci.builtinMethods["set_text"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* text = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_text",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, name, text});
            return nullptr;
        };

        // set_texture(child_name, tex)
        ci.builtinMethods["set_texture"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* tex  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_texture",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, name, tex});
            return nullptr;
        };

        // set_color(child_name, color)
        ci.builtinMethods["set_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // set_bg_color(child_name, color) — child FILL color (rect bg / SDF).
        ci.builtinMethods["set_bg_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bg_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // set_border_color(child_name, color) — child border/stroke color.
        ci.builtinMethods["set_border_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_border_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // set_child_visible(child_name, v)
        ci.builtinMethods["set_child_visible"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_child_visible",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, v});
            return nullptr;
        };

        // set_compact(container_name, on) — toggle compacting layout on a
        // container child: hidden spawned rows take no layout space, visible
        // rows re-pack contiguously.  Also settable as `compact: true` in the
        // prefab definition.
        ci.builtinMethods["set_compact"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_container_set_compact",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, v});
            return nullptr;
        };

        // set_child_font_size(child_name, size_px) — override a child's
        // `font_size` at runtime (useful for reusing a prefab as scaled
        // preview where the design-time font would render too large).
        ci.builtinMethods["set_child_font_size"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_child_font_size",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };

        // container_row_count(child_name) → i32 — # spawned rows
        ci.builtinMethods["container_row_count"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* rv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_container_row_count", i32, {i8p, i8p}),
                {base, name});
            return ctx.ir_gen->wrap(rv);
        };

        // container_row(child_name, idx) → prefab_instance — the spawned row
        // instance at that index (or null if out of range).  Enables AIML
        // code to iterate rows and call set_text / set_texture etc. on each.
        ci.builtinMethods["container_row"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* idx  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            llvm::Value* rv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_container_row", i8p, {i8p, i8p, i32}),
                {base, name, idx});
            return ctx.ir_gen->wrap(rv);
        };

        // set_bar_value(child_name, value) — update a "bar" child's fill
        ci.builtinMethods["set_bar_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_value",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_bar_max(child_name, max)
        ci.builtinMethods["set_bar_max"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_max",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_bar_fill(child_name, color) — recolor a "bar" child's fill
        ci.builtinMethods["set_bar_fill"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* c    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (c->getType() != i32) c = ctx.builder.CreateZExtOrTrunc(c, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_fill",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, c});
            return nullptr;
        };

        // ── slider child accessors ────────────────────────────────────────
        // get_slider_value(child_name) → f32
        ci.builtinMethods["get_slider_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_slider_value", f32, {i8p, i8p}),
                {base, name}));
        };
        // set_slider_value(child_name, value)
        ci.builtinMethods["set_slider_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_value",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_slider_min(child_name, min)
        ci.builtinMethods["set_slider_min"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_min",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_slider_max(child_name, max)
        ci.builtinMethods["set_slider_max"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_max",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };

        // child(name) → prefab_instance
        ci.builtinMethods["child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child", i8p, {i8p, i8p}), {base, name}));
        };

        // text_value(child_name) → string (text content of a text_input child)
        ci.builtinMethods["text_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_text_value", i8p, {i8p, i8p}), {base, name}));
        };

        // child_clicked(child_name) → i32 (1 if button child clicked this frame)
        ci.builtinMethods["child_clicked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_clicked", i32, {i8p, i8p}), {base, name}));
        };

        // child_contains(child_name, screen_x, screen_y) → i32 (1 if the SCREEN
        // point is inside the named child's bounds — works for any child type,
        // incl. non-interactive rects; used for drop-target detection).
        ci.builtinMethods["child_contains"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* sx   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            llvm::Value* sy   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
            if (sx->getType()->isIntegerTy()) sx = ctx.builder.CreateSIToFP(sx, f32);
            if (sy->getType()->isIntegerTy()) sy = ctx.builder.CreateSIToFP(sy, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_contains", i32, {i8p, i8p, f32, f32}), {base, name, sx, sy}));
        };

        // child_focused(child_name) → i32 (1 if text_input child has focus)
        ci.builtinMethods["child_focused"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_focused", i32, {i8p, i8p}), {base, name}));
        };

        // is_checked(child_name) → i32 (1 if checkbox child is toggled on)
        ci.builtinMethods["is_checked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_checked", i32, {i8p, i8p}), {base, name}));
        };

        // set_checked(child_name, checked) — set initial / programmatic toggle state
        ci.builtinMethods["set_checked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_checked", ctx.builder.getVoidTy(), {i8p, i8p, i32}),
                {base, name, v});
            return nullptr;
        };

        // dropdown_index(child_name) → i32 — current selected index
        ci.builtinMethods["dropdown_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_dropdown_index", i32, {i8p, i8p}), {base, name}));
        };

        // dropdown_value(child_name) → string — current selected option label
        ci.builtinMethods["dropdown_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_dropdown_value", i8p, {i8p, i8p}), {base, name}));
        };

        // dropdown_enum(child_name) → i32 — selected option's real enum member
        // value (for a dropdown declared with `enum: TypeName`).  i32 is enum-
        // compatible, so the result compares/assigns directly against the enum.
        ci.builtinMethods["dropdown_enum"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_dropdown_enum_value", i32, {i8p, i8p}), {base, name}));
        };

        // set_dropdown_options(child_name, options) — replace pipe-separated options list
        ci.builtinMethods["set_dropdown_options"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* opts = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_options", ctx.builder.getVoidTy(), {i8p, i8p, i8p}),
                {base, name, opts});
            return nullptr;
        };

        // set_dropdown_index(child_name, index) — set selected index
        ci.builtinMethods["set_dropdown_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_index", ctx.builder.getVoidTy(), {i8p, i8p, i32}),
                {base, name, v});
            return nullptr;
        };

        // set_dropdown_selected(child_name, value) — find value in the dropdown's
        // pipe-separated options and set that index.  No-op if value not found.
        ci.builtinMethods["set_dropdown_selected"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_selected", ctx.builder.getVoidTy(), {i8p, i8p, i8p}),
                {base, name, val});
            return nullptr;
        };

        // Phase 6: bind_list(list_ptr) — attach the raw list<list<i32>> struct
        // that CHILD_CONTAINER widgets iterate over.  The argument is the
        // AIML list value itself (or `list_field` on a model); we just take
        // its address as the struct pointer.
        // ── Widget↔model binding (spec 44.6 §5d) ──────────────────────────
        // bind_model(m) attaches a model instance; sync_from/to_model move each
        // bound child's field to/from its widget.  Offsets/tags are baked into
        // the prefab at compile time (see ast_compiler prefab emission).
        ci.builtinMethods["bind_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.empty()) return nullptr;
            llvm::Value* modelPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (modelPtr->getType() != i8p) modelPtr = ctx.builder.CreateBitCast(modelPtr, i8p);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_bind_model",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, modelPtr});
            return nullptr;
        };
        ci.builtinMethods["sync_from_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_sync_from_model",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };
        ci.builtinMethods["sync_to_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_sync_to_model",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };
        ci.builtinMethods["bind_list"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            /* Two forms:
             *   scene.bind_list(list)                        (instance-level)
             *   scene.bind_list("child_name", list)          (per-widget)   */
            if (call->args.size() >= 2) {
                llvm::Value* childNm = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_child_bind_list",
                    ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, childNm, listPtr});
            } else {
                llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_bind_list",
                    ctx.builder.getVoidTy(), {i8p, i8p}), {base, listPtr});
            }
            return nullptr;
        };
        // bound_list() — inverse of bind_list; returns the attached pointer or null.
        ci.builtinMethods["bound_list"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_bound_list", i8p, {i8p}), {base}));
        };
        // bound_index() — for spawned children: index into the parent's bound_list.
        ci.builtinMethods["bound_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_bound_index", ctx.builder.getInt32Ty(), {i8p}),
                {base}));
        };

        // add_child(other) — nest another instance under this one (inherits transform)
        ci.builtinMethods["add_child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* child = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_add_child",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, child});
            return nullptr;
        };
        // remove_child(other) — detach a nested instance (back to top-level)
        ci.builtinMethods["remove_child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* child = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_remove_child",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, child});
            return nullptr;
        };

        // set_clip(enabled) — clip nested children to this instance's bounds
        ci.builtinMethods["set_clip"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_clip",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v});
            return nullptr;
        };
        // set_scroll(sx, sy) — scroll offset (local px) for nested children
        ci.builtinMethods["set_scroll"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_scroll",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };
        // focus(child) — give keyboard focus to a text_input child.
        ci.builtinMethods["focus"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_focus",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };
        // scroll_to_end(child) — pin a textbox / scrollable container to the bottom.
        ci.builtinMethods["scroll_to_end"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_scroll_to_end",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };
        ci.builtinMethods["get_scroll_x"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_scroll_x", ctx.builder.getFloatTy(), {i8p}), {base}));
        };
        ci.builtinMethods["get_scroll_y"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_scroll_y", ctx.builder.getFloatTy(), {i8p}), {base}));
        };

        // set_connector_anchor(mode) — 0=center, 1=edge, 2=distributed.
        // Controls how cables (see engine.connect) attach to this instance.
        ci.builtinMethods["set_connector_anchor"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* m = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (m->getType() != i32) m = ctx.builder.CreateZExtOrTrunc(m, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_connector_anchor",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, m});
            return nullptr;
        };

        ctx.classes["prefab_instance"] = std::move(ci);
    } // end prefab_instance

    // ==========================================================================
    // connector — cable between two prefab instances (see engine.connect)
    // ==========================================================================
    {
        CompilerContext::ClassInfo ci;
        ci.name = "connector"; ci.isBuiltin = true;
        ci.preLoadBase = true;

        // set_colors(c0, c1) — gradient start/end (0xRRGGBBAA)
        ci.builtinMethods["set_colors"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (a->getType() != i32) a = ctx.builder.CreateZExtOrTrunc(a, i32);
            if (b->getType() != i32) b = ctx.builder.CreateZExtOrTrunc(b, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_colors",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, a, b});
            return nullptr;
        };
        // set_highlight(color) — specular streak tint
        ci.builtinMethods["set_highlight"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (a->getType() != i32) a = ctx.builder.CreateZExtOrTrunc(a, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_highlight",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, a});
            return nullptr;
        };
        // set_width(w) — cable thickness in px
        ci.builtinMethods["set_width"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* w = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (w->getType() != f32) w = ctx.builder.CreateSIToFP(w, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_width",
                ctx.builder.getVoidTy(), {i8p, f32}), {base, w});
            return nullptr;
        };
        // set_bow(amount) — perpendicular curve (0 = straight). Fraction of chord length.
        ci.builtinMethods["set_bow"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* w = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (w->getType() != f32) w = ctx.builder.CreateSIToFP(w, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_bow",
                ctx.builder.getVoidTy(), {i8p, f32}), {base, w});
            return nullptr;
        };
        // set_visible(v)
        ci.builtinMethods["set_visible"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_visible",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v});
            return nullptr;
        };
        // set_z(z) — z-order for interleaving with prefabs in draw_scene
        ci.builtinMethods["set_z"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* z = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (z->getType() != i32) z = ctx.builder.CreateZExtOrTrunc(z, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_z",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, z});
            return nullptr;
        };
        // set_port(tex, scale) — optional endpoint sprite; scale = size / width
        ci.builtinMethods["set_port"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* tex = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* sc  = call->args.size() > 1
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                : llvm::ConstantFP::get(f32, 1.6);
            if (sc->getType() != f32) sc = ctx.builder.CreateSIToFP(sc, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_port",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, tex, sc});
            return nullptr;
        };
        // destroy()
        ci.builtinMethods["destroy"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_destroy",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };

        ctx.classes["connector"] = std::move(ci);
    } // end connector

    // ==========================================================================
    // input — keyboard / mouse / gamepad / event-queue / mapping layer
    // ==========================================================================
    {
        // ── input_event type ─────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "input_event"; ci.isBuiltin = true; ci.preLoadBase = true;
            // Property getters (zero-arg, accessed via resolveProperty → builtinMethods)
#define INP_STR_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, i8p, {i8p}), {base})); \
            };
#define INP_I32_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {base})); \
            };
#define INP_F32_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getFloatTy(), {i8p}), {base})); \
            };
            INP_STR_GETTER("type",     "aiml_input_event_type")
            INP_STR_GETTER("key",      "aiml_input_event_key")
            INP_I32_GETTER("btn",      "aiml_input_event_btn")
            INP_F32_GETTER("pos_x",    "aiml_input_event_pos_x")
            INP_F32_GETTER("pos_y",    "aiml_input_event_pos_y")
            INP_F32_GETTER("scroll_x", "aiml_input_event_scroll_x")
            INP_F32_GETTER("scroll_y", "aiml_input_event_scroll_y")
            INP_STR_GETTER("char_",    "aiml_input_event_char_")
            INP_F32_GETTER("delta_x",  "aiml_input_event_delta_x")
            INP_F32_GETTER("delta_y",  "aiml_input_event_delta_y")
#undef INP_STR_GETTER
#undef INP_I32_GETTER
#undef INP_F32_GETTER
            ctx.classes["input_event"] = std::move(ci);
        }

        auto& inp = ctx.namespaces["input"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* f32 = ctx.builder.getFloatTy();
        auto* vd  = ctx.builder.getVoidTy();
        auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

        // Helper: get string arg i (i8*)
        // Helper: get i32 arg i
        // Helper: get f32 arg i

        // input.init(win)
        inp.builtinFuncs["init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* vd  = ctx.builder.getVoidTy();
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_init", vd, {p}), {w});
            return nullptr;
        };

        // input.update() / input.set_capture(on)
        inp.builtinFuncs["update"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_update", ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
        inp.builtinFuncs["set_capture"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* on = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_set_capture",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {on});
            return nullptr;
        };

        // input.key_down/pressed/released(key: string) → i32
#define INP_KEY_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* k = args.empty() \
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {k})); \
        };
        INP_KEY_FUNC("key_down",        "aiml_input_key_down")
        INP_KEY_FUNC("key_pressed",     "aiml_input_key_pressed")
        INP_KEY_FUNC("key_pressed_once","aiml_input_key_pressed_once")
        INP_KEY_FUNC("key_released",    "aiml_input_key_released")
#undef INP_KEY_FUNC

        // input.mouse_pos_x/y / delta_x/y / scroll_x/y → f32
#define INP_F32_0(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getFloatTy(), {}), {})); \
        };
        INP_F32_0("mouse_pos_x",    "aiml_input_mouse_pos_x")
        INP_F32_0("mouse_pos_y",    "aiml_input_mouse_pos_y")
        INP_F32_0("mouse_delta_x",  "aiml_input_mouse_delta_x")
        INP_F32_0("mouse_delta_y",  "aiml_input_mouse_delta_y")
        INP_F32_0("mouse_scroll_x", "aiml_input_mouse_scroll_x")
        INP_F32_0("mouse_scroll_y", "aiml_input_mouse_scroll_y")
#undef INP_F32_0

        // input.mouse_pos() / mouse_delta() / mouse_scroll() → vec2
        inp.builtinFuncs["mouse_pos"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_pos_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_pos_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        inp.builtinFuncs["mouse_delta"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_delta_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_delta_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        inp.builtinFuncs["mouse_scroll"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_scroll_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_scroll_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };

        // input.mouse_button/pressed/released(btn: i32) → i32
#define INP_BTN_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                    ctx.builder.getInt32Ty()); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), \
                    {ctx.builder.getInt32Ty()}), {b})); \
        };
        INP_BTN_FUNC("mouse_button",   "aiml_input_mouse_button")
        INP_BTN_FUNC("mouse_pressed",  "aiml_input_mouse_pressed")
        INP_BTN_FUNC("mouse_released", "aiml_input_mouse_released")
#undef INP_BTN_FUNC

        // input.gamepad_connected(pad) → i32
        inp.builtinFuncs["gamepad_connected"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* pad = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_gamepad_connected", i32, {i32}), {pad}));
        };

        // input.gamepad_axis(pad, axis) → f32
        inp.builtinFuncs["gamepad_axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_gamepad_axis", f32, {i32, i32}),
                {g32(0), g32(1)}));
        };

        // input.gamepad_button/pressed(pad, btn) → i32
#define INP_GP_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto g32 = [&](int i) -> llvm::Value* { \
                if ((int)args.size() <= i) return ctx.builder.getInt32(0); \
                return ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32); \
            }; \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {i32, i32}), {g32(0), g32(1)})); \
        };
        INP_GP_FUNC("gamepad_button",  "aiml_input_gamepad_button")
        INP_GP_FUNC("gamepad_pressed", "aiml_input_gamepad_pressed")
#undef INP_GP_FUNC

        // input.has_event() → i32
        inp.builtinFuncs["has_event"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_has_event", ctx.builder.getInt32Ty(), {}), {}));
        };

        // input.next_event() → input_event (returns opaque i8* pointer)
        inp.builtinFuncs["next_event"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_next_event", p, {}), {}));
        };

        // input.clear_events()
        inp.builtinFuncs["clear_events"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_clear_events",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };

        // input.map_axis(name, key, scale)
        inp.builtinFuncs["map_axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* scale = args.size() > 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}),
                {gstr(0), gstr(1), scale});
            return nullptr;
        };

        // input.map_axis_mouse(name, comp, scale)
        inp.builtinFuncs["map_axis_mouse"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* scale = args.size() > 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis_mouse",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}),
                {gstr(0), gstr(1), scale});
            return nullptr;
        };

        // input.map_axis_gamepad(name, pad, axis, scale)
        inp.builtinFuncs["map_axis_gamepad"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* scale = args.size() > 3
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis_gamepad",
                ctx.builder.getVoidTy(), {i8p, i32, i32, f32}),
                {name, g32(1), g32(2), scale});
            return nullptr;
        };

        // input.map_action(name, key)
        inp.builtinFuncs["map_action"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action",
                ctx.builder.getVoidTy(), {i8p, i8p}), {gstr(0), gstr(1)});
            return nullptr;
        };

        // input.map_action_mouse(name, btn)
        inp.builtinFuncs["map_action_mouse"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            llvm::Value* btn  = args.size() > 1
                ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32)
                : ctx.builder.getInt32(0);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action_mouse",
                ctx.builder.getVoidTy(), {i8p, i32}), {name, btn});
            return nullptr;
        };

        // input.map_action_gamepad(name, pad, btn)
        inp.builtinFuncs["map_action_gamepad"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action_gamepad",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {name, g32(1), g32(2)});
            return nullptr;
        };

        // input.unmap(name) / input.clear_mappings()
        inp.builtinFuncs["unmap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_unmap",
                ctx.builder.getVoidTy(), {i8p}), {name});
            return nullptr;
        };
        inp.builtinFuncs["clear_mappings"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_clear_mappings",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };

        // input.axis(name) → f32
        inp.builtinFuncs["axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_axis", ctx.builder.getFloatTy(), {i8p}), {name}));
        };

        // input.action_down/pressed/released(name) → i32
#define INP_ACTION_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* n = args.empty() \
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {n})); \
        };
        INP_ACTION_FUNC("action_down",     "aiml_input_action_down")
        INP_ACTION_FUNC("action_pressed",  "aiml_input_action_pressed")
        INP_ACTION_FUNC("action_released", "aiml_input_action_released")
#undef INP_ACTION_FUNC

        // ── Drag-and-drop polling ─────────────────────────────────────────
#define INP_DRAG_I32(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {i32}), {b})); \
        };
#define INP_DRAG_F32(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, f32, {i32}), {b})); \
        };
        INP_DRAG_I32("drag_active",    "aiml_input_drag_active")
        INP_DRAG_I32("drag_started",   "aiml_input_drag_started")
        INP_DRAG_I32("drag_ended",     "aiml_input_drag_ended")
        INP_DRAG_F32("drag_origin_x",  "aiml_input_drag_origin_x")
        INP_DRAG_F32("drag_origin_y",  "aiml_input_drag_origin_y")
        INP_DRAG_F32("drag_delta_x",   "aiml_input_drag_delta_x")
        INP_DRAG_F32("drag_delta_y",   "aiml_input_drag_delta_y")
#undef INP_DRAG_I32
#undef INP_DRAG_F32

        // input.drag_set_payload(data: ptr) / drag_get_payload() → ptr
        inp.builtinFuncs["drag_set_payload"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p  = llvm::PointerType::get(ctx.context, 0);
            auto* vd = ctx.builder.getVoidTy();
            llvm::Value* data = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_drag_set_payload", vd, {p}), {data});
            return nullptr;
        };
        inp.builtinFuncs["drag_get_payload"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_drag_get_payload", p, {}), {}));
        };
        inp.builtinFuncs["drag_set_threshold"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* vd  = ctx.builder.getVoidTy();
            llvm::Value* px = args.empty() ? llvm::ConstantFP::get(f32, 5.0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (!px->getType()->isFloatTy()) px = ctx.builder.CreateFPCast(px, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_drag_set_threshold", vd, {f32}), {px});
            return nullptr;
        };

        (void)p; (void)i8p; (void)i32; (void)f32; (void)vd; (void)null;
    } // end input

    // ==========================================================================
    // ui — Immediate-Mode UI (Grid-Layout + 2D Batch-Renderer + stb_truetype)
    // ==========================================================================
    {
        auto* p    = llvm::PointerType::get(ctx.context, 0);
        auto* i8p  = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
        auto* i32  = ctx.builder.getInt32Ty();
        auto* f32  = ctx.builder.getFloatTy();
        auto* vd   = ctx.builder.getVoidTy();
        auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

        // Helper: get arg[i] as pointer (buffer<T> → store to alloca if struct value)
        // ui_style passed by value → store to alloca → pass alloca ptr
        // This lambda captures ctx+args by ref; used in each handler via inline copy.

        // ── ui_font type ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ui_font"; ci.isBuiltin = true; ci.preLoadBase = true;
            ci.type = nullptr;
            ctx.classes["ui_font"] = std::move(ci);
        }

        // ── ui_style type ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ui_style"; ci.isBuiltin = false;
            ci.type = llvm::cast<llvm::StructType>(ctx.getLLVMType("ui_style"));
            // GEP indices matching AimlUIStyle C struct field order:
            auto ui_sm = [&](int idx, const char* tname) {
                CompilerContext::StructMember m;
                m.index    = idx;
                m.typeName = tname;
                m.access   = AccessLevel::Public;
                return m;
            };
            ci.members["color"]         = ui_sm(0,  "i32");
            ci.members["bg_color"]      = ui_sm(1,  "i32");
            ci.members["hover_color"]   = ui_sm(2,  "i32");
            ci.members["active_color"]  = ui_sm(3,  "i32");
            ci.members["border_color"]  = ui_sm(4,  "i32");
            ci.members["font_size"]     = ui_sm(5,  "f32");
            ci.members["border_width"]  = ui_sm(6,  "f32");
            ci.members["corner_radius"] = ui_sm(7,  "f32");
            ci.members["padding_h"]     = ui_sm(8,  "f32");
            ci.members["padding_v"]     = ui_sm(9,  "f32");
            ci.members["opacity"]       = ui_sm(10, "f32");
            ci.members["font"]          = ui_sm(12, "ui_font");
            ci.members["padding_x"]     = ui_sm(11, "f32");
            ci.members["padding_y"]     = ui_sm(13, "f32");
            ci.members["size_w"]        = ui_sm(14, "f32");
            ci.members["size_h"]        = ui_sm(15, "f32");
            ctx.classes["ui_style"] = std::move(ci);
        }

        // ── ui namespace ─────────────────────────────────────────────────────
        auto& ui = ctx.namespaces["ui"];

        // Helper macro: style arg → store struct to alloca → pass as i8*
#define UI_STYLE_ARG(argIdx) \
        ([&]() -> llvm::Value* { \
            if ((int)args.size() <= (argIdx)) return null; \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[argIdx])); \
            if (v->getType()->isPointerTy()) return v; \
            auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_style_arg"); \
            ctx.builder.CreateStore(v, tmp); return tmp; \
        }())

        // Helper macro: buffer<f32> arg → store struct to alloca → pass as ptr
#define UI_BUF_ARG(argIdx) \
        ([&]() -> llvm::Value* { \
            if ((int)args.size() <= (argIdx)) return null; \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[argIdx])); \
            if (v->getType()->isPointerTy()) return v; \
            auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_buf_arg"); \
            ctx.builder.CreateStore(v, tmp); return tmp; \
        }())

        // Helper macro: get arg[i] as pointer (simple, for i8* args)
#define UI_PTR_ARG(i) \
        ((int)args.size() > (i) \
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : null)

        // Helper macro: get arg[i] as i32
#define UI_I32_ARG(i) \
        ((int)args.size() > (i) \
            ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32) : ctx.builder.getInt32(0))

        // Helper macro: get arg[i] as f32
#define UI_F32_ARG(i) \
        ((int)args.size() > (i) \
            ? ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32) : llvm::ConstantFP::get(f32, 0.0))

        // ui.init()
        ui.builtinFuncs["init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_init",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.begin(win: window_handle)
        ui.builtinFuncs["begin"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* w = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr; };

        // ui.end()
        ui.builtinFuncs["end"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.begin_grid(cols: buffer<f32>, rows: buffer<f32>)
        ui.builtinFuncs["begin_grid"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto getArg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isPointerTy()) return v;
                auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_gbuf");
                ctx.builder.CreateStore(v, tmp); return tmp;
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin_grid",
                ctx.builder.getVoidTy(), {p, p}), {getArg(0), getArg(1)});
            return nullptr; };

        // ui.end_grid()
        ui.builtinFuncs["end_grid"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_grid",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.set_gap(h: f32, v: f32)
        ui.builtinFuncs["set_gap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto farg = [&](int i) { return (int)args.size() > i
                ? ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32)
                : llvm::ConstantFP::get(f32, 0.0); };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_gap",
                ctx.builder.getVoidTy(), {f32, f32}), {farg(0), farg(1)});
            return nullptr; };

        // ui.begin_cell(col, row, colspan, rowspan)
        ui.builtinFuncs["begin_cell"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto iarg = [&](int i) { return (int)args.size() > i
                ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32)
                : ctx.builder.getInt32(0); };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin_cell",
                ctx.builder.getVoidTy(), {i32, i32, i32, i32}),
                {iarg(0), iarg(1), iarg(2), iarg(3)});
            return nullptr; };

        // ui.end_cell()
        ui.builtinFuncs["end_cell"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_cell",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.set_align(h: string, v: string)
        ui.builtinFuncs["set_align"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_align",
                ctx.builder.getVoidTy(), {p, p}), {sarg(0), sarg(1)});
            return nullptr; };

        // ui.set_scroll(axis: string) — "x", "y", or "xy"
        ui.builtinFuncs["set_scroll"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* axis = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_scroll",
                ctx.builder.getVoidTy(), {p}), {axis});
            return nullptr; };

        // ui.label(text: string, preset: ui_label_style = null)
        ui.builtinFuncs["label"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_label",
                ctx.builder.getVoidTy(), {p, p}), {t, preset});
            return nullptr; };

        // ui.label_colored(text: string, color: i32, preset: ui_label_style = null)
        ui.builtinFuncs["label_colored"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* c = args.size() < 2 ? ctx.builder.getInt32(-1)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_label_colored",
                ctx.builder.getVoidTy(), {p, i32, p}), {t, c, preset});
            return nullptr; };

        // ui.button(label: string, preset: ui_button_style = null) → bool (i32)
        ui.builtinFuncs["button"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_button", ctx.builder.getInt32Ty(), {p, p}), {t, preset})); };

        // ui.button_icon(label: string, tex: gl_texture) → bool (i32)
        ui.builtinFuncs["button_icon"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_button_icon", ctx.builder.getInt32Ty(), {p, p}),
                {sarg(0), sarg(1)})); };

        // ui.checkbox(id, value: bool, preset: ui_button_style = null) → bool
        ui.builtinFuncs["checkbox"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id  = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* val = args.size() < 2 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_checkbox", i32, {p, i32, p}), {id, val, preset})); };

        // ui.spinbox(id, options: list<string>, current: i32, preset: ui_button_style = null) → i32
        ui.builtinFuncs["spinbox"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // Store the loaded list<string> struct into a temp alloca; pass pointer to C
            llvm::Type* listTy = ctx.getLLVMType("list<string>");
            llvm::AllocaInst* listAlloca = ctx.createEntryAlloca(listTy, "sbox_opts");
            if (args.size() >= 2) {
                ctx.builder.CreateStore(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), listAlloca);
            } else {
                ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(listTy), listAlloca);
            }

            llvm::Value* cur = args.size() < 3 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])), i32);
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_spinbox", i32, {p, p, i32, p}),
                {id, listAlloca, cur, preset})); };

        // ui.cell_hovered() → bool (i32)
        ui.builtinFuncs["cell_hovered"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_cell_hovered", ctx.builder.getInt32Ty(), {}), {})); };

        // ui.toggle(id, value: bool, preset: ui_button_style = null) → bool
        ui.builtinFuncs["toggle"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id  = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* val = args.size() < 2 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_toggle", i32, {p, i32, p}), {id, val, preset})); };

        // ui.radio(id, option: i32, current: i32, preset: ui_button_style = null) → i32
        ui.builtinFuncs["radio"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_radio", i32, {p, i32, i32, p}),
                {id, iarg(1), iarg(2), preset})); };

        // ui.slider(id, value: f32, min: f32, max: f32, preset: ui_slider_style = null) → f32
        ui.builtinFuncs["slider"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 5 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_slider", f32, {p, f32, f32, f32, p}),
                {id, farg(1), farg(2), farg(3), preset})); };

        // ui.slider_int(id, value: i32, min: i32, max: i32, preset: ui_slider_style = null) → i32
        ui.builtinFuncs["slider_int"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 5 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_slider_int", i32, {p, i32, i32, i32, p}),
                {id, iarg(1), iarg(2), iarg(3), preset})); };

        // ui.drag_f32(id, value: f32, speed: f32, preset: ui_slider_style = null) → f32
        ui.builtinFuncs["drag_f32"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_drag_f32", f32, {p, f32, f32, p}),
                {id, farg(1), farg(2), preset})); };

        // ui.text_input(id: string, buf: string, preset: ui_label_style = null) → string
        ui.builtinFuncs["text_input"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null : sarg(2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_text_input", p, {p, p, p}), {sarg(0), sarg(1), preset})); };

        // ui.text_area(id: string, buf: string, preset: ui_label_style = null) → string
        ui.builtinFuncs["text_area"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null : sarg(2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_text_area", p, {p, p, p}), {sarg(0), sarg(1), preset})); };

        // ui.progress_bar(value: f32, preset: ui_progress_style = null)
        ui.builtinFuncs["progress_bar"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_progress_bar",
                ctx.builder.getVoidTy(), {f32, p}), {v, preset});
            return nullptr; };

        // ui.progress_bar_colored(value: f32, fg: i32, bg: i32, preset: ui_progress_style = null)
        ui.builtinFuncs["progress_bar_colored"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_progress_bar_colored",
                ctx.builder.getVoidTy(), {f32, i32, i32, p}), {farg(0), iarg(1), iarg(2), preset});
            return nullptr; };

        // ui.image(tex: gl_texture)
        ui.builtinFuncs["image"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_image",
                ctx.builder.getVoidTy(), {p}), {t});
            return nullptr; };

        // ui.image_sized(tex: gl_texture, w: f32, h: f32)
        ui.builtinFuncs["image_sized"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* t = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_image_sized",
                ctx.builder.getVoidTy(), {p, f32, f32}), {t, farg(1), farg(2)});
            return nullptr; };

        // ui.rect(color: i32)
        ui.builtinFuncs["rect"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* c = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_rect",
                ctx.builder.getVoidTy(), {i32}), {c});
            return nullptr; };

        // ui.rect_rounded(color: i32, radius: f32, preset: ui_panel_style = null)
        ui.builtinFuncs["rect_rounded"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* c = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            llvm::Value* r = args.size() < 2 ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), f32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_rect_rounded",
                ctx.builder.getVoidTy(), {i32, f32, p}), {c, r, preset});
            return nullptr; };

        // ui.separator(), ui.same_line(), ui.newline()
        ui.builtinFuncs["separator"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_separator",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };
        ui.builtinFuncs["same_line"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_same_line",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };
        ui.builtinFuncs["newline"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_newline",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.spacer(size: f32)
        ui.builtinFuncs["spacer"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* s = args.empty() ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_spacer",
                ctx.builder.getVoidTy(), {f32}), {s});
            return nullptr; };

        // ui.begin_notebook(id: string) → i32 (active tab index)
        ui.builtinFuncs["begin_notebook"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_begin_notebook", ctx.builder.getInt32Ty(), {p}), {id})); };

        // ui.notebook_tab(label: string) → i32 (1 if active)
        ui.builtinFuncs["notebook_tab"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* lbl = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_notebook_tab", ctx.builder.getInt32Ty(), {p}), {lbl})); };

        // ui.end_notebook()
        ui.builtinFuncs["end_notebook"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_notebook",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.notebook_remove_clicked(id: string) → i32
        ui.builtinFuncs["notebook_remove_clicked"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_notebook_remove_clicked", ctx.builder.getInt32Ty(), {p}), {id})); };

        // ui.push_style(s: ui_style) — style is a struct value, pass by alloca ptr
        ui.builtinFuncs["push_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* sp = null;
            if (!args.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v->getType()->isPointerTy()) sp = v;
                else { auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_ps");
                       ctx.builder.CreateStore(v, tmp); sp = tmp; }
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_push_style",
                ctx.builder.getVoidTy(), {p}), {sp});
            return nullptr; };

        // ui.pop_style()
        ui.builtinFuncs["pop_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_pop_style",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.set_default_style(s: ui_style)
        ui.builtinFuncs["set_default_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* sp = null;
            if (!args.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v->getType()->isPointerTy()) sp = v;
                else { auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_ds");
                       ctx.builder.CreateStore(v, tmp); sp = tmp; }
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_default_style",
                ctx.builder.getVoidTy(), {p}), {sp});
            return nullptr; };

        // ui.load_font(path: path, size: f32) → ui_font
        ui.builtinFuncs["load_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* path = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* sz   = args.size() < 2 ? llvm::ConstantFP::get(f32, 16.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_load_font", p, {p, f32}), {path, sz})); };

        // ui.load_font_default(size: f32) → ui_font
        ui.builtinFuncs["load_font_default"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* sz = args.empty() ? llvm::ConstantFP::get(f32, 16.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_load_font_default", p, {f32}), {sz})); };

        // ui.set_default_font(font: ui_font)
        ui.builtinFuncs["set_default_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* f = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_default_font",
                ctx.builder.getVoidTy(), {p}), {f});
            return nullptr; };

        // ui.slider_style_new() → ui_slider_style
        // ui.button_style_new() → ui_button_style
        // ui.label_style_new()  → ui_label_style
        // ui.progress_style_new() → ui_progress_style
        // ui.panel_style_new()  → ui_panel_style
        {
            auto mk_factory = [](const std::string& fn) -> CompilerContext::FuncHandler {
                return [fn](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                    auto* pp = llvm::PointerType::get(ctx.context, 0);
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(fn, pp, {}), {}));
                };
            };
            ui.builtinFuncs["slider_style_new"]   = mk_factory("aiml_ui_slider_style_new");
            ui.builtinFuncs["button_style_new"]   = mk_factory("aiml_ui_button_style_new");
            ui.builtinFuncs["label_style_new"]    = mk_factory("aiml_ui_label_style_new");
            ui.builtinFuncs["progress_style_new"] = mk_factory("aiml_ui_progress_style_new");
            ui.builtinFuncs["panel_style_new"]    = mk_factory("aiml_ui_panel_style_new");
        }

        (void)p; (void)i8p; (void)i32; (void)f32; (void)vd; (void)null;
#undef UI_STYLE_ARG
#undef UI_BUF_ARG
#undef UI_PTR_ARG
#undef UI_I32_ARG
#undef UI_F32_ARG
    } // end ui

    // ── Widget Style Preset Classes ───────────────────────────────────────────
    {
        using MH = CompilerContext::MethodHandler;
        using CI = CompilerContext::ClassInfo;
        using EvalFn = std::function<AIML::IRValue*(AIML::AST::Expr*)>;

        auto mk_i32_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp  = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* vT  = llvm::Type::getVoidTy(ctx.context);
                llvm::Value* arg = call->args.empty()
                    ? ctx.builder.getInt32(0)
                    : ctx.builder.CreateZExtOrTrunc(
                          static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))), i32);
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, i32}), {base, arg});
                return nullptr;
            };
        };
        auto mk_f32_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp  = llvm::PointerType::get(ctx.context, 0);
                auto* f32 = ctx.builder.getFloatTy();
                auto* vT  = llvm::Type::getVoidTy(ctx.context);
                llvm::Value* raw = call->args.empty()
                    ? llvm::ConstantFP::get(f32, 0.0)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* arg = raw->getType()->isFloatTy() ? raw : ctx.builder.CreateFPCast(raw, f32);
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, f32}), {base, arg});
                return nullptr;
            };
        };
        auto mk_ptr_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp   = llvm::PointerType::get(ctx.context, 0);
                auto* vT   = llvm::Type::getVoidTy(ctx.context);
                auto* null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(pp));
                llvm::Value* arg = call->args.empty()
                    ? static_cast<llvm::Value*>(null)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, pp}), {base, arg});
                return nullptr;
            };
        };
        auto add_common = [&](CI& ci, const std::string& pfx) {
            ci.builtinMethods["set_color"]         = mk_i32_set(pfx + "_set_color");
            ci.builtinMethods["set_bg_color"]      = mk_i32_set(pfx + "_set_bg_color");
            ci.builtinMethods["set_hover_color"]   = mk_i32_set(pfx + "_set_hover_color");
            ci.builtinMethods["set_active_color"]  = mk_i32_set(pfx + "_set_active_color");
            ci.builtinMethods["set_border_color"]  = mk_i32_set(pfx + "_set_border_color");
            ci.builtinMethods["set_corner_radius"] = mk_f32_set(pfx + "_set_corner_radius");
            ci.builtinMethods["set_border_width"]  = mk_f32_set(pfx + "_set_border_width");
            ci.builtinMethods["set_font"]          = mk_ptr_set(pfx + "_set_font");
            ci.builtinMethods["set_padding_x"]     = mk_f32_set(pfx + "_set_padding_x");
            ci.builtinMethods["set_padding_y"]     = mk_f32_set(pfx + "_set_padding_y");
            ci.builtinMethods["set_size_w"]        = mk_f32_set(pfx + "_set_size_w");
            ci.builtinMethods["set_size_h"]        = mk_f32_set(pfx + "_set_size_h");
        };

        // ui_slider_style
        {
            CI ci; ci.name = "ui_slider_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_slider_style");
            ci.builtinMethods["set_thumb"] = mk_ptr_set("aiml_ui_slider_style_set_thumb");
            ci.builtinMethods["set_track"] = mk_ptr_set("aiml_ui_slider_style_set_track");
            ctx.classes["ui_slider_style"] = ci;
        }
        // ui_button_style
        {
            CI ci; ci.name = "ui_button_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_button_style");
            ci.builtinMethods["set_bg"]     = mk_ptr_set("aiml_ui_button_style_set_bg");
            ci.builtinMethods["set_hover"]  = mk_ptr_set("aiml_ui_button_style_set_hover");
            ci.builtinMethods["set_active"] = mk_ptr_set("aiml_ui_button_style_set_active");
            ctx.classes["ui_button_style"] = ci;
        }
        // ui_label_style
        {
            CI ci; ci.name = "ui_label_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_label_style");
            ctx.classes["ui_label_style"] = ci;
        }
        // ui_progress_style
        {
            CI ci; ci.name = "ui_progress_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_progress_style");
            ci.builtinMethods["set_fill"]   = mk_ptr_set("aiml_ui_progress_style_set_fill");
            ci.builtinMethods["set_bg_tex"] = mk_ptr_set("aiml_ui_progress_style_set_bg_tex");
            ctx.classes["ui_progress_style"] = ci;
        }
        // ui_panel_style
        {
            CI ci; ci.name = "ui_panel_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_panel_style");
            ci.builtinMethods["set_bg_tex"] = mk_ptr_set("aiml_ui_panel_style_set_bg_tex");
            ctx.classes["ui_panel_style"] = ci;
        }
    }

    // ── audio_sound + audio_group builtin classes + audio namespace ───────────
    {
        using CI = CompilerContext::ClassInfo;
        using MH = CompilerContext::MethodHandler;

        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* f32  = ctx.builder.getFloatTy();
        llvm::Type* vd   = ctx.builder.getVoidTy();
        llvm::StructType* vec3Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec3"));

        // Helper: store vec3 struct value into a temp alloca, return the alloca ptr
        auto storeVec3 = [vec3Ty](CompilerContext& ctx2, llvm::Value* v) -> llvm::Value* {
            llvm::AllocaInst* tmp = ctx2.builder.CreateAlloca(vec3Ty);
            ctx2.builder.CreateStore(v, tmp);
            return tmp;
        };

        // ── audio_sound class ────────────────────────────────────────────────
        {
            CI ci; ci.name = "audio_sound"; ci.isBuiltin = true;

            auto mk0v = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(f, {p});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk0i = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getInt32Ty(), {i8p});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
                };
            };
            auto mk0f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getFloatTy(), {i8p});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
                };
            };
            auto mk1i = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* i32 = ctx.builder.getInt32Ty();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty() ? ctx.builder.getInt32(0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, i32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk1f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* f32 = ctx.builder.getFloatTy();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty()
                        ? llvm::ConstantFP::get(f32, 0.0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    // ensure f32 even if caller passed f64
                    if (a0->getType()->isDoubleTy())
                        a0 = ctx.builder.CreateFPTrunc(a0, f32);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, f32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };

            ci.builtinMethods["play"]    = mk0v("aiml_audio_sound_play");
            ci.builtinMethods["pause"]   = mk0v("aiml_audio_sound_pause");
            ci.builtinMethods["stop"]    = mk0v("aiml_audio_sound_stop");
            ci.builtinMethods["free"]    = mk0v("aiml_audio_sound_free");
            ci.builtinMethods["is_playing"]  = mk0i("aiml_audio_sound_is_playing");
            ci.builtinMethods["is_finished"] = mk0i("aiml_audio_sound_is_finished");
            ci.builtinMethods["duration"]    = mk0f("aiml_audio_sound_duration");
            ci.builtinMethods["position"]    = mk0f("aiml_audio_sound_position");
            ci.builtinMethods["set_loop"]    = mk1i("aiml_audio_sound_set_loop");
            ci.builtinMethods["set_volume"]  = mk1f("aiml_audio_sound_set_volume");
            ci.builtinMethods["set_pitch"]   = mk1f("aiml_audio_sound_set_pitch");
            ci.builtinMethods["set_pan"]     = mk1f("aiml_audio_sound_set_pan");
            ci.builtinMethods["seek"]        = mk1f("aiml_audio_sound_seek");

            // set_group(group: audio_group)
            ci.builtinMethods["set_group"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* g = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (g->getType()->isPointerTy() && !g->getType()->getPointerAddressSpace()
                    && g->getType() == i8p) {
                    // already i8*
                } else if (!g->getType()->isPointerTy()) {
                    // loaded value — store to alloca and load as pointer
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(g->getType());
                    ctx.builder.CreateStore(g, tmp);
                    g = ctx.builder.CreateLoad(i8p, tmp);
                }
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_group",
                                                    ctx.builder.getVoidTy(), {i8p, i8p});
                ctx.builder.CreateCall(f, {p, g});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_position(pos: vec3)
            ci.builtinMethods["set_position"] = [storeVec3, vec3Ty](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (v->getType() != vec3Ty) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* x = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* y = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* z = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_position",
                    ctx.builder.getVoidTy(), {i8p, f32, f32, f32});
                ctx.builder.CreateCall(f, {p, x, y, z});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_velocity(vel: vec3)
            ci.builtinMethods["set_velocity"] = [storeVec3, vec3Ty](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (v->getType() != vec3Ty) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* x = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* y = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* z = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_velocity",
                    ctx.builder.getVoidTy(), {i8p, f32, f32, f32});
                ctx.builder.CreateCall(f, {p, x, y, z});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_attenuation(model: string, min_dist: f32, max_dist: f32)
            ci.builtinMethods["set_attenuation"] = [](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.size() < 3) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* model    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* min_dist = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* max_dist = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
                if (min_dist->getType()->isDoubleTy()) min_dist = ctx.builder.CreateFPTrunc(min_dist, f32);
                if (max_dist->getType()->isDoubleTy()) max_dist = ctx.builder.CreateFPTrunc(max_dist, f32);
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_attenuation",
                    ctx.builder.getVoidTy(), {i8p, i8p, f32, f32});
                ctx.builder.CreateCall(f, {p, model, min_dist, max_dist});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_spatialization(on: bool/i32)
            ci.builtinMethods["set_spatialization"] = mk1i("aiml_audio_sound_set_spatialization");

            // set_directional_attenuation(factor: f32)
            ci.builtinMethods["set_directional_attenuation"] = mk1f("aiml_audio_sound_set_directional_attenuation");

            ctx.classes["audio_sound"] = std::move(ci);
        }

        // ── audio_group class ────────────────────────────────────────────────
        {
            CI ci; ci.name = "audio_group"; ci.isBuiltin = true;

            auto mk0v = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(f, {p});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk1f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* f32 = ctx.builder.getFloatTy();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty()
                        ? llvm::ConstantFP::get(f32, 1.0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    if (a0->getType()->isDoubleTy()) a0 = ctx.builder.CreateFPTrunc(a0, f32);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, f32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };

            ci.builtinMethods["set_volume"]  = mk1f("aiml_audio_group_set_volume");
            ci.builtinMethods["pause_all"]   = mk0v("aiml_audio_group_pause_all");
            ci.builtinMethods["resume_all"]  = mk0v("aiml_audio_group_resume_all");
            ci.builtinMethods["stop_all"]    = mk0v("aiml_audio_group_stop_all");

            ctx.classes["audio_group"] = std::move(ci);
        }

        // ── audio namespace ──────────────────────────────────────────────────
        {
            auto& ns = ctx.namespaces["audio"];

            auto toF32 = [](CompilerContext& ctx, llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isDoubleTy()) return ctx.builder.CreateFPTrunc(v, ctx.builder.getFloatTy());
                return v;
            };

            // audio.sfx(stem) / audio.music(stem) → audio_handle (spec/44_7).
            auto mkAssetAudio = [](const char* runtime_fn, AIML::AssetKind kind,
                                   const char* method) {
                return [runtime_fn, kind, method](CompilerContext& ctx,
                    const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                    auto* p = llvm::PointerType::get(ctx.context, 0);
                    if (!args.empty() && ctx.currentCall &&
                        !ctx.currentCall->args.empty()) {
                        if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                                ctx.currentCall->args[0].get())) {
                            if (lit->type == "string" &&
                                !AIML::AssetScanner::instance().has_stem(kind, lit->value)) {
                                LOG_ERROR(std::string("audio.") + method +
                                          ": stem '" + lit->value +
                                          "' not in audio store");
                            }
                        }
                    }
                    llvm::Value* arg = args.empty()
                        ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction(runtime_fn, p, {p}), {arg}));
                };
            };
            ns.builtinFuncs["sfx"]   = mkAssetAudio("aiml_asset_get_sfx",   AIML::AssetKind::Audio, "sfx");
            ns.builtinFuncs["music"] = mkAssetAudio("aiml_asset_get_music", AIML::AssetKind::Audio, "music");

            // audio.init()
            ns.builtinFuncs["init"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Function* f = ctx.getFunction("aiml_audio_init", ctx.builder.getVoidTy(), {});
                ctx.builder.CreateCall(f, {});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.shutdown()
            ns.builtinFuncs["shutdown"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Function* f = ctx.getFunction("aiml_audio_shutdown", ctx.builder.getVoidTy(), {});
                ctx.builder.CreateCall(f, {});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.set_volume(v: f32)
            ns.builtinFuncs["set_volume"] = [toF32](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* v = args.empty()
                    ? llvm::ConstantFP::get(f32, 1.0)
                    : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                llvm::Function* f = ctx.getFunction("aiml_audio_set_volume", ctx.builder.getVoidTy(), {f32});
                ctx.builder.CreateCall(f, {v});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.load(p: path) → audio_sound (i8*)
            ns.builtinFuncs["load"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* path = args.empty()
                    ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Function* f = ctx.getFunction("aiml_audio_load", i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {path}));
            };

            // audio.load_stream(p: path) → audio_sound (i8*)
            ns.builtinFuncs["load_stream"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* path = args.empty()
                    ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Function* f = ctx.getFunction("aiml_audio_load_stream", i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {path}));
            };

            // audio.create_group() → audio_group (i8*)
            ns.builtinFuncs["create_group"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* f = ctx.getFunction("aiml_audio_create_group", i8p, {});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
            };

            // audio.set_listener(pos: vec3, forward: vec3, up: vec3)
            ns.builtinFuncs["set_listener"] = [vec3Ty](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                if (args.size() < 3) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                auto extract = [&](AIML::IRValue* irv, unsigned idx) -> llvm::Value* {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(irv));
                    return ctx.builder.CreateExtractValue(v, {idx});
                };
                llvm::Value* px = extract(args[0], 0u); llvm::Value* py = extract(args[0], 1u); llvm::Value* pz = extract(args[0], 2u);
                llvm::Value* fx = extract(args[1], 0u); llvm::Value* fy = extract(args[1], 1u); llvm::Value* fz = extract(args[1], 2u);
                llvm::Value* ux = extract(args[2], 0u); llvm::Value* uy = extract(args[2], 1u); llvm::Value* uz = extract(args[2], 2u);
                llvm::Function* fn = ctx.getFunction("aiml_audio_set_listener",
                    ctx.builder.getVoidTy(), {f32,f32,f32, f32,f32,f32, f32,f32,f32});
                ctx.builder.CreateCall(fn, {px,py,pz, fx,fy,fz, ux,uy,uz});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.set_listener_velocity(vel: vec3)
            ns.builtinFuncs["set_listener_velocity"] = [vec3Ty](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* vx = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* vy = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* vz = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* fn = ctx.getFunction("aiml_audio_set_listener_velocity",
                    ctx.builder.getVoidTy(), {f32, f32, f32});
                ctx.builder.CreateCall(fn, {vx, vy, vz});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };
        }
    }
    // ── end audio ──────────────────────────────────────────────────────────────

    // ── mirror_event + mirror_peer + mirror_session + mirror namespace ─────────
    // mirror_event: real LLVM struct {i8*(type,0), i8*(peer,1), i32(net_id,2)}
    // mirror_peer:  opaque i8* handle (no methods in v1)
    // mirror_session: opaque i8* handle, preLoadBase=false
    // ──────────────────────────────────────────────────────────────────────────
    {
        using CI     = CompilerContext::ClassInfo;
        using MH     = CompilerContext::MethodHandler;
        using EvalFn = std::function<AIML::IRValue*(AIML::AST::Expr*)>;

        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Type* vd  = ctx.builder.getVoidTy();

        // ── mirror_event (isBuiltin=false — real struct for field GEP access) ──
        // Layout MUST match AimlMirrorEvent in mirror_runtime.cpp:
        //   { i8* type, i8* peer, i32 net_id, i32 peer_id }
        // peer_id is the unique per-connection sender id assigned at accept-
        // time by the host (1..N); use this (not net_id) to identify which
        // client a peer_connected / peer_disconnected event refers to.
        {
            llvm::StructType* evTy = llvm::StructType::getTypeByName(ctx.context, "mirror_event");
            if (!evTy)
                evTy = llvm::StructType::create(ctx.context, {i8p, i8p, i32, i32}, "mirror_event");

            CI ci; ci.name = "mirror_event"; ci.type = evTy; ci.isBuiltin = false;
            auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
                CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
            };
            ci.members["type"]    = mk(i8p, 0, "string");
            ci.members["peer"]    = mk(i8p, 1, "mirror_peer");
            ci.members["net_id"]  = mk(i32, 2, "i32");
            ci.members["peer_id"] = mk(i32, 3, "i32");
            // NB: type_enum() is dispatched as a special-case in
            // ast_compiler_call.cpp (mirror_event is isBuiltin=false, so its
            // builtinMethods map is not consulted).
            ctx.classes["mirror_event"] = std::move(ci);
        }

        // ── rpc_event (isBuiltin=false — real struct, layout MUST mirror AimlRpcEvent in mirror_runtime.cpp)
        //   { i8* method, i32 net_id, i32 arg, i32 arg_kind, i8* arg_blob, i32 arg_blob_len, i32 peer_id }
        // Natural alignment inserts 4-byte padding between arg_kind (idx 3) and
        // arg_blob (idx 4) — matches the C struct on x86-64 / aarch64.
        //
        // peer_id is the unique per-connection sender id (host-assigned at
        // accept-time); multi-client servers MUST use this — NOT net_id —
        // to identify the sender, since clients can race-allocate the same
        // net_id for their respective AuthRequest objects.
        {
            llvm::StructType* evTy = llvm::StructType::getTypeByName(ctx.context, "rpc_event");
            if (!evTy)
                evTy = llvm::StructType::create(ctx.context, {i8p, i32, i32, i32, i8p, i32, i32}, "rpc_event");

            CI ci; ci.name = "rpc_event"; ci.type = evTy; ci.isBuiltin = false;
            auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
                CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
            };
            ci.members["method"]       = mk(i8p, 0, "string");
            ci.members["net_id"]       = mk(i32, 1, "i32");
            ci.members["arg"]          = mk(i32, 2, "i32");
            ci.members["arg_kind"]     = mk(i32, 3, "i32");
            ci.members["arg_blob"]     = mk(i8p, 4, "ptr");
            ci.members["arg_blob_len"] = mk(i32, 5, "i32");
            ci.members["peer_id"]      = mk(i32, 6, "i32");

            // (decode is exposed as mirror_session.decode_rpc(ev, target) — see below.)
#if 0
            // ── decode_into(out_var) — decode the typed-RPC blob into a model alloca.
            // The target type is taken from the VariableExpr passed as the first arg
            // via ctx.deserializeTargetType (set in ast_compiler_call.cpp before dispatch).
            // Returns 0 on success, -1 on error (e.g. arg_kind != 1, schema mismatch).
            ci.builtinMethods["decode_into"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 1) return ctx.ir_gen->getInt32(-1);
                // Resolve the target variable's alloca + its type name.
                llvm::Value* outPtr = nullptr;
                std::string  outTn;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (!sym) return ctx.ir_gen->getInt32(-1);
                    llvm::Value* av = sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                    // AIML v2: a value-model local's slot holds a %M* (pointer to the object), not
                    // the inline struct — load it so we decode INTO the object (and memset the
                    // object, not the 8-byte slot + adjacent stack). v1: the alloca IS the struct.
                    outTn  = sym->typeName;
                    outPtr = ctx.isRefcountModel(outTn)
                        ? ctx.builder.CreateLoad(i8p, av, "decode_modelptr")
                        : ctx.builder.CreateBitCast(av, i8p);
                }
                if (!outPtr || outTn.empty()) return ctx.ir_gen->getInt32(-1);
                if (!ctx.classes.count(outTn) || !ctx.classes[outTn].serNodesVar)
                    return ctx.ir_gen->getInt32(-1);

                auto& ci2 = ctx.classes[outTn];
                // Zero-initialise the target so heap-field pointers start null
                // (aiml_ser_decode docs: caller must zero-init `out`).
                llvm::DataLayout dl(&ctx.module);
                uint64_t outSz = dl.getTypeAllocSize(ci2.type);
                ctx.builder.CreateMemSet(
                    outPtr, ctx.builder.getInt8(0),
                    ctx.builder.getInt64(outSz),
                    llvm::MaybeAlign(1));

                // ev pointer: `base` points to a local rpc_event alloca; we need its address as i8*.
                llvm::Value* evPtr = ctx.builder.CreateBitCast(base, i8p);
                llvm::Value* nodesPtr = ci2.serNodesVar;
                llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_rpc_event_decode_into",
                        i32, {i8p, i8p, i8p, i32}),
                    {evPtr, outPtr, nodesPtr, nNodes});
                return ctx.ir_gen->wrap(r);
            };
#endif
            ctx.classes["rpc_event"] = std::move(ci);
        }

        // ── mirror_peer (opaque handle, no methods in v1) ─────────────────────
        {
            CI ci; ci.name = "mirror_peer"; ci.isBuiltin = true;
            ctx.classes["mirror_peer"] = std::move(ci);
        }

        // ── mirror_session ────────────────────────────────────────────────────
        // Helper: walk scope chain to find variable and return its alloca as i8*
        // (defined as a normal function pointer / capture, reused in all methods)
#define MIRROR_FIND_OBJ_PTR(call_arg_idx) \
        ([&]() -> llvm::Value* { \
            if (call->args.size() <= (call_arg_idx)) return nullptr; \
            if (auto* _vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[(call_arg_idx)].get())) { \
                CompilerContext::Scope* _sc = ctx.currentScope; \
                while (_sc) { if (_sc->variables.count(_vx->name)) { \
                    Symbol& _os = _sc->variables[_vx->name]; \
                    llvm::Value* _av = _os.alloca_inst \
                        ? static_cast<llvm::Value*>(_os.alloca_inst) \
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_os.ir_alloca_inst)); \
                    llvm::Value* _mp = ctx.builder.CreateBitCast(_av, i8p); \
                    if (ctx.isRefcountModel(_os.typeName)) _mp = ctx.builder.CreateLoad(i8p, _mp, "mirror_modelptr"); \
                    return _mp; } _sc = _sc->parent; } \
                if (ctx.symbol_table.count(_vx->name)) { \
                    Symbol& _os = ctx.symbol_table[_vx->name]; \
                    llvm::Value* _av = _os.alloca_inst \
                        ? static_cast<llvm::Value*>(_os.alloca_inst) \
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_os.ir_alloca_inst)); \
                    llvm::Value* _mp = ctx.builder.CreateBitCast(_av, i8p); \
                    if (ctx.isRefcountModel(_os.typeName)) _mp = ctx.builder.CreateLoad(i8p, _mp, "mirror_modelptr"); \
                    return _mp; } \
            } \
            IRValue* _av2 = evalExpr(call->args[(call_arg_idx)].get()); \
            if (!_av2) return nullptr; \
            llvm::Value* _v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_av2)); \
            if (_v->getType()->isPointerTy()) return ctx.builder.CreateBitCast(_v, i8p); \
            llvm::AllocaInst* _tmp = ctx.createEntryAlloca(_v->getType(), "reg_tmp"); \
            ctx.builder.CreateStore(_v, _tmp); \
            return ctx.builder.CreateBitCast(_tmp, i8p); \
        }())

        {
            CI ci; ci.name = "mirror_session"; ci.isBuiltin = true;

            // ── Zero-arg void methods ─────────────────────────────────────────
            auto mkVoid0 = [vd](const char* fn) -> MH {
                return [fn, vd](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                    llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* s = ctx.builder.CreateLoad(p, base, "sess_ptr");
                    ctx.builder.CreateCall(ctx.getFunction(fn, vd, {p}), {s});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mkI320 = [i32](const char* fn) -> MH {
                return [fn, i32](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                    llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* s = ctx.builder.CreateLoad(p, base, "sess_ptr");
                    return ctx.ir_gen->wrap(
                        ctx.builder.CreateCall(ctx.getFunction(fn, i32, {p}), {s}));
                };
            };

            ci.builtinMethods["close"]        = mkVoid0("aiml_mirror_session_close");
            ci.builtinMethods["update"]       = mkVoid0("aiml_mirror_session_update");
            ci.builtinMethods["is_connected"] = mkI320("aiml_mirror_session_is_connected");
            ci.builtinMethods["peer_count"]   = mkI320("aiml_mirror_session_peer_count");
            ci.builtinMethods["has_event"]    = mkI320("aiml_mirror_session_has_event");

            // session.peer_ip(peer_id) → string  (dotted IP of a peer, captured at accept)
            ci.builtinMethods["peer_ip"] = [i8p, i32](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* pid = (call && !call->args.empty())
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())))
                    : ctx.builder.getInt32(0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_peer_ip", i8p, {i8p, i32}), {s, pid}));
            };

            // ── register(obj) → i32 (net_id) ──────────────────────────────────
            ci.builtinMethods["register"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->getInt32(-1);

                int64_t objSize = 8;
                const std::string& tn = ctx.deserializeTargetType;
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].type) {
                    llvm::DataLayout dl(&ctx.module);
                    objSize = (int64_t)dl.getTypeAllocSize(ctx.classes[tn].type);
                }
                // (v2 %M* deref handled in MIRROR_FIND_OBJ_PTR — objPtr is the object.)
                // Spec 45: typed register for heap-containing structs.
                if (!tn.empty() && ctx.classes.count(tn)
                        && ctx.classes[tn].hasHeapFields && ctx.classes[tn].serNodesVar) {
                    auto& ci2 = ctx.classes[tn];
                    llvm::Value* nodesPtr = ci2.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction("aiml_mirror_session_register_typed",
                            i32, {i8p, i8p, i64, i8p, i32}),
                        {s, objPtr, ctx.builder.getInt64(objSize), nodesPtr, nNodes}));
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_register", i32, {i8p, i8p, i64}),
                    {s, objPtr, ctx.builder.getInt64(objSize)}));
            };

            // ── register_id(obj, id: i32) → void ─────────────────────────────
            ci.builtinMethods["register_id"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* idVal = ctx.builder.getInt32(0);
                if (call->args.size() >= 2) {
                    IRValue* idIRV = evalExpr(call->args[1].get());
                    if (idIRV) idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idIRV));
                }
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));

                int64_t objSize = 8;
                const std::string& tn = ctx.deserializeTargetType;
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].type) {
                    llvm::DataLayout dl(&ctx.module);
                    objSize = (int64_t)dl.getTypeAllocSize(ctx.classes[tn].type);
                }
                // (v2 %M* deref handled in MIRROR_FIND_OBJ_PTR — objPtr is the object.)
                // Spec 45: typed register_id for heap-containing structs.
                if (!tn.empty() && ctx.classes.count(tn)
                        && ctx.classes[tn].hasHeapFields && ctx.classes[tn].serNodesVar) {
                    auto& ci2 = ctx.classes[tn];
                    llvm::Value* nodesPtr = ci2.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                    ctx.builder.CreateCall(
                        ctx.getFunction("aiml_mirror_session_register_id_typed",
                            vd, {i8p, i8p, i64, i32, i8p, i32}),
                        {s, objPtr, ctx.builder.getInt64(objSize), idVal, nodesPtr, nNodes});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                }
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_register_id", vd, {i8p, i8p, i64, i32}),
                    {s, objPtr, ctx.builder.getInt64(objSize), idVal});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── unregister(obj) → void ────────────────────────────────────────
            ci.builtinMethods["unregister"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_unregister", vd, {i8p, i8p}),
                    {s, objPtr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── has_rpc() → i32 ───────────────────────────────────────────────
            ci.builtinMethods["has_rpc"] = mkI320("aiml_mirror_session_has_rpc");

            // ── next_rpc() → rpc_event (struct loaded from heap ptr) ──────────
            ci.builtinMethods["next_rpc"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_next_rpc", i8p, {i8p}), {s}, "rpc_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("rpc_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "rpc_tmp");
                llvm::DataLayout dl(&ctx.module);
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── send_rpc(net_id: i32, method: string, arg: i32 [, model_obj]) → void
            // If a 4th argument is supplied AND it is a VariableExpr referring to a
            // user model, the typed payload path is taken: the model is encoded via
            // aiml_ser_encode and shipped alongside the i32 discriminator.
            ci.builtinMethods["send_rpc"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                if (call->args.size() < 3) return ctx.ir_gen->getInt32(0);
                llvm::Value* nid    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* method = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* arg    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));

                // Optional typed-payload path: 4th arg = model variable
                if (call->args.size() >= 4) {
                    if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[3].get())) {
                        Symbol* sym = nullptr;
                        CompilerContext::Scope* sc = ctx.currentScope;
                        while (sc) {
                            if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                            sc = sc->parent;
                        }
                        if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                        if (sym) {
                            // Strip ptr<X> → X so that pointer-typed model args also work
                            std::string tn = sym->typeName;
                            bool isPtr = (tn.size() > 4 && tn.substr(0,4) == "ptr<" && tn.back() == '>');
                            if (isPtr) tn = tn.substr(4, tn.size()-5);
                            if (ctx.classes.count(tn) && ctx.classes[tn].serNodesVar) {
                                auto& ci2 = ctx.classes[tn];
                            llvm::Value* objAv = sym->alloca_inst
                                ? static_cast<llvm::Value*>(sym->alloca_inst)
                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                            // The alloca holds a POINTER to the struct when the arg is a ptr<X> OR,
                            // under AIML v2, a value-model whose local slot holds a %M* — load it.
                            // Otherwise (v1 inline value struct) the alloca IS the struct.
                            llvm::Value* objPtr;
                            if (isPtr || ctx.isRefcountModel(tn)) {
                                objPtr = ctx.builder.CreateLoad(i8p, objAv, "pay_ptr");
                            } else {
                                objPtr = ctx.builder.CreateBitCast(objAv, i8p);
                            }
                            llvm::Value* nodesPtr = ci2.serNodesVar;
                            llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                            ctx.builder.CreateCall(
                                ctx.getFunction("aiml_mirror_session_send_rpc_model",
                                    vd, {i8p, i32, i8p, i32, i8p, i8p, i32}),
                                {s, nid, method, arg, objPtr, nodesPtr, nNodes});
                                return ctx.ir_gen->getInt32(0);
                            } else {
                                // 4th arg present but type is not a serializable model → compiler error
                                LOG_ERROR(ctx.fmtLoc() + "send_rpc: 4th argument '" + vx->name +
                                    "' (type '" + sym->typeName + "') is not a serializable model type");
                            }
                        } // if sym
                    }
                }

                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_send_rpc", vd, {i8p, i32, i8p, i32}),
                    {s, nid, method, arg});
                return ctx.ir_gen->getInt32(0);
            };

            // ── decode_rpc(ev: rpc_event, target: <Model>) → i32
            // Decode the typed-RPC blob carried by `ev` into the caller-supplied
            // model variable. Returns 0 on success, -1 if ev.arg_kind != 1 or
            // schema mismatch. Heap fields inside `target` (strings/lists/maps)
            // are newly allocated by aiml_ser_decode and must be released by
            // the caller via the model lifetime (currently leaked — see Stufe 1).
            ci.builtinMethods["decode_rpc"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* /*base*/, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 2) return ctx.ir_gen->getInt32(-1);

                // Arg 0: rpc_event variable — get its alloca address as i8*.
                llvm::Value* evPtr = nullptr;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (sym) {
                        llvm::Value* av = sym->alloca_inst
                            ? static_cast<llvm::Value*>(sym->alloca_inst)
                            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                        evPtr = ctx.builder.CreateBitCast(av, i8p);
                    }
                }
                if (!evPtr) return ctx.ir_gen->getInt32(-1);

                // Arg 1: target model variable — alloca + type name.
                llvm::Value* outPtr = nullptr;
                std::string  outTn;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                    // `<` als standalone this-Referenz (analog visitVariable).
                    std::string vname = (vx->name == "<") ? std::string("this") : vx->name;
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vname)) { sym = &sc->variables[vname]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vname)) sym = &ctx.symbol_table[vname];
                    if (sym) {
                        llvm::Value* av = sym->alloca_inst
                            ? static_cast<llvm::Value*>(sym->alloca_inst)
                            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                        outPtr = ctx.builder.CreateBitCast(av, i8p);
                        outTn  = sym->typeName;
                        // ref<T>/ptr<T> sind heute funktional identisch
                        // (siehe spec/18_refs_and_pointers.md).  In beiden
                        // Fällen ist die alloca ein Pointer auf den Object-
                        // Pointer — wir müssen ihn dereferenzieren und den
                        // Class-Namen aus den Brackets extrahieren.
                        if ((outTn.rfind("ref<", 0) == 0 || outTn.rfind("ptr<", 0) == 0) &&
                            outTn.back() == '>') {
                            outTn = outTn.substr(4, outTn.size() - 5);
                            llvm::Value* ptrLoaded = ctx.builder.CreateLoad(i8p,
                                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                    sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst))),
                                "deref_obj");
                            outPtr = ctx.builder.CreateBitCast(ptrLoaded, i8p);
                        }
                    }
                }
                if (!outPtr || outTn.empty()
                    || !ctx.classes.count(outTn)
                    || !ctx.classes[outTn].serNodesVar)
                    return ctx.ir_gen->getInt32(-1);

                auto& ci2 = ctx.classes[outTn];
                // Zero-init: aiml_ser_decode requires `out` to be zero-initialised.
                // Critical for models with list/map/string members so the heap-field
                // pointers start null and the decode pass can populate them safely.
                llvm::DataLayout dl(&ctx.module);
                uint64_t outSz = dl.getTypeAllocSize(ci2.type);
                ctx.builder.CreateMemSet(
                    outPtr, ctx.builder.getInt8(0),
                    ctx.builder.getInt64(outSz),
                    llvm::MaybeAlign(1));

                llvm::Value* nodesPtr = ci2.serNodesVar;
                llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_rpc_event_decode_into",
                        i32, {i8p, i8p, i8p, i32}),
                    {evPtr, outPtr, nodesPtr, nNodes});
                return ctx.ir_gen->wrap(r);
            };

            // ── send_and_await(target_peer, method, req, out, timeout_ms) → i32
            //
            // One-shot RPC roundtrip: encode `req` (typed model), send to
            // `target_peer` with method name, then poll the incoming queue
            // for `timeout_ms` waiting for an RPC with method=="set_result";
            // on match decode into `out` (typed ptr<Model>) and return 1.
            // Return 0 on timeout / decode failure.
            //
            // Replaces the hand-rolled per-Type await-loop pattern that
            // client-side tests had to repeat — see 9rooms_v2 MockClient
            // refactor for the motivating use-case.
            //
            // Both `req` and `out` MUST be model-typed VariableExpr — the
            // builtin extracts schema-nodes from the registered class info.
            // `out` may be `ptr<Model>` or plain `Model` (ref/ptr identical
            // today — same convention as decode_rpc).
            ci.builtinMethods["send_and_await"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 5) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: expected (target_peer, method, req, out, timeout_ms) — got " +
                              std::to_string(call->args.size()) + " args");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* s        = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* targetId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* method   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));

                auto extractModelPtr = [&](AIML::AST::Expr* expr,
                                           llvm::Value*& outPtr,
                                           llvm::Value*& outNodesPtr,
                                           llvm::Value*& outNNodes,
                                           uint64_t& outSizeBytes,
                                           std::string& outTypeName,
                                           bool zeroInit) -> bool
                {
                    auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(expr);
                    if (!vx) return false;
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (!sym) return false;
                    std::string tn = sym->typeName;
                    bool isPtr = (tn.size() > 4 && tn.substr(0,4) == "ptr<" && tn.back() == '>');
                    bool isRef = (tn.size() > 4 && tn.substr(0,4) == "ref<" && tn.back() == '>');
                    if (isPtr || isRef) tn = tn.substr(4, tn.size() - 5);
                    if (!ctx.classes.count(tn) || !ctx.classes[tn].serNodesVar) return false;
                    auto& ci2 = ctx.classes[tn];

                    llvm::Value* av = sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                    if (isPtr || isRef) {
                        outPtr = ctx.builder.CreateLoad(i8p, av, "obj_deref");
                    } else {
                        outPtr = ctx.builder.CreateBitCast(av, i8p);
                    }
                    outNodesPtr = ci2.serNodesVar;
                    outNNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                    llvm::DataLayout dl(&ctx.module);
                    outSizeBytes = dl.getTypeAllocSize(ci2.type);
                    outTypeName  = tn;
                    if (zeroInit) {
                        ctx.builder.CreateMemSet(
                            outPtr, ctx.builder.getInt8(0),
                            ctx.builder.getInt64(outSizeBytes),
                            llvm::MaybeAlign(1));
                    }
                    return true;
                };

                llvm::Value *reqPtr=nullptr, *reqNodes=nullptr, *reqN=nullptr;
                llvm::Value *outPtr=nullptr, *outNodes=nullptr, *outN=nullptr;
                uint64_t reqSz=0, outSz=0;
                std::string reqTn, outTn;

                if (!extractModelPtr(call->args[2].get(), reqPtr, reqNodes, reqN, reqSz, reqTn, /*zeroInit=*/false)) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: 3rd arg must be a serializable model variable");
                    return ctx.ir_gen->getInt32(0);
                }
                if (!extractModelPtr(call->args[3].get(), outPtr, outNodes, outN, outSz, outTn, /*zeroInit=*/true)) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: 4th arg must be a serializable model variable (typically ptr<Model>)");
                    return ctx.ir_gen->getInt32(0);
                }

                llvm::Value* timeoutMs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[4].get())));

                // Filter-method is hard-coded to "set_result" — the dominant
                // pattern across all known callers.  Future signature could
                // accept a 6th arg if needed.
                llvm::Value* expectMethod = ctx.builder.CreateGlobalStringPtr("set_result");

                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_send_and_await",
                        i32, {i8p, i32, i8p, i32, i8p, i8p, i32, i8p, i8p, i8p, i32, i32}),
                    {s, targetId, method, ctx.builder.getInt32(0),
                     reqPtr, reqNodes, reqN,
                     expectMethod,
                     outPtr, outNodes, outN,
                     timeoutMs});
                return ctx.ir_gen->wrap(r);
            };

            // ── next_event() → mirror_event (struct loaded from heap ptr) ─────
            ci.builtinMethods["next_event"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_next_event", i8p, {i8p}), {s}, "ev_ptr");
                // Allocate local, memcpy struct from heap pointer.
                // visitSetStmt (isBuiltin=false, ptr val) will memcpy into target alloca.
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("mirror_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "ev_tmp");
                llvm::DataLayout dl(&ctx.module);
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── poll_event() → mirror_event (empty sentinel type=="" if queue empty) ──
            ci.builtinMethods["poll_event"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_poll_event", i8p, {i8p}), {s}, "poll_ev_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("mirror_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "poll_ev_tmp");
                llvm::DataLayout dl(&ctx.module);
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── poll_rpc() → rpc_event (empty sentinel method=="" if queue empty) ──
            ci.builtinMethods["poll_rpc"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_poll_rpc", i8p, {i8p}), {s}, "poll_rpc_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("rpc_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "poll_rpc_tmp");
                llvm::DataLayout dl(&ctx.module);
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── set_sync_rate(obj, hz: f32) ───────────────────────────────────
            ci.builtinMethods["set_sync_rate"] = [i8p, f32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* hzVal = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : llvm::ConstantFP::get(f32, 0.0);
                if (!hzVal->getType()->isFloatTy()) hzVal = ctx.builder.CreateFPCast(hzVal, f32);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_sync_rate", vd, {i8p, i8p, f32}),
                    {s, objPtr, hzVal});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── sync_now(obj) ─────────────────────────────────────────────────
            ci.builtinMethods["sync_now"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_sync_now", vd, {i8p, i8p}),
                    {s, objPtr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── set_sync_mode(obj, mode: string) ─────────────────────────────
            ci.builtinMethods["set_sync_mode"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* modeStr = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : ctx.builder.CreateGlobalStringPtr("auto");
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_sync_mode", vd, {i8p, i8p, i8p}),
                    {s, objPtr, modeStr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── set_rpc_target(obj, method: string, target: string) ───────────
            ci.builtinMethods["set_rpc_target"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* mStr = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : ctx.builder.CreateGlobalStringPtr("");
                llvm::Value* tStr = call->args.size() >= 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())))
                    : ctx.builder.CreateGlobalStringPtr("");
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_rpc_target", vd, {i8p, i8p, i8p, i8p}),
                    {s, objPtr, mStr, tStr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            ctx.classes["mirror_session"] = std::move(ci);
        }
#undef MIRROR_FIND_OBJ_PTR

        // ── mirror namespace (factory functions) ──────────────────────────────
        {
            auto& mir = ctx.namespaces["mirror"];

            mir.builtinFuncs["listen"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://0.0.0.0:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen", i8p, {i8p}), {url}));
            };
            mir.builtinFuncs["listen_max"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://0.0.0.0:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* mx = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : ctx.builder.getInt32(8);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen_max", i8p, {i8p, i32}), {url, mx}));
            };
            // mirror.listen_tls(url, cert, key) — wss:// host with a server cert
            mir.builtinFuncs["listen_tls"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("wss://0.0.0.0:9443")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* cert = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                llvm::Value* key = args.size() >= 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen_tls", i8p, {i8p, i8p, i8p}), {url, cert, key}));
            };
            // mirror.tls_insecure(on) — disable wss server-cert verification (dev only)
            mir.builtinFuncs["tls_insecure"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32t = ctx.builder.getInt32Ty();
                llvm::Value* on = args.empty() ? ctx.builder.getInt32(1)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ws_set_client_insecure", ctx.builder.getVoidTy(), {i32t}), {on});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };
            mir.builtinFuncs["connect"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://127.0.0.1:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_connect", i8p, {i8p}), {url}));
            };
            mir.builtinFuncs["connect_timeout"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://127.0.0.1:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* ms = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : ctx.builder.getInt32(2000);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_connect_timeout", i8p, {i8p, i32}), {url, ms}));
            };
        }

        (void)i64; (void)f32; (void)vd;
    }
    // ── end mirror ─────────────────────────────────────────────────────────────

    // -----------------------------------------------------------------------
    // log namespace (Spec 46)
    // -----------------------------------------------------------------------
    {
        auto& log = ctx.namespaces["log"];
        auto* p = llvm::PointerType::get(ctx.context, 0);
        auto* vd = ctx.builder.getVoidTy();

        /* Macro: register a log level function.
         * The handler expects 2 args: [msg, loc] — loc is injected by the compiler
         * (ast_compiler_call.cpp) before dispatch. If loc is missing, use "?".
         * NOTE: getOrInsert the LLVM function inside the handler (using the
         * handler's own ctx), NOT via a captured lambda from registration scope
         * — capturing the registration-time lambda by value still holds dangling
         * references to the registration-scope ctx/builder. */
#define REG_LOG(aimlName, cName) \
        log.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            auto* vd = ctx.builder.getVoidTy(); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) \
                fn = llvm::Function::Create( \
                    llvm::FunctionType::get(vd, {p, p}, false), \
                    llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* msg = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                                : ctx.builder.CreateGlobalStringPtr(""); \
            llvm::Value* loc = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                                : ctx.builder.CreateGlobalStringPtr("?"); \
            ctx.builder.CreateCall(fn, {msg, loc}); \
            return nullptr; \
        };

        REG_LOG("verbose",    "aiml_log_verbose")
        REG_LOG("debug",      "aiml_log_debug")
        REG_LOG("info",       "aiml_log_info")
        REG_LOG("warn",       "aiml_log_warn")
        REG_LOG("error",      "aiml_log_error")
        REG_LOG("fatal",      "aiml_log_fatal")
        REG_LOG("deprecated", "aiml_log_deprecated")
        REG_LOG("system",     "aiml_log_system")

#undef REG_LOG

        /* log.set_level(level_str) */
        log.builtinFuncs["set_level"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction("aiml_log_set_level");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {p}, false),
                llvm::Function::ExternalLinkage, "aiml_log_set_level", ctx.module);
            llvm::Value* lvl = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                                : ctx.builder.CreateGlobalStringPtr("info");
            ctx.builder.CreateCall(fn, {lvl});
            return nullptr;
        };

        /* log.set_file(path_str) */
        log.builtinFuncs["set_file"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction("aiml_log_set_file");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {p}, false),
                llvm::Function::ExternalLinkage, "aiml_log_set_file", ctx.module);
            llvm::Value* path = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                                 : llvm::Constant::getNullValue(p);
            ctx.builder.CreateCall(fn, {path});
            return nullptr;
        };
    } // end log namespace

    // ═══════════════════════════════════════════════════════════════════════
    // vocab namespace — i18n runtime.  Thin wrapper around
    // src/sys/vocab_runtime.cpp; see spec/44_8_vocab.md for the design.
    // Functions:
    //   vocab.load(langs_json, vocab_json) → i32   0=ok / -1 / -2
    //   vocab.set_language(idx)                    void
    //   vocab.current_language() → i32
    //   vocab.language_count() → i32
    //   vocab.language_code(idx) → string          nullable
    //   vocab.lookup(source) → string              always non-null
    // ═══════════════════════════════════════════════════════════════════════
    {
        auto& vocab = ctx.namespaces["vocab"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* vd  = ctx.builder.getVoidTy();
        auto* i32 = ctx.builder.getInt32Ty();

        // vocab.load(langs_json, vocab_json) → i32
        vocab.builtinFuncs["load"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8   = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_load");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {p8, p8}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_load", ctx.module);
            llvm::Value* langs = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.CreateGlobalStringPtr("{}");
            llvm::Value* voc = args.size() > 1
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.CreateGlobalStringPtr("{}");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {langs, voc}));
        };

        // vocab.set_language(idx)
        vocab.builtinFuncs["set_language"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* vdt  = ctx.builder.getVoidTy();
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_set_language");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(vdt, {i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_set_language", ctx.module);
            llvm::Value* idx = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.getInt32(0);
            if (idx->getType() != i32t) idx = ctx.builder.CreateZExtOrTrunc(idx, i32t);
            ctx.builder.CreateCall(fn, {idx});
            return nullptr;
        };

        // vocab.current_language() → i32
        vocab.builtinFuncs["current_language"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_current_language");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_current_language", ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
        };

        // vocab.language_count() → i32
        vocab.builtinFuncs["language_count"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_language_count");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_language_count", ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
        };

        // vocab.language_code(idx) → string (may be NULL)
        vocab.builtinFuncs["language_code"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8   = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_language_code");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p8, {i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_language_code", ctx.module);
            llvm::Value* idx = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.getInt32(0);
            if (idx->getType() != i32t) idx = ctx.builder.CreateZExtOrTrunc(idx, i32t);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {idx}));
        };

        // vocab.lookup(source) → string (never NULL — falls back to source)
        vocab.builtinFuncs["lookup"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8 = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction("aiml_vocab_lookup");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p8, {p8}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_lookup", ctx.module);
            llvm::Value* src = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {src}));
        };

        // Suppress "unused variable" warnings on the outer aliases when
        // the closures compile down to nothing referencing them.
        (void)p; (void)vd; (void)i32;
    } // end vocab namespace

    // ═══════════════════════════════════════════════════════════════════════
    // tensor builtin class (instance methods on tensor = i8*)
    // preLoadBase=true → baseAlloca is already the loaded i8* tensor handle
    // ═══════════════════════════════════════════════════════════════════════
    {
        using CI = CompilerContext::ClassInfo;
        CI ci;
        ci.name      = "tensor";
        ci.isBuiltin = true;
        ci.preLoadBase = true;

        // t.free()
        ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {tPtr});
            return nullptr;
        };

        // t.shape(dim) → i32
        ci.builtinMethods["shape"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_shape", i32, {i8p, i32});
            llvm::Value* dim = call->args.empty() ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, dim}));
        };

        // t.ndim() → i32
        ci.builtinMethods["ndim"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_ndim", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.size() → i32
        ci.builtinMethods["size"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_size", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.item() → f32
        ci.builtinMethods["item"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_item", ctx.builder.getFloatTy(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.get(idx) → f32
        ci.builtinMethods["get"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_get_flat", ctx.builder.getFloatTy(), {i8p, i32});
            llvm::Value* idx = call->args.empty() ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, idx}));
        };

        // t.set(idx, val)
        ci.builtinMethods["set"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_set_flat", ctx.builder.getVoidTy(), {i8p, i32, f32});
            if (call->args.size() < 2) return nullptr;
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(f, {tPtr, idx, val});
            return nullptr;
        };

        // t.fill(val)
        ci.builtinMethods["fill"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_fill", ctx.builder.getVoidTy(), {i8p, f32});
            if (call->args.empty()) return nullptr;
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, val});
            return nullptr;
        };

        // t.grad() → tensor (i8*)
        ci.builtinMethods["grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_grad", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.zero_grad()
        ci.builtinMethods["zero_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_zero_grad", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {tPtr});
            return nullptr;
        };

        // t.add_inplace(src)
        ci.builtinMethods["add_inplace"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_add_inplace", ctx.builder.getVoidTy(), {i8p, i8p});
            if (call->args.empty()) return nullptr;
            auto* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, src});
            return nullptr;
        };

        // t.scale_inplace(s)
        ci.builtinMethods["scale_inplace"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_scale_inplace", ctx.builder.getVoidTy(), {i8p, f32});
            if (call->args.empty()) return nullptr;
            auto* sVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (sVal->getType()->isIntegerTy())
                sVal = ctx.builder.CreateSIToFP(sVal, f32);
            ctx.builder.CreateCall(f, {tPtr, sVal});
            return nullptr;
        };

        // t.set_requires_grad(flag)
        ci.builtinMethods["set_requires_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_set_requires_grad", ctx.builder.getVoidTy(), {i8p, i32});
            llvm::Value* flag = call->args.empty() ? ctx.builder.getInt32(1)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, flag});
            return nullptr;
        };

        // t.requires_grad() → i32
        ci.builtinMethods["requires_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_requires_grad", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.print(label)
        ci.builtinMethods["print"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_print", ctx.builder.getVoidTy(), {i8p, i8p});
            llvm::Value* label = call->args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, label});
            return nullptr;
        };

        // t.clone() → tensor
        ci.builtinMethods["clone"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_clone", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.grad_view() → tensor  (shared data, eigener grad — kein memcpy)
        ci.builtinMethods["grad_view"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_grad_view", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.data_ptr() → ptr (float*)
        ci.builtinMethods["data_ptr"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_data_ptr", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        ctx.classes["tensor"] = std::move(ci);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // tensor namespace (static / factory functions)
    // ═══════════════════════════════════════════════════════════════════════
    {
        auto& tn = ctx.namespaces["tensor"];

        // ── Helper: build a stack [N x i32] from variadic i32 args ────────
        // Used by zeros, ones, randn, reshape — returns {ptr, ndim}.
        // `startIdx` = first arg index that is a shape dimension.
        auto buildShapeArray = [](CompilerContext& ctx, const std::vector<IRValue*>& args, size_t startIdx)
                -> std::pair<llvm::Value*, llvm::Value*> {
            auto* i32 = ctx.builder.getInt32Ty();
            int ndim = (int)args.size() - (int)startIdx;
            if (ndim <= 0) {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return {llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
                        ctx.builder.getInt32(0)};
            }
            llvm::ArrayType* arrTy = llvm::ArrayType::get(i32, ndim);
            llvm::Value* arrAlloca = ctx.createEntryAlloca(arrTy, "shape_arr");
            for (int i = 0; i < ndim; ++i) {
                llvm::Value* gep = ctx.builder.CreateConstInBoundsGEP2_32(arrTy, arrAlloca, 0, i);
                llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[startIdx + i]));
                ctx.builder.CreateStore(val, gep);
            }
            llvm::Value* ptr = ctx.builder.CreateConstInBoundsGEP2_32(arrTy, arrAlloca, 0, 0, "shape_ptr");
            return {ptr, ctx.builder.getInt32(ndim)};
        };

        // tensor.zeros(d0, d1, ...) → tensor
        tn.builtinFuncs["zeros"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_zeros", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim}));
        };

        // tensor.ones(d0, d1, ...) → tensor
        tn.builtinFuncs["ones"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_ones", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim}));
        };

        // tensor.randn(d0, d1, ...) → tensor  (mean=0, stddev=1)
        // tensor.randn(mean, stddev, d0, d1, ...) — if first two args are f32
        // For simplicity: always mean=0 stddev=1, all args are shape dims
        tn.builtinFuncs["randn"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_randn", i8p, {i8p, i32, f32, f32});
            llvm::Value* mean   = llvm::ConstantFP::get(f32, 0.0);
            llvm::Value* stddev = llvm::ConstantFP::get(f32, 1.0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim, mean, stddev}));
        };

        // tensor.randn_ex(mean, stddev, d0, d1, ...) → tensor
        tn.builtinFuncs["randn_ex"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* mean   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* stddev = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 2);
            llvm::Function* f = ctx.getFunction("aiml_tensor_randn", i8p, {i8p, i32, f32, f32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim, mean, stddev}));
        };

        // tensor.clone(t) → tensor
        tn.builtinFuncs["clone"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_clone", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.free(t)
        tn.builtinFuncs["free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        // ── Elementwise: (tensor, tensor) → tensor ───────────────────────
        #define REG_TT_TO_T(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p, i8p}); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))})); \
        }
        REG_TT_TO_T("add",    "aiml_tensor_add");
        REG_TT_TO_T("sub",    "aiml_tensor_sub");
        REG_TT_TO_T("mul",    "aiml_tensor_mul");
        REG_TT_TO_T("div",    "aiml_tensor_div");
        REG_TT_TO_T("matmul", "aiml_tensor_matmul");
        #undef REG_TT_TO_T

        // ── Unary: (tensor) → tensor ─────────────────────────────────────
        #define REG_T_TO_T(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p}); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))})); \
        }
        REG_T_TO_T("neg",  "aiml_tensor_neg");
        REG_T_TO_T("exp",  "aiml_tensor_exp");
        REG_T_TO_T("log",  "aiml_tensor_log_op");
        REG_T_TO_T("tanh", "aiml_tensor_tanh_op");
        REG_T_TO_T("relu", "aiml_tensor_relu");
        REG_T_TO_T("gelu", "aiml_tensor_gelu");
        REG_T_TO_T("abs",     "aiml_tensor_abs");
        REG_T_TO_T("sign",    "aiml_tensor_sign");
        REG_T_TO_T("step",    "aiml_tensor_step");
        #undef REG_T_TO_T

        // tensor.fma_inplace(target, src, scale) → void  (no allocation)
        #define REG_FMA_INPLACE(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            auto* voidTy = ctx.builder.getVoidTy(); \
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, voidTy, {i8p, i8p, f32}); \
            ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}); \
            return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
        };
        REG_FMA_INPLACE("fma_inplace",     "aiml_tensor_fma_inplace");
        REG_FMA_INPLACE("fma_sq_inplace",  "aiml_tensor_fma_sq_inplace");
        REG_FMA_INPLACE("fma_abs_inplace", "aiml_tensor_fma_abs_inplace");
        #undef REG_FMA_INPLACE

        // tensor.mul_scalar(t, s) → tensor
        tn.builtinFuncs["mul_scalar"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_mul_scalar", i8p, {i8p, f32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };

        // ── Reduction: (tensor, dim) → tensor ────────────────────────────
        #define REG_REDUCE(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p, i32}); \
            llvm::Value* dim = args.size() >= 2 \
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                : ctx.builder.getInt32(-1); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim})); \
        }
        REG_REDUCE("sum",  "aiml_tensor_sum");
        REG_REDUCE("mean", "aiml_tensor_mean");
        #undef REG_REDUCE

        // tensor.softmax(t, dim) → tensor
        tn.builtinFuncs["softmax"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_softmax", i8p, {i8p, i32});
            llvm::Value* dim = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim}));
        };

        // tensor.layer_norm(t, gamma, beta, eps) → tensor
        tn.builtinFuncs["layer_norm"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_layer_norm", i8p, {i8p, i8p, i8p, f32});
            llvm::Value* eps = args.size() >= 4
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                : llvm::ConstantFP::get(f32, 1e-5);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                eps}));
        };

        // tensor.reshape(t, d0, d1, ...) → tensor
        tn.builtinFuncs["reshape"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 1);
            llvm::Function* f = ctx.getFunction("aiml_tensor_reshape", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, shapePtr, ndim}));
        };

        // tensor.transpose(t, d0, d1) → tensor
        tn.builtinFuncs["transpose"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_transpose", i8p, {i8p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.slice(t, dim, start, end) → tensor
        tn.builtinFuncs["slice"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_slice", i8p, {i8p, i32, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))}));
        };

        // tensor.argmax_scalar(t) → i32
        tn.builtinFuncs["argmax_scalar"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_argmax_scalar", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.argmax(t, dim) → tensor
        tn.builtinFuncs["argmax"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_argmax", i8p, {i8p, i32});
            llvm::Value* dim = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim}));
        };

        // tensor.embedding_lookup(W, id_tensor, seq_len) → tensor
        // id_tensor = 1D float tensor with float-encoded token IDs
        tn.builtinFuncs["embedding_lookup"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_embedding_lookup", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.cross_entropy(logits, target_tensor, batch_size) → tensor
        tn.builtinFuncs["cross_entropy"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_cross_entropy", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.weighted_cross_entropy(logits, targets, weights, seq_len) → tensor
        tn.builtinFuncs["weighted_cross_entropy"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_weighted_cross_entropy", i8p, {i8p, i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))}));
        };

        // tensor.from_data(data_ptr, d0, d1, ...) → tensor
        // data_ptr = i8* (pointer to float array)
        tn.builtinFuncs["from_data"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* dataPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 1);
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_data", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, shapePtr, ndim}));
        };

        // tensor.from_int_list(list<i32>, count) → tensor
        // Bulk-converts int list to float tensor (replaces element-by-element loop)
        tn.builtinFuncs["from_int_list"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            // args[0] = list<i32> struct VALUE (loaded), args[1] = count
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* count = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            // list struct layout: {i32 size, i32 cap, i32* data}
            // ExtractValue index 2 = data pointer (i32*)
            auto* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list", i8p, {i32p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, count}));
        };

        // tensor.from_int_ptr(i8* data_ptr, count) → tensor
        // Like from_int_list but takes a raw pointer (e.g. array<i32:N>.data_ptr)
        tn.builtinFuncs["from_int_ptr"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* rawPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* count  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* dataPtr = ctx.builder.CreateBitCast(rawPtr, i32p);
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list", i8p, {i32p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, count}));
        };

        // tensor.from_int_list_batch(list<i32>, batch, seq) → tensor [B,S]
        tn.builtinFuncs["from_int_list_batch"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* batch   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* seq     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            auto* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list_batch", i8p, {i32p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, batch, seq}));
        };

        // ── Arena ────────────────────────────────────────────────────────
        tn.builtinFuncs["arena_init"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_arena_init", ctx.builder.getVoidTy(), {i32});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        tn.builtinFuncs["arena_free"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_arena_free", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        // tensor.kv_evict(store_name) — free tensor store from RAM, keep WAL
        tn.builtinFuncs["kv_evict"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_kv_evict", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        // ── Autograd control ─────────────────────────────────────────────
        tn.builtinFuncs["begin_graph"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_begin_graph", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["backward"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_backward", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        tn.builtinFuncs["thread_cleanup"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_thread_cleanup", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["no_grad_begin"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_no_grad_begin", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["no_grad_end"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_no_grad_end", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        // ── Adam optimizer ───────────────────────────────────────────────
        // tensor.adam_step(param, m, v, lr, beta1, beta2, eps, step)
        tn.builtinFuncs["adam_step"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 8) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_adam_step",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, f32, f32, f32, f32, i32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[7]))
            });
            return nullptr;
        };

        // tensor.adam_step_with_grad(param, m, v, grad, lr, beta1, beta2, eps, step)
        // PS variant: grad tensor is passed explicitly (not read from param->grad)
        tn.builtinFuncs["adam_step_with_grad"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 9) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_adam_step_with_grad",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p, f32, f32, f32, f32, i32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[7])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[8]))
            });
            return nullptr;
        };

        // tensor.print(t, label)
        tn.builtinFuncs["print"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_print", ctx.builder.getVoidTy(), {i8p, i8p});
            llvm::Value* label = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.CreateGlobalStringPtr("");
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), label});
            return nullptr;
        };

        // tensor.fill(t, val)
        tn.builtinFuncs["fill"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_fill", ctx.builder.getVoidTy(), {i8p, f32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            return nullptr;
        };

        // tensor.sha256(t) → string
        tn.builtinFuncs["sha256"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_sha256", i8p, {i8p});
            llvm::Value* t = args.empty()
                ? llvm::Constant::getNullValue(i8p)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {t}));
        };

        // tensor.add_inplace(dst, src)
        tn.builtinFuncs["add_inplace"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_add_inplace", ctx.builder.getVoidTy(), {i8p, i8p});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            return nullptr;
        };

        // tensor.scale_inplace(t, s)
        tn.builtinFuncs["scale_inplace"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_scale_inplace", ctx.builder.getVoidTy(), {i8p, f32});
            auto* sVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (sVal->getType()->isIntegerTy())
                sVal = ctx.builder.CreateSIToFP(sVal, f32);
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                sVal});
            return nullptr;
        };

        // tensor.l2_norm_sq(t) → f32 — sum of squared elements (for global grad norm clipping)
        tn.builtinFuncs["l2_norm_sq"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_l2_norm_sq", f32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.clip_scale(norm_sq, clip_norm) → f32 — computes gradient clipping scale factor
        tn.builtinFuncs["clip_scale"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_compute_clip_scale", f32, {f32, f32});
            auto* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (a0->getType()->isIntegerTy()) a0 = ctx.builder.CreateSIToFP(a0, f32);
            if (a1->getType()->isIntegerTy()) a1 = ctx.builder.CreateSIToFP(a1, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {a0, a1}));
        };

        // tensor.save(tensor, store_name, key) — persist tensor to AionDB Kv blob
        tn.builtinFuncs["save"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 3) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_save", ctx.builder.getVoidTy(), {i8p, i8p, i8p});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))});
            return nullptr;
        };

        // tensor.load(store_name, key) → tensor (or null if not found)
        tn.builtinFuncs["load"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_load", i8p, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };

        // tensor.bind(store_name, key, d0, d1, ...) → tensor (zero-copy DB binding)
        tn.builtinFuncs["bind"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            auto store_v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto key_v   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            // Shape args start at index 2
            std::vector<IRValue*> shape_args(args.begin() + 2, args.end());
            auto [shapePtr, ndim] = buildShapeArray(ctx, shape_args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_bind", i8p, {i8p, i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {store_v, key_v, shapePtr, ndim}));
        };

        // tensor.concat(a, b, dim) → tensor
        tn.builtinFuncs["concat"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_concat", i8p, {i8p, i8p, i32});
            llvm::Value* dim = args.size() >= 3
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), dim}));
        };
        // tensor.causal_mask(seq_len) → tensor [seq, seq]
        tn.builtinFuncs["causal_mask"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_causal_mask", i8p, {i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };
        // tensor.fused_mha(x, wq, wk, wv, wo, n_heads, embed_dim) → tensor
        tn.builtinFuncs["fused_mha"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 7) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_fused_mha", i8p,
                {i8p, i8p, i8p, i8p, i8p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6]))}));
        };
    } // end tensor namespace
}
