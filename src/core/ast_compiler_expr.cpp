#define DEBUG_MODE
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
    return nullptr;
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
            llvm::Value* trueStr  = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
            strVal = ctx.builder.CreateSelect(cond, trueStr, falseStr);
        } else if (ty->isIntegerTy(1)) {
            llvm::Value* trueStr  = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
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
            ctx.stmtTempStack.back().push_back(strVal);      // fresh conversion result
        }
        if (!currentStr) currentStr = strVal;
        else {
            currentStr = StringUtils::concat(ctx, currentStr, strVal);
            if (!ctx.stmtTempStack.empty()) ctx.stmtTempStack.back().push_back(currentStr);
        }
    }
    // spec/61 §7: an interpolation is always a FRESH owned string — a lone string part ("${x}") would
    // otherwise alias its operand while consumers adopt it as owned (UAF once the source is freed).
    if (currentStr && currentStr->getType()->isPointerTy() && !ctx.stmtTempStack.empty()) {
        auto& fr = ctx.stmtTempStack.back();
        if (std::find(fr.begin(), fr.end(), currentStr) == fr.end()) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            currentStr = ctx.builder.CreateCall(ctx.getFunction("aiml_str_dup", i8p, {i8p}), { currentStr }, "interp_own");
            fr.push_back(currentStr);
        }
    }
    return ctx.ir_gen->wrap(currentStr);
}
IRValue* Compiler::visitNewArrayExpr(NewArrayExpr* na) {
    llvm::Type* elemTy   = ctx.getLLVMType(na->elemTypeName);
    uint64_t    elemSize = ctx.module.getDataLayout().getTypeAllocSize(elemTy);

    IRValue* countIR = visitExpression(na->countExpr.get());
    if (!countIR) { LOG_ERROR("new_array: could not evaluate count"); return ctx.ir_gen->getInt32(0); }
    llvm::Value* count = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(countIR));
    if (count->getType()->isIntegerTy(32))
        count = ctx.builder.CreateZExt(count, ctx.builder.getInt64Ty(), "darr_count64");

    llvm::Function* mallocFn = ctx.module.getFunction("malloc");
    if (!mallocFn) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false);
        mallocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "malloc", ctx.module);
    }
    llvm::Value* bytes = ctx.builder.CreateMul(count, ctx.builder.getInt64(elemSize), "darr_bytes");
    llvm::Value* ptr   = ctx.builder.CreateCall(mallocFn, {bytes}, "darr_ptr");
    return ctx.ir_gen->wrap(ptr);
}

