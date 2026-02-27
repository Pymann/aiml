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

static std::string listElem(const std::string& t){ return t.substr(5, t.size()-6); }
static std::string mapValTn(const std::string& t){
    std::string inner = t.substr(4, t.size()-5);
    size_t sep = inner.find(':');
    return sep==std::string::npos ? "" : inner.substr(sep+1);
}
static std::string ptrInner(const std::string& t){ return t.substr(4, t.size()-5); }

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

static void emitWriteField(CompilerContext& ctx, llvm::Value* writer,
                           llvm::Value* structPtr, llvm::StructType* structLLVMTy,
                           const std::string& fname,
                           const CompilerContext::StructMember& fm){
    llvm::Value* fkey = ctx.builder.CreateGlobalStringPtr(fname, "fk_"+fname);
    std::string tn = fm.typeName;

    // ── list field ────────────────────────────────────────────────────────────
    if(isListType(tn)){
        std::string elemTn = listElem(tn);
        llvm::Type* elemTy = ctx.getLLVMType(elemTn);
        llvm::Type* listTy = ctx.getLLVMType(tn);
        llvm::Value* fp = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"lfp");
        llvm::Value* sg = ctx.builder.CreateStructGEP(listTy,fp,0,"lsg");
        llvm::Value* sz = ctx.builder.CreateLoad(i32T(ctx),sg,"lsz");
        llvm::Value* dg = ctx.builder.CreateStructGEP(listTy,fp,2,"ldg");
        llvm::Value* dp = ctx.builder.CreateLoad(ptrT(ctx),dg,"ldp");
        ctx.builder.CreateCall(gJwArrBgn(ctx),{writer,fkey});

        llvm::Function* pfn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* ph  = ctx.builder.GetInsertBlock();
        llvm::BasicBlock* lc  = llvm::BasicBlock::Create(ctx.context,fname+"_alc",pfn);
        llvm::BasicBlock* lb  = llvm::BasicBlock::Create(ctx.context,fname+"_alb",pfn);
        llvm::BasicBlock* le  = llvm::BasicBlock::Create(ctx.context,fname+"_ale",pfn);
        ctx.builder.CreateBr(lc);
        ctx.builder.SetInsertPoint(lc);
        llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"ali");
        idx->addIncoming(ctx.builder.getInt32(0),ph);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,sz),lb,le);
        ctx.builder.SetInsertPoint(lb);
        llvm::Value* ep = ctx.builder.CreateGEP(elemTy,dp,idx,"aep");
        if(elemTn=="string"){
            llvm::Value* sv=ctx.builder.CreateLoad(i8pT(ctx),ep,"sv");
            ctx.builder.CreateCall(gJwArrStr(ctx),{writer,sv});
        } else if(elemTn=="i64"||elemTn=="u64"){
            llvm::Value* ev=ctx.builder.CreateLoad(i64T(ctx),ep,"ev");
            ctx.builder.CreateCall(gJwArrI64(ctx),{writer,ev});
        } else if(elemTn=="f32"){
            llvm::Value* ev=ctx.builder.CreateLoad(f32T(ctx),ep,"ev");
            ctx.builder.CreateCall(gJwArrF32(ctx),{writer,ev});
        } else if(elemTn=="f64"){
            llvm::Value* ev=ctx.builder.CreateLoad(f64T(ctx),ep,"ev");
            ctx.builder.CreateCall(gJwArrF64(ctx),{writer,ev});
        } else if(elemTn=="bool"){
            llvm::Value* ev=ctx.builder.CreateLoad(i8T(ctx),ep,"ev");
            llvm::Value* ev32=ctx.builder.CreateZExt(ev,i32T(ctx));
            ctx.builder.CreateCall(gJwArrBool(ctx),{writer,ev32});
        } else {
            llvm::Value* ev=ctx.builder.CreateLoad(i32T(ctx),ep,"ev");
            ctx.builder.CreateCall(gJwArrI32(ctx),{writer,ev});
        }
        llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ani");
        idx->addIncoming(ni,ctx.builder.GetInsertBlock());
        ctx.builder.CreateBr(lc);
        ctx.builder.SetInsertPoint(le);
        ctx.builder.CreateCall(gJwArrEnd(ctx),{writer});
        return;
    }

    // ── map field ─────────────────────────────────────────────────────────────
    if(isMapType(tn)){
        std::string valTn = mapValTn(tn);
        llvm::Type* mapTy = ctx.getLLVMType(tn);
        llvm::Value* fp   = ctx.builder.CreateStructGEP(structLLVMTy,structPtr,fm.index,"mfp");
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
        llvm::Value* kep = ctx.builder.CreateGEP(i8pT(ctx),kp,idx,"kep");
        llvm::Value* ks  = ctx.builder.CreateLoad(i8pT(ctx),kep,"ks");
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
    if(tn=="string"){
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

    llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
    llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,"lc",fn);
    llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,"lb",fn);
    llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,"le",fn);
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(lc);
    llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"i");
    idx->addIncoming(ctx.builder.getInt32(0),ph);
    ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,sz),lb,le);
    ctx.builder.SetInsertPoint(lb);
    llvm::Value* ep = ctx.builder.CreateGEP(elemTy,dp,idx,"ep");
    if(elemTn=="string"){
        ctx.builder.CreateCall(gJwArrStr(ctx),{writer,ctx.builder.CreateLoad(i8pT(ctx),ep,"sv")});
    } else if(elemTn=="i64"||elemTn=="u64"){
        ctx.builder.CreateCall(gJwArrI64(ctx),{writer,ctx.builder.CreateLoad(i64T(ctx),ep,"ev")});
    } else if(elemTn=="f32"){
        ctx.builder.CreateCall(gJwArrF32(ctx),{writer,ctx.builder.CreateLoad(f32T(ctx),ep,"ev")});
    } else if(elemTn=="f64"){
        ctx.builder.CreateCall(gJwArrF64(ctx),{writer,ctx.builder.CreateLoad(f64T(ctx),ep,"ev")});
    } else if(elemTn=="bool"){
        llvm::Value* ev=ctx.builder.CreateLoad(i8T(ctx),ep,"ev");
        ctx.builder.CreateCall(gJwArrBool(ctx),{writer,ctx.builder.CreateZExt(ev,i32T(ctx))});
    } else {
        ctx.builder.CreateCall(gJwArrI32(ctx),{writer,ctx.builder.CreateLoad(i32T(ctx),ep,"ev")});
    }
    llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
    idx->addIncoming(ni,ctx.builder.GetInsertBlock());
    ctx.builder.CreateBr(lc);
    ctx.builder.SetInsertPoint(le);
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
    std::string valTn = mapValTn(mapTn);
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
    llvm::Value* kep = ctx.builder.CreateGEP(i8pT(ctx),kp,idx,"kep");
    llvm::Value* ks  = ctx.builder.CreateLoad(i8pT(ctx),kep,"ks");
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
    llvm::Function* mallocFn = ctx.getFunction("malloc",ptrT(ctx),{i64T(ctx)});
    uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(st);
    llvm::Value* heap = ctx.builder.CreateCall(mallocFn,{ctx.builder.getInt64(sz)},"hp");

    for(auto& [fname,fm]: sortedMembers(ctx,typeName)){
        llvm::Value* fkey = ctx.builder.CreateGlobalStringPtr(fname,"fk_"+fname);
        llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(st,heap,fm.index,"fp_"+fname);
        std::string tn = fm.typeName;

        if(isListType(tn)){
            std::string elemTn = listElem(tn);
            llvm::Type* elemTy = ctx.getLLVMType(elemTn);
            ListImpl::initList(ctx,fieldPtr,elemTy,4);
            llvm::Value* cnt = ctx.builder.CreateCall(gJrArrLen(ctx),{reader,fkey},"cnt");
            llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
            llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,fname+"_dlc",fn);
            llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,fname+"_dlb",fn);
            llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,fname+"_dle",fn);
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(lc);
            llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"dli");
            idx->addIncoming(ctx.builder.getInt32(0),ph);
            ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
            ctx.builder.SetInsertPoint(lb);
            llvm::Value* elem;
            if(elemTn=="string")        elem=ctx.builder.CreateCall(gJrArrStr(ctx),{reader,fkey,idx},"e");
            else if(elemTn=="i64"||elemTn=="u64") elem=ctx.builder.CreateCall(gJrArrI64(ctx),{reader,fkey,idx},"e");
            else if(elemTn=="f32")      elem=ctx.builder.CreateCall(gJrArrF32(ctx),{reader,fkey,idx},"e");
            else if(elemTn=="f64")      elem=ctx.builder.CreateCall(gJrArrF64(ctx),{reader,fkey,idx},"e");
            else if(elemTn=="bool"){
                llvm::Value* bv=ctx.builder.CreateCall(gJrArrBool(ctx),{reader,fkey,idx},"bv");
                elem=ctx.builder.CreateTrunc(bv,i8T(ctx));
            } else                      elem=ctx.builder.CreateCall(gJrArrI32(ctx),{reader,fkey,idx},"e");
            ListImpl::push(ctx,fieldPtr,elem);
            llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
            idx->addIncoming(ni,ctx.builder.GetInsertBlock());
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(le);
        } else if(isMapType(tn)){
            std::string valTn = mapValTn(tn);
            llvm::Type* keyTy = ctx.getLLVMType("string");
            llvm::Type* valTy = ctx.getLLVMType(valTn);
            llvm::StructType* mapTy = MapImpl::getMapStructType(ctx,keyTy,valTy);
            MapImpl::initMap(ctx,fieldPtr,keyTy,valTy,8);
            llvm::Value* cnt = ctx.builder.CreateCall(gJrMapCount(ctx),{reader,fkey},"cnt");
            llvm::BasicBlock* ph = ctx.builder.GetInsertBlock();
            llvm::BasicBlock* lc = llvm::BasicBlock::Create(ctx.context,fname+"_dmc",fn);
            llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context,fname+"_dmb",fn);
            llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context,fname+"_dme",fn);
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(lc);
            llvm::PHINode* idx = ctx.builder.CreatePHI(i32T(ctx),2,"dmi");
            idx->addIncoming(ctx.builder.getInt32(0),ph);
            ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(idx,cnt),lb,le);
            ctx.builder.SetInsertPoint(lb);
            llvm::Value* kv = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,fkey,idx},"k");
            llvm::Value* vv;
            if(valTn=="string")        vv=ctx.builder.CreateCall(gJrMapValStr(ctx),{reader,fkey,idx},"v");
            else if(valTn=="i64"||valTn=="u64") vv=ctx.builder.CreateCall(gJrMapValI64(ctx),{reader,fkey,idx},"v");
            else if(valTn=="f32")      vv=ctx.builder.CreateCall(gJrMapValF32(ctx),{reader,fkey,idx},"v");
            else if(valTn=="f64")      vv=ctx.builder.CreateCall(gJrMapValF64(ctx),{reader,fkey,idx},"v");
            else                       vv=ctx.builder.CreateCall(gJrMapValI32(ctx),{reader,fkey,idx},"v");
            MapImpl::put(ctx,fieldPtr,kv,vv,mapTy);
            llvm::Value* ni=ctx.builder.CreateAdd(idx,ctx.builder.getInt32(1),"ni");
            idx->addIncoming(ni,ctx.builder.GetInsertBlock());
            ctx.builder.CreateBr(lc);
            ctx.builder.SetInsertPoint(le);
        } else if(ctx.classes.count(tn) && ctx.classes[tn].type){
            // nested struct: sub-reader
            llvm::Value* sr = ctx.builder.CreateCall(gJrSub(ctx),{reader,fkey},"sr");
            llvm::StructType* subTy = ctx.classes[tn].type;
            for(auto& [sfn,sfm]: sortedMembers(ctx,tn)){
                llvm::Value* sfkey = ctx.builder.CreateGlobalStringPtr(sfn,"sfk");
                llvm::Value* sfp   = ctx.builder.CreateStructGEP(subTy,fieldPtr,sfm.index,"sfp");
                std::string stn = sfm.typeName;
                llvm::Value* sv;
                if(stn=="string")        sv=ctx.builder.CreateCall(gJrStr(ctx),{sr,sfkey},"sv");
                else if(stn=="i64"||stn=="u64") sv=ctx.builder.CreateCall(gJrI64(ctx),{sr,sfkey},"sv");
                else if(stn=="f32")      sv=ctx.builder.CreateCall(gJrF32(ctx),{sr,sfkey},"sv");
                else if(stn=="f64")      sv=ctx.builder.CreateCall(gJrF64(ctx),{sr,sfkey},"sv");
                else if(stn=="bool"){
                    llvm::Value* bv=ctx.builder.CreateCall(gJrBool(ctx),{sr,sfkey},"bv");
                    sv=ctx.builder.CreateTrunc(bv,i8T(ctx));
                } else                   sv=ctx.builder.CreateCall(gJrI32(ctx),{sr,sfkey},"sv");
                if(sfm.type && !sv->getType()->isIntegerTy() == sfm.type->isIntegerTy())
                    sv=ctx.builder.CreateIntCast(sv,sfm.type,true,"sc");
                ctx.builder.CreateStore(sv,sfp);
            }
            ctx.builder.CreateCall(gJrFree(ctx),{sr});
        } else {
            // scalar
            llvm::Value* v;
            if(tn=="string")        v=ctx.builder.CreateCall(gJrStr(ctx),{reader,fkey},"v");
            else if(tn=="i64"||tn=="u64") v=ctx.builder.CreateCall(gJrI64(ctx),{reader,fkey},"v");
            else if(tn=="f32")      v=ctx.builder.CreateCall(gJrF32(ctx),{reader,fkey},"v");
            else if(tn=="f64")      v=ctx.builder.CreateCall(gJrF64(ctx),{reader,fkey},"v");
            else if(tn=="bool"){
                llvm::Value* bv=ctx.builder.CreateCall(gJrBool(ctx),{reader,fkey},"bv");
                v=ctx.builder.CreateTrunc(bv,i8T(ctx));
            } else {
                v=ctx.builder.CreateCall(gJrI32(ctx),{reader,fkey},"v");
                if(fm.type && !fm.type->isIntegerTy(32))
                    v=ctx.builder.CreateIntCast(v,fm.type,true,"vc");
            }
            ctx.builder.CreateStore(v,fieldPtr);
        }
    }

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
    llvm::Type* elemTy = ctx.getLLVMType(elemTn);
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
    llvm::Value* elem;
    if(elemTn=="string")        elem=ctx.builder.CreateCall(gJrArrStr(ctx),{reader,vk,idx},"e");
    else if(elemTn=="i64"||elemTn=="u64") elem=ctx.builder.CreateCall(gJrArrI64(ctx),{reader,vk,idx},"e");
    else if(elemTn=="f32")      elem=ctx.builder.CreateCall(gJrArrF32(ctx),{reader,vk,idx},"e");
    else if(elemTn=="f64")      elem=ctx.builder.CreateCall(gJrArrF64(ctx),{reader,vk,idx},"e");
    else if(elemTn=="bool"){
        llvm::Value* bv=ctx.builder.CreateCall(gJrArrBool(ctx),{reader,vk,idx},"bv");
        elem=ctx.builder.CreateTrunc(bv,i8T(ctx));
    } else                      elem=ctx.builder.CreateCall(gJrArrI32(ctx),{reader,vk,idx},"e");
    ListImpl::push(ctx,heap,elem);
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
    std::string valTn = mapValTn(mapTn);
    llvm::Type* keyTy = ctx.getLLVMType("string");
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
    llvm::Value* kv = ctx.builder.CreateCall(gJrMapKey(ctx),{reader,ek,idx},"k");
    llvm::Value* vv;
    if(valTn=="string")        vv=ctx.builder.CreateCall(gJrMapValStr(ctx),{reader,ek,idx},"v");
    else if(valTn=="i64"||valTn=="u64") vv=ctx.builder.CreateCall(gJrMapValI64(ctx),{reader,ek,idx},"v");
    else if(valTn=="f32")      vv=ctx.builder.CreateCall(gJrMapValF32(ctx),{reader,ek,idx},"v");
    else if(valTn=="f64")      vv=ctx.builder.CreateCall(gJrMapValF64(ctx),{reader,ek,idx},"v");
    else                       vv=ctx.builder.CreateCall(gJrMapValI32(ctx),{reader,ek,idx},"v");
    MapImpl::put(ctx,heap,kv,vv,mapTy);
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

