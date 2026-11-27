#define DEBUG_MODE
#include "llvm_compat.h"
#include "clearance.h"
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "chan_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include <iostream>
#include <cmath>
#include <set>

namespace AIML {
namespace AST {

IRValue* Compiler::visitExpression(Expr* expr) {
    if (auto lit = dynamic_cast<LiteralExpr*>(expr)) return visitLiteral(lit);
    if (auto var = dynamic_cast<VariableExpr*>(expr)) return visitVariable(var);
    if (auto call = dynamic_cast<CallExpr*>(expr)) return visitCall(call);
    if (auto newExpr = dynamic_cast<NewExpr*>(expr)) return visitNewExpr(newExpr);
    if (auto na = dynamic_cast<NewArrayExpr*>(expr)) return visitNewArrayExpr(na);
    if (auto interp = dynamic_cast<StringInterpolationExpr*>(expr)) return visitStringInterpolation(interp);
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) return visitBinary(bin);
    if (auto unary = dynamic_cast<UnaryExpr*>(expr)) return visitUnary(unary);
    if (auto spawn = dynamic_cast<SpawnExpr*>(expr)) return visitSpawn(spawn);
    if (auto at = dynamic_cast<AtExpr*>(expr)) return visitAtExpr(at);
    if (auto blit = dynamic_cast<BufferLiteralExpr*>(expr)) return visitListLiteralValue(blit);
    return nullptr;
}

// spec/10: a list literal as a VALUE (today: call argument).  A fresh list is created
// that the literal fills; ownership lies with the caller of the evaluation.  Before,
// the literal was fanned out at the call and produced "Incorrect number of arguments"
// only in LLVM verification (backlog item 3).
IRValue* Compiler::visitListLiteralValue(BufferLiteralExpr* lit) {
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    // Element type: the expected parameter type wins (only that way is `[]` typeable),
    // otherwise derived from the first element.
    std::string elemName;
    const std::string& want = ctx.listLiteralArgType;
    if (want.rfind("list<", 0) == 0 && want.back() == '>')
        elemName = want.substr(5, want.size() - 6);
    if (elemName.empty() && !lit->elements.empty()) {
        if (auto* le = dynamic_cast<LiteralExpr*>(lit->elements[0].get())) {
            if (le->type == "string") elemName = "string";
            else if (le->value.find('.') != std::string::npos) elemName = "f64";
            else elemName = "i32";
        } else {
            elemName = declaredTypeOfExpr(lit->elements[0].get());
        }
    }
    if (elemName.empty()) {
        LOG_ERROR(ctx.fmtLoc(lit->sourceNode) + "list literal as argument: the element type cannot"
                  " be determined — the parameter must be `list<T>` (spec/10).");
        return nullptr;
    }
    llvm::Type* elemTy = ctx.isRefcountModel(elemName)
        ? static_cast<llvm::Type*>(llvm::PointerType::get(ctx.context, 0))
        : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
    if (!elemTy) {
        LOG_ERROR(ctx.fmtLoc(lit->sourceNode) + "list literal as an argument: unknown"
                  " element type '" + elemName + "' (spec/10).");
        return nullptr;
    }
    llvm::Value* handle = ctx.builder.CreateCall(
        ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "litarg_new");
    ListImpl::initList(ctx, handle, elemTy, 4);
    const std::string saved = ctx.listLiteralArgType;
    ctx.listLiteralArgType.clear();           // nested literals do not inherit the hint
    for (auto& elem : lit->elements) {
        IRValue* ev = visitExpression(elem.get());
        if (!ev) continue;
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(ev));
        llvm::Type* vt = val->getType();
        if (elemTy->isIntegerTy() && vt->isIntegerTy() && vt != elemTy)
            val = ctx.builder.CreateSExtOrTrunc(val, elemTy, "litarg_v");
        else if (elemTy->isIntegerTy() && vt->isFloatingPointTy())
            val = ctx.builder.CreateFPToSI(val, elemTy, "litarg_v");
        else if (elemTy->isFloatingPointTy() && vt->isIntegerTy())
            val = ctx.builder.CreateSIToFP(val, elemTy, "litarg_v");
        else if (elemTy->isFloatingPointTy() && vt->isFloatingPointTy() && vt != elemTy)
            val = ctx.builder.CreateFPCast(val, elemTy, "litarg_v");
        ListImpl::push(ctx, handle, val);
    }
    ctx.listLiteralArgType = saved;
    return ctx.ir_gen->wrap(handle);
}
IRValue* Compiler::visitStringInterpolation(StringInterpolationExpr* expr) {
    if (expr->parts.empty()) return ctx.ir_gen->createGlobalStringPtr("");
    
    llvm::Value* currentStr = nullptr;
    for (auto& part : expr->parts) {
        IRValue* irVal = visitExpression(part.get());
        if (!irVal) continue;
        
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(irVal));
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irVal->getType()));
        
        LOG_DEBUG("Interpolation Part Ty: " + std::to_string(ty->getTypeID()));
        if (ty->isIntegerTy()) LOG_DEBUG("  Bitwidth: " + std::to_string(ty->getIntegerBitWidth()));
        
        llvm::Value* strVal = val;
        // Type-dispatch: convert any non-string value to i8* for concatenation
        if (ty->isIntegerTy(64)) {
            strVal = StringUtils::int64ToString(ctx, val);
        } else if (ty->isIntegerTy(32)) {
            strVal = StringUtils::intToString(ctx, val);
        } else if (ty->isIntegerTy(16)) {
            strVal = StringUtils::intToString(ctx,
                ctx.builder.CreateZExt(val, ctx.builder.getInt32Ty()));
        } else if (ty->isIntegerTy(8)) {
            // bool (u8) — show as "true" / "false"
            llvm::Value* cond     = ctx.builder.CreateICmpNE(val, ctx.builder.getInt8(0));
            llvm::Value* trueStr  = ctx.builder.CreateGlobalString("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalString("false");
            strVal = ctx.builder.CreateSelect(cond, trueStr, falseStr);
        } else if (ty->isIntegerTy(1)) {
            llvm::Value* trueStr  = ctx.builder.CreateGlobalString("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalString("false");
            strVal = ctx.builder.CreateSelect(val, trueStr, falseStr);
        } else if (ty->isFloatTy()) {
            strVal = StringUtils::floatToString(ctx, val);
        } else if (ty->isDoubleTy()) {
            strVal = StringUtils::doubleToString(ctx, val);
        }
        
        // spec/61: register EVERY freshly-allocated interpolation string as an owned
        // statement temporary (a conversion result, and each concat accumulator).  A
        // single string variable/literal part (no conversion/concat) is a borrow/literal
        // and is NOT fresh → not registered (decref'ing it would double-free the source
        // or misread a literal).  The final result is consumed by the enclosing
        // decl/push/set/return (removed from the list); the intermediates are decref'd at
        // statement end — this reclaims the int→string + intermediate-concat temporaries.
        if (strVal != val && !ctx.stmtTempStack.empty()) {
            aimlTrackStringTemp(ctx, strVal);      // fresh conversion result
        }
        if (!currentStr) currentStr = strVal;
        else {
            currentStr = StringUtils::concat(ctx, currentStr, strVal);
            if (!ctx.stmtTempStack.empty()) aimlTrackStringTemp(ctx, currentStr);
        }
    }
    // spec/61 §7: an interpolation is always a FRESH owned string — a lone string part ("${x}") would
    // otherwise alias its operand while consumers adopt it as owned (UAF once the source is freed).
    if (currentStr && currentStr->getType()->isPointerTy() && !ctx.stmtTempStack.empty()) {
        auto& fr = ctx.stmtTempStack.back();
        if (std::find(fr.begin(), fr.end(), currentStr) == fr.end()) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            currentStr = ctx.builder.CreateCall(ctx.getFunction("aiml_str_dup", i8p, {i8p}), { currentStr }, "interp_own");
            aimlTrackStringTemp(ctx, currentStr);
        }
    }
    return ctx.ir_gen->wrap(currentStr);
}
IRValue* Compiler::visitNewArrayExpr(NewArrayExpr* na) {
    llvm::Type* elemTy   = ctx.getLLVMType(na->elemTypeName);
    uint64_t    elemSize = ctx.module().getDataLayout().getTypeAllocSize(elemTy);

    IRValue* countIR = visitExpression(na->countExpr.get());
    if (!countIR) { LOG_ERROR(ctx.fmtLoc() + "new_array: could not evaluate count"); return ctx.ir_gen->getInt32(0); }
    llvm::Value* count = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(countIR));
    count = ctx.toSizeType(count);

    llvm::Function* mallocFn = ctx.module().getFunction("malloc");
    if (!mallocFn) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            ctx.builder.getInt8Ty()->getPointerTo(), {ctx.getSizeType()}, false);
        mallocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "malloc", ctx.module());
    }
    (void)mallocFn;
    // spec/71 §6: the count lives in a header in front of the elements (aiml_darr_new).
    llvm::Type* i64 = ctx.builder.getInt64Ty();
    llvm::Value* ptr = ctx.builder.CreateCall(
        ctx.getFunction("aiml_darr_new", ctx.builder.getInt8Ty()->getPointerTo(), {i64, i64}),
        {ctx.builder.CreateSExtOrTrunc(count, i64), ctx.builder.getInt64(elemSize)}, "darr_ptr");
    return ctx.ir_gen->wrap(ptr);
}

