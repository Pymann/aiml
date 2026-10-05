// serialize_impl.cpp — yyjson-based serialization for AIML
// Format:
//   Primitives / Enums  → plain string ("42", "true", "hello")
//   Structs/Classes     → {"__type":"T","f1":v1,...}
//   list<T>             → {"__type":"list<T>","values":[...]}
//   map<string:V>       → {"__type":"map<string:V>","entries":{"k":v,...}}
//   Nested fields       → fully recursive (struct-in-struct, list-in-struct, etc.)
//   ptr<T>              → handled by caller (auto-deref on serialize, auto-malloc on deser)
//   any/chan/lock/...   → caller must call isSerializable() and emit a compiler error
#include "serialize_impl.h"
#include "string_utils.h"
#include "list_impl.h"
#include "map_impl.h"
#include <llvm/IR/IRBuilder.h>
#include <set>
#include <algorithm>

namespace SerializeImpl {

// ─── tiny type predicates ─────────────────────────────────────────────────────
static bool isListType(const std::string& t){ return t.rfind("list<",0)==0 && t.back()=='>'; }
static bool isMapType (const std::string& t){ return t.rfind("map<",0)==0  && t.back()=='>'; }
static bool isPtrType (const std::string& t){ return t.rfind("ptr<",0)==0  && t.back()=='>'; }
static bool isRefType (const std::string& t){ return t.rfind("ref<",0)==0  && t.back()=='>'; }

static std::string listElem(const std::string& t){ return t.substr(5, t.size()-6); }
static std::string mapValTn(const std::string& t){
    std::string inner = t.substr(4, t.size()-5);
    size_t sep = inner.find(':');
    return sep==std::string::npos ? "" : inner.substr(sep+1);
}
static std::string mapKeyTn(const std::string& t){
    std::string inner = t.substr(4, t.size()-5);
    size_t sep = inner.find(':');
    return sep==std::string::npos ? "" : inner.substr(0, sep);
}
static std::string ptrInner(const std::string& t){ return t.substr(4, t.size()-5); }
static std::string refInner(const std::string& t){ return t.substr(4, t.size()-5); }

static bool isPrimitive(const std::string& t){
    static const std::set<std::string> P{
        "i8","u8","i16","u16","i32","u32","i64","u64","f32","f64","bool","string"};
    return P.count(t)>0;
}

// sanitize typeName for use as LLVM function name suffix
static std::string sanitize(std::string s){
    for(char& c:s) if(c=='<'||c=='>'||c==':'||c==' ') c='_';
    return s;
}

// ─── LLVM type shorthands ─────────────────────────────────────────────────────
static llvm::Type* voidT(CompilerContext& c){ return llvm::Type::getVoidTy(c.context); }
static llvm::Type* i8T  (CompilerContext& c){ return llvm::Type::getInt8Ty(c.context); }
static llvm::Type* i32T (CompilerContext& c){ return llvm::Type::getInt32Ty(c.context); }
static llvm::Type* i64T (CompilerContext& c){ return llvm::Type::getInt64Ty(c.context); }
// Target-aware size_t: i64 on x86_64, i32 on wasm32. Use for libc-call args.
static llvm::Type* szT  (CompilerContext& c){ return c.getSizeType(); }
static llvm::Type* f32T (CompilerContext& c){ return llvm::Type::getFloatTy(c.context); }
static llvm::Type* f64T (CompilerContext& c){ return llvm::Type::getDoubleTy(c.context); }
static llvm::Type* ptrT (CompilerContext& c){ return llvm::PointerType::get(c.context,0); }
static llvm::Type* i8pT (CompilerContext& c){ return i8T(c)->getPointerTo(); }

// ─── lazy write-API declarations ──────────────────────────────────────────────
static llvm::Function* gJwNew    (CompilerContext& c){return c.getFunction("aiml_jw_new",    ptrT(c),{});}
static llvm::Function* gJwSetTyp (CompilerContext& c){return c.getFunction("aiml_jw_set_type",voidT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJwSetI32 (CompilerContext& c){return c.getFunction("aiml_jw_set_i32",voidT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJwSetI64 (CompilerContext& c){return c.getFunction("aiml_jw_set_i64",voidT(c),{ptrT(c),i8pT(c),i64T(c)});}
static llvm::Function* gJwSetF32 (CompilerContext& c){return c.getFunction("aiml_jw_set_f32",voidT(c),{ptrT(c),i8pT(c),f32T(c)});}
static llvm::Function* gJwSetF64 (CompilerContext& c){return c.getFunction("aiml_jw_set_f64",voidT(c),{ptrT(c),i8pT(c),f64T(c)});}
static llvm::Function* gJwSetBool(CompilerContext& c){return c.getFunction("aiml_jw_set_bool",voidT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJwSetStr (CompilerContext& c){return c.getFunction("aiml_jw_set_str",voidT(c),{ptrT(c),i8pT(c),i8pT(c)});}
static llvm::Function* gJwArrBgn (CompilerContext& c){return c.getFunction("aiml_jw_arr_begin",voidT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJwArrI32 (CompilerContext& c){return c.getFunction("aiml_jw_arr_i32",voidT(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJwArrI64 (CompilerContext& c){return c.getFunction("aiml_jw_arr_i64",voidT(c),{ptrT(c),i64T(c)});}
static llvm::Function* gJwArrF32 (CompilerContext& c){return c.getFunction("aiml_jw_arr_f32",voidT(c),{ptrT(c),f32T(c)});}
static llvm::Function* gJwArrF64 (CompilerContext& c){return c.getFunction("aiml_jw_arr_f64",voidT(c),{ptrT(c),f64T(c)});}
static llvm::Function* gJwArrBool(CompilerContext& c){return c.getFunction("aiml_jw_arr_bool",voidT(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJwArrStr (CompilerContext& c){return c.getFunction("aiml_jw_arr_str",voidT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJwArrEnd (CompilerContext& c){return c.getFunction("aiml_jw_arr_end",voidT(c),{ptrT(c)});}
static llvm::Function* gJwObjBgn (CompilerContext& c){return c.getFunction("aiml_jw_obj_begin",voidT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJwObjEnd (CompilerContext& c){return c.getFunction("aiml_jw_obj_end",voidT(c),{ptrT(c)});}
static llvm::Function* gJwMapI32 (CompilerContext& c){return c.getFunction("aiml_jw_map_i32",voidT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJwMapI64 (CompilerContext& c){return c.getFunction("aiml_jw_map_i64",voidT(c),{ptrT(c),i8pT(c),i64T(c)});}
static llvm::Function* gJwMapF32 (CompilerContext& c){return c.getFunction("aiml_jw_map_f32",voidT(c),{ptrT(c),i8pT(c),f32T(c)});}
static llvm::Function* gJwMapF64 (CompilerContext& c){return c.getFunction("aiml_jw_map_f64",voidT(c),{ptrT(c),i8pT(c),f64T(c)});}
static llvm::Function* gJwMapBool(CompilerContext& c){return c.getFunction("aiml_jw_map_bool",voidT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJwMapStr (CompilerContext& c){return c.getFunction("aiml_jw_map_str",voidT(c),{ptrT(c),i8pT(c),i8pT(c)});}
static llvm::Function* gJwDone   (CompilerContext& c){return c.getFunction("aiml_jw_done",i8pT(c),{ptrT(c)});}
static llvm::Function* gJsonStyleFmt(CompilerContext& c){return c.getFunction("aiml_json_style_format",i8pT(c),{i8pT(c),i32T(c),i8pT(c),i8pT(c)});}

// ─── lazy read-API declarations ───────────────────────────────────────────────
static llvm::Function* gJrParse    (CompilerContext& c){return c.getFunction("aiml_jr_parse",ptrT(c),{i8pT(c)});}
static llvm::Function* gJrSub      (CompilerContext& c){return c.getFunction("aiml_jr_sub",ptrT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrFree     (CompilerContext& c){return c.getFunction("aiml_jr_free",voidT(c),{ptrT(c)});}
static llvm::Function* gJrI32      (CompilerContext& c){return c.getFunction("aiml_jr_i32",i32T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrI64      (CompilerContext& c){return c.getFunction("aiml_jr_i64",i64T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrF32      (CompilerContext& c){return c.getFunction("aiml_jr_f32",f32T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrF64      (CompilerContext& c){return c.getFunction("aiml_jr_f64",f64T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrBool     (CompilerContext& c){return c.getFunction("aiml_jr_bool",i32T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrStr      (CompilerContext& c){return c.getFunction("aiml_jr_str",i8pT(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrArrLen   (CompilerContext& c){return c.getFunction("aiml_jr_arr_len",i32T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrArrI32   (CompilerContext& c){return c.getFunction("aiml_jr_arr_i32",i32T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrI64   (CompilerContext& c){return c.getFunction("aiml_jr_arr_i64",i64T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrF32   (CompilerContext& c){return c.getFunction("aiml_jr_arr_f32",f32T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrF64   (CompilerContext& c){return c.getFunction("aiml_jr_arr_f64",f64T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrBool  (CompilerContext& c){return c.getFunction("aiml_jr_arr_bool",i32T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrStr   (CompilerContext& c){return c.getFunction("aiml_jr_arr_str",i8pT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapCount (CompilerContext& c){return c.getFunction("aiml_jr_map_count",i32T(c),{ptrT(c),i8pT(c)});}
static llvm::Function* gJrMapKey   (CompilerContext& c){return c.getFunction("aiml_jr_map_key",i8pT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapValI32(CompilerContext& c){return c.getFunction("aiml_jr_map_val_i32",i32T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapValI64(CompilerContext& c){return c.getFunction("aiml_jr_map_val_i64",i64T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapValF32(CompilerContext& c){return c.getFunction("aiml_jr_map_val_f32",f32T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapValF64(CompilerContext& c){return c.getFunction("aiml_jr_map_val_f64",f64T(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrMapValStr(CompilerContext& c){return c.getFunction("aiml_jr_map_val_str",i8pT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJrArrSub   (CompilerContext& c){return c.getFunction("aiml_jr_arr_sub",ptrT(c),{ptrT(c),i8pT(c),i32T(c)});}
static llvm::Function* gJwArrObjBgn(CompilerContext& c){return c.getFunction("aiml_jw_arr_obj_begin",voidT(c),{ptrT(c)});}
static llvm::Function* gJwArrObjEnd(CompilerContext& c){return c.getFunction("aiml_jw_arr_obj_end",voidT(c),{ptrT(c)});}
static llvm::Function* gJwArrArrBgn(CompilerContext& c){return c.getFunction("aiml_jw_arr_arr_begin",voidT(c),{ptrT(c)});}
static llvm::Function* gJwArrArrEnd(CompilerContext& c){return c.getFunction("aiml_jw_arr_arr_end",voidT(c),{ptrT(c)});}
static llvm::Function* gJrValLen   (CompilerContext& c){return c.getFunction("aiml_jr_val_len",i32T(c),{ptrT(c)});}
static llvm::Function* gJrValI32   (CompilerContext& c){return c.getFunction("aiml_jr_val_i32",i32T(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValI64   (CompilerContext& c){return c.getFunction("aiml_jr_val_i64",i64T(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValF32   (CompilerContext& c){return c.getFunction("aiml_jr_val_f32",f32T(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValF64   (CompilerContext& c){return c.getFunction("aiml_jr_val_f64",f64T(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValBool  (CompilerContext& c){return c.getFunction("aiml_jr_val_bool",i32T(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValStr   (CompilerContext& c){return c.getFunction("aiml_jr_val_str",i8pT(c),{ptrT(c),i32T(c)});}
static llvm::Function* gJrValSub   (CompilerContext& c){return c.getFunction("aiml_jr_val_sub",ptrT(c),{ptrT(c),i32T(c)});}

// ─── sorted members helper ────────────────────────────────────────────────────
using MemberList = std::vector<std::pair<std::string, CompilerContext::StructMember>>;
static MemberList sortedMembers(CompilerContext& ctx, const std::string& typeName){
    MemberList r;
    if(!ctx.classes.count(typeName)) return r;
    for(auto& [n,m]:ctx.classes[typeName].members) r.emplace_back(n,m);
    std::sort(r.begin(),r.end(),[](auto& a,auto& b){return a.second.index<b.second.index;});
    return r;
}

// ─── forward declarations ─────────────────────────────────────────────────────
static void emitWriteField(CompilerContext& ctx, llvm::Value* writer,
                           llvm::Value* structPtr, llvm::StructType* structLLVMTy,
                           const std::string& fname, const CompilerContext::StructMember& fm);
static void emitWriteStructFields(CompilerContext& ctx, llvm::Value* writer,
                                  llvm::Value* structPtr, const std::string& typeName);
static llvm::Value* memv3LoadContainerHandleOrEmpty(CompilerContext& ctx, llvm::Value* fp,
                                                    llvm::Type* structTy);
static void emitWriteListElementsIntoWriter(CompilerContext& ctx, llvm::Value* writer,
                                             llvm::Value* dp, llvm::Value* sz,
                                             const std::string& elemTn,
                                             llvm::Function* parentFn,
                                             const std::string& lbl);
static void emitReadValListElements(CompilerContext& ctx, llvm::Value* subReader,
                                     llvm::Value* listAlloca, const std::string& elemTn,
                                     llvm::Function* parentFn, const std::string& lbl);
static void emitDeserStructFields(CompilerContext& ctx, llvm::Value* reader,
                                  llvm::Value* structPtr, const std::string& typeName,
                                  llvm::Function* parentFn);

// spec/61 §5.7: the stride of a list/map/buffer ELEMENT. A nested CONTAINER element
// (list<list<T>>, list<map<…>>, list<buffer>) is a refcounted HANDLE → pointer stride
// (8B), matching serFieldSubtree/serBuildSubtree (elem_size = pointer size) so deser,
// encode and free agree. A non-container element keeps its natural LLVM type stride.
static llvm::Type* listElemStrideTy(CompilerContext& ctx, const std::string& elemTn){
    if(ctx.memV3 && ctx.isMemV3ContainerHandle(elemTn)) return ptrT(ctx);
    return ctx.getLLVMType(elemTn);
}

// ─── emitWriteListElementsIntoWriter ─────────────────────────────────────────
// Writes dp[0..sz-1] of type elemTn into writer. Recursive: handles
// list<Struct>, list<list<T>>, and scalar elements at any nesting depth.
static void emitWriteListElementsIntoWriter(CompilerContext& ctx, llvm::Value* writer,
                                             llvm::Value* dp, llvm::Value* sz,
                                             const std::string& elemTn,
                                             llvm::Function* parentFn,
                                             const std::string& lbl)
{
    // AIML v2: a list<Model> whose element is a refcount-model stores %M* references (pointer
    // stride 8), NOT inline structs — walk it with pointer stride and deref each element, exactly
    // like list<ptr<T>>. Without this the GEP strides by sizeof(Model) and reads garbage → crash.
    bool elemIsRefModel = ctx.isRefcountModel(elemTn);
    // §5.7: a nested container element is a HANDLE pointer (ptr stride) — matches deser+free.
    bool elemIsHandle = ctx.memV3 && ctx.isMemV3ContainerHandle(elemTn);
    llvm::Type* elemTy = (elemIsRefModel || elemIsHandle) ? ptrT(ctx) : ctx.getLLVMType(elemTn);
    auto* ph = ctx.builder.GetInsertBlock();
    auto* lc = llvm::BasicBlock::Create(ctx.context, lbl+"_lc", parentFn);
    auto* lb = llvm::BasicBlock::Create(ctx.context, lbl+"_lb", parentFn);
    auto* le = llvm::BasicBlock::Create(ctx.context, lbl+"_le", parentFn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx), 2, lbl+"_i");
    idx->addIncoming(ctx.builder.getInt32(0), ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx, sz), lb, le);
    ctx.builder.SetInsertPoint(lb);
    llvm::Value* ep = ctx.builder.CreateGEP(elemTy, dp, idx, lbl+"_ep");

    if(elemIsRefModel && ctx.classes.count(elemTn) && ctx.classes[elemTn].type) {
        // v2: element slot holds a %M* → load it, null→{}, else serialize the pointee's fields.
        llvm::Value* rp = ctx.builder.CreateLoad(ptrT(ctx), ep, lbl+"_mrp");
        auto* okBB = llvm::BasicBlock::Create(ctx.context, lbl+"_mok", parentFn);
        auto* dnBB = llvm::BasicBlock::Create(ctx.context, lbl+"_mdn", parentFn);
        auto* emBB = llvm::BasicBlock::Create(ctx.context, lbl+"_mem", parentFn);
        ctx.builder.CreateCondBr(
            ctx.builder.CreateICmpNE(rp, llvm::Constant::getNullValue(ptrT(ctx))), okBB, emBB);
        ctx.builder.SetInsertPoint(okBB);
        ctx.builder.CreateCall(gJwArrObjBgn(ctx), {writer});
        emitWriteStructFields(ctx, writer, rp, elemTn);
        ctx.builder.CreateCall(gJwArrObjEnd(ctx), {writer});
        ctx.builder.CreateBr(dnBB);
        ctx.builder.SetInsertPoint(emBB);
        ctx.builder.CreateCall(gJwArrObjBgn(ctx), {writer});
        ctx.builder.CreateCall(gJwArrObjEnd(ctx), {writer});
        ctx.builder.CreateBr(dnBB);
        ctx.builder.SetInsertPoint(dnBB);
    } else if(ctx.classes.count(elemTn) && ctx.classes[elemTn].type) {
        ctx.builder.CreateCall(gJwArrObjBgn(ctx), {writer});
        emitWriteStructFields(ctx, writer, ep, elemTn);
        ctx.builder.CreateCall(gJwArrObjEnd(ctx), {writer});
    } else if(isRefType(elemTn) || isPtrType(elemTn)) {
        // list<ref<T>> / list<ptr<T>>: slot holds a pointer to a heap-allocated T.
        // Load the pointer, null-check, then inline-serialize T's fields.
        // Null elements become {} (empty object) — picked up as zero-struct on read.
        std::string innerTn = isRefType(elemTn) ? refInner(elemTn) : ptrInner(elemTn);
        if(ctx.classes.count(innerTn) && ctx.classes[innerTn].type){
            llvm::Value* rp = ctx.builder.CreateLoad(ptrT(ctx), ep, lbl+"_rp");
            auto* okBB = llvm::BasicBlock::Create(ctx.context, lbl+"_rok", parentFn);
            auto* dnBB = llvm::BasicBlock::Create(ctx.context, lbl+"_rdn", parentFn);
            auto* emBB = llvm::BasicBlock::Create(ctx.context, lbl+"_rem", parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(rp, llvm::Constant::getNullValue(ptrT(ctx))),
                okBB, emBB);
            ctx.builder.SetInsertPoint(okBB);
            ctx.builder.CreateCall(gJwArrObjBgn(ctx), {writer});
            emitWriteStructFields(ctx, writer, rp, innerTn);
            ctx.builder.CreateCall(gJwArrObjEnd(ctx), {writer});
            ctx.builder.CreateBr(dnBB);
            ctx.builder.SetInsertPoint(emBB);
            ctx.builder.CreateCall(gJwArrObjBgn(ctx), {writer});
            ctx.builder.CreateCall(gJwArrObjEnd(ctx), {writer});
            ctx.builder.CreateBr(dnBB);
            ctx.builder.SetInsertPoint(dnBB);
        }
    } else if(isListType(elemTn)) {
        llvm::Type* innerListTy = ctx.getLLVMType(elemTn);
        std::string innerElemTn = listElem(elemTn);
        // spec/61 §5.7: a nested list element is a refcounted HANDLE (ep is a pointer slot, ptr stride
        // above). Load the handle — which points AT the {size,cap,data} struct — then read size/data
        // from it. Deser (aiml_list_handle_new) and free (LIST_PTR) use the same handle layout. A null
        // handle (never produced today, but defensive) emits an empty inner array.
        llvm::Value* hbase = ep;
        if(elemIsHandle){
            hbase = ctx.builder.CreateLoad(ptrT(ctx), ep, "inner_conh");
            auto* okBB = llvm::BasicBlock::Create(ctx.context, lbl+"_ihok", parentFn);
            auto* emBB = llvm::BasicBlock::Create(ctx.context, lbl+"_ihem", parentFn);
            auto* dnBB = llvm::BasicBlock::Create(ctx.context, lbl+"_ihdn", parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(hbase, llvm::Constant::getNullValue(ptrT(ctx))), okBB, emBB);
            ctx.builder.SetInsertPoint(okBB);
            llvm::Value* isg = ctx.builder.CreateStructGEP(innerListTy, hbase, 0, "isg");
            llvm::Value* isz = ctx.builder.CreateLoad(i32T(ctx), isg, "isz");
            llvm::Value* idg = ctx.builder.CreateStructGEP(innerListTy, hbase, 2, "idg");
            llvm::Value* idp = ctx.builder.CreateLoad(ptrT(ctx), idg, "idp");
            ctx.builder.CreateCall(gJwArrArrBgn(ctx), {writer});
            emitWriteListElementsIntoWriter(ctx, writer, idp, isz, innerElemTn, parentFn, lbl+"i");
            ctx.builder.CreateCall(gJwArrArrEnd(ctx), {writer});
            ctx.builder.CreateBr(dnBB);
            ctx.builder.SetInsertPoint(emBB);
            ctx.builder.CreateCall(gJwArrArrBgn(ctx), {writer});
            ctx.builder.CreateCall(gJwArrArrEnd(ctx), {writer});
            ctx.builder.CreateBr(dnBB);
            ctx.builder.SetInsertPoint(dnBB);
        } else {
            llvm::Value* isg = ctx.builder.CreateStructGEP(innerListTy, hbase, 0, "isg");
            llvm::Value* isz = ctx.builder.CreateLoad(i32T(ctx), isg, "isz");
            llvm::Value* idg = ctx.builder.CreateStructGEP(innerListTy, hbase, 2, "idg");
            llvm::Value* idp = ctx.builder.CreateLoad(ptrT(ctx), idg, "idp");
            ctx.builder.CreateCall(gJwArrArrBgn(ctx), {writer});
            emitWriteListElementsIntoWriter(ctx, writer, idp, isz, innerElemTn, parentFn, lbl+"i");
            ctx.builder.CreateCall(gJwArrArrEnd(ctx), {writer});
        }
    } else if(elemTn=="string") {
        ctx.builder.CreateCall(gJwArrStr(ctx),
            {writer, ctx.builder.CreateLoad(i8pT(ctx), ep, "sv")});
    } else if(elemTn=="i64"||elemTn=="u64") {
        ctx.builder.CreateCall(gJwArrI64(ctx),
            {writer, ctx.builder.CreateLoad(i64T(ctx), ep, "ev")});
    } else if(elemTn=="f32") {
        ctx.builder.CreateCall(gJwArrF32(ctx),
            {writer, ctx.builder.CreateLoad(f32T(ctx), ep, "ev")});
    } else if(elemTn=="f64") {
        ctx.builder.CreateCall(gJwArrF64(ctx),
            {writer, ctx.builder.CreateLoad(f64T(ctx), ep, "ev")});
    } else if(elemTn=="bool") {
        llvm::Value* ev = ctx.builder.CreateLoad(i8T(ctx), ep, "ev");
        ctx.builder.CreateCall(gJwArrBool(ctx),
            {writer, ctx.builder.CreateZExt(ev, i32T(ctx))});
    } else {
        ctx.builder.CreateCall(gJwArrI32(ctx),
            {writer, ctx.builder.CreateLoad(i32T(ctx), ep, "ev")});
    }
    llvm::Value* ni = ctx.builder.CreateAdd(idx, ctx.builder.getInt32(1), "ni");
    idx->addIncoming(ni, ctx.builder.GetInsertBlock());
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
}

// ─── inline struct-field writer ───────────────────────────────────────────────
// Emits IR (into current insert point) that writes all fields of typeName
// from structPtr into the already-open yyjson writer object.
static void emitWriteStructFields(CompilerContext& ctx, llvm::Value* writer,
                                  llvm::Value* structPtr, const std::string& typeName){
    if(!ctx.classes.count(typeName)) return;
    llvm::StructType* st = ctx.classes[typeName].type;
    if(!st) return;
    for(auto& [fn,fm]: sortedMembers(ctx, typeName))
        emitWriteField(ctx, writer, structPtr, st, fn, fm);
}

// spec/61 §5 (memV3): a container FIELD holds a handle POINTER. Load it so callers can GEP the
// {size,cap,data}/map struct. A null handle (a bare `new Model` never initialised the field) is
// replaced by a pointer into a zeroed region so the size/data reads yield 0/null (empty), never a
// load from null. `fp` is &field (the pointer slot); returns a pointer to the live struct.
static llvm::Value* memv3LoadContainerHandleOrEmpty(CompilerContext& ctx, llvm::Value* fp,
                                                    llvm::Type* /*structTy*/){
    auto* ptr = ptrT(ctx);
    llvm::Value* h = ctx.builder.CreateLoad(ptr, fp, "conh");
    const char* gname = "__aiml_container_zero64";
    llvm::GlobalVariable* zg = ctx.module.getGlobalVariable(gname);
    if(!zg){
        llvm::ArrayType* at = llvm::ArrayType::get(i8T(ctx), 64);
        zg = new llvm::GlobalVariable(ctx.module, at, /*const=*/true,
            llvm::GlobalValue::PrivateLinkage, llvm::ConstantAggregateZero::get(at), gname);
    }
    llvm::Value* isn = ctx.builder.CreateICmpEQ(h, llvm::Constant::getNullValue(ptr), "conh_null");
    return ctx.builder.CreateSelect(isn, static_cast<llvm::Value*>(zg), h, "conh_safe");
}

static void emitWriteField(CompilerContext& ctx, llvm::Value* writer,
                           llvm::Value* structPtr, llvm::StructType* structLLVMTy,
                           const std::string& fname,
                           const CompilerContext::StructMember& fm){
    llvm::Value* fkey = ctx.builder.CreateGlobalStringPtr(fname, "fk_"+fname);
    std::string tn = fm.typeName;

    // ── list field ────────────────────────────────────────────────────────────
    if(isListType(tn)){
        std::string elemTn = listElem(tn);
        llvm::Type* listTy = ctx.getLLVMType(tn);
        llvm::Value* fp = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"lfp");
        // spec/61 §5 (memV3): a list FIELD is a handle POINTER, not an inline {size,cap,data} struct.
        // Load the handle to reach the struct — else the handle-pointer bytes are misread as
        // size/data → garbage length → the element loop walks arbitrary memory (nondeterministic UAF
        // in serialize of any model with a list field, test_serialize_struct). A null handle (never
        // produced by decode/decl, but possible for a bare `new Model`) → substitute an empty list.
        if(ctx.memV3 && ctx.isMemV3ContainerHandle(tn))
            fp = memv3LoadContainerHandleOrEmpty(ctx, fp, listTy);
        llvm::Value* sg = ctx.builder.CreateStructGEP(listTy,fp,0,"lsg");
        llvm::Value* sz = ctx.builder.CreateLoad(i32T(ctx),sg,"lsz");
        llvm::Value* dg = ctx.builder.CreateStructGEP(listTy,fp,2,"ldg");
        llvm::Value* dp = ctx.builder.CreateLoad(ptrT(ctx),dg,"ldp");
        ctx.builder.CreateCall(gJwArrBgn(ctx),{writer,fkey});
        llvm::Function* pfn = ctx.builder.GetInsertBlock()->getParent();
        emitWriteListElementsIntoWriter(ctx,writer,dp,sz,elemTn,pfn,fname+"_el");
        ctx.builder.CreateCall(gJwArrEnd(ctx),{writer});
        return;
    }

    // ── map field ─────────────────────────────────────────────────────────────
    if(isMapType(tn)){
        std::string keyTn = mapKeyTn(tn);
        std::string valTn = mapValTn(tn);
        bool intKey = (keyTn != "string");  // enum/integer key: stored as i32 in key array
        llvm::Type* mapTy = ctx.getLLVMType(tn);
        llvm::Value* fp   = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"mfp");
        // memV3: map FIELD is a handle pointer (see the list case) → load before GEP.
        if(ctx.memV3 && ctx.isMemV3ContainerHandle(tn))
            fp = memv3LoadContainerHandleOrEmpty(ctx, fp, mapTy);
        llvm::Value* cg   = ctx.builder.CreateStructGEP(mapTy,fp,1,"mcg");
        llvm::Value* cap  = ctx.builder.CreateLoad(i32T(ctx),cg,"mcap");
        llvm::Value* og   = ctx.builder.CreateStructGEP(mapTy,fp,2,"mog");
        llvm::Value* occ  = ctx.builder.CreateLoad(ptrT(ctx),og,"mocc");
        llvm::Value* kg   = ctx.builder.CreateStructGEP(mapTy,fp,3,"mkg");
        llvm::Value* kp   = ctx.builder.CreateLoad(ptrT(ctx),kg,"mkeys");
        llvm::Value* vg   = ctx.builder.CreateStructGEP(mapTy,fp,4,"mvg");
        llvm::Value* vp   = ctx.builder.CreateLoad(ptrT(ctx),vg,"mvals");
        ctx.builder.CreateCall(gJwObjBgn(ctx),{writer,fkey});

        llvm::Function* pfn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* ph  = ctx.builder.GetInsertBlock();
        llvm::BasicBlock* lc  = llvm::BasicBlock::Create(ctx.context,fname+"_mlc",pfn);
        llvm::BasicBlock* lb  = llvm::BasicBlock::Create(ctx.context,fname+"_mlb",pfn);
        llvm::BasicBlock* we  = llvm::BasicBlock::Create(ctx.context,fname+"_mwe",pfn);
        llvm::BasicBlock* se  = llvm::BasicBlock::Create(ctx.context,fname+"_mse",pfn);
        llvm::BasicBlock* le  = llvm::BasicBlock::Create(ctx.context,fname+"_mle",pfn);
        ctx.builder.CreateBr(lc);
        ctx.builder.SetInsertPoint(lc);
        llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"mli");
        idx->addIncoming(ctx.builder.getInt32(0),ph);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cap),lb,le);
        ctx.builder.SetInsertPoint(lb);
        llvm::Value* oe  = ctx.builder.CreateGEP(i8T(ctx),occ,idx,"oe");
        llvm::Value* occ2= ctx.builder.CreateLoad(i8T(ctx),oe,"oc2");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpNE(occ2,ctx.builder.getInt8(0)),we,se);
        ctx.builder.SetInsertPoint(we);
        llvm::Value* ks;
        if(intKey){
            // enum/integer key: key array stores i32 values → convert to string for JSON
            llvm::Value* kep = ctx.builder.CreateGEP(i32T(ctx),kp,idx,"kep");
            llvm::Value* kv  = ctx.builder.CreateLoad(i32T(ctx),kep,"kv");
            ks = StringUtils::intToString(ctx, kv);
        } else {
            llvm::Value* kep = ctx.builder.CreateGEP(i8pT(ctx),kp,idx,"kep");
            ks = ctx.builder.CreateLoad(i8pT(ctx),kep,"ks");
        }
        if(valTn=="string"){
            llvm::Value* vep=ctx.builder.CreateGEP(i8pT(ctx),vp,idx,"vep");
            llvm::Value* vs =ctx.builder.CreateLoad(i8pT(ctx),vep,"vs");
            ctx.builder.CreateCall(gJwMapStr(ctx),{writer,ks,vs});
        } else if(valTn=="i64"||valTn=="u64"){
            llvm::Value* vep=ctx.builder.CreateGEP(i64T(ctx),vp,idx,"vep");
            llvm::Value* vv =ctx.builder.CreateLoad(i64T(ctx),vep,"vv");
            ctx.builder.CreateCall(gJwMapI64(ctx),{writer,ks,vv});
        } else if(valTn=="f32"){
            llvm::Value* vep=ctx.builder.CreateGEP(f32T(ctx),vp,idx,"vep");
            llvm::Value* vv =ctx.builder.CreateLoad(f32T(ctx),vep,"vv");
            ctx.builder.CreateCall(gJwMapF32(ctx),{writer,ks,vv});
        } else if(valTn=="f64"){
            llvm::Value* vep=ctx.builder.CreateGEP(f64T(ctx),vp,idx,"vep");
            llvm::Value* vv =ctx.builder.CreateLoad(f64T(ctx),vep,"vv");
            ctx.builder.CreateCall(gJwMapF64(ctx),{writer,ks,vv});
        } else {
            llvm::Value* vep=ctx.builder.CreateGEP(i32T(ctx),vp,idx,"vep");
            llvm::Value* vv =ctx.builder.CreateLoad(i32T(ctx),vep,"vv");
            ctx.builder.CreateCall(gJwMapI32(ctx),{writer,ks,vv});
        }
        ctx.builder.CreateBr(se);
        ctx.builder.SetInsertPoint(se);
        llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"mni");
        idx->addIncoming(ni,se);
        ctx.builder.CreateBr(lc);
        ctx.builder.SetInsertPoint(le);
        ctx.builder.CreateCall(gJwObjEnd(ctx),{writer});
        return;
    }

    // ── nested struct field ───────────────────────────────────────────────────
    if(ctx.classes.count(tn) && ctx.classes[tn].type){
        llvm::Value* fp = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"sfp");
        ctx.builder.CreateCall(gJwObjBgn(ctx),{writer,fkey});
        emitWriteStructFields(ctx,writer,fp,tn);
        ctx.builder.CreateCall(gJwObjEnd(ctx),{writer});
        return;
    }

    // ── scalar field ──────────────────────────────────────────────────────────
    llvm::Value* fp = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"fp");
    if(tn=="string"||tn=="path"){   // spec/42: a path serialises as its text
        llvm::Value* v=ctx.builder.CreateLoad(i8pT(ctx),fp,"sv");
        ctx.builder.CreateCall(gJwSetStr(ctx),{writer,fkey,v});
    } else if(tn=="i64"||tn=="u64"){
        llvm::Value* v=ctx.builder.CreateLoad(i64T(ctx),fp,"v");
        ctx.builder.CreateCall(gJwSetI64(ctx),{writer,fkey,v});
    } else if(tn=="f32"){
        llvm::Value* v=ctx.builder.CreateLoad(f32T(ctx),fp,"v");
        ctx.builder.CreateCall(gJwSetF32(ctx),{writer,fkey,v});
    } else if(tn=="f64"){
        llvm::Value* v=ctx.builder.CreateLoad(f64T(ctx),fp,"v");
        ctx.builder.CreateCall(gJwSetF64(ctx),{writer,fkey,v});
    } else if(tn=="bool"){
        llvm::Value* v=ctx.builder.CreateLoad(i8T(ctx),fp,"v");
        llvm::Value* v32=ctx.builder.CreateZExt(v,i32T(ctx));
        ctx.builder.CreateCall(gJwSetBool(ctx),{writer,fkey,v32});
    } else {
        // i32/u32/i16/u16/i8/u8/enum → cast to i32
        llvm::Type* fty = fm.type ? fm.type : i32T(ctx);
        llvm::Value* v=ctx.builder.CreateLoad(fty,fp,"v");
        if(!v->getType()->isIntegerTy(32))
            v=ctx.builder.CreateIntCast(v,i32T(ctx),true,"vc");
        ctx.builder.CreateCall(gJwSetI32(ctx),{writer,fkey,v});
    }
}

// ─── emitSerializeStruct ──────────────────────────────────────────────────────
// Emits IR function: __aiml_ser_TYPENAME(ptr structPtr) -> ptr (i8* JSON string)
static llvm::Function* emitSerializeStruct(CompilerContext& ctx,
                                           const std::string& typeName){
    std::string fnName = "__aiml_ser_"+typeName;
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    if(!ctx.classes.count(typeName)) return nullptr;
    llvm::StructType* st = ctx.classes[typeName].type;
    if(!st) return nullptr;

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(i8pT(ctx),{ptrT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("sp");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* writer = ctx.builder.CreateCall(gJwNew(ctx),{},"w");
    ctx.builder.CreateCall(gJwSetTyp(ctx),{writer,ctx.builder.CreateGlobalStringPtr(typeName,"tn")});
    emitWriteStructFields(ctx,writer,fn->getArg(0),typeName);
    llvm::Value* result = ctx.builder.CreateCall(gJwDone(ctx),{writer},"json");
    ctx.builder.CreateRet(result);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── emitSerializeList ────────────────────────────────────────────────────────
static llvm::Function* emitSerializeList(CompilerContext& ctx,
                                         const std::string& listTn){
    std::string fnName = "__aiml_ser_"+sanitize(listTn);
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    std::string elemTn = listElem(listTn);
    llvm::Type* elemTy = ctx.getLLVMType(elemTn);
    llvm::Type* listTy = ctx.getLLVMType(listTn);

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(i8pT(ctx),{ptrT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("lp");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* writer = ctx.builder.CreateCall(gJwNew(ctx),{},"w");
    ctx.builder.CreateCall(gJwSetTyp(ctx),{writer,ctx.builder.CreateGlobalStringPtr(listTn,"tn")});

    llvm::Value* sg  = ctx.builder.CreateStructGEP(listTy,fn->getArg(0),0,"sg");
    llvm::Value* sz  = ctx.builder.CreateLoad(i32T(ctx),sg,"sz");
    llvm::Value* dg  = ctx.builder.CreateStructGEP(listTy,fn->getArg(0),2,"dg");
    llvm::Value* dp  = ctx.builder.CreateLoad(ptrT(ctx),dg,"dp");
    llvm::Value* vk  = ctx.builder.CreateGlobalStringPtr("values","vk");
    ctx.builder.CreateCall(gJwArrBgn(ctx),{writer,vk});
    emitWriteListElementsIntoWriter(ctx, writer, dp, sz, elemTn, fn, "sl_el");
    ctx.builder.CreateCall(gJwArrEnd(ctx),{writer});
    llvm::Value* result=ctx.builder.CreateCall(gJwDone(ctx),{writer},"json");
    ctx.builder.CreateRet(result);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── emitSerializeMap ─────────────────────────────────────────────────────────
static llvm::Function* emitSerializeMap(CompilerContext& ctx,
                                        const std::string& mapTn){
    std::string fnName = "__aiml_ser_"+sanitize(mapTn);
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    std::string keyTn = mapKeyTn(mapTn);
    std::string valTn = mapValTn(mapTn);
    bool intKey = (keyTn != "string");
    llvm::Type* mapTy = ctx.getLLVMType(mapTn);

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(i8pT(ctx),{ptrT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("mp");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* writer = ctx.builder.CreateCall(gJwNew(ctx),{},"w");
    ctx.builder.CreateCall(gJwSetTyp(ctx),{writer,ctx.builder.CreateGlobalStringPtr(mapTn,"tn")});

    llvm::Value* cg  = ctx.builder.CreateStructGEP(mapTy,fn->getArg(0),1,"cg");
    llvm::Value* cap = ctx.builder.CreateLoad(i32T(ctx),cg,"cap");
    llvm::Value* og  = ctx.builder.CreateStructGEP(mapTy,fn->getArg(0),2,"og");
    llvm::Value* occ = ctx.builder.CreateLoad(ptrT(ctx),og,"occ");
    llvm::Value* kg  = ctx.builder.CreateStructGEP(mapTy,fn->getArg(0),3,"kg");
    llvm::Value* kp  = ctx.builder.CreateLoad(ptrT(ctx),kg,"kp");
    llvm::Value* vg  = ctx.builder.CreateStructGEP(mapTy,fn->getArg(0),4,"vg");
    llvm::Value* vp  = ctx.builder.CreateLoad(ptrT(ctx),vg,"vp");
    llvm::Value* ek  = ctx.builder.CreateGlobalStringPtr("entries","ek");
    ctx.builder.CreateCall(gJwObjBgn(ctx),{writer,ek});

    llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
    llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,"lc",fn);
    llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,"lb",fn);
    llvm::BasicBlock* we = llvm::BasicBlock::Create(ctx.context,"we",fn);
    llvm::BasicBlock* se = llvm::BasicBlock::Create(ctx.context,"se",fn);
    llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,"le",fn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"i");
    idx->addIncoming(ctx.builder.getInt32(0),ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cap),lb,le);
    ctx.builder.SetInsertPoint(lb);
    llvm::Value* oe  = ctx.builder.CreateGEP(i8T(ctx),occ,idx,"oe");
    llvm::Value* ov  = ctx.builder.CreateLoad(i8T(ctx),oe,"ov");
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpNE(ov,ctx.builder.getInt8(0)),we,se);
    ctx.builder.SetInsertPoint(we);
    llvm::Value* ks;
    if(intKey){
        llvm::Value* kep = ctx.builder.CreateGEP(i32T(ctx),kp,idx,"kep");
        llvm::Value* kv  = ctx.builder.CreateLoad(i32T(ctx),kep,"kv");
        ks = StringUtils::intToString(ctx, kv);
    } else {
        llvm::Value* kep = ctx.builder.CreateGEP(i8pT(ctx),kp,idx,"kep");
        ks = ctx.builder.CreateLoad(i8pT(ctx),kep,"ks");
    }
    if(valTn=="string"){
        llvm::Value* vep=ctx.builder.CreateGEP(i8pT(ctx),vp,idx,"vep");
        ctx.builder.CreateCall(gJwMapStr(ctx),{writer,ks,ctx.builder.CreateLoad(i8pT(ctx),vep,"vs")});
    } else if(valTn=="i64"||valTn=="u64"){
        llvm::Value* vep=ctx.builder.CreateGEP(i64T(ctx),vp,idx,"vep");
        ctx.builder.CreateCall(gJwMapI64(ctx),{writer,ks,ctx.builder.CreateLoad(i64T(ctx),vep,"vv")});
    } else if(valTn=="f32"){
        llvm::Value* vep=ctx.builder.CreateGEP(f32T(ctx),vp,idx,"vep");
        ctx.builder.CreateCall(gJwMapF32(ctx),{writer,ks,ctx.builder.CreateLoad(f32T(ctx),vep,"vv")});
    } else if(valTn=="f64"){
        llvm::Value* vep=ctx.builder.CreateGEP(f64T(ctx),vp,idx,"vep");
        ctx.builder.CreateCall(gJwMapF64(ctx),{writer,ks,ctx.builder.CreateLoad(f64T(ctx),vep,"vv")});
    } else {
        llvm::Value* vep=ctx.builder.CreateGEP(i32T(ctx),vp,idx,"vep");
        ctx.builder.CreateCall(gJwMapI32(ctx),{writer,ks,ctx.builder.CreateLoad(i32T(ctx),vep,"vv")});
    }
    ctx.builder.CreateBr(se);
    ctx.builder.SetInsertPoint(se);
    llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
    idx->addIncoming(ni,se);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
    ctx.builder.CreateCall(gJwObjEnd(ctx),{writer});
    llvm::Value* result=ctx.builder.CreateCall(gJwDone(ctx),{writer},"json");
    ctx.builder.CreateRet(result);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── helper: read scalar from reader into ptr ───────────────────────────────
static void emitReadScalar(CompilerContext& ctx, llvm::Value* reader, llvm::Value* fkey,
                           llvm::Value* dst, const std::string& tn, llvm::Type* fmType){
    llvm::Value* v;
    if(tn=="string")        v=ctx.builder.CreateCall(gJrStr(ctx),{reader,fkey},"sv");
    else if(tn=="path"){    // spec/42: read the text, store the interned path, drop the reader's dup
        llvm::Value* text=ctx.builder.CreateCall(gJrStr(ctx),{reader,fkey},"ptext");
        v=ctx.builder.CreateCall(ctx.getFunction("aiml_path_intern",i8pT(ctx),{i8pT(ctx)}),{text},"pv");
        ctx.builder.CreateCall(ctx.getFunction("aiml_str_decref",ctx.builder.getVoidTy(),{i8pT(ctx)}),{text});
    }
    else if(tn=="i64"||tn=="u64") v=ctx.builder.CreateCall(gJrI64(ctx),{reader,fkey},"sv");
    else if(tn=="f32")      v=ctx.builder.CreateCall(gJrF32(ctx),{reader,fkey},"sv");
    else if(tn=="f64")      v=ctx.builder.CreateCall(gJrF64(ctx),{reader,fkey},"sv");
    else if(tn=="bool"){
        llvm::Value* bv=ctx.builder.CreateCall(gJrBool(ctx),{reader,fkey},"bv");
        v=ctx.builder.CreateTrunc(bv,i8T(ctx));
    } else {
        v=ctx.builder.CreateCall(gJrI32(ctx),{reader,fkey},"sv");
        if(fmType && !fmType->isIntegerTy(32))
            v=ctx.builder.CreateIntCast(v,fmType,true,"sc");
    }
    ctx.builder.CreateStore(v,dst);
}

// ─── helper: read array element of primitive type via val_* (keyless) ────────
static llvm::Value* emitReadValElem(CompilerContext& ctx, llvm::Value* reader,
                                    llvm::Value* idx, const std::string& elemTn){
    if(elemTn=="string")        return ctx.builder.CreateCall(gJrValStr(ctx),{reader,idx},"ve");
    if(elemTn=="i64"||elemTn=="u64") return ctx.builder.CreateCall(gJrValI64(ctx),{reader,idx},"ve");
    if(elemTn=="f32")           return ctx.builder.CreateCall(gJrValF32(ctx),{reader,idx},"ve");
    if(elemTn=="f64")           return ctx.builder.CreateCall(gJrValF64(ctx),{reader,idx},"ve");
    if(elemTn=="bool"){
        llvm::Value* bv=ctx.builder.CreateCall(gJrValBool(ctx),{reader,idx},"bv");
        return ctx.builder.CreateTrunc(bv,i8T(ctx));
    }
    return ctx.builder.CreateCall(gJrValI32(ctx),{reader,idx},"ve");
}

// ─── helper: read array element of primitive type via arr_* (keyed) ──────────
static llvm::Value* emitReadArrElem(CompilerContext& ctx, llvm::Value* reader,
                                    llvm::Value* key, llvm::Value* idx,
                                    const std::string& elemTn){
    if(elemTn=="string")        return ctx.builder.CreateCall(gJrArrStr(ctx),{reader,key,idx},"ae");
    if(elemTn=="i64"||elemTn=="u64") return ctx.builder.CreateCall(gJrArrI64(ctx),{reader,key,idx},"ae");
    if(elemTn=="f32")           return ctx.builder.CreateCall(gJrArrF32(ctx),{reader,key,idx},"ae");
    if(elemTn=="f64")           return ctx.builder.CreateCall(gJrArrF64(ctx),{reader,key,idx},"ae");
    if(elemTn=="bool"){
        llvm::Value* bv=ctx.builder.CreateCall(gJrArrBool(ctx),{reader,key,idx},"bv");
        return ctx.builder.CreateTrunc(bv,i8T(ctx));
    }
    return ctx.builder.CreateCall(gJrArrI32(ctx),{reader,key,idx},"ae");
}

// ─── emitReadValListElements ─────────────────────────────────────────────────
// Reads elements from subReader (positioned at a JSON array, val_* API)
// into listAlloca (list<elemTn>). Recursive: handles list<Struct>,
// list<list<T>>, and scalar elements at any nesting depth.
static void emitReadValListElements(CompilerContext& ctx, llvm::Value* subReader,
                                     llvm::Value* listAlloca, const std::string& elemTn,
                                     llvm::Function* parentFn, const std::string& lbl)
{
    llvm::Value* cnt = ctx.builder.CreateCall(gJrValLen(ctx), {subReader}, "cnt");
    auto* ph = ctx.builder.GetInsertBlock();
    auto* lc = llvm::BasicBlock::Create(ctx.context, lbl+"_lc", parentFn);
    auto* lb = llvm::BasicBlock::Create(ctx.context, lbl+"_lb", parentFn);
    auto* le = llvm::BasicBlock::Create(ctx.context, lbl+"_le", parentFn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx), 2, lbl+"_i");
    idx->addIncoming(ctx.builder.getInt32(0), ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx, cnt), lb, le);
    ctx.builder.SetInsertPoint(lb);

    if(ctx.classes.count(elemTn) && ctx.classes[elemTn].type){
        llvm::StructType* subSt = ctx.classes[elemTn].type;
        uint64_t subSz = ctx.module.getDataLayout().getTypeAllocSize(subSt);
        llvm::Value* sr = ctx.builder.CreateCall(gJrValSub(ctx), {subReader, idx}, "sr");
        auto* srOk   = llvm::BasicBlock::Create(ctx.context, lbl+"_srok",   parentFn);
        auto* srDone = llvm::BasicBlock::Create(ctx.context, lbl+"_srdone", parentFn);
        ctx.builder.CreateCondBr(
            ctx.builder.CreateICmpNE(sr, llvm::Constant::getNullValue(ptrT(ctx))),
            srOk, srDone);
        ctx.builder.SetInsertPoint(srOk);
        llvm::Value* tmp = ctx.createEntryAlloca(subSt, "stmp");
        ctx.builder.CreateMemSet(tmp, ctx.builder.getInt8(0), subSz, llvm::MaybeAlign(8));
        emitDeserStructFields(ctx, sr, tmp, elemTn, parentFn);
        llvm::Value* sval = ctx.builder.CreateLoad(subSt, tmp, "sval");
        ListImpl::push(ctx, listAlloca, sval);
        ctx.builder.CreateCall(gJrFree(ctx), {sr});
        ctx.builder.CreateBr(srDone);
        ctx.builder.SetInsertPoint(srDone);
    } else if(isRefType(elemTn) || isPtrType(elemTn)){
        // list<ref<T>> / list<ptr<T>>: deserialize each JSON object into a
        // fresh heap struct, then push the pointer into the list.
        std::string innerTn = isRefType(elemTn) ? refInner(elemTn) : ptrInner(elemTn);
        if(ctx.classes.count(innerTn) && ctx.classes[innerTn].type){
            llvm::StructType* subSt = ctx.classes[innerTn].type;
            uint64_t subSz = ctx.module.getDataLayout().getTypeAllocSize(subSt);
            llvm::Value* sr = ctx.builder.CreateCall(gJrValSub(ctx), {subReader, idx}, "sr");
            auto* srOk   = llvm::BasicBlock::Create(ctx.context, lbl+"_rsrok",   parentFn);
            auto* srNull = llvm::BasicBlock::Create(ctx.context, lbl+"_rsrnull", parentFn);
            auto* srDone = llvm::BasicBlock::Create(ctx.context, lbl+"_rsrdone", parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(sr, llvm::Constant::getNullValue(ptrT(ctx))),
                srOk, srNull);
            // calloc(1, subSz) — heap-allocate inner struct, zero-init
            ctx.builder.SetInsertPoint(srOk);
            llvm::Function* callocFn = ctx.module.getFunction("calloc");
            if(!callocFn){
                llvm::FunctionType* ct = llvm::FunctionType::get(
                    ptrT(ctx), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
                callocFn = llvm::Function::Create(ct, llvm::Function::ExternalLinkage,
                    "calloc", ctx.module);
            }
            llvm::Value* heap = ctx.builder.CreateCall(callocFn,
                {ctx.builder.getInt64(1), ctx.builder.getInt64(subSz)}, "rheap");
            emitDeserStructFields(ctx, sr, heap, innerTn, parentFn);
            ListImpl::push(ctx, listAlloca, heap);
            ctx.builder.CreateCall(gJrFree(ctx), {sr});
            ctx.builder.CreateBr(srDone);
            // Null sub-reader → push null pointer.
            ctx.builder.SetInsertPoint(srNull);
            ListImpl::push(ctx, listAlloca,
                llvm::Constant::getNullValue(ptrT(ctx)));
            ctx.builder.CreateBr(srDone);
            ctx.builder.SetInsertPoint(srDone);
        }
    } else if(isListType(elemTn)){
        std::string innerElemTn = listElem(elemTn);
        llvm::Value* isr = ctx.builder.CreateCall(gJrValSub(ctx), {subReader, idx}, "isr");
        if(ctx.memV3 && ctx.isMemV3ContainerHandle(elemTn)){
            // spec/61 §5.7: nested inner list element = HANDLE. Heap-allocate, fill, push pointer.
            llvm::Type* innerElemTy = listElemStrideTy(ctx, innerElemTn);
            llvm::Function* nwh = ctx.getFunction("aiml_list_handle_new", ptrT(ctx), {});
            llvm::Value* ih = ctx.builder.CreateCall(nwh,{},"deser_vilh");
            ListImpl::initList(ctx, ih, innerElemTy, 4);
            auto* isrOk   = llvm::BasicBlock::Create(ctx.context, lbl+"_visrok",   parentFn);
            auto* isrDone = llvm::BasicBlock::Create(ctx.context, lbl+"_visrdone", parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(isr, llvm::Constant::getNullValue(ptrT(ctx))), isrOk, isrDone);
            ctx.builder.SetInsertPoint(isrOk);
            emitReadValListElements(ctx, isr, ih, innerElemTn, parentFn, lbl+"i");
            ctx.builder.CreateCall(gJrFree(ctx), {isr});
            ctx.builder.CreateBr(isrDone);
            ctx.builder.SetInsertPoint(isrDone);
            ListImpl::push(ctx, listAlloca, ih);
        } else {
            llvm::Type* innerListTy = ctx.getLLVMType(elemTn);
            llvm::Type* innerElemTy = ctx.getLLVMType(innerElemTn);
            llvm::Value* ilAlloca = ctx.createEntryAlloca(innerListTy, "ila");
            ListImpl::initList(ctx, ilAlloca, innerElemTy, 4);
            auto* isrOk   = llvm::BasicBlock::Create(ctx.context, lbl+"_isrok",   parentFn);
            auto* isrDone = llvm::BasicBlock::Create(ctx.context, lbl+"_isrdone", parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(isr, llvm::Constant::getNullValue(ptrT(ctx))),
                isrOk, isrDone);
            ctx.builder.SetInsertPoint(isrOk);
            emitReadValListElements(ctx, isr, ilAlloca, innerElemTn, parentFn, lbl+"i");
            ctx.builder.CreateCall(gJrFree(ctx), {isr});
            ctx.builder.CreateBr(isrDone);
            ctx.builder.SetInsertPoint(isrDone);
            llvm::Value* ilVal = ctx.builder.CreateLoad(innerListTy, ilAlloca, "ilval");
            ListImpl::push(ctx, listAlloca, ilVal);
        }
    } else {
        llvm::Value* ie = emitReadValElem(ctx, subReader, idx, elemTn);
        ListImpl::push(ctx, listAlloca, ie);
    }
    llvm::Value* ni = ctx.builder.CreateAdd(idx, ctx.builder.getInt32(1), "ni");
    idx->addIncoming(ni, ctx.builder.GetInsertBlock());
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
}

// ─── helper: deserialize struct fields from a reader into structPtr ──────────
// reader must be a valid (non-null) reader pointing at a JSON object.
// Handles scalar fields, list fields (including list<list<T>>), and map fields.
static void emitDeserStructFields(CompilerContext& ctx, llvm::Value* reader,
                                  llvm::Value* structPtr, const std::string& typeName,
                                  llvm::Function* parentFn){
    if(!ctx.classes.count(typeName)) return;
    llvm::StructType* st = ctx.classes[typeName].type;
    if(!st) return;
    for(auto& [fname,fm]: sortedMembers(ctx,typeName)){
        llvm::Value* fkey = ctx.builder.CreateGlobalStringPtr(fname,"fk_"+fname);
        llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(st,structPtr,fm.index,"fp_"+fname);
        std::string tn = fm.typeName;

        if(isListType(tn)){
            std::string elemTn = listElem(tn);
            // §5.7: nested container element = HANDLE (ptr stride) so deser/encode/free agree.
            llvm::Type* elemTy = listElemStrideTy(ctx, elemTn);
            // spec/61 §5 (memV3): a list FIELD is a refcounted HANDLE pointer, not an inline struct.
            // Allocate the handle, store it into the field, and target it for initList/push — else the
            // {size,cap,data} is written into the 8-byte ptr field and a later `.len`/`.at` derefs
            // garbage (deserialize of any model with a list field crashed — test_serialize_struct and
            // the whole JSON serialize/persist cluster).
            llvm::Value* listTgt = fieldPtr;
            if(ctx.memV3 && ctx.isMemV3ContainerHandle(tn)){
                llvm::Function* nw = ctx.getFunction("aiml_list_handle_new", ptrT(ctx), {});
                listTgt = ctx.builder.CreateCall(nw,{},"deser_lh");
                ctx.builder.CreateStore(listTgt,fieldPtr);
            }
            ListImpl::initList(ctx,listTgt,elemTy,4);
            llvm::Value* cnt = ctx.builder.CreateCall(gJrArrLen(ctx),{reader,fkey},"cnt");
            llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
            llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,fname+"_dlc",parentFn);
            llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,fname+"_dlb",parentFn);
            llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,fname+"_dle",parentFn);
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(lc);
            llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"dli");
            idx->addIncoming(ctx.builder.getInt32(0),ph);
            ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
            ctx.builder.SetInsertPoint(lb);

            if(ctx.isRefcountModel(elemTn) && ctx.classes.count(elemTn) && ctx.classes[elemTn].type){
                // AIML v2: list<Model> element is a %M* → alloc a refcounted MODL shell (rc=1,
                // zeroed = decode precondition), decode the sub-object into it, push the POINTER
                // (the list owns that reference). Null sub-reader → push null (index alignment).
                auto& ci2 = ctx.classes[elemTn];
                uint64_t subSz = (ci2.padded_size > 0)
                    ? (uint64_t)ci2.padded_size
                    : ctx.module.getDataLayout().getTypeAllocSize(ci2.type);
                llvm::Value* sr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,fkey,idx},"msr");
                llvm::BasicBlock* srOk   = llvm::BasicBlock::Create(ctx.context,fname+"_msrok",parentFn);
                llvm::BasicBlock* srNull = llvm::BasicBlock::Create(ctx.context,fname+"_msrnull",parentFn);
                llvm::BasicBlock* srDone = llvm::BasicBlock::Create(ctx.context,fname+"_msrdone",parentFn);
                ctx.builder.CreateCondBr(
                    ctx.builder.CreateICmpNE(sr,llvm::Constant::getNullValue(ptrT(ctx))),srOk,srNull);
                ctx.builder.SetInsertPoint(srOk);
                llvm::Function* allocFn = ctx.getFunction("aiml_model_alloc", ptrT(ctx), { ctx.getSizeType() });
                llvm::Value* obj = ctx.builder.CreateCall(allocFn, { ctx.getSizeConst(subSz) }, "msr_alloc");
                emitDeserStructFields(ctx,sr,obj,elemTn,parentFn);
                ListImpl::push(ctx,listTgt,obj);
                ctx.builder.CreateCall(gJrFree(ctx),{sr});
                ctx.builder.CreateBr(srDone);
                ctx.builder.SetInsertPoint(srNull);
                ListImpl::push(ctx,listTgt,llvm::Constant::getNullValue(ptrT(ctx)));
                ctx.builder.CreateBr(srDone);
                ctx.builder.SetInsertPoint(srDone);
            } else if(ctx.classes.count(elemTn) && ctx.classes[elemTn].type){
                // list<Struct>: sub-reader per element
                llvm::StructType* subSt = ctx.classes[elemTn].type;
                uint64_t subSz = ctx.module.getDataLayout().getTypeAllocSize(subSt);
                llvm::Value* sr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,fkey,idx},"sr");
                // null-safe: skip if sub-reader is null
                llvm::BasicBlock* srOk = llvm::BasicBlock::Create(ctx.context,fname+"_srok",parentFn);
                llvm::BasicBlock* srDone = llvm::BasicBlock::Create(ctx.context,fname+"_srdone",parentFn);
                ctx.builder.CreateCondBr(
                    ctx.builder.CreateICmpNE(sr,llvm::Constant::getNullValue(ptrT(ctx))),srOk,srDone);
                ctx.builder.SetInsertPoint(srOk);
                llvm::Value* tmp = ctx.createEntryAlloca(subSt,"stmp");
                // zero-init: string fields → null (but gJrStr returns strdup("") so safe)
                // Still zero-init lists/maps need explicit initList/initMap below
                ctx.builder.CreateMemSet(tmp,ctx.builder.getInt8(0),subSz,llvm::MaybeAlign(8));
                // recursively deserialize sub-struct fields
                emitDeserStructFields(ctx,sr,tmp,elemTn,parentFn);
                // load struct value from alloca so push copies struct data (not pointer)
                llvm::Value* sval = ctx.builder.CreateLoad(subSt,tmp,"sval");
                ListImpl::push(ctx,listTgt,sval);
                ctx.builder.CreateCall(gJrFree(ctx),{sr});
                ctx.builder.CreateBr(srDone);
                ctx.builder.SetInsertPoint(srDone);
            } else if(isRefType(elemTn) || isPtrType(elemTn)){
                // list<ref<T>> / list<ptr<T>>: per element, calloc a heap T,
                // deserialize into it, push the pointer into the list.  Null
                // sub-reader → push null pointer (preserves index alignment).
                std::string innerTn = isRefType(elemTn) ? refInner(elemTn) : ptrInner(elemTn);
                if(ctx.classes.count(innerTn) && ctx.classes[innerTn].type){
                    llvm::StructType* subSt = ctx.classes[innerTn].type;
                    uint64_t subSz = ctx.module.getDataLayout().getTypeAllocSize(subSt);
                    llvm::Value* sr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,fkey,idx},"rsr");
                    llvm::BasicBlock* srOk   = llvm::BasicBlock::Create(ctx.context,fname+"_rsrok",parentFn);
                    llvm::BasicBlock* srNull = llvm::BasicBlock::Create(ctx.context,fname+"_rsrnull",parentFn);
                    llvm::BasicBlock* srDone = llvm::BasicBlock::Create(ctx.context,fname+"_rsrdone",parentFn);
                    ctx.builder.CreateCondBr(
                        ctx.builder.CreateICmpNE(sr,llvm::Constant::getNullValue(ptrT(ctx))),srOk,srNull);
                    ctx.builder.SetInsertPoint(srOk);
                    llvm::Function* callocFn = ctx.module.getFunction("calloc");
                    if(!callocFn){
                        llvm::FunctionType* ct = llvm::FunctionType::get(
                            ptrT(ctx), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
                        callocFn = llvm::Function::Create(ct, llvm::Function::ExternalLinkage,
                            "calloc", ctx.module);
                    }
                    llvm::Value* heap = ctx.builder.CreateCall(callocFn,
                        {ctx.builder.getInt64(1), ctx.builder.getInt64(subSz)}, "rheap");
                    emitDeserStructFields(ctx,sr,heap,innerTn,parentFn);
                    ListImpl::push(ctx,listTgt,heap);
                    ctx.builder.CreateCall(gJrFree(ctx),{sr});
                    ctx.builder.CreateBr(srDone);
                    ctx.builder.SetInsertPoint(srNull);
                    ListImpl::push(ctx,listTgt,llvm::Constant::getNullValue(ptrT(ctx)));
                    ctx.builder.CreateBr(srDone);
                    ctx.builder.SetInsertPoint(srDone);
                }
            } else if(isListType(elemTn)){
                // list<list<T>> (any depth/struct): sub-reader per inner array, recurse.
                std::string innerElemTn = listElem(elemTn);
                llvm::Value* isr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,fkey,idx},"isr");
                if(ctx.memV3 && ctx.isMemV3ContainerHandle(elemTn)){
                    // spec/61 §5.7: nested inner list element = refcounted HANDLE. Allocate a heap
                    // handle, fill it, push the HANDLE POINTER (8B) — matches serFieldSubtree free
                    // (elem_size = ptr, inner LIST_PTR) and the .get/access handle load.
                    llvm::Type* innerElemTy = listElemStrideTy(ctx, innerElemTn);
                    llvm::Function* nwh = ctx.getFunction("aiml_list_handle_new", ptrT(ctx), {});
                    llvm::Value* ih = ctx.builder.CreateCall(nwh,{},"deser_ilh");
                    ListImpl::initList(ctx,ih,innerElemTy,4);
                    llvm::BasicBlock* isrOk = llvm::BasicBlock::Create(ctx.context,fname+"_isrok",parentFn);
                    llvm::BasicBlock* isrDone = llvm::BasicBlock::Create(ctx.context,fname+"_isrdone",parentFn);
                    ctx.builder.CreateCondBr(
                        ctx.builder.CreateICmpNE(isr,llvm::Constant::getNullValue(ptrT(ctx))),isrOk,isrDone);
                    ctx.builder.SetInsertPoint(isrOk);
                    emitReadValListElements(ctx,isr,ih,innerElemTn,parentFn,fname+"_il");
                    ctx.builder.CreateCall(gJrFree(ctx),{isr});
                    ctx.builder.CreateBr(isrDone);
                    ctx.builder.SetInsertPoint(isrDone);
                    ListImpl::push(ctx,listTgt,ih);
                } else {
                    llvm::Type* innerListTy = ctx.getLLVMType(elemTn);
                    llvm::Type* innerElemTy = ctx.getLLVMType(innerElemTn);
                    llvm::Value* ilAlloca = ctx.createEntryAlloca(innerListTy,"ila");
                    ListImpl::initList(ctx,ilAlloca,innerElemTy,4);
                    llvm::BasicBlock* isrOk = llvm::BasicBlock::Create(ctx.context,fname+"_isrok",parentFn);
                    llvm::BasicBlock* isrDone = llvm::BasicBlock::Create(ctx.context,fname+"_isrdone",parentFn);
                    ctx.builder.CreateCondBr(
                        ctx.builder.CreateICmpNE(isr,llvm::Constant::getNullValue(ptrT(ctx))),isrOk,isrDone);
                    ctx.builder.SetInsertPoint(isrOk);
                    emitReadValListElements(ctx,isr,ilAlloca,innerElemTn,parentFn,fname+"_il");
                    ctx.builder.CreateCall(gJrFree(ctx),{isr});
                    ctx.builder.CreateBr(isrDone);
                    ctx.builder.SetInsertPoint(isrDone);
                    llvm::Value* ilVal = ctx.builder.CreateLoad(innerListTy,ilAlloca,"ilval");
                    ListImpl::push(ctx,listTgt,ilVal);
                }
            } else {
                // list of primitives
                llvm::Value* elem = emitReadArrElem(ctx,reader,fkey,idx,elemTn);
                ListImpl::push(ctx,listTgt,elem);
            }
            llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
            idx->addIncoming(ni,ctx.builder.GetInsertBlock());
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(le);
        } else if(isMapType(tn)){
            std::string keyTn = mapKeyTn(tn);
            std::string valTn = mapValTn(tn);
            bool intKey = (keyTn != "string");
            llvm::Type* keyTy = intKey ? i32T(ctx) : ctx.getLLVMType("string");
            llvm::Type* valTy = ctx.getLLVMType(valTn);
            llvm::StructType* mapTy = MapImpl::getMapStructType(ctx,keyTy,valTy);
            // spec/61 §5 (memV3): a map FIELD is a refcounted HANDLE pointer (see the list case above).
            llvm::Value* mapTgt = fieldPtr;
            if(ctx.memV3 && ctx.isMemV3ContainerHandle(tn)){
                llvm::Function* nw = ctx.getFunction("aiml_map_handle_new", ptrT(ctx), {});
                mapTgt = ctx.builder.CreateCall(nw,{},"deser_mh");
                ctx.builder.CreateStore(mapTgt,fieldPtr);
            }
            MapImpl::initMap(ctx,mapTgt,keyTy,valTy,8);
            llvm::Value* cnt = ctx.builder.CreateCall(gJrMapCount(ctx),{reader,fkey},"cnt");
            llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
            llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,fname+"_dmc",parentFn);
            llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,fname+"_dmb",parentFn);
            llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,fname+"_dme",parentFn);
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(lc);
            llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"dmi");
            idx->addIncoming(ctx.builder.getInt32(0),ph);
            ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
            ctx.builder.SetInsertPoint(lb);
            llvm::Value* kv;
            // aiml_jr_map_key / _val_str return OWNED heap dups (yyjson_wrapper). MapImpl::put now
            // DUPS string keys/values into map-owned storage (§8), so free the reader's dup after
            // put — otherwise it leaks (was leaking regardless, pre-§8).
            llvm::Function* decf = ctx.getFunction("aiml_str_decref",ctx.builder.getVoidTy(),{i8pT(ctx)});
            llvm::Value* keyDup = nullptr;
            if(intKey){
                // enum/integer key: JSON key is a string like "1" → convert via atoi, then free it.
                keyDup = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,fkey,idx},"ks");
                llvm::Function* atoiFn = ctx.getFunction("atoi",i32T(ctx),{i8pT(ctx)});
                kv = ctx.builder.CreateCall(atoiFn,{keyDup},"k");
            } else {
                kv = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,fkey,idx},"k");
            }
            llvm::Value* vv;
            if(valTn=="string")        vv=ctx.builder.CreateCall(gJrMapValStr(ctx),{reader,fkey,idx},"v");
            else if(valTn=="i64"||valTn=="u64") vv=ctx.builder.CreateCall(gJrMapValI64(ctx),{reader,fkey,idx},"v");
            else if(valTn=="f32")      vv=ctx.builder.CreateCall(gJrMapValF32(ctx),{reader,fkey,idx},"v");
            else if(valTn=="f64")      vv=ctx.builder.CreateCall(gJrMapValF64(ctx),{reader,fkey,idx},"v");
            else                       vv=ctx.builder.CreateCall(gJrMapValI32(ctx),{reader,fkey,idx},"v");
            MapImpl::put(ctx,mapTgt,kv,vv,mapTy,valTn=="string");
            if(intKey && keyDup) ctx.builder.CreateCall(decf,{keyDup});   // reader dup of the int-key string
            else if(!intKey)     ctx.builder.CreateCall(decf,{kv});       // reader dup of the string key
            if(valTn=="string")  ctx.builder.CreateCall(decf,{vv});       // reader dup of the string value
            llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
            idx->addIncoming(ni,ctx.builder.GetInsertBlock());
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(le);
        } else if(ctx.classes.count(tn) && ctx.classes[tn].type){
            // nested struct: sub-reader, recursive
            llvm::Value* sr = ctx.builder.CreateCall(gJrSub(ctx),{reader,fkey},"sr");
            // null-safe: if sub-reader null, zero-init the field
            llvm::BasicBlock* srOk = llvm::BasicBlock::Create(ctx.context,fname+"_nsrok",parentFn);
            llvm::BasicBlock* srDone = llvm::BasicBlock::Create(ctx.context,fname+"_nsrdone",parentFn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(sr,llvm::Constant::getNullValue(ptrT(ctx))),srOk,srDone);
            ctx.builder.SetInsertPoint(srOk);
            emitDeserStructFields(ctx,sr,fieldPtr,tn,parentFn);
            ctx.builder.CreateCall(gJrFree(ctx),{sr});
            ctx.builder.CreateBr(srDone);
            ctx.builder.SetInsertPoint(srDone);
        } else {
            // scalar
            emitReadScalar(ctx,reader,fkey,fieldPtr,tn,fm.type);
        }
    }
}

// ─── emitDeserializeStruct ────────────────────────────────────────────────────
// Emits IR function: __aiml_deser_TYPENAME(ptr json) -> ptr (heap struct, caller memcpy's)
static llvm::Function* emitDeserializeStruct(CompilerContext& ctx,
                                             const std::string& typeName){
    std::string fnName = "__aiml_deser_"+typeName;
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    if(!ctx.classes.count(typeName)) return nullptr;
    llvm::StructType* st = ctx.classes[typeName].type;
    if(!st) return nullptr;

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(ptrT(ctx),{i8pT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("json");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* reader = ctx.builder.CreateCall(gJrParse(ctx),{fn->getArg(0)},"r");
    llvm::Function* mallocFn = ctx.getFunction("malloc",ptrT(ctx),{szT(ctx)});
    uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(st);
    llvm::Value* heap = ctx.builder.CreateCall(mallocFn,{llvm::ConstantInt::get(szT(ctx), sz)},"hp");
    // zero-init entire struct — prevents null pointers for string/list/map fields
    ctx.builder.CreateMemSet(heap,ctx.builder.getInt8(0),sz,llvm::MaybeAlign(8));

    emitDeserStructFields(ctx,reader,heap,typeName,fn);

    ctx.builder.CreateCall(gJrFree(ctx),{reader});
    ctx.builder.CreateRet(heap);
    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── emitDeserializeList ──────────────────────────────────────────────────────
static llvm::Function* emitDeserializeList(CompilerContext& ctx,
                                           const std::string& listTn){
    std::string fnName = "__aiml_deser_"+sanitize(listTn);
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    std::string elemTn = listElem(listTn);
    llvm::Type* elemTy = listElemStrideTy(ctx, elemTn);  // §5.7 nested container element = ptr stride
    llvm::Type* listTy = ctx.getLLVMType(listTn);

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(ptrT(ctx),{i8pT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("json");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* reader = ctx.builder.CreateCall(gJrParse(ctx),{fn->getArg(0)},"r");
    llvm::Function* mallocFn = ctx.getFunction("malloc",ptrT(ctx),{i64T(ctx)});
    uint64_t lsz = ctx.module.getDataLayout().getTypeAllocSize(listTy);
    llvm::Value* heap = ctx.builder.CreateCall(mallocFn,{ctx.builder.getInt64(lsz)},"hp");
    ListImpl::initList(ctx,heap,elemTy,4);

    llvm::Value* vk  = ctx.builder.CreateGlobalStringPtr("values","vk");
    llvm::Value* cnt = ctx.builder.CreateCall(gJrArrLen(ctx),{reader,vk},"cnt");

    llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
    llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,"lc",fn);
    llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,"lb",fn);
    llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,"le",fn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"i");
    idx->addIncoming(ctx.builder.getInt32(0),ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
    ctx.builder.SetInsertPoint(lb);
    if(ctx.classes.count(elemTn) && ctx.classes[elemTn].type){
        // list<Struct>: sub-reader, deserialize into temp alloca, push
        llvm::StructType* subSt = ctx.classes[elemTn].type;
        uint64_t subSz = ctx.module.getDataLayout().getTypeAllocSize(subSt);
        llvm::Value* sr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,vk,idx},"sr");
        llvm::BasicBlock* srOk = llvm::BasicBlock::Create(ctx.context,"dl_srok",fn);
        llvm::BasicBlock* srDone = llvm::BasicBlock::Create(ctx.context,"dl_srdone",fn);
        ctx.builder.CreateCondBr(
            ctx.builder.CreateICmpNE(sr,llvm::Constant::getNullValue(ptrT(ctx))),srOk,srDone);
        ctx.builder.SetInsertPoint(srOk);
        llvm::Value* tmp = ctx.createEntryAlloca(subSt,"stmp");
        ctx.builder.CreateMemSet(tmp,ctx.builder.getInt8(0),subSz,llvm::MaybeAlign(8));
        emitDeserStructFields(ctx,sr,tmp,elemTn,fn);
        llvm::Value* sval = ctx.builder.CreateLoad(subSt,tmp,"sval");
        ListImpl::push(ctx,heap,sval);
        ctx.builder.CreateCall(gJrFree(ctx),{sr});
        ctx.builder.CreateBr(srDone);
        ctx.builder.SetInsertPoint(srDone);
    } else if(isListType(elemTn)){
        // list<list<T>> (any depth/struct): sub-reader for inner array, recurse
        std::string innerElemTn = listElem(elemTn);
        llvm::Value* isr = ctx.builder.CreateCall(gJrArrSub(ctx),{reader,vk,idx},"isr");
        if(ctx.memV3 && ctx.isMemV3ContainerHandle(elemTn)){
            // spec/61 §5.7: nested inner list element = HANDLE. Heap-allocate, fill, push pointer.
            llvm::Type* innerElemTy = listElemStrideTy(ctx, innerElemTn);
            llvm::Function* nwh = ctx.getFunction("aiml_list_handle_new", ptrT(ctx), {});
            llvm::Value* ih = ctx.builder.CreateCall(nwh,{},"deser_dlh");
            ListImpl::initList(ctx,ih,innerElemTy,4);
            llvm::BasicBlock* isrOk = llvm::BasicBlock::Create(ctx.context,"dl_isrok",fn);
            llvm::BasicBlock* isrDone = llvm::BasicBlock::Create(ctx.context,"dl_isrdone",fn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(isr,llvm::Constant::getNullValue(ptrT(ctx))),isrOk,isrDone);
            ctx.builder.SetInsertPoint(isrOk);
            emitReadValListElements(ctx,isr,ih,innerElemTn,fn,"dl_il");
            ctx.builder.CreateCall(gJrFree(ctx),{isr});
            ctx.builder.CreateBr(isrDone);
            ctx.builder.SetInsertPoint(isrDone);
            ListImpl::push(ctx,heap,ih);
        } else {
            llvm::Type* innerListTy = ctx.getLLVMType(elemTn);
            llvm::Type* innerElemTy = ctx.getLLVMType(innerElemTn);
            llvm::Value* ilAlloca = ctx.createEntryAlloca(innerListTy,"ila");
            ListImpl::initList(ctx,ilAlloca,innerElemTy,4);
            llvm::BasicBlock* isrOk = llvm::BasicBlock::Create(ctx.context,"dl_isrok",fn);
            llvm::BasicBlock* isrDone = llvm::BasicBlock::Create(ctx.context,"dl_isrdone",fn);
            ctx.builder.CreateCondBr(
                ctx.builder.CreateICmpNE(isr,llvm::Constant::getNullValue(ptrT(ctx))),isrOk,isrDone);
            ctx.builder.SetInsertPoint(isrOk);
            emitReadValListElements(ctx,isr,ilAlloca,innerElemTn,fn,"dl_il");
            ctx.builder.CreateCall(gJrFree(ctx),{isr});
            ctx.builder.CreateBr(isrDone);
            ctx.builder.SetInsertPoint(isrDone);
            llvm::Value* ilVal = ctx.builder.CreateLoad(innerListTy,ilAlloca,"ilval");
            ListImpl::push(ctx,heap,ilVal);
        }
    } else {
        llvm::Value* elem = emitReadArrElem(ctx,reader,vk,idx,elemTn);
        ListImpl::push(ctx,heap,elem);
    }
    llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
    idx->addIncoming(ni,ctx.builder.GetInsertBlock());
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
    ctx.builder.CreateCall(gJrFree(ctx),{reader});
    ctx.builder.CreateRet(heap);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── emitDeserializeMap ───────────────────────────────────────────────────────
static llvm::Function* emitDeserializeMap(CompilerContext& ctx,
                                          const std::string& mapTn){
    std::string fnName = "__aiml_deser_"+sanitize(mapTn);
    if(auto* f=ctx.module.getFunction(fnName)) return f;
    std::string keyTn = mapKeyTn(mapTn);
    std::string valTn = mapValTn(mapTn);
    bool intKey = (keyTn != "string");
    llvm::Type* keyTy = intKey ? i32T(ctx) : ctx.getLLVMType("string");
    llvm::Type* valTy = ctx.getLLVMType(valTn);
    llvm::StructType* mapTy = MapImpl::getMapStructType(ctx,keyTy,valTy);

    auto savedIP = ctx.builder.saveIP();
    auto* ft = llvm::FunctionType::get(ptrT(ctx),{i8pT(ctx)},false);
    auto* fn = llvm::Function::Create(ft,llvm::Function::InternalLinkage,fnName,ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);
    fn->getArg(0)->setName("json");
    auto* entry = llvm::BasicBlock::Create(ctx.context,"entry",fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* reader = ctx.builder.CreateCall(gJrParse(ctx),{fn->getArg(0)},"r");
    llvm::Function* mallocFn = ctx.getFunction("malloc",ptrT(ctx),{i64T(ctx)});
    uint64_t msz = ctx.module.getDataLayout().getTypeAllocSize(mapTy);
    llvm::Value* heap = ctx.builder.CreateCall(mallocFn,{ctx.builder.getInt64(msz)},"hp");
    MapImpl::initMap(ctx,heap,keyTy,valTy,8);

    llvm::Value* ek  = ctx.builder.CreateGlobalStringPtr("entries","ek");
    llvm::Value* cnt = ctx.builder.CreateCall(gJrMapCount(ctx),{reader,ek},"cnt");

    llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
    llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,"lc",fn);
    llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,"lb",fn);
    llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,"le",fn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"i");
    idx->addIncoming(ctx.builder.getInt32(0),ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
    ctx.builder.SetInsertPoint(lb);
    llvm::Value* kv;
    // aiml_jr_map_key / _val_str return OWNED heap dups; MapImpl::put DUPS string key/value into
    // map-owned storage (§8) → free the reader's dup after put.
    llvm::Function* decf = ctx.getFunction("aiml_str_decref",ctx.builder.getVoidTy(),{i8pT(ctx)});
    llvm::Value* keyDup = nullptr;
    if(intKey){
        keyDup = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,ek,idx},"ks");
        llvm::Function* atoiFn = ctx.getFunction("atoi",i32T(ctx),{i8pT(ctx)});
        kv = ctx.builder.CreateCall(atoiFn,{keyDup},"k");
    } else {
        kv = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,ek,idx},"k");
    }
    llvm::Value* vv;
    if(valTn=="string")        vv=ctx.builder.CreateCall(gJrMapValStr(ctx),{reader,ek,idx},"v");
    else if(valTn=="i64"||valTn=="u64") vv=ctx.builder.CreateCall(gJrMapValI64(ctx),{reader,ek,idx},"v");
    else if(valTn=="f32")      vv=ctx.builder.CreateCall(gJrMapValF32(ctx),{reader,ek,idx},"v");
    else if(valTn=="f64")      vv=ctx.builder.CreateCall(gJrMapValF64(ctx),{reader,ek,idx},"v");
    else                       vv=ctx.builder.CreateCall(gJrMapValI32(ctx),{reader,ek,idx},"v");
    MapImpl::put(ctx,heap,kv,vv,mapTy,valTn=="string");
    if(intKey && keyDup) ctx.builder.CreateCall(decf,{keyDup});
    else if(!intKey)     ctx.builder.CreateCall(decf,{kv});
    if(valTn=="string")  ctx.builder.CreateCall(decf,{vv});
    llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
    idx->addIncoming(ni,ctx.builder.GetInsertBlock());
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
    ctx.builder.CreateCall(gJrFree(ctx),{reader});
    ctx.builder.CreateRet(heap);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ─── Public API ───────────────────────────────────────────────────────────────

// Returns true if t is a primitive or enum (i.e. serializable as a JSON scalar).
static bool isScalarSerializable(CompilerContext& ctx, const std::string& t){
    return isPrimitive(t) || ctx.enums.count(t) > 0;
}

bool isSerializable(CompilerContext& ctx, const std::string& typeName){
    if(isPrimitive(typeName)) return true;
    if(ctx.enums.count(typeName)) return true;
    if(typeName=="any"||typeName=="Code"||
       typeName=="lock"||typeName=="thread_handle") return false;
    if(typeName.rfind("chan<",0)==0) return false;
    if(typeName=="file_handle"||typeName=="process") return false;
    if(isPtrType(typeName)) return isSerializable(ctx,ptrInner(typeName));
    if(isRefType(typeName)) return isSerializable(ctx,refInner(typeName));
    if(isListType(typeName)){
        // Fully recursive: list<list<Struct>>, list<list<list<T>>>, etc. all supported.
        return isSerializable(ctx, listElem(typeName));
    }
    if(isMapType(typeName)){
        // Supported map key types: string, primitive integers, enums.
        // Complex keys (lists, structs, float) are not supported.
        std::string kt = mapKeyTn(typeName);
        bool keyOk = (kt == "string") || ctx.enums.count(kt) > 0 ||
                     kt=="i8"||kt=="u8"||kt=="i16"||kt=="u16"||
                     kt=="i32"||kt=="u32"||kt=="i64"||kt=="u64";
        if(!keyOk) return false;
        // Map value serialization only handles scalar types (primitive/enum) and string.
        std::string vt = mapValTn(typeName);
        return !vt.empty() && isScalarSerializable(ctx, vt);
    }
    // buffer<T>: binary path uses raw memcpy — only safe for primitive element types.
    if(typeName.rfind("buffer<",0)==0 && typeName.back()=='>'){
        std::string et = typeName.substr(7, typeName.size()-8);
        static const std::set<std::string> POD{
            "i8","u8","i16","u16","i32","u32","i64","u64","f32","f64","bool"};
        return POD.count(et) > 0;
    }
    if(ctx.classes.count(typeName)){
        for(auto& [n,m]:ctx.classes[typeName].members)
            if(!isSerializable(ctx,m.typeName)) return false;
        return true;
    }
    return false;
}

llvm::Value* serialize(CompilerContext& ctx, llvm::Value* val,
                       const std::string& typeName, llvm::Value* stylePtr){
    llvm::Value* json = nullptr;
    // spec/61 §7: track whether `json` is a FRESH OWNED rc-string (aiml_jw_done from a compound
    // struct/list/map) so it is registered as an owned string temp below — else the serialized JSON
    // leaks per call (HTTP /servers.json `set list_json = sys.serialize(lst)`, leak_probes/round3 §3).
    // Only the compound paths are marked: the `string` branch is a passthrough (borrow, must NOT be
    // registered) and the scalar *ToString paths are handled by their own managed-string lifetime.
    bool jsonOwned = false;

    // Enums → plain integer string
    if(ctx.enums.count(typeName))
        json = StringUtils::intToString(ctx,val);

    // Primitives → plain string
    else if(typeName=="string") json = val;
    else if(typeName=="i64"||typeName=="u64") json = StringUtils::int64ToString(ctx,val);
    else if(typeName=="f32"||typeName=="f64") json = StringUtils::floatToString(ctx,val);
    else if(typeName=="bool") json = StringUtils::boolToString(ctx,val);
    else if(typeName=="i8"||typeName=="u8"||typeName=="i16"||typeName=="u16"||
       typeName=="i32"||typeName=="u32"){
        if(!val->getType()->isIntegerTy(32))
            val=ctx.builder.CreateIntCast(val,i32T(ctx),true,"ic");
        json = StringUtils::intToString(ctx,val);
    }

    // Compound types — val is a raw pointer (alloca / heap struct)
    else if(ctx.classes.count(typeName)){
        llvm::Function* fn=emitSerializeStruct(ctx,typeName);
        if (fn) { json = ctx.builder.CreateCall(fn,{val},"ser_r"); jsonOwned = true; }
        else json = ctx.builder.CreateGlobalStringPtr("{}");
    }
    else if(isListType(typeName)){
        llvm::Function* fn=emitSerializeList(ctx,typeName);
        if (fn) { json = ctx.builder.CreateCall(fn,{val},"ser_r"); jsonOwned = true; }
        else json = ctx.builder.CreateGlobalStringPtr("{}");
    }
    else if(isMapType(typeName)){
        llvm::Function* fn=emitSerializeMap(ctx,typeName);
        if (fn) { json = ctx.builder.CreateCall(fn,{val},"ser_r"); jsonOwned = true; }
        else json = ctx.builder.CreateGlobalStringPtr("{}");
    }

    if(!json) json = ctx.builder.CreateGlobalStringPtr("(unsupported)");

    if (stylePtr) {
        auto* prettyPtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("JsonStyle"), stylePtr, 0, "jsonstyle_pretty_ptr");
        auto* indentPtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("JsonStyle"), stylePtr, 1, "jsonstyle_indent_ptr");
        auto* newlinePtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("JsonStyle"), stylePtr, 2, "jsonstyle_newline_ptr");
        llvm::Value* prettyVal = ctx.builder.CreateLoad(i32T(ctx), prettyPtr, "jsonstyle_pretty");
        llvm::Value* indentVal = ctx.builder.CreateLoad(i8pT(ctx), indentPtr, "jsonstyle_indent");
        llvm::Value* newlineVal = ctx.builder.CreateLoad(i8pT(ctx), newlinePtr, "jsonstyle_newline");
        // aiml_json_style_format returns a FRESH owned rc-string and does NOT free its input, so an
        // owned compound `json` would leak here — release it before replacing (the styled result is
        // itself owned → stays registered below).
        llvm::Value* styled = ctx.builder.CreateCall(gJsonStyleFmt(ctx), {json, prettyVal, indentVal, newlineVal}, "ser_style");
        if (jsonOwned && ctx.lifetimeDropsEnabled) {
            llvm::Function* dref = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8pT(ctx)});
            ctx.builder.CreateCall(dref, { json });
        }
        json = styled;
        jsonOwned = true;
    }

    // spec/61 §7: register a fresh owned JSON rc-string as an owned string temp so a consuming
    // decl/set MOVES it (one drop) and a transient is decref'd at stmt end — else it leaks (round3 §3).
    if (jsonOwned && ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
        AIML::AST::aimlTrackStringTemp(ctx, json);
    return json;
}

llvm::Value* deserialize(CompilerContext& ctx, llvm::Value* jsonStr,
                         const std::string& typeName){
    llvm::Type* i8p = i8pT(ctx);
    llvm::Type* i32 = i32T(ctx);
    llvm::Type* i64 = i64T(ctx);
    llvm::Type* f64 = f64T(ctx);

    // Primitives from plain string
    if(typeName=="string") return jsonStr;
    if(typeName=="bool"){
        llvm::Function* strcmpFn=ctx.getFunction("strcmp",i32,{i8p,i8p});
        llvm::Value* ts=ctx.builder.CreateGlobalStringPtr("true","ts");
        llvm::Value* cmp=ctx.builder.CreateCall(strcmpFn,{jsonStr,ts},"cmp");
        return ctx.builder.CreateICmpEQ(cmp,ctx.builder.getInt32(0),"bv");
    }
    if(typeName=="i64"||typeName=="u64"){
        llvm::Function* fn=ctx.getFunction("atoll",i64,{i8p});
        return ctx.builder.CreateCall(fn,{jsonStr},"iv");
    }
    if(typeName=="f32"){
        llvm::Function* fn=ctx.getFunction("atof",f64,{i8p});
        llvm::Value* d=ctx.builder.CreateCall(fn,{jsonStr},"dv");
        return ctx.builder.CreateFPTrunc(d,f32T(ctx),"fv");
    }
    if(typeName=="f64"){
        llvm::Function* fn=ctx.getFunction("atof",f64,{i8p});
        return ctx.builder.CreateCall(fn,{jsonStr},"dv");
    }
    if(isPrimitive(typeName)||ctx.enums.count(typeName)){
        llvm::Function* fn=ctx.getFunction("atoi",i32,{i8p});
        return ctx.builder.CreateCall(fn,{jsonStr},"iv");
    }

    // Compound types
    if(ctx.classes.count(typeName)){
        llvm::Function* fn=emitDeserializeStruct(ctx,typeName);
        if(!fn) return llvm::Constant::getNullValue(ptrT(ctx));
        return ctx.builder.CreateCall(fn,{jsonStr},"deser_r");
    }
    if(isListType(typeName)){
        llvm::Function* fn=emitDeserializeList(ctx,typeName);
        if(!fn) return llvm::Constant::getNullValue(ptrT(ctx));
        return ctx.builder.CreateCall(fn,{jsonStr},"deser_r");
    }
    if(isMapType(typeName)){
        llvm::Function* fn=emitDeserializeMap(ctx,typeName);
        if(!fn) return llvm::Constant::getNullValue(ptrT(ctx));
        return ctx.builder.CreateCall(fn,{jsonStr},"deser_r");
    }

    return llvm::Constant::getNullValue(i8p);
}

} // namespace SerializeImpl
