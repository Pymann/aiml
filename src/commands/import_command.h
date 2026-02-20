#pragma once
#include "../core/context.h"
#include "command.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdio>

namespace fs = std::filesystem;

class ImportCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: import: "path/to/file.aiml"
        if (!node["import"].IsScalar()) return;
        
        std::string importPathRaw = node["import"].as<std::string>();
        fs::path importPath(importPathRaw);
        
        // 1. Path Resolution (Relative to CWD)
        fs::path fullPath = fs::absolute(importPath);
        if (!fs::exists(fullPath)) {
             LOG_ERROR("Import file not found: " + fullPath.string());
             return;
        }
        
        // Canonical to avoid duplicates
        fullPath = fs::canonical(fullPath);
        std::string fullPathStr = fullPath.string();

        // Check Cycle/Duplication (In-Memory)
        if (ctx.loadedFiles.count(fullPathStr)) {
            LOG_DEBUG("File already loaded: " + fullPathStr);
            return;
        }
        
        // Integrity Check (compilations.yaml)
        checkIntegrity(fullPathStr);

        // Add to loaded files
        ctx.loadedFiles.insert(fullPathStr);
        
        // 2. Determine Mode
        if (ctx.jitMode) {
            // JIT Mode: Compile source in-place
            LOG_INFO("Importing (JIT): " + fullPathStr);
            
            // recursive parse
            try {
                YAML::Node importedConfig = YAML::LoadFile(fullPathStr);
                
                // Save context state if needed?
                // For now, assume single module, shared context.
                // But imported file might have 'namespace' command.
                // We must restore user's namespace after import!
                std::string oldNamespace = ctx.currentNamespace;
                ctx.currentNamespace = ""; // Reset for imported file? Or inherit?
                // Spec says "namespace command sets namespace for entire file".
                // So imported file should start with empty (global) or set its own.
                // It should NOT inherit.
                
                if (ctx.params.compileCallback) {
                    ctx.params.compileCallback(ctx, importedConfig);
                }
                
                ctx.currentNamespace = oldNamespace; // Restore
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to parse import " + fullPathStr + ": " + e.what());
            }

        } else {
            // Static Compilation Mode (Split)
            compileStatic(ctx, fullPath);
        }
    }

private:
    void compileStatic(CompilerContext& ctx, const fs::path& sourcePath) {
        // Cache Dir logic
        // Env var AIML_CACHE or default __aiml_cache
        std::string cacheDirName = "__aiml_cache";
        if (const char* env_p = std::getenv("AIML_CACHE")) {
             cacheDirName = env_p;
        }
        fs::path cacheDir = fs::current_path() / cacheDirName;
        
        // Mirror structure? "src/utils/math.aiml" -> "cache/src/utils/math.o"
        // Need relative path from CWD
        fs::path relPath = fs::relative(sourcePath, fs::current_path());
        if (relPath.empty()) relPath = sourcePath.filename();
        
        fs::path objectPath = cacheDir / relPath;
        objectPath.replace_extension(".o");
        
        // Ensure dirs exist
        fs::create_directories(objectPath.parent_path());
        
        // Check timestamps
        bool needsRecompile = true;
        if (fs::exists(objectPath)) {
            auto srcTime = fs::last_write_time(sourcePath);
            auto objTime = fs::last_write_time(objectPath);
            if (objTime >= srcTime) {
                needsRecompile = false;
            }
        }
        
        if (needsRecompile) {
            LOG_INFO("Compiling " + sourcePath.string() + " -> " + objectPath.string());
            
            // Spawn separate compiler process
            // Command: aiml <source> -o <object>
            if (ctx.compilerPath.empty()) {
                LOG_ERROR("Compiler path not set. Cannot spawn recursive compilation.");
                return;
            }
            
            std::string cmd = ctx.compilerPath + " \"" + sourcePath.string() + "\" -o \"" + objectPath.string() + "\"";
            LOG_DEBUG("Exec: " + cmd);
            
            int ret = std::system(cmd.c_str());
            if (ret != 0) {
                 LOG_ERROR("Compilation failed for " + sourcePath.string());
                 // Don't register object if failed?
                 return;
            }
        } else {
            LOG_DEBUG("Using cached: " + objectPath.string());
        }
        
        // Register Object for Linking
        ctx.objectFilesToLink.push_back(objectPath.string());
        
        // Header Scan: Parse source in HeaderOnlyMode to register types/functions
        LOG_DEBUG("Header Scan: " + sourcePath.string());
        bool oldHeaderMode = ctx.headerOnlyMode;
        std::string oldNamespace = ctx.currentNamespace;
        
        ctx.headerOnlyMode = true;
        ctx.currentNamespace = ""; // Reset for imported file
        
        try {
            YAML::Node importedConfig = YAML::LoadFile(sourcePath.string());
            if (ctx.params.compileCallback) {
                ctx.params.compileCallback(ctx, importedConfig);
            }
        } catch (const std::exception& e) {
             LOG_ERROR("Failed to parse header " + sourcePath.string() + ": " + e.what());
        }
        
        ctx.headerOnlyMode = oldHeaderMode;
        ctx.currentNamespace = oldNamespace;
    }

    void checkIntegrity(const std::string& sourcePath) {
        fs::path path(sourcePath);
        std::string filename = path.filename().string();
        
        // Calculate SHA512
        std::string currentHash = calculateSHA512(sourcePath);
        if (currentHash.empty()) {
             LOG_ERROR("Failed to calculate hash for " + sourcePath);
             return;
        }
        
        // Load Manifest
        // Manifest is in cache dir
        std::string cacheDirName = "__aiml_cache";
        if (const char* env_p = std::getenv("AIML_CACHE")) cacheDirName = env_p;
        fs::path cacheDir = fs::current_path() / cacheDirName;
        fs::create_directories(cacheDir);
        fs::path manifestPath = cacheDir / "compilations.yaml";
        
        YAML::Node manifest;
        if (fs::exists(manifestPath)) {
            try {
                manifest = YAML::LoadFile(manifestPath.string());
            } catch(...) { /* ignore corrupt manifest */ }
        }
        
        if (manifest[filename]) {
             std::string storedHash = manifest[filename].as<std::string>();
             if (storedHash != currentHash) {
                  std::string msg = "Integrity Violation! File '" + filename + "' has changed (Hash Mismatch).\n";
                  msg += "Redundant filenames (no matter folder location) are not allowed.\n";
                  msg += "Aborting to prevent inconsistent build.";
                  LOG_ERROR(msg);
                  std::cerr << msg << std::endl;
                  std::exit(1); // Abort immediately
             }
        } else {
             // New entry
             manifest[filename] = currentHash;
             // Write back
             std::ofstream fout(manifestPath.string());
             fout << manifest;
        }
    }
    
    std::string calculateSHA512(const std::string& filepath) {
        // Use sha512sum command
        std::string cmd = "sha512sum \"" + filepath + "\"";
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) return "";
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        // Output format: "hash  filename\n"
        std::stringstream ss(result);
        std::string hash;
        ss >> hash;
        return hash;
    }
};
