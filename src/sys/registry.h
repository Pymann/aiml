#pragma once
#include <map>
#include <string>
#include <memory>
#include <functional>
#include "syscall.h"
#include "intrinsics.h"

class SystemRegistry {
public:
    static SystemRegistry& instance() {
        static SystemRegistry inst;
        return inst;
    }

    SystemCall* get(const std::string& name) {
        if (calls.find(name) != calls.end()) {
            return calls[name].get();
        }
        return nullptr;
    }

private:
    SystemRegistry() {
        registerCall("sys.exit", std::make_unique<SysExit>());
        registerCall("io.write", std::make_unique<IoWrite>());
        registerCall("time.sleep_ms", std::make_unique<TimeSleepMs>());
        registerCall("free", std::make_unique<MemoryFree>());
    }

    void registerCall(const std::string& name, std::unique_ptr<SystemCall> call) {
        calls[name] = std::move(call);
    }

    std::map<std::string, std::unique_ptr<SystemCall>> calls;
};