IRValue* Compiler::visitNewExpr(NewExpr* newExpr) {
    std::string className = newExpr->typeName;
    // spec/29_2 §20: record `new X` here, so that X is bundled along.
    // visitNewExpr looks the class up directly in ctx.classes and thereby bypasses
    // resolveType, where the bundle tracking hangs (context.h).  The consequence was:
    // a class that occurs ONLY in a `new` — typically
    // `throw: { new: StepFailed }` — was missing from the bundle, and the receiver
    // reported `Unknown class for new`.  Measured in
    // tests/test_bundle_exec_semantics.aiml.
    if (!className.empty() && ctx.classes.count(className) &&
        !ctx.classes[className].isBuiltin &&
        !ctx.bundleTrackingTarget.empty() &&
        ctx.functions.count(ctx.bundleTrackingTarget))
        ctx.functions[ctx.bundleTrackingTarget].referencedTypes.insert(className);
    if (!ctx.inServiceSynthFn) {                                       // spec/66 §5.6
        if (ctx.serviceDecls.count(className)) ctx.serviceUses.insert(className);
        // a pure client uses the service via its stub
        if (className.size() > 6 && className.compare(className.size() - 6, 6, "Client") == 0) {
            const std::string base = className.substr(0, className.size() - 6);
            if (ctx.serviceDecls.count(base)) ctx.serviceUses.insert(base);
        }
    }
    if (!className.empty() && ctx.classes.count(className)) {
        auto& classInfo = ctx.classes[className];
        llvm::StructType* structTy = classInfo.type;

        // Use padded_size for calloc so hot-reloaded versions with new append-only
        // fields fit without reallocation (new slots are zero-initialized by calloc).
        uint64_t allocSize = (classInfo.padded_size > 0)
            ? (uint64_t)classInfo.padded_size
            : ctx.module().getDataLayout().getTypeAllocSize(structTy);

        // Ensure calloc exists.  size_t width is target-dependent (i64 on
        // x86-64, i32 on wasm32) — using ctx.getSizeType() makes the linker
        // signature match libc's.
        llvm::Type* sz = ctx.getSizeType();
        llvm::Function* callocFunc = ctx.module().getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {sz, sz}, false);
             llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module());
        }

        IRValue* rawPtr;
        // spec/61 §6.2: `{ new: T }` initialising a `shared<T>` slot allocates a refcounted %M*
        // block (not a value calloc), so it becomes a co-owned handle. The target type carries
        // the `shared<>` wrapper (deserializeTargetType is set to the decl type during init).
        bool sharedTarget = ctx.isRefcountModel(ctx.deserializeTargetType);
        // spec/61 §9.3: evaluate field initializers before allocating — one that throws leaves no
        // half-built object behind.
        std::map<std::string, llvm::Value*> preInit;
        for (auto& [name, member] : classInfo.members) {
             if (!newExpr->initValues.count(name)) continue;
             IRValue* fieldInitV = visitExpression(newExpr->initValues[name].get());
             llvm::Value* iv = fieldInitV ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV)) : nullptr;
             if (!iv) {
                 LOG_ERROR(ctx.fmtLoc(newExpr->sourceNode) + "Failed to compile AST expression for initialization of member: " + name);
                 continue;
             }
             preInit[name] = iv;
        }
        if (ctx.isRefcountModel(className) || sharedTarget) {
            // AIML v2 (doc/aiml_v2_impl_plan.md P1): a heap-field model is a refcounted MODL
            // object. Allocate the shell via aiml_model_alloc (8-byte {magic,refcount=1} header,
            // zeroed struct) instead of calloc, so incref/decref manage its lifetime and a shared
            // reference is freed exactly once. Fields are default-initialised below exactly as the
            // calloc path (aiml_model_alloc returns the zeroed data pointer past the header).
            auto* i8pTy = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* allocFn = ctx.getFunction("aiml_model_alloc", i8pTy, { sz });
            llvm::Value* obj = ctx.builder.CreateCall(allocFn, { ctx.getSizeConst(allocSize) }, "model_alloc");
            rawPtr = ctx.ir_gen->wrap(obj);
        } else {
            std::vector<IRValue*> callArgs;
            callArgs.push_back(ctx.ir_gen->wrap(ctx.getSizeConst(1)));
            callArgs.push_back(ctx.ir_gen->wrap(ctx.getSizeConst(allocSize)));
            rawPtr = ctx.ir_gen->createCall("calloc", callArgs, "calloc_call");
        }
        llvm::Value* rawPtrCast = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPtr));

        // Default-initialise every field via the SAME path the stack `decl`
        // uses (initClassContainerMembers): list/map → empty container, string →
        // "" (NOT null — calloc leaves i8* null and `.len`=strlen(null) segfaults),
        // chan/buffer → init, nested class → recurse, plus per-member defaults.
        // spec/15_models.md §"Default Field Initialization" mandates this for
        // BOTH stack decls AND `{ new: Model }` — previously `{new:}` only inited
        // explicit-default/chan/buffer, so unset string/list/map/nested fields
        // were left as null/zero.  Repro: tests/test_model_string_default_heap_new.aiml.
        initClassContainerMembers(rawPtrCast, className);

        // Apply per-instantiation overrides (`{ new: T, field: val }`) on top of
        // the defaults so an explicit init still wins.
        for (auto& [name, member] : classInfo.members) {
             auto pv = preInit.find(name);
             if (pv == preInit.end()) continue;
             llvm::Value* initVal = pv->second;
             llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtrCast, member.index, name + "_gep");
             // spec/61 §4: the field owns its string — adopt an owned statement temp, share anything else.
             if (member.typeName == "string" && initVal->getType()->isPointerTy() && ctx.lifetimeDropsEnabled) {
                 bool ownedTemp = false;
                 for (auto& fr : ctx.stmtTempStack) for (llvm::Value* t : fr) if (t == initVal) ownedTemp = true;
                 if (ownedTemp) consumeStringTemp(initVal);
                 else ctx.builder.CreateCall(ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(),
                          {llvm::PointerType::get(ctx.context, 0)}), { initVal });
             }
             ctx.builder.CreateStore(initVal, fieldPtr);
             // spec/61 §6.2: an owning ptr<T> member takes the box over — the local it came
             // from must not free it too (as for `set: [obj.field, p]`).
             if (member.typeName.rfind("ptr<", 0) == 0 && member.typeName.back() == '>')
                 consumeLocalDrop(newExpr->initValues[name].get());
        }

        // classgroup container (spec/58): run the synthesized wiring ctor so the
        // new group has all instances allocated (phase 0) + sees pointers
        // back-patched (phase 1).  Nothing else auto-invokes methods on {new:}.
        if (ctx.classGroupContainers.count(className)) {
            std::string ctorName = className + "___ctor";
            if (ctx.module().getFunction(ctorName))
                ctx.ir_gen->createCall(ctorName, { rawPtr }, "");
        }
        return rawPtr;
    }
    
    LOG_ERROR(ctx.fmtLoc(newExpr->sourceNode) + "Unknown class for new: " + className);
    return ctx.ir_gen->getInt32(0);
}
IRValue* Compiler::visitLiteral(LiteralExpr* lit) {
    if (lit->type == "i32" || lit->type == "int") {
        try {
            return ctx.ir_gen->getInt32(std::stoi(lit->value, nullptr, 0));
        } catch (...) {
            return ctx.ir_gen->getInt32(0);
        }
    } else if (lit->type == "string") {
        // Asset-Registry strict-lint (spec/44_7): catches stale asset-path
        // literals that should be stems.  Fires on:
        //   - literal starts with "resources/" OR "assets/" AND
        //   - ends with a known image / audio / shader / font extension
        // This is narrow enough to skip legitimate `tests/foo.wav`-style
        // test file paths but catches every Migration-leftover in seeds,
        // prefab specs, icon_path sets, anywhere in DataCreatures.
        {
            const std::string& s = lit->value;
            bool stale_prefix = s.rfind("resources/", 0) == 0 || s.rfind("assets/", 0) == 0;
            if (stale_prefix) {
                auto dot = s.find_last_of('.');
                bool asset_ext = false;
                if (dot != std::string::npos && dot + 1 < s.size()) {
                    std::string ext = s.substr(dot + 1);
                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                    static const std::set<std::string> kAssetExts = {
                        "png","jpg","jpeg","webp","svg",
                        "ogg","wav","mp3","flac",
                        "glsl","frag","vert",
                        "ttf","otf",
                    };
                    asset_ext = kAssetExts.count(ext) > 0;
                }
                if (asset_ext) {
                    std::string suggest;
                    size_t a = s.find_last_of('/');
                    a = (a == std::string::npos) ? 0 : a + 1;
                    size_t b = (dot == std::string::npos || dot < a) ? s.size() : dot;
                    suggest = s.substr(a, b - a);
                    LOG_ERROR(ctx.fmtLoc(lit->sourceNode) +
                              "asset-path literal not allowed: \"" + s +
                              "\" — use the stem \"" + suggest +
                              "\" (spec/44_7 Asset Registry)");
                }
            }
        }
        return ctx.ir_gen->createGlobalStringPtr(lit->value);
    } else if (lit->type == "bool") {
        return ctx.ir_gen->getInt32(lit->value == "true" ? 1 : 0);
    } else if (lit->type == "null") {
        auto* i8p = static_cast<llvm::PointerType*>(llvm::PointerType::get(ctx.context, 0));
        return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
    } else if (lit->type == "unknown") {
        // Strict parsing for unquoted scalars that are numbers or booleans
        if (lit->value == "true") return ctx.ir_gen->getInt32(1);
        if (lit->value == "false") return ctx.ir_gen->getInt32(0);

        // Float literal if it contains '.', 'e', or 'E' — emit as f32
        // But NOT if the string starts with 0x/0X (hex literal with A-F digits)
        const std::string& sv = lit->value;
        bool isHex = sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X');
        bool looksFloat = !isHex && (sv.find('.') != std::string::npos ||
                          sv.find('e') != std::string::npos ||
                          sv.find('E') != std::string::npos);
        if (looksFloat) {
            // spec/04_1 §3 + §7a: a written-out constant has no type of its own — build it in
            // the type the context gives it (ctx.floatLitTy), f32 when there is none (spec/10).
            // Parsing with std::stof first and widening afterwards dropped every digit past the
            // 8th, so an f64 never saw more than f32 precision.
            llvm::Type* litTy = ctx.floatLitTy ? ctx.floatLitTy : ctx.builder.getFloatTy();
            try {
                return ctx.ir_gen->wrap(llvm::ConstantFP::get(litTy, std::stod(sv)));
            } catch (...) {}
        }

        try {
            size_t pos = 0;
            if (isHex) {
                // Hex literals may exceed signed int range — parse as unsigned
                unsigned long ul = std::stoul(sv, &pos, 16);
                return ctx.ir_gen->getInt32(static_cast<int32_t>(static_cast<uint32_t>(ul)));
            }
            // Parse as 64-bit so integer literals beyond the i32 range (e.g.
            // 3000000000, 2147483648) are valid i64 literals instead of a parse
            // error.  Values within i32 range stay i32 (unchanged behaviour).
            long long ll = std::stoll(sv, &pos, 0);
            if (ll >= -2147483648LL && ll <= 2147483647LL) {
                return ctx.ir_gen->getInt32(static_cast<int32_t>(ll));
            }
            return ctx.ir_gen->wrap(llvm::ConstantInt::get(
                ctx.builder.getInt64Ty(), static_cast<uint64_t>(ll), /*isSigned=*/true));
        } catch (...) {
            LOG_ERROR(ctx.fmtLoc(lit->sourceNode) + "Invalid unknown literal (expected bool or number): " + sv);
            return ctx.ir_gen->getInt32(0);
        }
    }
    
    // Fallback stub
    LOG_DEBUG("Visit Unhandled Literal Type: " + lit->type + " Value: " + lit->value);
    return ctx.ir_gen->getInt32(0);
}
IRValue* Compiler::visitVariable(VariableExpr* var) {
    std::string varName = var->name;
    if (varName == "<") {
        // Standalone `<` als this-Referenz (z.B. `decode_rpc(rpc, <)`).
        varName = "this";
    } else if (varName.find("<.") == 0) {
        varName = "this." + varName.substr(2);
    }
    
    size_t dotPos = varName.find('.');
    size_t dashPos = varName.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    // spec/01: the deprecated hyphen index/slice notation (`x-0`, `x-1-4`) is removed. Reject it with
    // a pointer to the replacement forms instead of silently reinterpreting `x-0` as subtraction.
    if (isDash) {
        LOG_ERROR(ctx.fmtLoc() + "deprecated hyphen index/slice '" + varName +
            "' is removed — use { at: [" + varName.substr(0, dashPos) + ", <index>] } to read, "
            "idx: [<container>, <index>, <value>] to write, or { call: [" + varName.substr(0, dashPos) +
            ".slice, <start>, <end>] } to slice (spec/01).");
        return nullptr;
    }

    std::string baseName = (sepPos != std::string::npos) ? varName.substr(0, sepPos) : varName;
    std::string propName = (sepPos != std::string::npos) ? varName.substr(sepPos + 1) : "";

    // 0. math namespace constants (compile-time f32 literals)
    if (baseName == "math" && !propName.empty()) {
        auto f = [&](double v) -> IRValue* {
            return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), v));
        };
        if (propName == "PI")  return f(M_PI);
        if (propName == "TAU") return f(2.0 * M_PI);
        if (propName == "E")   return f(M_E);
        if (propName == "INF") return f((double)INFINITY);
        if (propName == "NAN") return f((double)NAN);
    }

    // 0b. gl namespace constants (compile-time i32 literals — OpenGL enum values)
    if (baseName == "gl" && !propName.empty()) {
        auto i = [&](int32_t v) -> IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.getInt32(v));
        };
        if (propName == "TRIANGLES")              return i(0x0004);
        if (propName == "LINES")                  return i(0x0001);
        if (propName == "POINTS")                 return i(0x0000);
        if (propName == "TRIANGLE_STRIP")         return i(0x0005);
        if (propName == "REPEAT")                 return i(0x2901);
        if (propName == "CLAMP_TO_EDGE")          return i(0x812F);
        if (propName == "MIRRORED_REPEAT")        return i(0x8370);
        if (propName == "LINEAR")                 return i(0x2601);
        if (propName == "NEAREST")                return i(0x2600);
        if (propName == "LINEAR_MIPMAP_LINEAR")   return i(0x2703);
        if (propName == "NEAREST_MIPMAP_LINEAR")  return i(0x2702);
        if (propName == "LINEAR_MIPMAP_NEAREST")  return i(0x2701);
        if (propName == "NEAREST_MIPMAP_NEAREST") return i(0x2700);
        // spec/43_5 §4/§6
        if (propName == "RGBA8")                  return i(0x8058);
        if (propName == "SRGB8_ALPHA8")           return i(0x8C43);
        if (propName == "RGBA16F")                return i(0x881A);
        if (propName == "LESS")                   return i(0x0201);
        if (propName == "LEQUAL")                 return i(0x0203);
        if (propName == "CW")                     return i(0x0900);
        if (propName == "CCW")                    return i(0x0901);
        if (propName == "ZERO")                   return i(0x0000);
        if (propName == "ONE")                    return i(0x0001);
        if (propName == "SRC_ALPHA")              return i(0x0302);
        if (propName == "ONE_MINUS_SRC_ALPHA")    return i(0x0303);
        if (propName == "POSITIVE_X")             return i(0x8515);
        if (propName == "NEGATIVE_X")             return i(0x8516);
        if (propName == "POSITIVE_Y")             return i(0x8517);
        if (propName == "NEGATIVE_Y")             return i(0x8518);
        if (propName == "POSITIVE_Z")             return i(0x8519);
        if (propName == "NEGATIVE_Z")             return i(0x851A);
    }

    // 1. Unambiguous Enum Check
    if (dotPos != std::string::npos) {
        if ((ctx.currentNs && ctx.currentNs->enums.count(baseName)) || ctx.enums.count(baseName)) {
             return resolveEnum(baseName, propName);
        }
    }

    // 2. Strict Variable Lookup
    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        // special built-in names
        if (baseName == "sys" && propName == "env") {
            // spec/23: sys.env is documented as a PROPERTY.  It used to compile to a
            // null pointer here, so `set: [m, sys.env]` succeeded and the first read
            // (.len/.contains/.get) dereferenced null.  Emit the same call as the
            // builtin form (builtin_registry.cpp, sys.builtinFuncs["env"]).
            llvm::Type* envStrTy = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::StructType* envMapTy = llvm::StructType::get(ctx.context, {
                ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(),
                ctx.builder.getInt8Ty()->getPointerTo(),
                envStrTy->getPointerTo(), envStrTy->getPointerTo()
            });
            llvm::Function* envFn = ctx.getFunction("aiml_sys_env",
                ctx.builder.getInt8Ty()->getPointerTo(), {});
            llvm::Value* envRaw = ctx.builder.CreateCall(envFn);
            return ctx.ir_gen->wrap(ctx.builder.CreateBitCast(envRaw, envMapTy->getPointerTo(), "sys_env_map"));
        }
        if (baseName == "io" && (propName == "stdout" || propName == "stderr" || propName == "stdin")) {
            // spec/23: the process's own standard stream — never a fresh fdopen per access (that
            // leaked one buffered FILE per use and lost output that was never flushed).
            int fd = (propName == "stdin") ? 0 : (propName == "stdout" ? 1 : 2);
            llvm::Function* stdFn = ctx.getFunction("aiml_io_std_stream",
                ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt32Ty()});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(stdFn, {ctx.builder.getInt32(fd)}));
        }

        // `${ns.name}` — a name with a dot that is not a variable but a
        // FUNCTION WITHOUT arguments: then it is meant like a value and is
        // called.  This is the read access to a namespace full of
        // constants (cosmos: the job parameters in `prm`, doc/spec/106
        // §106.5): `${prm.branch}` instead of `${ { call: [prm.branch] } }`.
        //
        // Only with a DOT and only with arity 0: a bare function name
        // stays a function pointer (see below), and a function with
        // arguments cannot be called without arguments.  A variable
        // always wins, because it was already found above.
        if (dotPos != std::string::npos) {
            llvm::Function* zero = nullptr;
            auto fit = ctx.functions.find(varName);
            if (fit != ctx.functions.end() && fit->second.func) zero = fit->second.func;
            // A merely DECLARED function counts too: in a
            // compiler context (spec/29_2 §14a) an earlier bundle is only
            // present as a declaration in the module — its table is gone.  Without
            // this branch `${prm.branch}` became a function pointer and was printed as a
            // string (measured: data garbage in the command).
            if (!zero) zero = ctx.module().getFunction(varName);
            if (zero && zero->arg_size() == 0 && !zero->getReturnType()->isVoidTy())
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(zero, {}, varName + "_val"));
        }

        // Check if varName is an AIML function used as a function pointer / callback.
        // Must come BEFORE getLLVMType so function names don't trigger "Unknown Type" errors.
        {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = nullptr;
            auto fit = ctx.functions.find(varName);
            if (fit != ctx.functions.end() && fit->second.func)
                fn = fit->second.func;
            if (!fn) fn = ctx.module().getFunction(varName);
            if (fn) {
                llvm::Value* fp = ctx.builder.CreateBitCast(fn, i8p, varName + "_fnptr");
                return ctx.ir_gen->wrap(fp);
            }
        }

        // Type-name used as an expression (e.g. type-cast syntax) — return 0 as placeholder
        if (ctx.getLLVMType(varName) || varName == "string" || varName == "i32" || varName == "i64" || varName == "u8" || varName == "f32" || varName == "f64")
            return ctx.ir_gen->getInt32(0);

        LOG_ERROR(ctx.fmtLoc(var->sourceNode) + "Undefined variable: " + baseName);
        return nullptr;
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);

    // list/map by-ref args: the alloca contains the pointer, not the struct.
    // Read paths must dereference once before they access the struct.
    if (sym->is_byref_arg) {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* allocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* loaded = ctx.builder.CreateLoad(i8p, allocaRaw, "byref_deref_var");
        baseAlloca = ctx.ir_gen->wrap(loaded);
    }

    if (propName.empty()) {
        // For byref args: baseAlloca is now the struct pointer.  Loading the
        // struct yields a copy of the current size/capacity/data values of the
        // CALLER struct — exactly what is needed when passing it on.
        return ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_val");
    } else {
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash) {
            llvm::Value* baseAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
            // spec/61 §5 (memV3): a list<T> local's slot holds a POINTER to the refcounted handle
            // (the {size,cap,data} struct part). The dash slice/index ops below treat baseAllocaVal as
            // the struct pointer, so load the handle first — else getSize/getElementPtr/slice read the
            // handle-pointer bytes as {size,cap,data} → garbage/crash (test_list_slice `lst-1-4`). The
            // `.len`/method path already loads the handle in its own branch.
            // spec/61 §5 storage-location invariance: memV3 container LOCALS *and* GLOBALS are
            // handle-pointer slots (is_byref_arg), so both load the handle here.
            if (ctx.memV3 && ctx.isMemV3ContainerHandle(sym->typeName)) {
                auto* i8pH = llvm::PointerType::get(ctx.context, 0);
                baseAllocaVal = ctx.builder.CreateLoad(i8pH, baseAllocaVal, "conh_dash");
            }

            bool isDynamicList = sym->typeName.find("list<") == 0;
            bool isRawArray = llvmBaseElemTy->isArrayTy();
            
            if (isDynamicList || isRawArray) {
                llvm::Type* elemType = isRawArray ? llvmBaseElemTy->getArrayElementType() : nullptr;
                if (isDynamicList) {
                    std::string elemName = sym->typeName.substr(5, sym->typeName.length() - 6);
                    elemType = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                }

                size_t dashInProp = propName.find('-');
                if (dashInProp != std::string::npos) {
                    // Slicing: varname-start-end (e.g. lst-1-4, lst-2-, lst--2)
                    if (!isDynamicList) {
                        LOG_ERROR(ctx.fmtLoc(var->sourceNode) + "Array Slicing not supported on raw arrays yet.");
                        return nullptr;
                    }

                    std::string startStr = propName.substr(0, dashInProp);
                    std::string endStr = propName.substr(dashInProp + 1);
                    
                    llvm::Value* startVal = startStr.empty() ? ctx.builder.getInt32(0) : ctx.builder.getInt32(std::stoi(startStr));
                    llvm::Value* endVal = nullptr;
                    if (endStr.empty()) endVal = ListImpl::getSize(ctx, baseAllocaVal);
                    else endVal = ctx.builder.getInt32(std::stoi(endStr));
                    
                    return ctx.ir_gen->wrap(ListImpl::slice(ctx, baseAllocaVal, startVal, endVal, elemType));
                } else {
                    // Single Element Read
                    llvm::Value* idxVal = nullptr;
                    try {
                        idxVal = ctx.builder.getInt32(std::stoi(propName));
                    } catch(...) {
                        Symbol* idxSym = resolveVariable(propName);
                        if (idxSym) {
                            IRValue* idxAlloca = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                            IRType* idxElemTy = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                            idxVal = ctx.builder.CreateLoad(static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxElemTy)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxAlloca)));
                            if (idxVal->getType()->isIntegerTy(64)) idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                        } else {
                            LOG_ERROR(ctx.fmtLoc(var->sourceNode) + "Invalid list index: " + propName);
                            return ctx.ir_gen->getInt32(0);
                        }
                    }
                    
                    llvm::Value* elemPtr = nullptr;
                    if (isRawArray) {
                        elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, baseAllocaVal, {ctx.builder.getInt32(0), idxVal});
                    } else {
                        elemPtr = ListImpl::getElementPtr(ctx, baseAllocaVal, idxVal, elemType);
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "lst_elem"));
                }
            }
        }
        return resolveProperty(sym, propName);
    }
    return ctx.ir_gen->getInt32(0); 
}
IRValue* Compiler::visitStringCompare(bool negate, llvm::Value* rawLhs, llvm::Value* rawRhs) {
    llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
    std::string suffix = negate ? "_ne" : "";
    llvm::BasicBlock* checkNullBB = llvm::BasicBlock::Create(ctx.context, "cmp_check_null" + suffix, parentFunc);
    llvm::BasicBlock* doStrcmpBB  = llvm::BasicBlock::Create(ctx.context, "cmp_do_strcmp"  + suffix, parentFunc);
    llvm::BasicBlock* mergeBB     = llvm::BasicBlock::Create(ctx.context, "cmp_merge"       + suffix, parentFunc);
    llvm::Value* isLhsNull = ctx.builder.CreateIsNull(rawLhs);
    llvm::Value* ptrEq     = ctx.builder.CreateICmpEQ(rawLhs, rawRhs);
    // ptrResult must be computed in the entry block (before any SetInsertPoint calls),
    // because PHI nodes must precede all non-PHI instructions in their basic block.
    llvm::Value* ptrResult = negate ? ctx.builder.CreateNot(ptrEq) : ptrEq;
    llvm::Value* anyNull   = ctx.builder.CreateOr(isLhsNull, ctx.builder.CreateIsNull(rawRhs));
    ctx.builder.CreateCondBr(anyNull, checkNullBB, doStrcmpBB);
    ctx.builder.SetInsertPoint(checkNullBB);
    ctx.builder.CreateBr(mergeBB);
    ctx.builder.SetInsertPoint(doStrcmpBB);
    llvm::Function* strcmpFunc = ctx.module().getFunction("strcmp");
    if (!strcmpFunc) strcmpFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strcmp", ctx.module());
    llvm::Value* cmpRes    = ctx.builder.CreateCall(strcmpFunc, {rawLhs, rawRhs});
    llvm::Value* strResult = negate
        ? ctx.builder.CreateICmpNE(cmpRes, ctx.builder.getInt32(0))
        : ctx.builder.CreateICmpEQ(cmpRes, ctx.builder.getInt32(0));
    ctx.builder.CreateBr(mergeBB);
    ctx.builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, "strcmp_res" + suffix);
    phi->addIncoming(ptrResult, checkNullBB);
    phi->addIncoming(strResult, doStrcmpBB);
    return ctx.ir_gen->wrap(phi);
}

