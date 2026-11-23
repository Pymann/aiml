#include "hashmap_impl.h"
#include "list_impl.h"
#include "../../logger.h"

namespace HashmapImpl {

namespace {

bool isIntName(const std::string& n) {
    return n == "i8" || n == "i16" || n == "i32" || n == "i64" ||
           n == "u8" || n == "u16" || n == "u32" || n == "u64";
}

llvm::Type* i8p(CompilerContext& ctx) { return ctx.builder.getInt8Ty()->getPointerTo(); }

llvm::Value* loadHandle(CompilerContext& ctx, llvm::Value* slot) {
    return ctx.builder.CreateLoad(i8p(ctx), slot, "hm_handle");
}

// Integer (any width) -> i64; bool/unsigned without, otherwise with sign extension.
llvm::Value* toI64(CompilerContext& ctx, llvm::Value* v, bool zeroExtend) {
    auto* i64 = ctx.builder.getInt64Ty();
    if (v->getType()->isIntegerTy(64)) return v;
    if (v->getType()->isIntegerTy())
        return zeroExtend || v->getType()->isIntegerTy(1) ? ctx.builder.CreateZExt(v, i64, "hm_zext")
                                                          : ctx.builder.CreateSExt(v, i64, "hm_sext");
    if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i64, "hm_fptosi");
    if (v->getType()->isPointerTy()) return ctx.builder.CreatePtrToInt(v, i64, "hm_ptoi");
    return ctx.builder.getInt64(0);
}

// Key -> (string*, i64) according to the key kind.
void keyArgs(CompilerContext& ctx, const Kinds& k, llvm::Value* key, llvm::Value*& sk, llvm::Value*& ik) {
    if (k.key == 0) {
        sk = key->getType()->isPointerTy() ? ctx.builder.CreateBitCast(key, i8p(ctx)) : llvm::ConstantPointerNull::get(
                 llvm::cast<llvm::PointerType>(i8p(ctx)));
        ik = ctx.builder.getInt64(0);
    } else {
        sk = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p(ctx)));
        ik = toI64(ctx, key, k.keyName.size() > 0 && k.keyName[0] == 'u');
    }
}

llvm::Type* valueType(CompilerContext& ctx, const Kinds& k) {
    return static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(k.valName)));
}

// i64-Bitmuster -> Werttyp V.
llvm::Value* fromBits(CompilerContext& ctx, const Kinds& k, llvm::Value* bits) {
    llvm::Type* vt = valueType(ctx, k);
    if (k.val == 1) {
        llvm::Value* d = ctx.builder.CreateBitCast(bits, ctx.builder.getDoubleTy(), "hm_f64");
        if (vt && vt->isFloatTy()) return ctx.builder.CreateFPTrunc(d, vt, "hm_f32");
        return d;
    }
    if (vt && vt->isIntegerTy() && !vt->isIntegerTy(64)) return ctx.builder.CreateTrunc(bits, vt, "hm_trunc");
    return bits;
}

// Value V -> i64 bit pattern.
llvm::Value* toBits(CompilerContext& ctx, const Kinds& k, llvm::Value* v) {
    if (k.val == 1) {
        llvm::Value* d = v;
        if (v->getType()->isFloatTy()) d = ctx.builder.CreateFPExt(v, ctx.builder.getDoubleTy(), "hm_fpext");
        else if (v->getType()->isIntegerTy()) d = ctx.builder.CreateSIToFP(v, ctx.builder.getDoubleTy(), "hm_sitofp");
        return ctx.builder.CreateBitCast(d, ctx.builder.getInt64Ty(), "hm_bits");
    }
    return toI64(ctx, v, k.valName == "bool" || (!k.valName.empty() && k.valName[0] == 'u'));
}

bool expectArgs(CompilerContext& ctx, const std::string& methodName, const std::vector<llvm::Value*>& args, size_t n) {
    if (args.size() == n) return true;
    LOG_ERROR(ctx.fmtLoc() + "hashmap." + methodName + " expects " + std::to_string(n) + " argument(s), got " +
              std::to_string(args.size()));
    return false;
}

} // namespace

Kinds kinds(CompilerContext& ctx, const std::string& typeName, bool report) {
    Kinds k;
    std::string inner = typeName.substr(8, typeName.size() - 9);
    size_t colon = inner.find(':');
    if (colon == std::string::npos) {
        if (report) LOG_ERROR(ctx.fmtLoc() + "hashmap needs a key and a value type: hashmap<K:V>, got '" + typeName + "'");
        return k;
    }
    k.keyName = inner.substr(0, colon);
    k.valName = inner.substr(colon + 1);
    bool keyEnum = ctx.enums.count(k.keyName) > 0;
    bool valEnum = ctx.enums.count(k.valName) > 0;
    if (k.keyName == "string") k.key = 0;
    else if (isIntName(k.keyName) || keyEnum) k.key = 1;
    if (k.valName == "string") k.val = 2;
    else if (k.valName == "f32" || k.valName == "f64") k.val = 1;
    else if (isIntName(k.valName) || k.valName == "bool" || valEnum) k.val = 0;
    if (report && k.key < 0)
        LOG_ERROR(ctx.fmtLoc() + "hashmap: key type '" + k.keyName + "' is not supported (string, integer, enum)");
    if (report && k.val < 0)
        LOG_ERROR(ctx.fmtLoc() + "hashmap: value type '" + k.valName + "' is not supported (integer, bool, f32, f64, string, enum)");
    if (k.key < 0 || k.val < 0) { k.key = -1; k.val = -1; }
    return k;
}

