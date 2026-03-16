#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "compiler.h"
#include "logger.h"
#include <filesystem>


int main(int argc, char** argv) {
    std::string inputFile;
    std::string outputFile;
    LogLevel logLevel = LogLevel::WARNING;

    bool runMode = false;

    // Simple arg parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-level" && i + 1 < argc) {
            std::string levelStr = argv[++i];
            if (levelStr == "DEBUG") logLevel = LogLevel::DEBUG;
            else if (levelStr == "INFO") logLevel = LogLevel::INFO;
            else if (levelStr == "WARN") logLevel = LogLevel::WARNING;
            else if (levelStr == "ERROR") logLevel = LogLevel::ERROR;
            else if (levelStr == "VERBOSE") logLevel = LogLevel::VERBOSE;
        } else if (arg == "-r" || arg == "--run") {
            runMode = true;
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        } else {
            if (!inputFile.empty()) {
                std::cerr << "Multiple input files specified: " << inputFile << " and " << arg << std::endl;
                return 1;
            }
            inputFile = arg;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Usage: aiml <input.aiml> [--log-level DEBUG|INFO|WARN|ERROR] [-v] [-r|--run] [-o output.ll]" << std::endl;
        return 1;
    }

    if (outputFile.empty()) {
        size_t lastDot = inputFile.find_last_of(".");
        if (lastDot != std::string::npos) {
            outputFile = inputFile.substr(0, lastDot) + ".ll";
        } else {
            outputFile = inputFile + ".ll";
        }
    }

    Logger::instance().setLevel(logLevel);
    
    AIMLCompiler compiler;
    
    // Resolve compiler path (argv[0]) to absolute path for recursive calls
    try {
        if (argc > 0) {
            std::string argv0 = argv[0];
            if (std::filesystem::exists(argv0)) {
                std::filesystem::path exePath = std::filesystem::absolute(argv0);
                compiler.compilerPath = exePath.string();
            } else {
                // Not a local path, assume in PATH
                // check if "which argv0" or similar?
                // For now, just use argv0 as is, hoping system() finds it.
                // Or try to resolve using 'which'?
                compiler.compilerPath = argv0; 
            }
            LOG_DEBUG("Compiler Path: " + compiler.compilerPath);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to resolve compiler path: " + std::string(e.what()));
    }

    
    // Check if output is object file
    LOG_DEBUG("inputFile=" + inputFile);
    LOG_DEBUG("outputFile=" + outputFile);
    LOG_DEBUG("runMode=" + std::to_string(runMode));
    
    bool isObjectFile = outputFile.length() >= 2 && outputFile.substr(outputFile.length() - 2) == ".o";
    LOG_DEBUG("isObjectFile=" + std::to_string(isObjectFile));

    if (runMode) {
        // JIT Mode
        LOG_DEBUG("JIT Run Mode. Saving IR to " + outputFile);
        compiler.compile(inputFile, "/dev/null"); // Compile module
        
        if (Logger::instance().hasErrors()) {
            LOG_ERROR("Compilation failed with errors.");
            return 1;
        }

        compiler.generateEntryPoint();            // Add main wrapper
        return compiler.jitRun();
    } 
    
    // Determine Output Type
    bool isLL = outputFile.length() >= 3 && outputFile.substr(outputFile.length() - 3) == ".ll";
    bool isObj = outputFile.length() >= 2 && outputFile.substr(outputFile.length() - 2) == ".o";
    bool isExe = !isLL && !isObj;

    if (isLL) {
        LOG_DEBUG("Standard IR Compilation Mode");
        // Compile and generate entry point for LL debugging
        compiler.compile(inputFile, outputFile); // Parse and populate module
        
        if (Logger::instance().hasErrors()) {
            LOG_ERROR("Compilation failed with errors.");
            return 1;
        }
        
        // This writes file WITHOUT entry point wrapper.
        
        // We can't easily print module from here without LLVM headers.
        // HACK: call compile again with empty file? No.
        // Better: compiler should have emitIR method?
        // OR: Since we exposed generateEntryPoint, we can't use compiler.compile(..., output) because it writes too early.
        // Let's assume user is okay with NO entry point in .ll if they didn't ask for executable?
        // But for debugging we want it.
        // Let's add 'saveIR' to compiler? 
        // For now: Just use valid output file in compile() for parsing, but that doesn't include entry point.
        
        // If users want to see wrapper, this is a limitation.
        // For 'test_namespace.aiml', we want to run it (-r). 
        // The -r path is above (runMode). 
        // The -o output.ll path is for inspection.
        
    } else {
        // Compile to Object (Intermediate or Final)
        std::string objFile = isExe ? outputFile + ".o" : outputFile;

        LOG_DEBUG("Compiling to Object: " + objFile);

        // Disable internal import cache — the build tool handles caching.
        compiler.noCache = true;
        // Store import .o files next to the main .o file.
        compiler.outputDir = std::filesystem::path(objFile).parent_path().string();

        // 1. Compile Module
        compiler.compile(inputFile, "/dev/null"); 
        
        if (Logger::instance().hasErrors()) {
            LOG_ERROR("Compilation failed with errors.");
            return 1;
        }
        
        // 2. Generate Entry Point — always for .o and .exe
        compiler.generateEntryPoint();
        
        // 3. Emit Object
        compiler.compileToObject(inputFile, objFile);
        
        // Linking
        if (isExe) {
            LOG_INFO("Linking executable: " + outputFile);
            std::string linkCmd = "clang -o \"" + outputFile + "\" \"" + objFile + "\"";
            
            // Link math library handling? (Standard C libs are linked by clang default)
            linkCmd += " -lm -lstdc++"; // Ensure stdlibs

            for (const auto& dep : compiler.linkedObjects) {
                linkCmd += " \"" + dep + "\"";
            }
            
            LOG_DEBUG("Link Command: " + linkCmd);
            int ret = std::system(linkCmd.c_str());
            if (ret != 0) {
                LOG_ERROR("Linking failed.");
                return 1;
            }
            
            // Cleanup temp object
            if (isExe) {
                std::filesystem::remove(objFile);
            }
            
        } else {
             // Just object file — write sidecar .deps file for build systems (e.g. exodus)
             std::string depsPath = objFile + ".deps";
             if (!compiler.linkedObjects.empty()) {
                 std::ofstream depsFout(depsPath);
                 for (const auto& dep : compiler.linkedObjects) depsFout << dep << "\n";
                 std::string linkMsg = "Compiled " + outputFile + ". Link with: ";
                 for (const auto& dep : compiler.linkedObjects) linkMsg += dep + " ";
                 LOG_INFO(linkMsg);
             } else {
                 // Remove stale deps file if no extra objects
                 std::filesystem::remove(depsPath);
             }
        }
    }
    
    return 0;
}