// coerceToBool — normalize any scalar/pointer to i1
static llvm::Value* coerceToBool(CompilerContext& ctx, llvm::Value* v) {
    llvm::Type* ty = v->getType();
    if (ty->isIntegerTy(1)) return v;
    if (ty->isIntegerTy())  return ctx.builder.CreateICmpNE(v, llvm::ConstantInt::get(ty, 0), "tobool");
    if (ty->isFloatTy() || ty->isDoubleTy())
        return ctx.builder.CreateFCmpONE(v, llvm::ConstantFP::get(ty, 0.0), "tobool");
    if (ty->isPointerTy())
        return ctx.builder.CreateICmpNE(v, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(ty)), "tobool");
    return nullptr;
}

// spec/04_1 §7: { cast: [value, type] } — exactly the conversions of the table.
// spec/13 §6 + spec/04_1 §7.  ONE float→integer conversion for all store paths
// (member store, idx element, decl init, local set, default value) AND for `cast` itself:
// two inputs have no integer image and would yield poison with a bare CreateFPToSI,
// so they are fixed here -- NaN → 0, out of range clamped.  Clamping happens
// BEFORE the conversion, because afterwards one would be comparing poison.
llvm::Value* coerceFloatToIntegral(CompilerContext& ctx, llvm::Value* v, llvm::Type* dstTy,
                                   const std::string& dstName) {
    if (!v || !dstTy || !v->getType()->isFloatingPointTy() || !dstTy->isIntegerTy()) return v;
    auto& bld = ctx.builder;
    llvm::Type* from = v->getType();
    // `bool` is stored as i8: the same answer as `cast` and as a condition
    // (ONE, so NaN is false) -- not the clamping further below.
    if (dstName == "bool" || dstTy->isIntegerTy(1)) {
        llvm::Value* nz = bld.CreateFCmpONE(v, llvm::ConstantFP::get(from, 0.0), "st_f2b");
        return dstTy->isIntegerTy(1) ? nz : bld.CreateZExt(nz, dstTy, "st_f2b_w");
    }
    unsigned bits = dstTy->getIntegerBitWidth();
    bool uns = dstName == "u8" || dstName == "u16" || dstName == "u32" || dstName == "u64"
            || dstName == "usize" || dstName == "size_t";
    double lo = uns ? 0.0 : -std::ldexp(1.0, (int)bits - 1);
    double hi = uns ? std::ldexp(1.0, (int)bits) - 1.0 : std::ldexp(1.0, (int)bits - 1) - 1.0;
    llvm::Value* zero  = llvm::ConstantFP::get(from, 0.0);
    llvm::Value* isNan = bld.CreateFCmpUNO(v, v, "st_nan");
    llvm::Value* safe  = bld.CreateSelect(isNan, zero, v, "st_nan0");
    // Form the limits in the SOURCE type: for f32 they are not exactly representable and are
    // rounded -- that is exactly why we clamp and do not just compare.
    llvm::Value* loV = llvm::ConstantFP::get(from, lo);
    llvm::Value* hiV = llvm::ConstantFP::get(from, hi);
    safe = bld.CreateSelect(bld.CreateFCmpOLT(safe, loV), loV, safe, "st_clamp_lo");
    safe = bld.CreateSelect(bld.CreateFCmpOGT(safe, hiV), hiV, safe, "st_clamp_hi");
    llvm::Value* conv = uns ? bld.CreateFPToUI(safe, dstTy, "st_f2u")
                            : bld.CreateFPToSI(safe, dstTy, "st_f2i");
    // After clamping the rounded upper limit can still overflow → pin it down.
    llvm::Value* atHi = bld.CreateFCmpOGE(safe, hiV, "st_at_hi");
    llvm::Value* hiI  = llvm::ConstantInt::get(dstTy, uns ? llvm::APInt::getMaxValue(bits)
                                                          : llvm::APInt::getSignedMaxValue(bits));
    return bld.CreateSelect(atHi, hiI, conv, "st_f2i_sat");
}