llvm::Value* create(CompilerContext& ctx, const std::string& typeName) {
    Kinds k = kinds(ctx, typeName);
    auto* i32 = ctx.builder.getInt32Ty();
    llvm::Function* fn = ctx.getFunction("aiml_hashmap_new", i8p(ctx), {i32, i32});
    return ctx.builder.CreateCall(fn, {ctx.builder.getInt32(k.key < 0 ? 0 : k.key),
                                       ctx.builder.getInt32(k.val < 0 ? 0 : k.val)}, "hm_new");
}

void initSlot(CompilerContext& ctx, llvm::Value* slot, const std::string& typeName) {
    ctx.builder.CreateStore(create(ctx, typeName), slot);
}

llvm::Value* method(CompilerContext& ctx, const std::string& typeName, llvm::Value* slot,
                    const std::string& methodName, const std::vector<llvm::Value*>& args, bool& known) {
    known = true;
    Kinds k = kinds(ctx, typeName, false);
    if (k.key < 0) { known = false; return nullptr; }
    auto* i32 = ctx.builder.getInt32Ty();
    auto* i64 = ctx.builder.getInt64Ty();
    auto* ptr = i8p(ctx);
    llvm::Value* h = loadHandle(ctx, slot);
    llvm::Value* sk = nullptr;
    llvm::Value* ik = nullptr;

    if (methodName == "put" || methodName == "set") {
        if (!expectArgs(ctx, methodName, args, 2)) return nullptr;
        keyArgs(ctx, k, args[0], sk, ik);
        if (k.val == 2) {
            llvm::Value* v = args[1]->getType()->isPointerTy() ? ctx.builder.CreateBitCast(args[1], ptr) : args[1];
            ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_put_str", ctx.builder.getVoidTy(), {ptr, ptr, i64, ptr}),
                                   {h, sk, ik, v});
        } else {
            ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_put_bits", ctx.builder.getVoidTy(), {ptr, ptr, i64, i64}),
                                   {h, sk, ik, toBits(ctx, k, args[1])});
        }
        return nullptr;
    }
    if (methodName == "get") {
        if (!expectArgs(ctx, methodName, args, 1)) return nullptr;
        keyArgs(ctx, k, args[0], sk, ik);
        if (k.val == 2)
            return ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_get_str", ptr, {ptr, ptr, i64}), {h, sk, ik}, "hm_get_s");
        llvm::Value* bits = ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_get_bits", i64, {ptr, ptr, i64}), {h, sk, ik}, "hm_get");
        return fromBits(ctx, k, bits);
    }
    if (methodName == "add") {
        if (!expectArgs(ctx, methodName, args, 2)) return nullptr;
        if (k.val != 0) {
            LOG_ERROR(ctx.fmtLoc() + "hashmap.add only exists for integer values, not for '" + k.valName + "'");
            return nullptr;
        }
        keyArgs(ctx, k, args[0], sk, ik);
        llvm::Value* sum = ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_add", i64, {ptr, ptr, i64, i64}),
                                                  {h, sk, ik, toI64(ctx, args[1], false)}, "hm_add");
        return fromBits(ctx, k, sum);
    }
    if (methodName == "contains" || methodName == "has" || methodName == "remove") {
        if (!expectArgs(ctx, methodName, args, 1)) return nullptr;
        keyArgs(ctx, k, args[0], sk, ik);
        const char* fname = methodName == "remove" ? "aiml_hashmap_remove" : "aiml_hashmap_contains";
        return ctx.builder.CreateCall(ctx.getFunction(fname, i32, {ptr, ptr, i64}), {h, sk, ik}, "hm_flag");
    }
    if (methodName == "len" || methodName == "size") {
        return ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_len", i32, {ptr}), {h}, "hm_len");
    }
    if (methodName == "clear") {
        ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_clear", ctx.builder.getVoidTy(), {ptr}), {h});
        return nullptr;
    }
    if (methodName == "keys") {
        llvm::Type* elemTy = k.key == 0 ? ptr
            : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(k.keyName)));
        if (!elemTy) elemTy = i64;
        int elemSize = elemTy->isPointerTy() ? 8 : static_cast<int>(elemTy->getPrimitiveSizeInBits() / 8);
        if (elemSize != 4 && elemSize != 8) {
            LOG_ERROR(ctx.fmtLoc() + "hashmap.keys: key type '" + k.keyName + "' needs i32 or i64");
            return nullptr;
        }
        llvm::Value* list = nullptr;
        if (ctx.memV3) {
            list = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", ptr, {}), {}, "hm_keys_handle");
        } else {
            list = ctx.createEntryAlloca(ListImpl::getListStructType(ctx), "hm_keys_list");
        }
        ListImpl::initList(ctx, list, elemTy, 8);
        ctx.builder.CreateCall(ctx.getFunction("aiml_hashmap_keys_fill", i32, {ptr, llvm::PointerType::get(ctx.context, 0), i32}),
                               {h, list, ctx.builder.getInt32(elemSize)});
        return list;
    }
    known = false;
    LOG_ERROR(ctx.fmtLoc() + "Unknown hashmap method: " + methodName +
              " (put, get, add, contains, remove, len, clear, keys)");
    return nullptr;
}

} // namespace HashmapImpl
