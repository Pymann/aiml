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
    int capturedLine = -1;
    std::string currentKey;
    bool expectingKey = true;
    int depth = 0;
    bool inCatch = false;
    
public:
    TryBuilder(Builder* b) : StateHandler(b), tryStmt(std::make_unique<TryStmt>()), capturedLine(b->currentLine) {}

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
            if (inCatch && value != "type" && value != "var" && value != "body")
                LOG_ERROR("line " + std::to_string(builder->currentLine) + ": catch entries are "
                          "{type, var, body} (spec/22) — '" + value + "' is not allowed here");
            if (value == "catch") inCatch = true;
            currentKey = value;
            expectingKey = false;
            
            if (currentKey == "catch") {
                 expectingKey = true; 
            }
            return true;
        } else {
            if (currentKey == "type") {
                tryStmt->catches.push_back(CatchClause{});     // spec/22: ein Zweig je `type:`
                tryStmt->catches.back().type = value;
            } else if (currentKey == "var") {
                if (tryStmt->catches.empty()) tryStmt->catches.push_back(CatchClause{});
                tryStmt->catches.back().var = value;
            }
            expectingKey = true;
            return true;
        }
    }

    void receiveChildNode(std::unique_ptr<Node> node) override {
        if (auto b = dynamic_cast<BlockStmt*>(node.get())) {
            b = dynamic_cast<BlockStmt*>(node.release());
            if (currentKey == "body") {
                if (!inCatch && !tryStmt->tryBlock) {
                    LOG_DEBUG("TryBuilder: Received tryBlock with " + std::to_string(b->statements.size()) + " statements");
                    tryStmt->tryBlock = std::unique_ptr<BlockStmt>(b);
                } else {
                    // Rumpf des zuletzt begonnenen catch-Zweigs (ein Zweig ohne `type:`
                    // faengt alles).
                    if (tryStmt->catches.empty()) tryStmt->catches.push_back(CatchClause{});
                    if (tryStmt->catches.back().body)
                        tryStmt->catches.push_back(CatchClause{});
                    tryStmt->catches.back().body = std::unique_ptr<BlockStmt>(b);
                }
            }
        }
        expectingKey = true;
    }

    std::unique_ptr<Node> finish() override {
        tryStmt->sourceLine = capturedLine;
        return std::move(tryStmt);
    }
};

} // namespace AST
} // namespace AIML