IRValue* Compiler::visitCast(BinaryExpr* bin) {
    auto* target = dynamic_cast<VariableExpr*>(bin->rhs.get());
    if (!target) {
        LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "cast: second operand must be a type name");
        return nullptr;
    }
    IRValue* src = visitExpression(bin->lhs.get());
    if (!src) return nullptr;
    llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(src));
    llvm::Type* from = val->getType();
    const std::string& to = target->name;

    // spec/65 F65-14: a restricted session gets NO pointer cast.
    // With pointer arithmetic, code in the same process reaches every byte, regardless
    // of any naming rule — including the host's runtime state.  A level
    // check that allows this checks the front door of a house without walls.
    if (aiml::clearance::restricted()) {
        const bool to_ptr  = (to.rfind("ptr<", 0) == 0) || to == "ptr";
        const bool from_ptr = from->isPointerTy();
        static const std::set<std::string> kIntTypes = {
            "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "isize"
        };
        if (to_ptr || (from_ptr && kIntTypes.count(to))) {
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode) +
                      "clearance-dialect: `cast:` to/from a pointer is not allowed in a "
                      "restricted session (spec/65 F65-14) — "
                      "pointer arithmetic reaches every byte in the same process.");
            return nullptr;
        }
    }
    auto& bld = ctx.builder;
    auto unsupported = [&]() -> IRValue* {
        std::string fromName;
        llvm::raw_string_ostream os(fromName);
        from->print(os);
        LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "cast: unsupported conversion " + os.str() + " -> " + to
                  + " (spec/04_1 §7)");
        return nullptr;
    };
    bool fromBool  = from->isIntegerTy(1) || from->isIntegerTy(8);   // bool is stored as i8
    bool fromInt   = from->isIntegerTy(32) || from->isIntegerTy(64);
    bool fromFloat = from->isFloatTy() || from->isDoubleTy();

    // float → integer: the same conversion as every store path (coerceFloatToIntegral)
    auto floatToInt = [&](llvm::Type* dst) -> IRValue* {
        return ctx.ir_gen->wrap(coerceFloatToIntegral(ctx, val, dst, to));
    };

    if (to == "i32" || to == "i64") {
        llvm::Type* dst = (to == "i32") ? bld.getInt32Ty() : bld.getInt64Ty();
        if (fromBool)  return ctx.ir_gen->wrap(bld.CreateZExt(val, dst, "cast_b2i"));
        if (fromInt)   return ctx.ir_gen->wrap(bld.CreateSExtOrTrunc(val, dst, "cast_i2i"));
        if (fromFloat) return floatToInt(dst);
        return unsupported();
    }
    if (to == "f32" || to == "f64") {
        llvm::Type* dst = (to == "f32") ? bld.getFloatTy() : bld.getDoubleTy();
        /* bool first: it is stored as i8, so SIToFP on the
         * raw value would mostly be right, but true is guaranteed 0/1 only via
         * the zero check. */
        if (fromBool) {
            llvm::Value* one = bld.CreateICmpNE(val, llvm::ConstantInt::get(from, 0), "cast_b_nz");
            return ctx.ir_gen->wrap(bld.CreateUIToFP(one, dst, "cast_b2f"));
        }
        if (fromInt)   return ctx.ir_gen->wrap(bld.CreateSIToFP(val, dst, "cast_i2f"));
        if (from == dst) return src;
        if (fromFloat) return ctx.ir_gen->wrap(from->isDoubleTy()
                               ? bld.CreateFPTrunc(val, dst, "cast_f2f_t")
                               : bld.CreateFPExt(val, dst, "cast_f2f_e"));
        return unsupported();
    }
    if (to == "bool") {
        if (fromBool) return ctx.ir_gen->wrap(bld.CreateICmpNE(val, llvm::ConstantInt::get(from, 0), "cast_b2b"));
        if (fromInt)  return ctx.ir_gen->wrap(bld.CreateICmpNE(val, llvm::ConstantInt::get(from, 0), "cast_i2b"));
        /* ONE, not UNE: NaN is therefore false — the same answer that
         * coerceToBool gives a condition.  `cast` and `if` must not judge
         * the same value differently (spec/04_1 §7). */
        if (fromFloat) return ctx.ir_gen->wrap(bld.CreateFCmpONE(val, llvm::ConstantFP::get(from, 0.0), "cast_f2b"));
        return unsupported();
    }
    return unsupported();
}

