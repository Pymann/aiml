#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>
#include <regex>
#include "list_impl.h"

namespace StringUtils {

    // Helper to declare external C functions if missing
    // MOVED TO utils.h for shared use

    inline llvm::Value* concat(CompilerContext& ctx, llvm::Value* s1, llvm::Value* s2) {
        // char* concat(char* s1, char* s2)
        // 1. len1 = strlen(s1)
        // 2. len2 = strlen(s2)
        // 3. new_len = len1 + len2 + 1
        // 4. ptr = malloc(new_len)
        // 5. strcpy(ptr, s1)
        // 6. strcat(ptr, s2)
        // 7. return ptr
        
        llvm::Type* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();

        llvm::Function* strlenFunc = ctx.getFunction("strlen", i64, {i8Ptr});
        llvm::Function* mallocFunc = ctx.getFunction("malloc", i8Ptr, {i64});
        llvm::Function* strcpyFunc = ctx.getFunction("strcpy", i8Ptr, {i8Ptr, i8Ptr});
        llvm::Function* strcatFunc = ctx.getFunction("strcat", i8Ptr, {i8Ptr, i8Ptr});

        llvm::Value* len1 = ctx.builder.CreateCall(strlenFunc, {s1});
        llvm::Value* len2 = ctx.builder.CreateCall(strlenFunc, {s2});
        
        llvm::Value* sum = ctx.builder.CreateAdd(len1, len2);
        llvm::Value* size = ctx.builder.CreateAdd(sum, ctx.builder.getInt64(1)); // +1 for null
        
        llvm::Value* newStr = ctx.builder.CreateCall(mallocFunc, {size});
        
        ctx.builder.CreateCall(strcpyFunc, {newStr, s1});
        ctx.builder.CreateCall(strcatFunc, {newStr, s2});
        
        return newStr;
    }

    inline llvm::Value* intToString(CompilerContext& ctx, llvm::Value* intVal) {
        // char* intToString(int i)
        // malloc(12) -> enough for i32
        // sprintf(buf, "%d", i)
        // return buf
        
        llvm::Type* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        
        llvm::Function* mallocFunc = ctx.getFunction("malloc", i8Ptr, {i64});
        llvm::Function* sprintfFunc = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8Ptr, i8Ptr}, true);
        
        llvm::Value* buf = ctx.builder.CreateCall(mallocFunc, {ctx.builder.getInt64(12)});
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%d");
        
        ctx.builder.CreateCall(sprintfFunc, {buf, fmt, intVal});
        return buf;
    }

    inline llvm::Value* int64ToString(CompilerContext& ctx, llvm::Value* intVal) {
        // char* int64ToString(int64_t i)
        // malloc(24) -> enough for i64
        // sprintf(buf, "%ld", i)
        
        llvm::Type* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        
        llvm::Function* mallocFunc = ctx.getFunction("malloc", i8Ptr, {i64});
        // sprintf varargs
        llvm::Function* sprintfFunc = ctx.getFunction("sprintf", ctx.builder.getInt32Ty(), {i8Ptr, i8Ptr}, true);
        
        llvm::Value* buf = ctx.builder.CreateCall(mallocFunc, {ctx.builder.getInt64(24)});
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("%ld");
        
        ctx.builder.CreateCall(sprintfFunc, {buf, fmt, intVal});
        return buf;
    }

    inline llvm::Value* expandExpression(CompilerContext& ctx, const std::string& input, CompilerContext::Evaluator evaluator) {
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
                        LOG_ERROR("YAML Parse Error in String Expansion: " + content + " -> " + e.what());
                        val = ctx.builder.CreateGlobalStringPtr("ERROR"); 
                    }
                } else {
                     // No evaluator provided (e.g. from resolveValue fallback)
                     // Does this happen in SysGetArg? Only if arg is "${...}"
                     // Just return the raw string or empty?
                     // Let's return the content as is, wrapped in braces? or just "<???>"
                     LOG_ERROR("String Expansion requires evaluator: " + content);
                     val = ctx.builder.CreateGlobalStringPtr("<???>");
                }
                
                // Convert to String
                llvm::Value* varStr = nullptr;
                if (val) {
                     if (val->getType()->isIntegerTy(32)) {
                         varStr = intToString(ctx, val);
                     } else if (val->getType()->isIntegerTy(64)) {
                         varStr = int64ToString(ctx, val);
                     } else if (val->getType()->isPointerTy()) {
                         // Check if i8*
                         varStr = val; 
                     } else {
                         // Try to cast or print error?
                         // For now assume all pointers are strings (risky)
                         varStr = ctx.builder.CreateBitCast(val, ctx.builder.getInt8Ty()->getPointerTo());
                     }
                } else {
                     varStr = ctx.builder.CreateGlobalStringPtr("null");
                }
                
                currentStr = concat(ctx, currentStr, varStr);
                pos = scan + 1;
            } else {
                // No closing brace found, treat as literal
                LOG_ERROR("Unmatched brace in expression: " + input);
                std::string literal = input.substr(start);
                currentStr = concat(ctx, currentStr, ctx.builder.CreateGlobalStringPtr(literal));
                break;
            }
        }
        
        return currentStr;
    }
}
