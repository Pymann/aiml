#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"
#include "../../../logger.h"
#include "block_builder.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

class TryBuilder : public StateHandler {
    std::unique_ptr<TryStmt> tryStmt;
    std::string currentKey;
    bool expectingKey = true;
    int depth = 0;
    
public:
    TryBuilder(Builder* b) : StateHandler(b), tryStmt(std::make_unique<TryStmt>()) {}

    bool OnMapStart() override {
        depth++;
        expectingKey = true;
        LOG_DEBUG("TryBuilder OnMapStart: depth=" + std::to_string(depth));
        return true;
    }

    bool OnMapEnd() override {
        depth--;
        LOG_DEBUG("TryBuilder OnMapEnd: depth=" + std::to_string(depth));
        if (depth == 0) {
            builder->popState();
        }
        return true;
    }

    bool OnSequenceStart() override {
        LOG_DEBUG("TryBuilder OnSequenceStart: currentKey=" + currentKey + " depth=" + std::to_string(depth));
        if (currentKey == "body") {
            // BlockBuilder consumes its own matching SequenceEnd — do NOT increment depth here.
            builder->pushState(std::make_unique<BlockBuilder>(builder));
            return true;
        }
        depth++;
        return true;
    }

    bool OnSequenceEnd() override {
        depth--;
        expectingKey = true;
        LOG_DEBUG("TryBuilder OnSequenceEnd: currentKey=" + currentKey + " depth=" + std::to_string(depth));
        if (depth == 0) {
            builder->popState();
        }
        return true;
    }

    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (expectingKey) {
            currentKey = value;
            expectingKey = false;
            
            if (currentKey == "catch") {
                 expectingKey = true; 
            }
            return true;
        } else {
            if (currentKey == "type") tryStmt->catchType = value;
            else if (currentKey == "var") tryStmt->catchVar = value;
            expectingKey = true;
            return true;
        }
    }

    void receiveChildNode(std::unique_ptr<Node> node) override {
        if (auto b = dynamic_cast<BlockStmt*>(node.get())) {
            b = dynamic_cast<BlockStmt*>(node.release());
            if (currentKey == "body") {
                if (!tryStmt->tryBlock) {
                    LOG_DEBUG("TryBuilder: Received tryBlock with " + std::to_string(b->statements.size()) + " statements");
                    tryStmt->tryBlock = std::unique_ptr<BlockStmt>(b);
                } else {
                    LOG_DEBUG("TryBuilder: Received catchBlock with " + std::to_string(b->statements.size()) + " statements");
                    tryStmt->catchBlock = std::unique_ptr<BlockStmt>(b);
                }
            }
        }
        expectingKey = true;
    }

    std::unique_ptr<Node> finish() override {
        return std::move(tryStmt);
    }
};

} // namespace AST
} // namespace AIML