IRValue* Compiler::visitBinary(BinaryExpr* bin) {
    // `{ at: [container, index] }` in an expression/interpolation context (e.g. `${ { at: [x, i] } }`)
    // is produced by the parser as a BinaryExpr op="at", not an AtExpr. Route it to element-access
    // semantics so the dash-free read form `{at:[x,i]}` works uniformly for lists AND arrays, in bare
    // code and inside string interpolation (dash-index eradication). Must run before lhs/rhs are
    // evaluated as values — visitAtExpr needs the container as an lvalue/variable, not a loaded value.
    if (bin->op == "at") {
        AtExpr tmp;
        tmp.sourceNode = bin->sourceNode;
        tmp.container = std::move(bin->lhs);
        tmp.indices.push_back(std::move(bin->rhs));
        IRValue* r = visitAtExpr(&tmp);
        bin->lhs = std::move(tmp.container);
        bin->rhs = std::move(tmp.indices[0]);
        return r;
    }
    // spec/04_1 §4: `and` / `or` evaluate SHORT-circuit — "if first operand is false,
    // remaining operands are not evaluated".  That MUST be here, before the
    // evaluation of the operands: until 2026-10-15 both sides were evaluated
    // and only then combined bitwise (CreateAnd/CreateOr), so the spec rule
    // was simply not implemented.  That made the language's most common guard pattern
    // ineffective:
    //     { '&&': [{ pneq: [p, null] }, { gt: [{ call: [p.has] }, 0] }] }
    // The guard held, the second operand ran anyway → SIGSEGV (measured
    // in the cosmos client, addr=0x8).  `equiv`/`xor` need both sides and
    // stay below.
    if (bin->op == "and" || bin->op == "&&" || bin->op == "or" || bin->op == "||") {
        const bool isAnd = (bin->op == "and" || bin->op == "&&");
        IRValue* lhsV = visitExpression(bin->lhs.get());
        if (!lhsV) return nullptr;
        llvm::Value* lb = coerceToBool(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhsV)));
        if (!lb) {
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode, bin->sourceLine) +
                "Cannot apply logical operator '" + bin->op + "' to non-scalar type");
            return nullptr;
        }
        llvm::BasicBlock* decideBB = ctx.builder.GetInsertBlock();
        llvm::Function* fnOwner = decideBB ? decideBB->getParent() : nullptr;
        if (!fnOwner) return nullptr;
        llvm::BasicBlock* rhsBB  = llvm::BasicBlock::Create(ctx.context, isAnd ? "and.rhs" : "or.rhs", fnOwner);
        llvm::BasicBlock* endBB  = llvm::BasicBlock::Create(ctx.context, isAnd ? "and.end" : "or.end", fnOwner);
        // AND: only continue if the left is true.  OR: only if the left is false.
        if (isAnd) ctx.builder.CreateCondBr(lb, rhsBB, endBB);
        else       ctx.builder.CreateCondBr(lb, endBB, rhsBB);
        ctx.builder.SetInsertPoint(rhsBB);
        IRValue* rhsV = visitExpression(bin->rhs.get());
        if (!rhsV) return nullptr;
        llvm::Value* rb = coerceToBool(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhsV)));
        if (!rb) {
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode, bin->sourceLine) +
                "Cannot apply logical operator '" + bin->op + "' to non-scalar type");
            return nullptr;
        }
        // The right operand may itself have created blocks — the branch
        // comes from the CURRENT block, not from rhsBB.
        llvm::BasicBlock* rhsEndBB = ctx.builder.GetInsertBlock();
        if (!aiml_llvm::terminator(rhsEndBB)) ctx.builder.CreateBr(endBB);
        ctx.builder.SetInsertPoint(endBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, isAnd ? "and" : "or");
        phi->addIncoming(ctx.builder.getInt1(!isAnd), decideBB);   // AND→false, OR→true
        phi->addIncoming(rb, rhsEndBB);
        return ctx.ir_gen->wrap(phi);
    }
    if (bin->op == "cast") return visitCast(bin);
    IRValue* lhs = visitExpression(bin->lhs.get());
    IRValue* rhs = visitExpression(bin->rhs.get());
    if (!lhs || !rhs) return nullptr;
    // spec/43_0 operators: a user call returning a math struct arrives as the pointer to its
    // result temp (method_struct_tmp) → operate on the value.
    auto loadMathTemp = [&](IRValue* side, Expr* expr) -> IRValue* {
        if (!dynamic_cast<CallExpr*>(expr)) return side;
        auto* tmp = llvm::dyn_cast_or_null<llvm::AllocaInst>(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(side)));
        auto* st = tmp ? llvm::dyn_cast<llvm::StructType>(tmp->getAllocatedType()) : nullptr;
        if (!st || !st->hasName()) return side;
        std::string nm = st->getName().str();
        if (nm != "vec2" && nm != "vec3" && nm != "vec4" && nm != "quat" && nm != "mat4") return side;
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(st, tmp, "math_call_val"));
    };
    lhs = loadMathTemp(lhs, bin->lhs.get());
    rhs = loadMathTemp(rhs, bin->rhs.get());

    llvm::Type* lTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(lhs->getType()));
    llvm::Type* rTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(rhs->getType()));
    
    if (lTy != rTy) {
        if (lTy->isIntegerTy() && rTy->isIntegerTy()) {
            unsigned lBits = lTy->getIntegerBitWidth();
            unsigned rBits = rTy->getIntegerBitWidth();
            if (lBits < rBits) {
                lhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
            } else {
                rhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
            }
        } else if (lTy->isPointerTy() && rTy->isIntegerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        } else if (rTy->isPointerTy() && lTy->isIntegerTy()) {
            lhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
        } else if (lTy->isPointerTy() && rTy->isPointerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        } else if ((lTy->isFloatingPointTy() || lTy->isIntegerTy()) &&
                   (rTy->isFloatingPointTy() || rTy->isIntegerTy())) {
            /* spec/04_1 §7a: a LITERAL has no type of its own — it is formed in the type
             * of the other operand.  That converts nothing at runtime
             * (the constant is created directly in the target type), so it is not an
             * implicit cast.  Two TYPED values of different width, on the other hand,
             * are an error: whether f64→f32 (precision) or f32→i32 (fraction)
             * may be lost is the author's decision.
             *
             * Without this branch the pair fell through unchanged and reached
             * LLVM as an instruction with two operand types → the module check
             * failed ("Both operands ... not of the same type!"), instead of giving
             * the message promised here. */
            auto* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
            auto* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));

            /* Form the constant anew in the target type; nullptr if it is none. */
            auto asFloatConst = [&](llvm::Value* v, llvm::Type* dstTy) -> llvm::Value* {
                if (auto* cfp = llvm::dyn_cast<llvm::ConstantFP>(v)) {
                    llvm::APFloat apf = cfp->getValueAPF();
                    bool lost = false;
                    apf.convert(dstTy->getFltSemantics(), llvm::APFloat::rmNearestTiesToEven, &lost);
                    return llvm::ConstantFP::get(dstTy->getContext(), apf);
                }
                if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v))
                    return llvm::ConstantFP::get(dstTy, (double)ci->getSExtValue());
                return nullptr;
            };

            bool handled = false;
            /* A written-out literal is formed from its SOURCE TEXT in the target type.
             * The route via asFloatConst() only sees the constant already rounded to f32:
             * `{ gt: [wert_f64, 0.1234567891] }` compared against
             * 0.123456791043282, because the digits were already gone before widening. */
            if (lTy->isFloatingPointTy()) {
                if (llvm::Constant* c = floatLiteralIn(bin->rhs.get(), lTy)) { rhs = ctx.ir_gen->wrap(static_cast<llvm::Value*>(c)); handled = true; }
                else if (llvm::Value* c = asFloatConst(rv, lTy)) { rhs = ctx.ir_gen->wrap(c); handled = true; }
            }
            if (!handled && rTy->isFloatingPointTy()) {
                if (llvm::Constant* c = floatLiteralIn(bin->lhs.get(), rTy)) { lhs = ctx.ir_gen->wrap(static_cast<llvm::Value*>(c)); handled = true; }
                else if (llvm::Value* c = asFloatConst(lv, rTy)) { lhs = ctx.ir_gen->wrap(c); handled = true; }
            }
            if (!handled) {
                auto typeName = [](llvm::Type* t) -> const char* {
                    if (t->isDoubleTy())      return "f64";
                    if (t->isFloatTy())       return "f32";
                    if (t->isIntegerTy(64))   return "i64";
                    if (t->isIntegerTy(32))   return "i32";
                    if (t->isIntegerTy(1) || t->isIntegerTy(8)) return "bool";
                    return "numeric";
                };
                LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "operator '" + bin->op
                          + "': operands have different types (" + typeName(lTy) + " vs "
                          + typeName(rTy) + ") — convert one side explicitly with "
                          + "{ cast: [value, " + typeName(lTy) + "] } (spec/04_1 §3, §7a)");
                return nullptr;
            }
        }
        // The operation dispatch below must see the coerced LLVM types, not the
        // original pre-cast pair, otherwise CreateICmp* can still observe
        // mismatched operands even though lhs/rhs were already normalized.
        lTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(lhs->getType()));
        rTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(rhs->getType()));
    }
    
    // ── Math struct operator overloading ──────────────────────────────────
    if (lTy->isStructTy()) {
        auto* structTy = static_cast<llvm::StructType*>(lTy);
        std::string sn = structTy->getName().str();
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));

        // Store helper
        auto store = [&](llvm::Value* val, llvm::Type* ty) -> llvm::Value* {
            llvm::AllocaInst* a = ctx.createEntryAlloca(ty); ctx.builder.CreateStore(val,a); return a;
        };
        auto callVoidFn = [&](const char* nm, std::vector<llvm::Type*> pts, std::vector<llvm::Value*> xs, llvm::Type* outTy) -> llvm::Value* {
            llvm::Function* fn = ctx.module().getFunction(nm);
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),llvm::Function::ExternalLinkage,nm,ctx.module());
            llvm::AllocaInst* out = ctx.createEntryAlloca(outTy);
            xs.push_back(out);
            ctx.builder.CreateCall(fn,xs);
            return ctx.builder.CreateLoad(outTy,out);
        };
        auto op = llvm::PointerType::get(ctx.context,0);

        bool isVec2=(sn=="vec2"), isVec3=(sn=="vec3"), isVec4=(sn=="vec4"), isQuat=(sn=="quat"), isMat4=(sn=="mat4");

        if (isVec2 || isVec3 || isVec4 || isQuat) {
            // Check if rhs is f32/scalar (for scale/div)
            bool rhsIsScalar = rTy->isFloatTy() || rTy->isDoubleTy() || rTy->isIntegerTy();
            if (rhsIsScalar) rv = rTy->isFloatTy() ? rv : rTy->isDoubleTy() ?
                ctx.builder.CreateFPTrunc(rv, ctx.builder.getFloatTy()) :
                ctx.builder.CreateSIToFP(rv, ctx.builder.getFloatTy());

            std::string pre = "aiml_math_" + sn + "_";
            if (bin->op == "+" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"add").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "-" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"sub").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "*" && !rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"mul").c_str(),{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            if (bin->op == "*" &&  rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"scale").c_str(),{op,ctx.builder.getFloatTy(),op},{store(lv,structTy),rv},structTy));
            if (bin->op == "/" &&  rhsIsScalar) return ctx.ir_gen->wrap(callVoidFn((pre+"div").c_str(),{op,ctx.builder.getFloatTy(),op},{store(lv,structTy),rv},structTy));
            // spec/43_0: `neq` is the negation of `eq` — the same runtime function, only
            // the other comparison direction.  Before, `neq` fell into the general struct path
            // and ended in "Cannot apply eq/neq to struct type without __eq__" (backlog 7).
            if (bin->op == "eq" || bin->op == "neq") {
                llvm::Function* fn=ctx.module().getFunction((pre+"eq").c_str());
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(),{op,op},false),llvm::Function::ExternalLinkage,(pre+"eq").c_str(),ctx.module());
                llvm::Value* res=ctx.builder.CreateCall(fn,{store(lv,structTy),store(rv,structTy)});
                return ctx.ir_gen->wrap(bin->op == "neq"
                    ? ctx.builder.CreateICmpEQ(res,ctx.builder.getInt32(0))
                    : ctx.builder.CreateICmpNE(res,ctx.builder.getInt32(0)));
            }
        }

        if (isMat4) {
            // mat4 * mat4
            if (rTy == structTy) {
                return ctx.ir_gen->wrap(callVoidFn("aiml_math_mat4_mul",{op,op,op},{store(lv,structTy),store(rv,structTy)},structTy));
            }
            // mat4 * vec4
            llvm::Type* vec4Ty = ctx.getLLVMType("vec4");
            if (rTy == vec4Ty) {
                return ctx.ir_gen->wrap(callVoidFn("aiml_math_mat4_mul_vec4",{op,op,op},{store(lv,structTy),store(rv,vec4Ty)},vec4Ty));
            }
        }
    }

    // Float arithmetic and comparison — must come before integer fallback
    if (lTy->isFloatTy() || lTy->isDoubleTy()) {
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        if (bin->op == "+" || bin->op == "add") return ctx.ir_gen->wrap(ctx.builder.CreateFAdd(lv, rv));
        if (bin->op == "-" || bin->op == "sub") return ctx.ir_gen->wrap(ctx.builder.CreateFSub(lv, rv));
        if (bin->op == "*" || bin->op == "mul") return ctx.ir_gen->wrap(ctx.builder.CreateFMul(lv, rv));
        if (bin->op == "/" || bin->op == "div") return ctx.ir_gen->wrap(ctx.builder.CreateFDiv(lv, rv));
        if (bin->op == "eq")                         return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOEQ(lv, rv));
        if (bin->op == "neq")                        return ctx.ir_gen->wrap(ctx.builder.CreateFCmpONE(lv, rv));
        if (bin->op == "lt")                         return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOLT(lv, rv));
        if (bin->op == "gt")                         return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOGT(lv, rv));
        if (bin->op == "le"  || bin->op == "lte")    return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOLE(lv, rv));
        if (bin->op == "ge"  || bin->op == "gte")    return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOGE(lv, rv));
        if (bin->op == "peq")                        return ctx.ir_gen->wrap(ctx.builder.CreateFCmpOEQ(lv, rv));
        if (bin->op == "pneq")                       return ctx.ir_gen->wrap(ctx.builder.CreateFCmpONE(lv, rv));
    }

    if (bin->op == "+") return ctx.ir_gen->createAdd(lhs, rhs);
    if (bin->op == "-") return ctx.ir_gen->createSub(lhs, rhs);
    if (bin->op == "*") return ctx.ir_gen->createMul(lhs, rhs);
    if (bin->op == "/") return ctx.ir_gen->createSDiv(lhs, rhs);
    if (bin->op == "%") return ctx.ir_gen->createSRem(lhs, rhs);

    // spec/04_1 §5: bitwise operators on integers (operands already widened to one type above).
    if (bin->op == "band" || bin->op == "bor" || bin->op == "bxor" ||
        bin->op == "shl" || bin->op == "shr" || bin->op == "ushr") {
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        if (!lv->getType()->isIntegerTy() || !rv->getType()->isIntegerTy()) {
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "bitwise operator '" + bin->op + "' needs integer operands");
            return nullptr;
        }
        if (rv->getType() != lv->getType()) rv = ctx.builder.CreateSExtOrTrunc(rv, lv->getType(), "bit_rhs");
        // spec/04_1: the shift amount must lie in [0, width).  A CONSTANT outside
        // is statically recognisable and therefore an error; a dynamic one is explicitly masked
        // to the width.  Before, the C UB came through: `{ shl: [6, -4] }` yielded garbage
        // (measured 372022024), and something else on a different target (backlog item 2).
        if (bin->op == "shl" || bin->op == "shr" || bin->op == "ushr") {
            const unsigned bits = lv->getType()->getIntegerBitWidth();
            if (auto* amountConst = llvm::dyn_cast<llvm::ConstantInt>(rv)) {
                const llvm::APInt& amount = amountConst->getValue();
                if (amount.isNegative() || amount.uge(bits)) {
                    LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "shift amount " +
                              std::to_string(amount.getSExtValue()) + " lies outside [0, " +
                              std::to_string(bits) + ") for '" + bin->op + "' — a constant" +
                              " amount must be valid (spec/04_1).");
                    return nullptr;
                }
            } else {
                rv = ctx.builder.CreateAnd(rv, llvm::ConstantInt::get(lv->getType(), bits - 1),
                                           "shift_mask");
            }
        }
        if (bin->op == "band") return ctx.ir_gen->wrap(ctx.builder.CreateAnd(lv, rv, "band"));
        if (bin->op == "bor")  return ctx.ir_gen->wrap(ctx.builder.CreateOr(lv, rv, "bor"));
        if (bin->op == "bxor") return ctx.ir_gen->wrap(ctx.builder.CreateXor(lv, rv, "bxor"));
        if (bin->op == "shl")  return ctx.ir_gen->wrap(ctx.builder.CreateShl(lv, rv, "shl"));
        if (bin->op == "shr")  return ctx.ir_gen->wrap(ctx.builder.CreateAShr(lv, rv, "shr"));
        return ctx.ir_gen->wrap(ctx.builder.CreateLShr(lv, rv, "ushr"));
    }

    // `peq` / `pneq` — raw pointer/integer identity comparison (address equality).
    // Use these when you explicitly want to compare pointer addresses, not content.
    if (bin->op == "peq" || bin->op == "pneq") {
        bool isEq = (bin->op == "peq");
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        // Interface pointers are fat pointers (AimlFatPtr_<Iface> = {obj,
        // vtable}).  Address identity / null comparison is on the OBJECT slot
        // (field 0), not the whole struct — otherwise we'd emit ICmp on a
        // struct type and crash LLVM.  Repro: tests/test_peq_iface_ptr_null.aiml
        auto extractFatObj = [&](llvm::Value* v) -> llvm::Value* {
            if (v && v->getType()->isStructTy()) {
                auto* st = llvm::cast<llvm::StructType>(v->getType());
                if (st->hasName() && st->getName().starts_with("AimlFatPtr_"))
                    return ctx.builder.CreateExtractValue(v, 0, "fatptr_obj");
            }
            return v;
        };
        lv = extractFatObj(lv);
        rv = extractFatObj(rv);
        llvm::Type* rawLTy = lv->getType();
        llvm::Type* rawRTy = rv->getType();

        if (rawLTy != rawRTy) {
            if (rawLTy->isIntegerTy() && rawRTy->isIntegerTy()) {
                unsigned lBits = rawLTy->getIntegerBitWidth();
                unsigned rBits = rawRTy->getIntegerBitWidth();
                if (lBits < rBits) lv = ctx.builder.CreateSExt(lv, rawRTy);
                else rv = ctx.builder.CreateSExt(rv, rawLTy);
            } else if (rawLTy->isPointerTy() && rawRTy->isIntegerTy()) {
                rv = ctx.builder.CreateIntToPtr(rv, rawLTy);
            } else if (rawRTy->isPointerTy() && rawLTy->isIntegerTy()) {
                lv = ctx.builder.CreateIntToPtr(lv, rawRTy);
            } else if (rawLTy->isPointerTy() && rawRTy->isPointerTy()) {
                rv = ctx.builder.CreateBitCast(rv, rawLTy);
            }
        }
        llvm::Value* cmp = isEq ? ctx.builder.CreateICmpEQ(lv, rv)
                                : ctx.builder.CreateICmpNE(lv, rv);
        return ctx.ir_gen->wrap(cmp);
    }

    if (bin->op == "eq" || bin->op == "neq") {
        bool isNeq = (bin->op == "neq");

        // Helper: try to dispatch __eq__ via alloca pointers.
        // Returns nullptr if no __eq__ found for the given typeName.
        auto tryEqDispatch = [&](const std::string& typeName,
                                 llvm::Value* thisRaw, llvm::Value* otherRaw) -> IRValue* {
            if (typeName.empty()) return nullptr;
            std::string eqFnName = typeName + "___eq__";
            llvm::Function* eqFn = ctx.module().getFunction(eqFnName);
            if (!eqFn || eqFn->arg_size() != 2) return nullptr;
            if (thisRaw->getType() != eqFn->getFunctionType()->getParamType(0))
                thisRaw = ctx.builder.CreateBitCast(thisRaw, eqFn->getFunctionType()->getParamType(0));
            if (otherRaw->getType() != eqFn->getFunctionType()->getParamType(1))
                otherRaw = ctx.builder.CreateBitCast(otherRaw, eqFn->getFunctionType()->getParamType(1));
            llvm::Value* result = ctx.builder.CreateCall(eqFn, {thisRaw, otherRaw});
            llvm::Value* cmp = isNeq
                ? ctx.builder.CreateICmpEQ(result, ctx.builder.getInt32(0))
                : ctx.builder.CreateICmpNE(result, ctx.builder.getInt32(0));
            return ctx.ir_gen->wrap(cmp);
        };

        // Struct type: class instances stored directly on stack (not behind a pointer).
        // Use alloca pointers as 'this' and 'other' for __eq__ dispatch.
        if (lTy->isStructTy()) {
            auto* varLhs = dynamic_cast<VariableExpr*>(bin->lhs.get());
            auto* varRhs = dynamic_cast<VariableExpr*>(bin->rhs.get());
            if (varLhs && varRhs) {
                Symbol* sLhs = resolveVariable(varLhs->name);
                Symbol* sRhs = resolveVariable(varRhs->name);
                if (sLhs && sRhs) {
                    llvm::Value* allocaL = sLhs->alloca_inst;
                    llvm::Value* allocaR = sRhs->alloca_inst;
                    if (!allocaL && sLhs->ir_alloca_inst)
                        allocaL = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sLhs->ir_alloca_inst));
                    if (!allocaR && sRhs->ir_alloca_inst)
                        allocaR = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sRhs->ir_alloca_inst));
                    if (allocaL && allocaR) {
                        if (auto* res = tryEqDispatch(sLhs->typeName, allocaL, allocaR))
                            return res;
                    }
                }
            }
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "Cannot apply eq/neq to struct type without __eq__: " +
                      lTy->getStructName().str());
            return ctx.ir_gen->getInt32(0);
        }

        // Pointer type: try __eq__ dispatch first. For `string` fall back
        // to byte-wise string compare; for any other opaque-handle pointer
        // type (prefab_instance, gl_texture, mirror_session, window_handle,
        // socket_handle, …) compare by pointer identity. Without this
        // distinction the old code unconditionally ran strcmp on the raw
        // bytes of the pointee — so two distinct prefab handles whose
        // structs began with the same byte pattern (often a leading NUL)
        // compared as equal, breaking e.g. drag-and-drop "which tile is
        // this?" lookups.
        if (lTy->isPointerTy() && rTy->isPointerTy()) {
            // Resolve type-name for VariableExpr (incl. dotted member-access
            // like `tick.terrain_id`) and LiteralExpr.  Without the dot path a
            // string-typed struct field would pass through as unknown and fall back to
            // pointer identity instead of strcmp → breaks spec/39 §0
            // ("For string pointers: byte-by-byte content comparison").
            auto resolveTypeName = [&](Expr* expr) {
                std::string name;
                if (auto* varEx = dynamic_cast<VariableExpr*>(expr)) {
                    // A member of the CURRENT object appears in the source as `<.field`
                    // (or `<`), not as `this.field`.  Without this rewrite
                    // resolveVariable("<") failed, the type name stayed empty and a comparison
                    // of TWO string members of the same class fell back to pointer identity
                    // — silently "always unequal", although both types are statically
                    // known.  (cai Wikipedia reader: `{ eq: [<.tag_name, <.skip_tag] }`
                    // never matched, 10,000 articles discarded.)  `this.field` already worked.
                    std::string vname = varEx->name;
                    if (vname == "<") vname = "this";
                    else if (vname.rfind("<.", 0) == 0) vname = "this." + vname.substr(2);

                    if (Symbol* s = resolveVariable(vname)) {
                        name = s->typeName;
                    } else {
                        size_t dotPos = vname.find('.');
                        if (dotPos != std::string::npos) {
                            std::string baseName = vname.substr(0, dotPos);
                            std::string propPath = vname.substr(dotPos + 1);
                            if (Symbol* baseSym = resolveVariable(baseName)) {
                                std::string propTypeName;
                                resolvePropertyWritePtr(baseSym, propPath, propTypeName);
                                name = propTypeName;
                            }
                        }
                    }
                } else if (auto* lit = dynamic_cast<LiteralExpr*>(expr)) {
                    name = lit->type;
                } else if (dynamic_cast<StringInterpolationExpr*>(expr)) {
                    name = "string";
                } else if (auto* ce = dynamic_cast<CallExpr*>(expr)) {
                    // spec/39 §4: a string-returning call compares by content, not identity
                    if (auto* fi = resolveCallTarget(ce->functionName)) name = fi->returnTypeName;
                    // A string builtin is in no FunctionInfo, but has a
                    // statically known return type.  Without that
                    // `{ eq: [zeichen.lower(), zeichen.upper()] }` fell back to pointer identity
                    // — two FRESH allocations, so silently "always unequal".
                    // The short name alone is not enough: `slice`/`reverse`/`replace`
                    // exist on lists too, that is why the receiver must be a string.
                    // Related to the name list in `ownedStringReturnExpr`
                    // (ast_compiler.cpp), which however checks FRESHNESS, not the type —
                    // there `split`, which returns a list, belongs to it.
                    if (name.empty()) {
                        const std::string& fn = ce->functionName;
                        size_t dp = fn.rfind('.');
                        if (dp != std::string::npos && dp > 0) {
                            static const std::set<std::string> strMeths = {
                                "substr", "lower", "upper", "title", "trim", "replace",
                                "slice", "concat", "join", "to_string", "repeat",
                                "reverse", "pad_left", "pad_right", "format" };
                            if (strMeths.count(fn.substr(dp + 1))) {
                                std::string recv = fn.substr(0, dp);
                                if (recv == "<") recv = "this";
                                else if (recv.rfind("<.", 0) == 0) recv = "this." + recv.substr(2);
                                std::string recvTy;
                                if (Symbol* rs = resolveVariable(recv)) {
                                    recvTy = rs->typeName;
                                } else {
                                    size_t rd = recv.find('.');
                                    if (rd != std::string::npos)
                                        if (Symbol* rb = resolveVariable(recv.substr(0, rd)))
                                            resolvePropertyWritePtr(rb, recv.substr(rd + 1), recvTy);
                                }
                                if (recvTy == "string") name = "string";
                            }
                        }
                    }
                }
                return name;
            };
            std::string lhsTypeName = resolveTypeName(bin->lhs.get());
            std::string rhsTypeName = resolveTypeName(bin->rhs.get());
            llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
            llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
            if (auto* res = tryEqDispatch(lhsTypeName, lv, rv))
                return res;
            if (lhsTypeName == "string" || rhsTypeName == "string") {
                return visitStringCompare(isNeq, lv, rv);
            }
            // Warning if both types are unknown AND we have to fall back to pointer identity
            // — very likely a user bug (e.g. eq on
            // a string field whose type resolver failed).  peq/pneq
            // is the explicit pointer compare; eq should mean content.
            if (lhsTypeName.empty() && rhsTypeName.empty()) {
                LOG_WARNING(ctx.fmtLoc(bin->sourceNode) +
                            "eq/neq on two pointers without resolvable type — "
                            "falling back to pointer-identity comparison. "
                            "Use peq/pneq for explicit address comparison or "
                            "copy into a typed local first.");
            }
            // Default: pointer-identity comparison.
            llvm::Value* cmp = isNeq
                ? ctx.builder.CreateICmpNE(lv, rv)
                : ctx.builder.CreateICmpEQ(lv, rv);
            return ctx.ir_gen->wrap(cmp);
        }

        return isNeq ? ctx.ir_gen->createICmpNE(lhs, rhs)
                     : ctx.ir_gen->createICmpEQ(lhs, rhs);
    }
    if (bin->op == "lt")                       return ctx.ir_gen->createICmpSLT(lhs, rhs);
    if (bin->op == "gt")                       return ctx.ir_gen->createICmpSGT(lhs, rhs);
    if (bin->op == "le" || bin->op == "lte")   return ctx.ir_gen->createICmpSLE(lhs, rhs);
    if (bin->op == "ge" || bin->op == "gte")   return ctx.ir_gen->createICmpSGE(lhs, rhs);

    if (bin->op == "equiv" || bin->op == "xor" ||
        bin->op == "and" || bin->op == "&&" ||
        bin->op == "or"  || bin->op == "||") {
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
        llvm::Value* lb = coerceToBool(ctx, lv);
        llvm::Value* rb = coerceToBool(ctx, rv);
        if (!lb || !rb) {
            LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "Cannot apply logical operator '" + bin->op + "' to non-scalar type");
            return nullptr;
        }
        if (bin->op == "equiv") return ctx.ir_gen->wrap(ctx.builder.CreateICmpEQ(lb, rb, "equiv"));
        if (bin->op == "xor")   return ctx.ir_gen->wrap(ctx.builder.CreateXor(lb, rb, "xor"));
        if (bin->op == "and" || bin->op == "&&") return ctx.ir_gen->wrap(ctx.builder.CreateAnd(lb, rb, "and"));
        if (bin->op == "or"  || bin->op == "||") return ctx.ir_gen->wrap(ctx.builder.CreateOr(lb, rb, "or"));
    }

    LOG_ERROR(ctx.fmtLoc(bin->sourceNode) + "Unknown binary operator: " + bin->op);
    return nullptr;
}
// Helper: strip one level of ptr<...> or ref<...> wrapper, returns inner type string
static std::string stripPtrWrapper(const std::string& typeName) {
    if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) && typeName.back() == '>')
        return typeName.substr(4, typeName.size() - 5);
    return "";
}