bool isSerializable(CompilerContext& ctx, const std::string& typeName){
    if(isPrimitive(typeName)) return true;
    if(ctx.enums.count(typeName)) return true;
    if(typeName=="any"||typeName=="Code"||
       typeName=="lock"||typeName=="thread_handle") return false;
    if(typeName.rfind("chan<",0)==0) return false;
    if(typeName=="file_handle"||typeName=="process") return false;
    if(isPtrType(typeName)) return isSerializable(ctx,ptrInner(typeName));
    if(isListType(typeName)) return isSerializable(ctx,listElem(typeName));
    if(isMapType(typeName)){
        std::string vt=mapValTn(typeName);
        return !vt.empty() && isSerializable(ctx,vt);
    }
    if(ctx.classes.count(typeName)){
        for(auto& [n,m]:ctx.classes[typeName].members)
            if(!isSerializable(ctx,m.typeName)) return false;
        return true;
    }
    return false;
}

llvm::Value* serialize(CompilerContext& ctx, llvm::Value* val,
                       const std::string& typeName){
    // Enums → plain integer string
    if(ctx.enums.count(typeName))
        return StringUtils::intToString(ctx,val);

    // Primitives → plain string
    if(typeName=="string") return val;
    if(typeName=="i64"||typeName=="u64") return StringUtils::int64ToString(ctx,val);
    if(typeName=="f32"||typeName=="f64") return StringUtils::floatToString(ctx,val);
    if(typeName=="bool") return StringUtils::boolToString(ctx,val);
    if(typeName=="i8"||typeName=="u8"||typeName=="i16"||typeName=="u16"||
       typeName=="i32"||typeName=="u32"){
        if(!val->getType()->isIntegerTy(32))
            val=ctx.builder.CreateIntCast(val,i32T(ctx),true,"ic");
        return StringUtils::intToString(ctx,val);
    }

    // Compound types — val is a raw pointer (alloca / heap struct)
    if(ctx.classes.count(typeName)){
        llvm::Function* fn=emitSerializeStruct(ctx,typeName);
        if(!fn) return ctx.builder.CreateGlobalStringPtr("{}");
        return ctx.builder.CreateCall(fn,{val},"ser_r");
    }
    if(isListType(typeName)){
        llvm::Function* fn=emitSerializeList(ctx,typeName);
        if(!fn) return ctx.builder.CreateGlobalStringPtr("{}");
        return ctx.builder.CreateCall(fn,{val},"ser_r");
    }
    if(isMapType(typeName)){
        llvm::Function* fn=emitSerializeMap(ctx,typeName);
        if(!fn) return ctx.builder.CreateGlobalStringPtr("{}");
        return ctx.builder.CreateCall(fn,{val},"ser_r");
    }

    return ctx.builder.CreateGlobalStringPtr("(unsupported)");
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
