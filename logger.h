#pragma once
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstring>

enum class LogLevel {
    VERBOSE,
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    void setLevel(LogLevel level) {
        level_ = level;
    }

    bool hasErrors() const {
        return errorCount_ > 0;
    }
    
    void clearErrors() {
        std::lock_guard<std::mutex> lock(mutex_);
        errorCount_ = 0;
    }

    int errorCount() const { return errorCount_; }
    void setErrorCount(int n) { errorCount_ = n; }

    // Suppress all output (errors still counted but not printed)
    void setSuppressed(bool s) { suppressed_ = s; }
    bool isSuppressed() const { return suppressed_; }

    // --check (spec/49): Befunde strukturiert mitschneiden statt nur zu drucken.
    // Gesammelt wird der fertige Meldungstext samt "file:line:col: "-Praefix plus
    // die Stelle im Compiler, die ihn ausgeloest hat — letztere ist heute die
    // einzige stabile Kennung eines Checks (siehe spec/49, offener Punkt Codes).
    struct Diag { const char* sev; std::string text; std::string origin; };
    void setCollect(bool c) { collect_ = c; }
    const std::vector<Diag>& diags() const { return diags_; }

    template<typename T>
    void log(LogLevel level, const T& msg, const char* file = nullptr, int line = 0) {
        if (collect_ && (level == LogLevel::ERROR || level == LogLevel::WARNING)) {
            std::ostringstream os; os << msg;
            std::string org;
            if (file) {
                const char* base = std::strrchr(file, '/');
                org = (base ? base + 1 : file);
                org += ":" + std::to_string(line);
            }
            diags_.push_back({level == LogLevel::ERROR ? "error" : "warning", os.str(), org});
        }
        if (level < level_) return;
        if (suppressed_) {
            if (level == LogLevel::ERROR) errorCount_++;
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (level) {
            case LogLevel::VERBOSE:
                std::cout << "[VERBOSE] ";
                break;
            case LogLevel::DEBUG:
                std::cout << "[DEBUG] ";
                break;
            case LogLevel::INFO:
                std::cout << "[INFO]  ";
                break;
            case LogLevel::WARNING:
                std::cout << "[WARNING]  ";
                break;
            case LogLevel::ERROR:
                std::cerr << "[ERROR] ";
                break;
        }
        
        if (file) {
            if (level == LogLevel::ERROR) {
                std::cerr << "[" << file << ":" << line << "] ";
            } else {
                std::cout << "[" << file << ":" << line << "] ";
            }
        }
        
        if (level == LogLevel::ERROR) {
            errorCount_++;
            std::cerr << msg << std::endl;
        } else {
            std::cout << msg << std::endl;
        }
    }

private:
    Logger() : level_(LogLevel::INFO), errorCount_(0), suppressed_(false), collect_(false) {}
    LogLevel level_;
    int errorCount_;
    bool collect_;
    std::vector<Diag> diags_;
    bool suppressed_;
    std::mutex mutex_;
};

// Macros for cleaner usage
#define LOG_VERBOSE(msg) Logger::instance().log(LogLevel::VERBOSE, msg, __FILE__, __LINE__)
#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::instance().log(LogLevel::INFO, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::instance().log(LogLevel::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERROR, msg, __FILE__, __LINE__)