IRValue* Compiler::visitUnary(UnaryExpr* unary) {
    // '&'  / '&&'  — address-of (N levels): return the alloca pointer of the variable
    if (unary->op.find_first_not_of('&') == std::string::npos) {
        // Address of a plain variable OR a dotted member-access (obj.field).
        std::string lvTy;
        IRValue* alloca = emitLValueAddress(unary->operand.get(), lvTy);
        if (!alloca) { LOG_ERROR(ctx.fmtLoc(unary->sourceNode) + "Unary '&': operand must be an addressable lvalue (variable or member-access)"); return nullptr; }
        // AIML v2: `&x` where x is a refcount-MODEL lvalue — the slot already holds a %M* reference,
        // so `&x` (a ptr<M> for the callee to mutate through) is that STORED pointer, NOT the address
        // of the slot (%M**). Load it. Under v1 (isRefcountModel=false) the slot IS the struct →
        // its address is the ptr<M>, unchanged.
        if (unary->op == "&" && ctx.isRefcountModel(lvTy)) {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i8p, slot, "amp_refmodel"));
        }
        if (unary->op == "&") {
            return alloca; // pointer to x
        }
        // '&&x' → alloca of a pointer-to-x (temp)
        llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        llvm::AllocaInst* ptrToPtr = ctx.createEntryAlloca(rawAlloca->getType(), "addr_of_addr");
        ctx.builder.CreateStore(rawAlloca, ptrToPtr);
        return ctx.ir_gen->wrap(ptrToPtr);
    }

    // '?', '??', '???' — dereference (N levels)
    if (unary->op.find_first_not_of('?') == std::string::npos) {
        int depth = (int)unary->op.size();

        // Resolve base symbol to get the type chain
        auto* varExpr = dynamic_cast<VariableExpr*>(unary->operand.get());
        Symbol* sym = varExpr ? resolveVariable(varExpr->name) : nullptr;

        // Get the current pointer value (evaluating the operand loads its value)
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));

        // Build the type chain from the symbol's typeName (e.g. "ptr<ptr<i32>>")
        std::string curTypeName = sym ? sym->typeName : "";
        for (int i = 0; i < depth; ++i) {
            std::string inner = stripPtrWrapper(curTypeName);
            llvm::Type* loadTy = nullptr;
            if (!inner.empty()) {
                loadTy = ctx.getLLVMType(inner);
                curTypeName = inner; // advance one level
            }
            if (!loadTy) {
                // Fallback: load as opaque pointer (handles string/any)
                loadTy = ctx.builder.getInt8Ty()->getPointerTo();
            }
            ptr = ctx.builder.CreateLoad(loadTy, ptr, "deref");
        }
        return ctx.ir_gen->wrap(ptr);
    }

    // '-' — arithmetic negation (scalar or math struct)
    if (unary->op == "-") {
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(val->getType()));
        if (ty->isStructTy()) {
            auto* structTy = static_cast<llvm::StructType*>(ty);
            std::string sn = structTy->getName().str();
            std::string nm = "aiml_math_" + sn + "_neg";
            auto op2 = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module().getFunction(nm.c_str());
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op2,op2},false),
                llvm::Function::ExternalLinkage,nm.c_str(),ctx.module());
            llvm::AllocaInst* src = ctx.createEntryAlloca(structTy);
            ctx.builder.CreateStore(v, src);
            llvm::AllocaInst* out = ctx.createEntryAlloca(structTy);
            ctx.builder.CreateCall(fn,{src,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(structTy,out));
        }
        if (ty->isFloatTy() || ty->isDoubleTy())
            return ctx.ir_gen->wrap(ctx.builder.CreateFNeg(v, "fneg"));
        return ctx.ir_gen->wrap(ctx.builder.CreateNeg(v, "neg"));
    }

    // '!' / 'not' — logical not
    if (unary->op == "!" || unary->op == "not") {
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        llvm::Value* bv = coerceToBool(ctx, v);
        if (!bv) {
            LOG_ERROR(ctx.fmtLoc(unary->sourceNode) + "Cannot apply 'not' to non-scalar type");
            return nullptr;
        }
        return ctx.ir_gen->wrap(ctx.builder.CreateNot(bv, "not"));
    }

    // spec/04_1 §5: bitwise NOT
    if (unary->op == "bnot") {
        IRValue* val = visitExpression(unary->operand.get());
        if (!val) return nullptr;
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        if (!v->getType()->isIntegerTy()) {
            LOG_ERROR(ctx.fmtLoc(unary->sourceNode) + "bitwise operator 'bnot' needs an integer operand");
            return nullptr;
        }
        return ctx.ir_gen->wrap(ctx.builder.CreateNot(v, "bnot"));
    }

    LOG_ERROR(ctx.fmtLoc(unary->sourceNode) + "Unknown unary operator: " + unary->op);
    return nullptr;
}
IRValue* Compiler::visitSpawn(SpawnExpr* spawn) {
    // 1. Resolve the target function
    CompilerContext::FunctionInfo* fi = resolveFunction(spawn->functionName);
    if (!fi || !fi->func) {
        LOG_ERROR(ctx.fmtLoc(spawn->sourceNode) + "spawn: unknown function: " + spawn->functionName);
        return nullptr;
    }
    llvm::Function* targetFn = fi->func;

    // 2. Evaluate arguments
    std::vector<llvm::Value*> argVals;
    for (auto& arg : spawn->args) {
        IRValue* v = visitExpression(arg.get());
        if (v) argVals.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)));
    }
    // spec/61 §8: a ptr<T> parameter of the thread function takes the box (moved into the thread)
    if (fi && ctx.memV3 && ctx.lifetimeDropsEnabled)
        for (size_t ai = 0; ai < spawn->args.size() && ai < fi->argTypeNames.size(); ++ai)
            movePtrArg(spawn->args[ai].get(), fi->argTypeNames[ai], spawn->functionName,
                       ai < fi->argNames.size() ? fi->argNames[ai] : "");

    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();

    // spec/61 Class 5: the thread co-owns each chan argument (retained here, released by the trampoline)
    std::vector<bool> chanArg(argVals.size(), false);
    std::vector<bool> hashmapArg(argVals.size(), false);   // spec/69: hashmap args co-owned the same way
    if (ctx.memV3 && ctx.lifetimeDropsEnabled)
        for (size_t ai = 0; ai < argVals.size() && ai < fi->argTypeNames.size(); ++ai) {
            const std::string& at = fi->argTypeNames[ai];
            bool hm = at.size() > 9 && at.rfind("hashmap<", 0) == 0 && at.back() == '>';
            if (((at.rfind("chan<", 0) == 0 && at.back() == '>') || hm) && argVals[ai]->getType()->isPointerTy()) {
                chanArg[ai] = true;
                hashmapArg[ai] = hm;
                ctx.builder.CreateCall(ctx.getFunction(hm ? "aiml_hashmap_retain" : "aiml_chan_retain",
                    ctx.builder.getVoidTy(), {i8p}), { argVals[ai] });
            }
        }
    auto releaseChanArgs = [&](const std::vector<llvm::Value*>& vals) {
        for (size_t i = 0; i < vals.size() && i < chanArg.size(); ++i)
            if (chanArg[i])
                ctx.builder.CreateCall(ctx.getFunction(hashmapArg[i] ? "aiml_hashmap_release" : "aiml_chan_release",
                    ctx.builder.getVoidTy(), {i8p}), { vals[i] });
    };

    // 3. Build or reuse a trampoline: void*(void*) that unpacks args and calls targetFn
    std::string trampolineName = "__aiml_spawn_" + spawn->functionName;
    llvm::Function* trampoline = ctx.module().getFunction(trampolineName);
    if (!trampoline) {
        std::vector<llvm::Type*> argTypes;
        for (auto* v : argVals) argTypes.push_back(v->getType());
        llvm::StructType* argStructTy = argTypes.empty()
            ? nullptr : llvm::StructType::get(ctx.context, argTypes);

        llvm::FunctionType* trampolineTy = llvm::FunctionType::get(i8p, {i8p}, false);
        trampoline = llvm::Function::Create(trampolineTy,
            llvm::Function::InternalLinkage, trampolineName, ctx.module());

        llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
        llvm::BasicBlock* tBB     = llvm::BasicBlock::Create(ctx.context, "entry",  trampoline);
        llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "normal", trampoline);
        llvm::BasicBlock* catchBB  = llvm::BasicBlock::Create(ctx.context, "catch",  trampoline);

        // Give trampoline a personality so it can have a landingpad
        llvm::Function* persPtr = ctx.module().getFunction("__gxx_personality_v0");
        if (!persPtr) {
            llvm::FunctionType* persTy = llvm::FunctionType::get(ctx.builder.getInt32Ty(), true);
            persPtr = llvm::Function::Create(persTy, llvm::Function::ExternalLinkage,
                                             "__gxx_personality_v0", ctx.module());
        }
        trampoline->setPersonalityFn(persPtr);

        ctx.builder.SetInsertPoint(tBB);
        llvm::Value* rawArgPtr = trampoline->arg_begin();
        std::vector<llvm::Value*> callArgs;
        if (argStructTy) {
            llvm::Value* argStructPtr = ctx.builder.CreateBitCast(
                rawArgPtr, argStructTy->getPointerTo(), "args");
            for (size_t i = 0; i < argTypes.size(); ++i) {
                llvm::Value* gep = ctx.builder.CreateStructGEP(argStructTy, argStructPtr, (unsigned)i);
                callArgs.push_back(ctx.builder.CreateLoad(argTypes[i], gep));
            }
            ctx.builder.CreateInvoke(targetFn, normalBB, catchBB, callArgs);

            ctx.builder.SetInsertPoint(normalBB);
            releaseChanArgs(callArgs);
            llvm::Function* freeFn = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(freeFn, {rawArgPtr});
            ctx.builder.CreateRet(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        } else {
            ctx.builder.CreateInvoke(targetFn, normalBB, catchBB, callArgs);

            ctx.builder.SetInsertPoint(normalBB);
            ctx.builder.CreateRet(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        }

        // catch-all: swallow any exception so it never escapes the pthread boundary
        ctx.builder.SetInsertPoint(catchBB);
        llvm::Type* lpTy = llvm::StructType::get(ctx.context,
            {i8p, ctx.builder.getInt32Ty()});
        llvm::LandingPadInst* lp = ctx.builder.CreateLandingPad(lpTy, 1, "lp");
        lp->addClause(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Value* excPtr = ctx.builder.CreateExtractValue(lp, {0u});
        llvm::Function* beginCatch = ctx.getFunction("__cxa_begin_catch", i8p, {i8p});
        ctx.builder.CreateCall(beginCatch, {excPtr});
        llvm::Function* endCatch = ctx.getFunction("__cxa_end_catch", ctx.builder.getVoidTy(), {});
        ctx.builder.CreateCall(endCatch, {});
        if (argStructTy) {
            releaseChanArgs(callArgs);
            llvm::Function* freeFn2 = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(freeFn2, {rawArgPtr});
        }
        ctx.builder.CreateRet(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));

        ctx.builder.restoreIP(savedIP);
    }

    // 4. Malloc args struct and fill it
    llvm::Value* argsVoidPtr = llvm::ConstantPointerNull::get(
        static_cast<llvm::PointerType*>(i8p));
    if (!argVals.empty()) {
        std::vector<llvm::Type*> argTypes;
        for (auto* v : argVals) argTypes.push_back(v->getType());
        llvm::StructType* argStructTy = llvm::StructType::get(ctx.context, argTypes);
        llvm::DataLayout dl(ctx.module().getDataLayout());
        uint64_t argStructSize = dl.getTypeAllocSize(argStructTy);

        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.getSizeType()});
        llvm::Value* rawArgsPtr  = ctx.builder.CreateCall(mallocFn,
            {ctx.getSizeConst(argStructSize)}, "args_raw");
        llvm::Value* argsPtr = ctx.builder.CreateBitCast(
            rawArgsPtr, argStructTy->getPointerTo(), "args_ptr");
        for (size_t i = 0; i < argVals.size(); ++i) {
            llvm::Value* gep = ctx.builder.CreateStructGEP(argStructTy, argsPtr, (unsigned)i);
            ctx.builder.CreateStore(argVals[i], gep);
        }
        argsVoidPtr = rawArgsPtr;
    }

    // 5. Malloc the thread_handle struct { i64 tid, i8 joined, i8 detached }
    llvm::StructType* handleTy = llvm::StructType::getTypeByName(ctx.context, "thread_handle");
    if (!handleTy) {
        LOG_ERROR(ctx.fmtLoc(spawn->sourceNode) + "spawn: thread_handle type not found");
        return nullptr;
    }
    llvm::DataLayout dl(ctx.module().getDataLayout());
    llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.getSizeType()});
    llvm::Value* rawHandle = ctx.builder.CreateCall(mallocFn,
        {ctx.getSizeConst(dl.getTypeAllocSize(handleTy))}, "handle_raw");
    llvm::Value* handlePtr = ctx.builder.CreateBitCast(rawHandle, handleTy->getPointerTo(), "handle_ptr");
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr"));
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr"));

    // 6. pthread_create(&handle->tid, NULL, trampoline, args)
    // `pthread_t` is pointer-wide (i64 native, i32 on wasm32), the handle slot
    // on the other hand is i64 everywhere — `thread.id` should not change type per target.
    // So pthread_create writes into a suitably wide temporary field, and that
    // moves from there into the slot; otherwise its upper half would stay garbage.
    llvm::Type* tidTy = ctx.getSizeType();
    llvm::Value* tidTmp = ctx.createEntryAlloca(tidTy, "tid_tmp");
    ctx.builder.CreateStore(llvm::ConstantInt::get(tidTy, 0), tidTmp);
    llvm::Value* trampolinePtr = ctx.builder.CreateBitCast(trampoline, i8p);
    llvm::Function* pthreadCreate = ctx.getFunction("pthread_create",
        ctx.builder.getInt32Ty(), {tidTy->getPointerTo(), i8p, i8p, i8p});
    ctx.builder.CreateCall(pthreadCreate, {
        tidTmp,
        llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
        trampolinePtr,
        argsVoidPtr
    });
    ctx.builder.CreateStore(
        ctx.builder.CreateZExtOrTrunc(ctx.builder.CreateLoad(tidTy, tidTmp, "tid_raw"),
                                      ctx.builder.getInt64Ty(), "tid_i64"),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_gep"));

    // 7. Return the handle pointer (stored into the thread_handle* alloca by visitSetStmt)
    return ctx.ir_gen->wrap(handlePtr);
}

