#pragma once
#include <map>
#include <string>
#include <memory>
#include <functional>
#include "command.h"
#include "decl_command.h"
#include "set_command.h"
#include "print_command.h"
#include "print_command.h"
#include "call_command.h"
#include "if_command.h"
#include "while_command.h"
#include "function_command.h"
#include "return_command.h"
#include "store_command.h"
#include "class_command.h"
#include "class_command.h"
#include "enum_command.h"

class CommandRegistry {
public:
    CommandRegistry() {
        registerCommand("decl", []() { return std::make_unique<DeclCommand>(); });
        registerCommand("set", []() { return std::make_unique<SetCommand>(); });
        registerCommand("print", []() { return std::make_unique<PrintCommand>(); });
        registerCommand("call", []() { return std::make_unique<CallCommand>(); });
        registerCommand("if", []() { return std::make_unique<IfCommand>(); });
        registerCommand("while", []() { return std::make_unique<WhileCommand>(); });
        registerCommand("def", []() { return std::make_unique<FunctionCommand>(); });
        registerCommand("return", []() { return std::make_unique<ReturnCommand>(); });
        registerCommand("store", []() { return std::make_unique<StoreCommand>(); });
        registerCommand("class", []() { return std::make_unique<ClassCommand>(); });
        registerCommand("enum", []() { return std::make_unique<EnumCommand>(); });
    }

    std::unique_ptr<AIMLCommand> create(const std::string& name) {
        if (factories.find(name) != factories.end()) {
            return factories[name]();
        }
        return nullptr;
    }

private:
    using Factory = std::function<std::unique_ptr<AIMLCommand>()>;
    std::map<std::string, Factory> factories;

    void registerCommand(const std::string& name, Factory factory) {
        factories[name] = factory;
    }
};
