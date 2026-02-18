#pragma once
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>

enum class LogLevel {
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

    template<typename T>
    void log(LogLevel level, const T& msg, const char* file = nullptr, int line = 0) {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (level) {
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
            std::cerr << msg << std::endl;
        } else {
            std::cout << msg << std::endl;
        }
    }

private:
    Logger() : level_(LogLevel::INFO) {} // Default level
    LogLevel level_;
    std::mutex mutex_;
};

// Macros for cleaner usage
#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::instance().log(LogLevel::INFO, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::instance().log(LogLevel::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERROR, msg, __FILE__, __LINE__)
