#pragma once
#include <map>
#include <string>
#include <memory>
#include <functional>
#include "syscall.h"
#include "intrinsics.h"
#include "io_commands.h"
#include "time_commands.h"
#include "sys_commands.h"
#include "process_commands.h"

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
        registerCall("io.read", std::make_unique<IoRead>());
        registerCall("io.open", std::make_unique<IoOpen>());
        registerCall("io.close", std::make_unique<IoClose>());
        registerCall("io.read_all", std::make_unique<IoReadAll>());
        registerCall("time.now", std::make_unique<TimeNow>());
        registerCall("time.sleep_ms", std::make_unique<TimeSleepMs>());
        registerCall("sys.get_arg", std::make_unique<SysGetArg>());
        registerCall("sys.size", std::make_unique<SysSize>());
        registerCall("sys.type", std::make_unique<SysType>());
        registerCall("sys.env", std::make_unique<SysEnv>());
        registerCall("sys.exe", std::make_unique<SysExe>());
        registerCall("free", std::make_unique<MemoryFree>());
    }

    void registerCall(const std::string& name, std::unique_ptr<SystemCall> call) {
        calls[name] = std::move(call);
    }

    std::map<std::string, std::unique_ptr<SystemCall>> calls;
};