IRValue* Compiler::visitAtExpr(AtExpr* at) {
    const bool wantAddress = ctx.atAddressWanted;   // spec/71 §1.3 (see context.h)
    ctx.atAddressWanted = false;
    // Evaluate container — must be a variable (or struct/ptr field path).
    auto* varExpr = dynamic_cast<VariableExpr*>(at->container.get());
    if (!varExpr) {
        LOG_ERROR(ctx.fmtLoc() + "at: container must be a variable or struct field path");
        return nullptr;
    }

    // Class-method this-shortcut: `<` and `<.member` → `this` / `this.member`,
    // analogous to visitVariable.
    std::string varName = varExpr->name;
    if (varName == "<") {
        varName = "this";
    } else if (varName.find("<.") == 0) {
        varName = "this." + varName.substr(2);
    }

    // Field-path resolution: "h.xs", "a.b.c.list", "ptr.field" — when the
    // bare name isn't a symbol, try splitting at the first dot and resolving
    // through resolvePropertyWritePtr (which handles arbitrarily nested
    // struct/ptr access and gives us an alloca-style pointer to the inner
    // list<T>/array<T> / dyn_array<T> / tensor).
    Symbol* sym = resolveVariable(varName);
    Symbol  tempSym;  // backs synthesized Symbol when we resolve a field path
    if (!sym) {
        size_t dotPos = varName.find('.');
        if (dotPos != std::string::npos) {
            std::string baseName = varName.substr(0, dotPos);
            std::string propPath = varName.substr(dotPos + 1);
            Symbol* baseSym = resolveVariable(baseName);
            if (baseSym) {
                std::string propTypeName;
                IRValue* propPtr = resolvePropertyWritePtr(baseSym, propPath, propTypeName);
                if (propPtr && !propTypeName.empty()) {
                    tempSym.typeName        = propTypeName;
                    tempSym.ir_alloca_inst  = propPtr;
                    // ir_element_type is not strictly needed — the branches
                    // below only read typeName + alloca_inst (via ir_alloca_inst).
                    sym = &tempSym;
                }
            }
        }
    }
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "at: unknown variable '" + varName + "'");
        return nullptr;
    }

    // Evaluate all index expressions
    std::vector<llvm::Value*> indexVals;
    for (auto& idx : at->indices) {
        IRValue* v = visitExpression(idx.get());
        if (!v) return nullptr;
        llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
        // Truncate i64 → i32 for GEP
        if (raw->getType()->isIntegerTy(64))
            raw = ctx.builder.CreateTrunc(raw, ctx.builder.getInt32Ty());
        indexVals.push_back(raw);
    }

    const std::string& tName = sym->typeName;

    // ── array<T:N[:M[:...]]> ──
    if (tName.rfind("array<", 0) == 0 && tName.back() == '>') {
        std::string inner = tName.substr(6, tName.size() - 7);
        // Parse "T:D0:D1:..."
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t sep = inner.find(':', pos);
            if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
            parts.push_back(inner.substr(pos, sep - pos));
            pos = sep + 1;
        }
        llvm::Type* elemTy = ctx.getLLVMType(parts[0]);
        // Collect dimension sizes (parts[1..])
        std::vector<uint64_t> dims;
        for (size_t i = 1; i < parts.size(); ++i)
            dims.push_back(std::stoull(parts[i]));

        // spec/71 §4: every index inside its dimension (one index → the flat element count).
        {
            uint64_t total = 1;
            for (auto d : dims) total *= d;
            if (indexVals.size() == 1)
                ListImpl::checkBounds(ctx, indexVals[0], ctx.builder.getInt32((uint32_t)total));
            else
                for (size_t i = 0; i < indexVals.size() && i < dims.size(); ++i)
                    ListImpl::checkBounds(ctx, indexVals[i], ctx.builder.getInt32((uint32_t)dims[i]));
        }
        // Compute flat index from multi-dim indices (row-major)
        llvm::Value* flatIdx = nullptr;
        if (indexVals.size() == 1) {
            flatIdx = indexVals[0];
        } else {
            // flat = i0 * (d1*d2*...) + i1 * (d2*d3*...) + ... + i_last
            flatIdx = indexVals[0];
            for (size_t i = 1; i < indexVals.size(); ++i) {
                uint64_t stride = 1;
                for (size_t j = i; j < dims.size(); ++j) stride *= dims[j];
                flatIdx = ctx.builder.CreateMul(flatIdx, ctx.builder.getInt32((uint32_t)stride));
                flatIdx = ctx.builder.CreateAdd(flatIdx, indexVals[i]);
            }
        }

        // GEP into the alloca [N x T]
        uint64_t totalCount = 1;
        for (auto d : dims) totalCount *= d;
        llvm::Type* arrTy = llvm::ArrayType::get(elemTy, totalCount);
        llvm::Value* arrPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* elemPtr = ctx.builder.CreateGEP(arrTy, arrPtr,
            {ctx.builder.getInt32(0), flatIdx}, "arr_gep");
        llvm::Value* val = ctx.builder.CreateLoad(elemTy, elemPtr, "arr_at");
        return ctx.ir_gen->wrap(val);
    }

    // ── dyn_array<T> ──
    if (tName.rfind("dyn_array<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(10, tName.size() - 11);
        llvm::Type* elemTy   = ctx.getLLVMType(elemName);
        llvm::Type* i8p      = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* ptrAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* rawPtr  = ctx.builder.CreateLoad(i8p, ptrAlloca, "darr_base");
        llvm::Value* typedPtr = ctx.builder.CreatePointerCast(rawPtr, elemTy->getPointerTo(), "darr_typed");
        if (indexVals.empty()) { LOG_ERROR(ctx.fmtLoc() + "at: dyn_array requires index"); return nullptr; }
        ListImpl::checkBounds(ctx, indexVals[0], ctx.builder.CreateCall(
            ctx.getFunction("aiml_darr_len", ctx.builder.getInt32Ty(), {i8p}), {rawPtr}, "darr_len"));   // spec/71 §4
        llvm::Value* elemPtr = ctx.builder.CreateGEP(elemTy, typedPtr, indexVals[0], "darr_gep");
        llvm::Value* val     = ctx.builder.CreateLoad(elemTy, elemPtr, "darr_at");
        return ctx.ir_gen->wrap(val);
    }

    // ── buffer<T> (spec/71 §1.3): element i of the written data; out of range throws ──
    if (tName.rfind("buffer<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(7, tName.size() - 8);
        llvm::Type* elemTy = ctx.getLLVMType(elemName);
        llvm::Type* bufTy  = ctx.getLLVMType("buffer");
        llvm::Type* i8p    = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* buf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        if (sym->is_byref_arg) buf = ctx.builder.CreateLoad(i8p, buf, "byref_deref_bat");
        if (indexVals.empty() || !elemTy) { LOG_ERROR(ctx.fmtLoc() + "at: buffer<T> requires one index"); return nullptr; }
        uint32_t esz = (uint32_t)ctx.module().getDataLayout().getTypeAllocSize(elemTy);
        llvm::Value* bytes = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(),
                                 ctx.builder.CreateStructGEP(bufTy, buf, 0, "bsz_ptr"), "buf_bytes");
        llvm::Value* n = esz > 1 ? ctx.builder.CreateSDiv(bytes, ctx.builder.getInt32(esz), "buf_n") : bytes;
        ListImpl::checkBounds(ctx, indexVals[0], n);
        llvm::Value* data = ctx.builder.CreateLoad(i8p, ctx.builder.CreateStructGEP(bufTy, buf, 4, "bdata_fld"), "bdata");
        llvm::Value* off = ctx.builder.CreateMul(ctx.builder.CreateSExtOrTrunc(indexVals[0], ctx.builder.getInt64Ty()),
                                                 ctx.builder.getInt64(esz), "boff");
        llvm::Value* ep = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), data, off, "belem_ptr");
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemTy, ep, "buf_at"));
    }

    // ── list<T> ──
    if (tName.rfind("list<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(5, tName.size() - 6);
        llvm::Type* elemTy = ctx.getLLVMType(elemName);
        llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        // Byref function arg: the alloca holds a POINTER to the caller's
        // list struct, not the struct itself.  Deref once so getElementPtr
        // sees the real list.  resolveProperty does the same for `.len`.
        // Without this, `at: [lst, i]` GEPs into the 4-byte ptr alloca and
        // reads garbage at offset 8 → WASM bounds trap on the next load.
        // Repro: tests/wasm/test_mirror_rpc_client_multifile.aiml phase 4.
        if (sym->is_byref_arg) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            listPtr = ctx.builder.CreateLoad(i8p, listPtr, "byref_deref_at");
        }
        if (indexVals.empty()) { LOG_ERROR(ctx.fmtLoc() + "at: list requires at least one index"); return nullptr; }
        ListImpl::checkIndex(ctx, listPtr, indexVals[0]);   // spec/10: out of range throws
        // Trailing member path: { at: [list, i, "field", ...] } = element[i].field... — the dash-free
        // form of `list-i.field`. Get a pointer to the element (a %M* for a refcount model, else the
        // inline struct slot), then resolve the member path on it and load the final field.
        if (!at->memberPath.empty()) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* elemBase;
            if (ctx.isRefcountModel(elemName)) {
                llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
                elemBase = ctx.builder.CreateLoad(i8p, slot, "at_mp_modelptr");
            } else {
                elemBase = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);
            }
            std::string joined;
            for (size_t i = 0; i < at->memberPath.size(); ++i)
                joined += (i ? "." : "") + at->memberPath[i];
            Symbol elemSym;
            elemSym.typeName       = elemName;
            elemSym.ir_alloca_inst = ctx.ir_gen->wrap(elemBase);
            // elemBase IS the struct address (a %M* for a model, or the inline element ptr), so the
            // element "type" is the struct itself (not a pointer) — resolvePropertyWritePtr then GEPs
            // fields directly from it instead of loading a pointer first.
            if (ctx.classes.count(elemName) && ctx.classes[elemName].type)
                elemSym.ir_element_type = ctx.ir_gen->wrapType(ctx.classes[elemName].type);
            std::string fldTypeName;
            IRValue* fldPtr = resolvePropertyWritePtr(&elemSym, joined, fldTypeName);
            if (!fldPtr || fldTypeName.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "at: member path '" + joined + "' unresolved on element of " + tName);
                return nullptr;
            }
            llvm::Value* fp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fldPtr));
            // A memV3 container final field: resolvePropertyWritePtr already LOADED and returned the
            // handle (wantContainerSlot=false), so fldPtr IS the handle — return it directly (bindable
            // / method-callable). A scalar/string field returns its slot → load the value.
            if (ctx.memV3 && ctx.isMemV3ContainerHandle(fldTypeName)) {
                return fldPtr;
            }
            llvm::Type* fldTy = ctx.getLLVMType(fldTypeName);
            llvm::Value* fv = ctx.builder.CreateLoad(fldTy, fp, "at_mp_fld");
            if (fldTypeName == "string") {   // borrowing a string out adds an alias → incref (see below)
                llvm::Function* incref = ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(), {i8p});
                ctx.builder.CreateCall(incref, {fv});
                // spec/61 §4/§5.4: this incref made a FRESH owned alias of the field string — register
                // it as an owned stmt-temp (exactly like the list<string> element path below). A
                // `decl x = at(list,i,.field)` then MOVES it (one owned ref); a transient (interpolation
                // `"${at(..,.field)}"`, a borrowed call arg) is decref'd at statement end → balanced.
                // Without this the incref'd alias leaked 12 B/access (leak_probes/round5 §1:
                // at_field_borrow/local/interpolation_1000). The incref stays: a receiving local must
                // still read the string after the source list is released.
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                    aimlTrackStringTemp(ctx, fv);
            }
            return ctx.ir_gen->wrap(fv);
        }
        // AIML v2 P3/P2: a list<Model> element is a %M* — load and return the reference (a
        // pointer-to-struct; field access derefs it, `decl e = at(..)` increfs in visitDeclStmt).
        // A borrow: no incref here (transient `at(..).field` reads must not leak a ref).
        if (ctx.isRefcountModel(elemName)) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
            llvm::Value* objp = ctx.builder.CreateLoad(i8p, slot, "list_at_modelptr");
            return ctx.ir_gen->wrap(objp);
        }
        // spec/61 §5.7 (memV3): a NESTED CONTAINER element is a HANDLE pointer — load and return it.
        // A borrow here (no incref); binding it into a `list<T>` local increfs (emitListHandleDecl's
        // at-init share path), so transient `at(..).len` reads do not leak a handle ref.
        if (ctx.memV3 && ctx.isMemV3ContainerHandle(elemName)) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
            llvm::Value* hp = ctx.builder.CreateLoad(i8p, slot, "list_at_innerh");
            return ctx.ir_gen->wrap(hp);
        }
        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);
        if (wantAddress && elemTy->isStructTy()) return ctx.ir_gen->wrap(elemPtr);
        llvm::Value* val = ctx.builder.CreateLoad(elemTy, elemPtr, "list_at");
        // Refcount: borrowing a string out of a list adds an alias.  Incref
        // so the caller's local var holds a real ref; when the underlying
        // list is later released (e.g. mirror's aiml_ser_free_decoded), the
        // string survives.  No-op on non-refcounted strings (literals).
        // Repro: tests/test_mirror_close_string_uaf.aiml
        if (elemName == "string") {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* voidTy = llvm::Type::getVoidTy(ctx.context);
            llvm::Function* incref = ctx.module().getFunction("aiml_str_incref");
            if (!incref) {
                auto* fty = llvm::FunctionType::get(voidTy, {i8p}, false);
                incref = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                                "aiml_str_incref", ctx.module());
            }
            ctx.builder.CreateCall(incref, {val});
            // spec/61 §7: this incref made a FRESH owned alias — register it as an owned stmt-temp
            // (like the str.* producers do). A `decl x = at(list,i)` then MOVES it (the decl-init
            // share-incref is skipped for owned temps), so the element is not incref'd TWICE and
            // left leaking when both x's drop and the list release decref only once (grai
            // match_key_of). A transient `at(..)` read is decref'd at statement end → balanced.
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                aimlTrackStringTemp(ctx, val);
        }
        return ctx.ir_gen->wrap(val);
    }

    // ── tensor ──
    if (tName == "tensor") {
        // aiml_tensor_get_flat(tensor*, i32 index) → f32, after the bounds check (spec/71 §4)
        llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* tensorVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), tPtr, "tensor_ptr");
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        // spec/71 §1.5/§4: a flat index into the elements; out of range throws.
        ListImpl::checkBounds(ctx, indexVals[0], ctx.builder.CreateCall(
            ctx.getFunction("aiml_tensor_size", i32, {i8p}), {tensorVal}, "tensor_len"));
        llvm::Function* getFn = ctx.getFunction("aiml_tensor_get_flat", f32, {i8p, i32});
        llvm::Value* val = ctx.builder.CreateCall(getFn, {tensorVal, indexVals[0]}, "tensor_at");
        return ctx.ir_gen->wrap(val);
    }

    LOG_ERROR(ctx.fmtLoc() + "at: unsupported container type '" + tName + "'");
    return nullptr;
}

} // namespace AST
} // namespace AIML
