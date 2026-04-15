#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>
#include <regex>
#include "list_impl.h"

namespace StringUtils {

    // Helper to declare external C functions if missing
    // MOVED TO utils.h for shared use

    // Emit __aiml_str_concat once per module; subsequent calls just emit a call instruction.
    inline llvm::Value* concat(CompilerContext& ctx, llvm::Value* s1, llvm::Value* s2) {
        llvm::PointerType* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();

        llvm::Function* impl = ctx.module.getFunction("__aiml_str_concat");
        if (!impl) {
            llvm::Function* strlenFunc = ctx.getFunction("strlen", i64, {i8Ptr});
            llvm::Function* mallocFunc = ctx.getFunction("malloc", i8Ptr, {i64});
            llvm::Function* strcpyFunc = ctx.getFunction("strcpy", i8Ptr, {i8Ptr, i8Ptr});
            llvm::Function* strcatFunc = ctx.getFunction("strcat", i8Ptr, {i8Ptr, i8Ptr});

            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8Ptr, {i8Ptr, i8Ptr}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_str_concat", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.context, "entry", impl);
            ctx.builder.SetInsertPoint(entry);

            llvm::Value* a1 = impl->getArg(0);
            llvm::Value* a2 = impl->getArg(1);
            llvm::Value* nullPtr  = llvm::ConstantPointerNull::get(i8Ptr);
            llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("", "empty");
            llvm::Value* safeS1 = ctx.builder.CreateSelect(ctx.builder.CreateICmpEQ(a1, nullPtr), emptyStr, a1);
            llvm::Value* safeS2 = ctx.builder.CreateSelect(ctx.builder.CreateICmpEQ(a2, nullPtr), emptyStr, a2);
            llvm::Value* len1 = ctx.builder.CreateCall(strlenFunc, {safeS1});
            llvm::Value* len2 = ctx.builder.CreateCall(strlenFunc, {safeS2});
            llvm::Value* size = ctx.builder.CreateAdd(ctx.builder.CreateAdd(len1, len2), ctx.builder.getInt64(1));
            llvm::Value* newStr = ctx.builder.CreateCall(mallocFunc, {size});
            ctx.builder.CreateCall(strcpyFunc, {newStr, safeS1});
            ctx.builder.CreateCall(strcatFunc, {newStr, safeS2});
            ctx.builder.CreateRet(newStr);
            ctx.builder.restoreIP(savedIP);
        }
        LOG_VERBOSE("[IR] concat: calling __aiml_str_concat");
        return ctx.builder.CreateCall(impl, {s1, s2});
    }

