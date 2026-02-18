#include <iostream>
#include <string>
#include <vector>
#include "compiler.h"
#include "logger.h"

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
        } else if (arg == "-v") {
            logLevel = LogLevel::DEBUG;
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
    
    // Check if output is object file
    LOG_DEBUG("inputFile=" + inputFile);
    LOG_DEBUG("outputFile=" + outputFile);
    LOG_DEBUG("runMode=" + std::to_string(runMode));
    
    bool isObjectFile = outputFile.length() >= 2 && outputFile.substr(outputFile.length() - 2) == ".o";
    LOG_DEBUG("isObjectFile=" + std::to_string(isObjectFile));

    if (runMode) {
        // JIT Mode
        compiler.compile(inputFile, "/dev/null"); 
        return compiler.jitRun();
    } else if (isObjectFile) {
        // Compile to Object Mode
        LOG_DEBUG("Calling compileToObject");
        compiler.compile(inputFile, "/dev/null"); // Populate module
        compiler.compileToObject(inputFile, outputFile);
    } else {
        // Standard IR Compilation Mode
        LOG_DEBUG("Calling compile");
        compiler.compile(inputFile, outputFile);
    }
    
    return 0;
}