IRValue* Compiler::visitNewExpr(NewExpr* newExpr) {
    std::string className = newExpr->typeName;
    if (!className.empty() && ctx.classes.count(className)) {
        auto& classInfo = ctx.classes[className];
        llvm::StructType* structTy = classInfo.type;

        // Use padded_size for calloc so hot-reloaded versions with new append-only
        // fields fit without reallocation (new slots are zero-initialized by calloc).
        uint64_t allocSize = (classInfo.padded_size > 0)
            ? (uint64_t)classInfo.padded_size
            : ctx.module.getDataLayout().getTypeAllocSize(structTy);

        // Ensure calloc exists.  size_t width is target-dependent (i64 on
        // x86-64, i32 on wasm32) — using ctx.getSizeType() makes the linker
        // signature match libc's.
        llvm::Type* sz = ctx.getSizeType();
        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {sz, sz}, false);
             llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
        }

        IRValue* rawPtr;
        // spec/61 §6.2: `{ new: T }` initialising a `shared<T>` slot allocates a refcounted %M*
        // block (not a value calloc), so it becomes a co-owned handle. The target type carries
        // the `shared<>` wrapper (deserializeTargetType is set to the decl type during init).
        bool sharedTarget = ctx.isRefcountModel(ctx.deserializeTargetType);
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
             if (!newExpr->initValues.count(name)) continue;
             IRValue* fieldInitV = visitExpression(newExpr->initValues[name].get());
             if (!fieldInitV) {
                 LOG_ERROR(ctx.fmtLoc(newExpr->sourceNode) + "Failed to compile AST expression for initialization of member: " + name);
                 continue;
             }
             llvm::Value* initVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV));
             if (!initVal) {
                 LOG_ERROR(ctx.fmtLoc(newExpr->sourceNode) + "Null LLVM value evaluated for initialization of member: " + name);
                 continue;
             }
             llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtrCast, member.index, name + "_gep");
             ctx.builder.CreateStore(initVal, fieldPtr);
        }

        // classgroup container (spec/58): run the synthesized wiring ctor so the
        // new group has all instances allocated (phase 0) + sees pointers
        // back-patched (phase 1).  Nothing else auto-invokes methods on {new:}.
        if (ctx.classGroupContainers.count(className)) {
            std::string ctorName = className + "___ctor";
            if (ctx.module.getFunction(ctorName))
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
            try {
                float f = std::stof(sv);
                return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), (double)f));
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
            return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        }
        if (baseName == "io" && (propName == "stdout" || propName == "stderr" || propName == "stdin")) {
            int fd = (propName == "stdin") ? 0 : (propName == "stdout" ? 1 : 2);
            std::string fmode = (propName == "stdin") ? "r" : "w";
            llvm::Function* fdopenFunc = ctx.module.getFunction("fdopen");
            if (!fdopenFunc) {
                llvm::FunctionType* fdType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false);
                fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module);
            }
            llvm::Value* modeStr = ctx.builder.CreateGlobalStringPtr(fmode);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), modeStr}));
        }

        // Check if varName is an AIML function used as a function pointer / callback.
        // Must come BEFORE getLLVMType so function names don't trigger "Unknown Type" errors.
        {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = nullptr;
            auto fit = ctx.functions.find(varName);
            if (fit != ctx.functions.end() && fit->second.func)
                fn = fit->second.func;
            if (!fn) fn = ctx.module.getFunction(varName);
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

    // list/map by-ref args: alloca enthält den Pointer, nicht den Struct.
    // Lese-Pfade müssen einmal dereferenzieren bevor sie auf den Struct zugreifen.
    if (sym->is_byref_arg) {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* allocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* loaded = ctx.builder.CreateLoad(i8p, allocaRaw, "byref_deref_var");
        baseAlloca = ctx.ir_gen->wrap(loaded);
    }

    if (propName.empty()) {
        // Für byref-args: baseAlloca ist jetzt der struct-Pointer.  Load des
        // Structs liefert eine Kopie der aktuellen size/capacity/data-Werte des
        // CALLER-Structs — genau was beim Weitergeben gebraucht wird.
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
    llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
    if (!strcmpFunc) strcmpFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strcmp", ctx.module);
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
    IRValue* lhs = visitExpression(bin->lhs.get());
    IRValue* rhs = visitExpression(bin->rhs.get());
    if (!lhs || !rhs) return nullptr;
    
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
            llvm::Function* fn = ctx.module.getFunction(nm);
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),llvm::Function::ExternalLinkage,nm,ctx.module);
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
            if (bin->op == "eq") {
                llvm::Function* fn=ctx.module.getFunction((pre+"eq").c_str());
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(),{op,op},false),llvm::Function::ExternalLinkage,(pre+"eq").c_str(),ctx.module);
                llvm::Value* res=ctx.builder.CreateCall(fn,{store(lv,structTy),store(rv,structTy)});
                return ctx.ir_gen->wrap(ctx.builder.CreateICmpNE(res,ctx.builder.getInt32(0)));
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
                if (st->hasName() && st->getName().startswith("AimlFatPtr_"))
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
            llvm::Function* eqFn = ctx.module.getFunction(eqFnName);
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
            // wie `tick.terrain_id`) und LiteralExpr.  Ohne den Dot-Pfad würde
            // string-typed Struct-Feld als unbekannt durchlaufen und auf
            // Pointer-Identity statt strcmp fallen → bricht spec/39 §0
            // ("For string pointers: byte-by-byte content comparison").
            auto resolveTypeName = [&](Expr* expr) {
                std::string name;
                if (auto* varEx = dynamic_cast<VariableExpr*>(expr)) {
                    if (Symbol* s = resolveVariable(varEx->name)) {
                        name = s->typeName;
                    } else {
                        size_t dotPos = varEx->name.find('.');
                        if (dotPos != std::string::npos) {
                            std::string baseName = varEx->name.substr(0, dotPos);
                            std::string propPath = varEx->name.substr(dotPos + 1);
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
            // Warning wenn beide Typen unbekannt UND wir auf Pointer-Identity
            // fallen müssen — sehr wahrscheinlich ein User-Bug (z.B. eq auf
            // einem string-Feld dessen Typ-Resolver gefailed hat).  peq/pneq
            // ist der explizite Pointer-Compare; eq sollte content meinen.
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
            llvm::Function* fn = ctx.module.getFunction(nm.c_str());
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op2,op2},false),
                llvm::Function::ExternalLinkage,nm.c_str(),ctx.module);
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

    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();

    // 3. Build or reuse a trampoline: void*(void*) that unpacks args and calls targetFn
    std::string trampolineName = "__aiml_spawn_" + spawn->functionName;
    llvm::Function* trampoline = ctx.module.getFunction(trampolineName);
    if (!trampoline) {
        std::vector<llvm::Type*> argTypes;
        for (auto* v : argVals) argTypes.push_back(v->getType());
        llvm::StructType* argStructTy = argTypes.empty()
            ? nullptr : llvm::StructType::get(ctx.context, argTypes);

        llvm::FunctionType* trampolineTy = llvm::FunctionType::get(i8p, {i8p}, false);
        trampoline = llvm::Function::Create(trampolineTy,
            llvm::Function::InternalLinkage, trampolineName, ctx.module);

        llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
        llvm::BasicBlock* tBB     = llvm::BasicBlock::Create(ctx.context, "entry",  trampoline);
        llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(ctx.context, "normal", trampoline);
        llvm::BasicBlock* catchBB  = llvm::BasicBlock::Create(ctx.context, "catch",  trampoline);

        // Give trampoline a personality so it can have a landingpad
        llvm::Function* persPtr = ctx.module.getFunction("__gxx_personality_v0");
        if (!persPtr) {
            llvm::FunctionType* persTy = llvm::FunctionType::get(ctx.builder.getInt32Ty(), true);
            persPtr = llvm::Function::Create(persTy, llvm::Function::ExternalLinkage,
                                             "__gxx_personality_v0", ctx.module);
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
        llvm::DataLayout dl(&ctx.module);
        uint64_t argStructSize = dl.getTypeAllocSize(argStructTy);

        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* rawArgsPtr  = ctx.builder.CreateCall(mallocFn,
            {ctx.builder.getInt64(argStructSize)}, "args_raw");
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
    llvm::DataLayout dl(&ctx.module);
    llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
    llvm::Value* rawHandle = ctx.builder.CreateCall(mallocFn,
        {ctx.builder.getInt64(dl.getTypeAllocSize(handleTy))}, "handle_raw");
    llvm::Value* handlePtr = ctx.builder.CreateBitCast(rawHandle, handleTy->getPointerTo(), "handle_ptr");
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr"));
    ctx.builder.CreateStore(ctx.builder.getInt8(0),
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr"));

    // 6. pthread_create(&handle->tid, NULL, trampoline, args)
    llvm::Type* i64p = ctx.builder.getInt64Ty()->getPointerTo();
    llvm::Value* tidGEP = ctx.builder.CreateBitCast(
        ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_gep"), i64p);
    llvm::Value* trampolinePtr = ctx.builder.CreateBitCast(trampoline, i8p);
    llvm::Function* pthreadCreate = ctx.getFunction("pthread_create",
        ctx.builder.getInt32Ty(), {i64p, i8p, i8p, i8p});
    ctx.builder.CreateCall(pthreadCreate, {
        tidGEP,
        llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
        trampolinePtr,
        argsVoidPtr
    });

    // 7. Return the handle pointer (stored into the thread_handle* alloca by visitSetStmt)
    return ctx.ir_gen->wrap(handlePtr);
}

IRValue* Compiler::visitAtExpr(AtExpr* at) {
    // Evaluate container — must be a variable (or struct/ptr field path).
    auto* varExpr = dynamic_cast<VariableExpr*>(at->container.get());
    if (!varExpr) {
        LOG_ERROR(ctx.fmtLoc() + "at: container must be a variable or struct field path");
        return nullptr;
    }

    // Class-method this-shortcut: `<` und `<.member` → `this` / `this.member`,
    // analog visitVariable.
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
                    // ir_element_type ist nicht zwingend nötig — die Branches
                    // unten lesen nur typeName + alloca_inst (über ir_alloca_inst).
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
        if (indexVals.empty()) { LOG_ERROR("at: dyn_array requires index"); return nullptr; }
        llvm::Value* elemPtr = ctx.builder.CreateGEP(elemTy, typedPtr, indexVals[0], "darr_gep");
        llvm::Value* val     = ctx.builder.CreateLoad(elemTy, elemPtr, "darr_at");
        return ctx.ir_gen->wrap(val);
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
        if (indexVals.empty()) { LOG_ERROR("at: list requires at least one index"); return nullptr; }
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
                    ctx.stmtTempStack.back().push_back(fv);
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
        llvm::Value* val = ctx.builder.CreateLoad(elemTy, elemPtr, "list_at");
        // Refcount: borrowing a string out of a list adds an alias.  Incref
        // so the caller's local var holds a real ref; when the underlying
        // list is later released (e.g. mirror's aiml_ser_free_decoded), the
        // string survives.  No-op on non-refcounted strings (literals).
        // Repro: tests/test_mirror_close_string_uaf.aiml
        if (elemName == "string") {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* voidTy = llvm::Type::getVoidTy(ctx.context);
            llvm::Function* incref = ctx.module.getFunction("aiml_str_incref");
            if (!incref) {
                auto* fty = llvm::FunctionType::get(voidTy, {i8p}, false);
                incref = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                                "aiml_str_incref", ctx.module);
            }
            ctx.builder.CreateCall(incref, {val});
            // spec/61 §7: this incref made a FRESH owned alias — register it as an owned stmt-temp
            // (like the str.* producers do). A `decl x = at(list,i)` then MOVES it (the decl-init
            // share-incref is skipped for owned temps), so the element is not incref'd TWICE and
            // left leaking when both x's drop and the list release decref only once (grai
            // match_key_of). A transient `at(..)` read is decref'd at statement end → balanced.
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                ctx.stmtTempStack.back().push_back(val);
        }
        return ctx.ir_gen->wrap(val);
    }

    // ── tensor ──
    if (tName == "tensor") {
        // Call aiml_tensor_get(tensor*, i32 index) → f32
        llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* tensorVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), tPtr, "tensor_ptr");
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Function* getFn = ctx.getFunction("aiml_tensor_get", f32, {i8p, i32});
        llvm::Value* val = ctx.builder.CreateCall(getFn, {tensorVal, indexVals[0]}, "tensor_at");
        return ctx.ir_gen->wrap(val);
    }

    LOG_ERROR(ctx.fmtLoc() + "at: unsupported container type '" + tName + "'");
    return nullptr;
}

} // namespace AST
} // namespace AIML