    // Emit __aiml_int_to_string once per module.
    inline llvm::Value* intToString(CompilerContext& ctx, llvm::Value* intVal) {
        llvm::PointerType* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();

        llvm::Function* impl = ctx.module.getFunction("__aiml_int_to_string");
        if (!impl) {
            llvm::Function* mallocFunc  = ctx.getFunction("malloc",  i8Ptr, {i64});
            llvm::Function* sprintfFunc = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8Ptr, i8Ptr}, true);

            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8Ptr, {ctx.builder.getInt32Ty()}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_int_to_string", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.context, "entry", impl);
            ctx.builder.SetInsertPoint(entry);
            llvm::Value* buf = ctx.builder.CreateCall(mallocFunc, {ctx.builder.getInt64(12)});
            llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%d", "fmt_d");
            ctx.builder.CreateCall(sprintfFunc, {buf, fmt, impl->getArg(0)});
            ctx.builder.CreateRet(buf);
            ctx.builder.restoreIP(savedIP);
        }
        LOG_VERBOSE("[IR] intToString: calling __aiml_int_to_string");
        return ctx.builder.CreateCall(impl, {intVal});
    }

    // Emit __aiml_int64_to_string once per module.
    inline llvm::Value* int64ToString(CompilerContext& ctx, llvm::Value* intVal) {
        llvm::PointerType* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();

        llvm::Function* impl = ctx.module.getFunction("__aiml_int64_to_string");
        if (!impl) {
            llvm::Function* mallocFunc  = ctx.getFunction("malloc",  i8Ptr, {i64});
            llvm::Function* sprintfFunc = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8Ptr, i8Ptr}, true);

            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8Ptr, {i64}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_int64_to_string", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.context, "entry", impl);
            ctx.builder.SetInsertPoint(entry);
            llvm::Value* buf = ctx.builder.CreateCall(mallocFunc, {ctx.builder.getInt64(24)});
            llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%ld", "fmt_ld");
            ctx.builder.CreateCall(sprintfFunc, {buf, fmt, impl->getArg(0)});
            ctx.builder.CreateRet(buf);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.builder.CreateCall(impl, {intVal});
    }

    // Emit __aiml_f32_to_string once per module: sprintf(buf, "%.6g", val) -> i8*
    inline llvm::Value* floatToString(CompilerContext& ctx, llvm::Value* floatVal) {
        llvm::PointerType* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type*        i64  = ctx.builder.getInt64Ty();
        llvm::Type*        f32  = ctx.builder.getFloatTy();

        llvm::Function* impl = ctx.module.getFunction("__aiml_f32_to_string");
        if (!impl) {
            llvm::Function* mallocFn  = ctx.getFunction("malloc",  i8p, {i64});
            llvm::Function* sprintfFn = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8p, i8p}, true);
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8p, {f32}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_f32_to_string", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.context, "entry", impl);
            ctx.builder.SetInsertPoint(entry);
            llvm::Value* buf = ctx.builder.CreateCall(mallocFn, {ctx.builder.getInt64(32)});
            llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%.6g", "fmt_f32");
            // widen f32 to f64 for vararg sprintf
            llvm::Value* ext = ctx.builder.CreateFPExt(impl->getArg(0), ctx.builder.getDoubleTy());
            ctx.builder.CreateCall(sprintfFn, {buf, fmt, ext});
            ctx.builder.CreateRet(buf);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.builder.CreateCall(impl, {floatVal});
    }

    // Emit __aiml_f64_to_string once per module: sprintf(buf, "%.15g", val) -> i8*
    inline llvm::Value* doubleToString(CompilerContext& ctx, llvm::Value* dblVal) {
        llvm::PointerType* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type*        i64 = ctx.builder.getInt64Ty();
        llvm::Type*        f64 = ctx.builder.getDoubleTy();

        llvm::Function* impl = ctx.module.getFunction("__aiml_f64_to_string");
        if (!impl) {
            llvm::Function* mallocFn  = ctx.getFunction("malloc",  i8p, {i64});
            llvm::Function* sprintfFn = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8p, i8p}, true);
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8p, {f64}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_f64_to_string", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.context, "entry", impl);
            ctx.builder.SetInsertPoint(entry);
            llvm::Value* buf = ctx.builder.CreateCall(mallocFn, {ctx.builder.getInt64(32)});
            llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%.15g", "fmt_f64");
            ctx.builder.CreateCall(sprintfFn, {buf, fmt, impl->getArg(0)});
            ctx.builder.CreateRet(buf);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.builder.CreateCall(impl, {dblVal});
    }

    // bool (i8) -> "true" or "false" via select on a constant
    inline llvm::Value* boolToString(CompilerContext& ctx, llvm::Value* boolVal) {
        llvm::Value* trueStr  = ctx.builder.CreateGlobalStringPtr("true",  "str_true");
        llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false", "str_false");
        llvm::Value* cond = ctx.builder.CreateICmpNE(boolVal, ctx.builder.getInt8(0), "bool_cond");
        return ctx.builder.CreateSelect(cond, trueStr, falseStr, "bool_str");
    }

    inline llvm::Value* expandExpression(CompilerContext& ctx, const std::string& input, std::function<llvm::Value*(CompilerContext&, const YAML::Node&)> evaluator) {
        llvm::Value* currentStr = ctx.builder.CreateGlobalStringPtr("");
        
        size_t pos = 0;
        LOG_DEBUG("Expanding Expression: " + input);
        
        while (pos < input.length()) {
            size_t start = input.find("${", pos);
            if (start == std::string::npos) {
                // No more expressions
                std::string suffix = input.substr(pos);
                if (!suffix.empty()) {
                    currentStr = concat(ctx, currentStr, ctx.builder.CreateGlobalStringPtr(suffix));
                }
                break;
            }
            
            // Append prefix
            if (start > pos) {
                std::string prefix = input.substr(pos, start - pos);
                currentStr = concat(ctx, currentStr, ctx.builder.CreateGlobalStringPtr(prefix));
            }
            
            // Find closing brace matching ${
            size_t scan = start + 2;
            int depth = 1; // Started with {
            bool inString = false;
            char quoteChar = 0;
            bool found = false;
            
            // LOG_DEBUG("Scanning from: " + std::to_string(scan));
            
            while (scan < input.length()) {
                char c = input[scan];
                
                if (inString) {
                    if (c == quoteChar) {
                         bool escaped = (scan > 0 && input[scan-1] == '\\');
                         if (!escaped) inString = false;
                    }
                } else {
                    if (c == '"' || c == '\'') {
                        inString = true;
                        quoteChar = c;
                    } else if (c == '{') {
                        depth++;
                    } else if (c == '}') {
                        depth--;
                        if (depth == 0) {
                            found = true;
                            // scan is now at the closing brace
                            break;
                        }
                    }
                }
                scan++;
            }
            
            if (found) {
                std::string content = input.substr(start + 2, scan - (start + 2));
                // Trim whitespace
                size_t first = content.find_first_not_of(" \t");
                if (first == std::string::npos) content = "";
                else {
                    size_t last = content.find_last_not_of(" \t");
                    content = content.substr(first, (last - first + 1));
                }
                
                LOG_DEBUG("Found Expression Content: '" + content + "'");
                
                // Evaluate Content
                llvm::Value* val = nullptr;
                if (evaluator) {
                    try {
                        YAML::Node node = YAML::Load(content);
                        val = evaluator(ctx, node);
                    } catch (const std::exception& e) {
                        LOG_ERROR(ctx.fmtLoc() + "YAML Parse Error in String Expansion: " + content + " -> " + e.what());
                        val = ctx.builder.CreateGlobalStringPtr("ERROR"); 
                    }
                } else {
                     // No evaluator provided (e.g. from resolveValue fallback)
                     // Does this happen in SysGetArg? Only if arg is "${...}"
                     // Just return the raw string or empty?
                     // Let's return the content as is, wrapped in braces? or just "<???>"
                     LOG_ERROR(ctx.fmtLoc() + "String Expansion requires evaluator: " + content);
                     val = ctx.builder.CreateGlobalStringPtr("<???>");
                }
                
                // Convert to String
                llvm::Value* varStr = nullptr;
                if (val) {
                     if (val->getType()->isIntegerTy(64)) {
                         varStr = int64ToString(ctx, val);
                     } else if (val->getType()->isIntegerTy(32)) {
                         varStr = intToString(ctx, val);
                     } else if (val->getType()->isIntegerTy(1)
                             || val->getType()->isIntegerTy(8)
                             || val->getType()->isIntegerTy(16)) {
                         // bool / i8 / i16 — widen to i32 before converting
                         llvm::Value* wide = ctx.builder.CreateZExt(val, ctx.builder.getInt32Ty());
                         varStr = intToString(ctx, wide);
                     } else if (val->getType()->isFloatTy()) {
                         varStr = floatToString(ctx, val);
                     } else if (val->getType()->isDoubleTy()) {
                         varStr = doubleToString(ctx, val);
                     } else if (val->getType()->isPointerTy()) {
                         varStr = val; // assume i8* string
                     } else {
                         varStr = ctx.builder.CreateGlobalStringPtr("?");
                     }
                } else {
                     varStr = ctx.builder.CreateGlobalStringPtr("null");
                }
                
                currentStr = concat(ctx, currentStr, varStr);
                pos = scan + 1;
            } else {
                // No closing brace found, treat as literal
                LOG_ERROR(ctx.fmtLoc() + "Unmatched brace in expression: " + input);
                std::string literal = input.substr(start);
                currentStr = concat(ctx, currentStr, ctx.builder.CreateGlobalStringPtr(literal));
                break;
            }
        }
        
        return currentStr;
    }
}
