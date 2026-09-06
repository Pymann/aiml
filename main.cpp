#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include "compiler.h"
#include "src/core/asset_scanner.h"   // AssetScanner::all_stores() → Asset-Deps in die .d schreiben
#include "logger.h"
#include <filesystem>


int main(int argc, char** argv) {
    std::string inputFile;
    std::string outputFile;
    LogLevel logLevel = LogLevel::WARNING;

    bool runMode = false;
    bool emitMain = false;
    bool reportPrefabs = false;
    std::string reportFilter;
    std::string targetTriple;
    std::string assetRoots;  // raw comma-separated string, parsed in compile()

    // Program args: everything after '--' is forwarded to the AIML program
    int programArgc = 0;
    char** programArgv = nullptr;

    // Simple arg parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--") {
            // Everything after '--' becomes the AIML program's argv[1..]
            // We'll prepend argv[0] (inputFile) later
            programArgc = argc - i;  // includes slot for argv[0]
            programArgv = argv + i;  // points at '--'; we'll fix up below
            break;
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string levelStr = argv[++i];
            if (levelStr == "DEBUG") logLevel = LogLevel::DEBUG;
            else if (levelStr == "INFO") logLevel = LogLevel::INFO;
            else if (levelStr == "WARN") logLevel = LogLevel::WARNING;
            else if (levelStr == "ERROR") logLevel = LogLevel::ERROR;
            else if (levelStr == "VERBOSE") logLevel = LogLevel::VERBOSE;
        } else if (arg == "-r" || arg == "--run") {
            runMode = true;
        } else if (arg == "--emit-main") {
            emitMain = true;
        } else if (arg == "--prefab-report") {
            reportPrefabs = true;
        } else if (arg.rfind("--prefab-report=", 0) == 0) {
            // --prefab-report=<scene> — focus on one scene: case-insensitive
            // substring matched against source path / owner class / action class.
            reportPrefabs = true;
            reportFilter = arg.substr(strlen("--prefab-report="));
        } else if (arg == "--target" && i + 1 < argc) {
            targetTriple = argv[++i];
            if (targetTriple == "wasm32") targetTriple = "wasm32-unknown-emscripten";
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg.rfind("--asset-roots=", 0) == 0) {
            // Asset Registry (spec/22, backlog 2026-06-07): scan these roots
            // (dirs or single files) at compile-start, register stems by kind.
            assetRoots = arg.substr(strlen("--asset-roots="));
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
        std::cerr << "Usage: aiml <input.aiml> [-o output.o|output.ll] [--log-level DEBUG|INFO|WARN|ERROR] [-r]" << std::endl;
        return 1;
    }

    // Default: no -o means JIT run (like python)
    if (outputFile.empty() && !runMode) {
        runMode = true;
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
    if (!targetTriple.empty()) compiler.setTargetTriple(targetTriple);
    if (!assetRoots.empty()) compiler.setAssetRoots(assetRoots);
    
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

    if (reportPrefabs) {
        // --prefab-report: analysis-only.  Walk the whole import graph and print
        // a prefab wiring report; emit no objects/binary.  noCache forces every
        // import to be re-walked (a cache hit would skip a module's facts).
        compiler.reportPrefabsMode = true;
        compiler.prefabReportFilter = reportFilter;
        compiler.noCache = true;
        compiler.compile(inputFile, "/dev/null");
        return 0;
    }

    if (runMode) {
        // JIT Mode
        LOG_DEBUG("JIT Run Mode. Saving IR to " + outputFile);
        compiler.compile(inputFile, "/dev/null"); // Compile module
        
        if (Logger::instance().hasErrors()) {
            LOG_ERROR("Compilation failed with errors.");
            return 1;
        }

        compiler.generateEntryPoint();            // Add main wrapper
        if (programArgc > 0) {
            // Build proper argv: inputFile as argv[0], then post-'--' args
            std::vector<char*> progArgvVec;
            progArgvVec.push_back(const_cast<char*>(inputFile.c_str()));
            for (int i = 1; i < programArgc; i++)
                progArgvVec.push_back(programArgv[i]);  // programArgv[0] was '--'
            progArgvVec.push_back(nullptr);
            return compiler.jitRun((int)progArgvVec.size() - 1, progArgvVec.data());
        }
        return compiler.jitRun();
    } 
    
    // Determine Output Type
    bool isLL = outputFile.length() >= 3 && outputFile.substr(outputFile.length() - 3) == ".ll";
    bool isObj = outputFile.length() >= 2 && outputFile.substr(outputFile.length() - 2) == ".o";
    bool isExe = !isLL && !isObj;

    if (isLL) {
        LOG_DEBUG("Standard IR Compilation Mode");
        // When emitting text IR for an external toolchain, also have the
        // import resolver emit .ll side files (instead of .o) — exodus picks
        // them up automatically. Native object builds keep .o behavior.
        compiler.emitImportsAsLL = true;
        // Disable internal import cache — the build tool (exodus) handles
        // caching and expects side files next to the main output.
        compiler.noCache = true;
        compiler.outputDir = std::filesystem::path(outputFile).parent_path().string();
        // Compile and generate entry point for LL debugging
        compiler.compile(inputFile, outputFile); // Parse and populate module

        if (Logger::instance().hasErrors()) {
            LOG_ERROR("Compilation failed with errors.");
            return 1;
        }

        // --emit-main: add main entry point and re-emit IR text.
        // Required when the .ll is consumed standalone by another toolchain
        // (e.g. emcc → WASM) since emcc needs a main() to produce a runnable.
        if (emitMain) {
            compiler.generateEntryPoint();
            compiler.saveModule(outputFile);
        }
        // This writes file WITHOUT entry point wrapper (unless --emit-main).

        // Write .d depfile (same shape as the .o path uses) so exodus can
        // resolve the expected import side-files (.ll) for the link step.
        {
            std::string depPath = outputFile.substr(0, outputFile.size() - 3) + ".d";
            std::ofstream depOut(depPath);
            depOut << outputFile << ": " << inputFile;
            for (const auto& src : compiler.importedSourceFiles) {
                depOut << " \\\n  " << src;
            }
            // Asset-Deps: die via --asset-roots gescannten Assets werden in die
            // Registry gebacken → dieses Objekt hängt an ihren Pfaden.  So triggert
            // Exodus' .d-mtime-Check einen Recompile, wenn ein Asset sich ändert
            // (kein --clean mehr nötig nach png→webp o.ä.).
            for (const auto& kv : AIML::AssetScanner::instance().all_stores()) {
                for (const auto& se : kv.second) {
                    depOut << " \\\n  " << se.second.abs_path;
                }
            }
            depOut << "\n";
        }
        
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
             // Write standard .d dependency file (Makefile format) for build systems
             std::string depPath = objFile.substr(0, objFile.size() - 2) + ".d";
             {
                 std::ofstream depOut(depPath);
                 depOut << objFile << ": " << inputFile;
                 for (const auto& src : compiler.importedSourceFiles) {
                     depOut << " \\\n  " << src;
                 }
                 // Asset-Deps (siehe .ll-Zweig): gescannte Assets als Abhängigkeiten
                 // → Recompile bei Asset-Änderung ohne --clean.
                 for (const auto& kv : AIML::AssetScanner::instance().all_stores()) {
                     for (const auto& se : kv.second) {
                         depOut << " \\\n  " << se.second.abs_path;
                     }
                 }
                 depOut << "\n";
             }

             if (!compiler.linkedObjects.empty()) {
                 std::string linkMsg = "Compiled " + outputFile + ". Link with: ";
                 for (const auto& dep : compiler.linkedObjects) linkMsg += dep + " ";
                 LOG_INFO(linkMsg);
             }

             // Remove stale .deps sidecar (legacy format)
             std::filesystem::remove(objFile + ".deps");
        }
    }
    
    return 0;
}
