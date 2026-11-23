#include "../clearance.h"
#include "../../../logger.h"
#include "../ast_builder.h"
#include "class_builder.h"
#include "expression_builder.h"
#include "function_builder.h"
#include "rule_builder.h"

namespace AIML {
namespace AST {

// spec/15 F15-40 applies to list fields; other types keep their previous
// path through the ExpressionBuilder (a `buffer` default `[1,2,3]`, for instance).
static bool isListMemberType(const std::string& t) {
    return (t.rfind("list<", 0) == 0 || t.rfind("cascade<", 0) == 0) && !t.empty() && t.back() == '>';
}

bool ClassBuilder::OnMapStart() {
    // A map INSIDE the default literal is an element, not an expression.
    if (defaultSeqDepth > 0) { defaultSeqItems = true; return true; }
    if (insideRules) {
        // A rule item map opens → delegate to RuleBuilder (forward this open).
        auto rb = std::make_unique<RuleBuilder>(builder);
        rb->OnMapStart();
        builder->pushState(std::move(rb));
        return true;
    }
    if (insideMembers && sequenceDepth == 2) {
         if (memberAttrIndex == 2) {
             // Let ExpressionBuilder handle the default value AST node
             auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
             exprBuilder->OnMapStart();
             builder->pushState(std::move(exprBuilder));
             return true;
         }
         if (memberAttrIndex == 3) {
             // Handle modifiers map naturally
             expectingKey = true;
             return true;
         }
    }
    if (insideMethods) {
        expectingKey = true;
        return true; 
    }
    if (insideMembers || insideModels || insideImplements || insideRpc) {
        return true;
    }
    
    if (mapDepth++ > 0) return true;
    expectingKey = true;
    return true;
}

bool ClassBuilder::OnMapEnd() {
    if (insideMethods) return true;
    if (insideMembers && sequenceDepth > 1) return true; // Map inside member types
    if (--mapDepth > 0) return true;
    builder->popState();
    return true;
}

bool ClassBuilder::OnScalar(const std::string& value, const std::string& tag) {
    // A scalar in the default literal of a list field is an element.
    if (defaultSeqDepth > 0) { defaultSeqItems = true; return true; }
    if (insideMethods && expectingKey && value == "def") {
         builder->pushState(std::make_unique<FunctionBuilder>(builder));
         expectingKey = true; // For when FunctionBuilder returns
         return true;
    }

    if (mapDepth > 1) return true;

    // Ignore expectingKey if we are within a member definition sequence and not at the modifiers map yet
    if (expectingKey && !(insideMembers && sequenceDepth == 2 && memberAttrIndex < 3)) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    
    if (!insideMembers && !insideMethods && !insideModels && !insideImplements && !insideRules && !insideRpc) {
        if (currentKey == "name") classNode->name = value;
        else if (currentKey == "level") {   // spec/65 F65-19
            int parsed = aiml::clearance::kAbsent;
            if (aiml::clearance::parseLevel(value, "class " + classNode->name, parsed))
                classNode->level = parsed;
        }
        else if (currentKey == "renders") classNode->renders = value;
        else if (currentKey == "extends") classNode->parentName = value;
        else if (currentKey == "version") {
            if (!value.empty()) {
                try { classNode->version = std::stoull(value); } catch (...) {}
            }
        }
        // Scalar shorthand: `implements: Iface` (one) instead of `implements: [Iface]`.
        // Same for `models:`.  Without this, the single-iface form silently parses
        // to interfaces=[] → no vtable emitted → SIGSEGV on interface dispatch.
        else if (currentKey == "implements") {
            if (!value.empty()) classNode->interfaces.push_back(value);
        }
        else if (currentKey == "models") {
            if (!value.empty()) classNode->models.push_back(value);
        }
        else if (currentKey == "rpc") {
            if (!value.empty()) classNode->rpcMethods.push_back(value);
        }
    }
    else if (insideRpc) {
        if (!value.empty()) classNode->rpcMethods.push_back(value);
    }
    else if (insideModels) {
        if (!value.empty()) classNode->models.push_back(value);
    }
    else if (insideImplements) {
        if (!value.empty()) classNode->interfaces.push_back(value);
    }
    else if (insideMembers) {
        LOG_VERBOSE("insideMembers scalar: " + value);
        if (sequenceDepth == 2) {
            if (memberAttrIndex == 0) { currentField = value; memberAttrIndex++; return true; }
            if (memberAttrIndex == 1) { currentType = value; memberAttrIndex++; return true; }
            if (memberAttrIndex == 2) {
                 if (tag == "!") {
                     currentInitValue = std::make_unique<LiteralExpr>(value, "string");
                 } else if (value == "null") {
                     // `null` default: pointer/handle zero-init, not a variable lookup.
                     currentInitValue = std::make_unique<LiteralExpr>(value, "null");
                 } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
                     currentInitValue = std::make_unique<LiteralExpr>(value, "unknown");
                 } else {
                     currentInitValue = std::make_unique<VariableExpr>(value);
                 }
                 memberAttrIndex++;
                 return true;
            }
            if (memberAttrIndex == 3) {
                // We are inside the modifiers map!
                if (!expectingKey) {
                    if (currentKey == "version") {
                        try { currentVersion = std::stoull(value); } catch(...) {}
                    } else if (currentKey == "level") {   // spec/65 F65-33
                        int parsed = aiml::clearance::kAbsent;
                        if (aiml::clearance::parseLevel(value, "class member " + currentField, parsed))
                            currentLevel = parsed;
                    } else if (currentKey == "access") {
                        currentAccess = value;
                    } else if (currentKey == "const") {
                        currentConst = (value == "true" || value == "1");
                    } else if (currentKey == "volatile") {
                        currentVolatile = (value == "true" || value == "1");
                    }
                    expectingKey = true;
                } else {
                    currentKey = value;
                    expectingKey = false;
                }
                return true;
            }
        }
    }
    
    if (!insideMembers && !insideMethods && !insideModels && !insideImplements && !insideRules && !insideRpc) expectingKey = true;
    return true;
}

bool ClassBuilder::OnSequenceStart() {
    if (currentKey == "members") {
        insideMembers = true;
        currentKey = ""; // consumed
        sequenceDepth++;
    }
    else if (currentKey == "methods") {
        insideMethods = true;
        currentKey = ""; // consumed
    }
    else if (currentKey == "models") {
        insideModels = true;
        currentKey = ""; // consumed
    }
    else if (currentKey == "implements") {
        insideImplements = true;
        currentKey = ""; // consumed
    }
    else if (currentKey == "rules") {
        insideRules = true;
        currentKey = ""; // consumed
    }
    else if (currentKey == "rpc") {
        insideRpc = true;
        currentKey = ""; // consumed
    }
    else if (insideMembers) {
        if (defaultSeqDepth > 0 ||
            (sequenceDepth == 2 && memberAttrIndex == 2 && isListMemberType(currentType))) {
            // A sequence in the default literal is itself an element: `[[]]` has
            // one element and is therefore not empty, even without a scalar in it.
            if (defaultSeqDepth > 0) defaultSeqItems = true;
            ++defaultSeqDepth;
            return true;
        }
        if (sequenceDepth == 2 && memberAttrIndex >= 2) {
             auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
             exprBuilder->OnSequenceStart();
             builder->pushState(std::move(exprBuilder));
             return true;
        }
        sequenceDepth++;
        if (sequenceDepth == 2) {
            memberAttrIndex = 0;
            currentField = "";
            currentType = "";
            currentInitValue = nullptr;
            currentVersion = 1;
            currentAccess = "public";
            currentConst = false;
            currentVolatile = false;
        }
    }
    return true;
}

bool ClassBuilder::OnSequenceEnd() {
    if (defaultSeqDepth > 0) {
        if (--defaultSeqDepth == 0) {
            if (defaultSeqItems)
                LOG_ERROR(builder->fmtLoc() + "member '" + currentField + "' of class '" +
                          classNode->name + "' has a non-empty list default — a list<T> field "
                          "only takes `[]` (the empty list it gets anyway); fill the list in "
                          "code (spec/15 F15-40)");
            defaultSeqItems = false;
            memberAttrIndex++;
        }
        return true;
    }
    if (insideMembers) {
        sequenceDepth--;
        if (sequenceDepth == 1) {
            if (!currentField.empty() && !currentType.empty()) {
                StructMember m; m.name = currentField; m.type = currentType;
                m.initialValue = std::move(currentInitValue);
                m.version = currentVersion;
                m.access = currentAccess;
                m.is_const = currentConst;
                m.is_volatile = currentVolatile;
                m.level = currentLevel;
                classNode->members.push_back(std::move(m));
                currentField = ""; currentType = "";
                currentLevel = -1;   // spec/65: do not inherit to the next member
                return true;
            }
        } else if (sequenceDepth == 0) {
            insideMembers = false;
            expectingKey = true;
        }
    } else if (insideRules) {
        insideRules = false;
        expectingKey = true;
    } else if (insideMethods) {
        insideMethods = false;
        expectingKey = true;
    } else if (insideModels) {
        insideModels = false;
        expectingKey = true;
    } else if (insideImplements) {
        insideImplements = false;
        expectingKey = true;
    } else if (insideRpc) {
        insideRpc = false;
        expectingKey = true;
    }
    return true;
}

void ClassBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.get())) {
        e = dynamic_cast<Expr*>(node.release());
        if (insideMembers && sequenceDepth == 2) {
             if (memberAttrIndex == 2) {
                  currentInitValue = std::unique_ptr<Expr>(e);
             }
             memberAttrIndex++;
             return;
        }
    }
    if (auto r = dynamic_cast<RuleDecl*>(node.get())) {
        LOG_DEBUG("ClassBuilder received rule");
        (void)r;
        classNode->rules.push_back(std::unique_ptr<RuleDecl>(static_cast<RuleDecl*>(node.release())));
        expectingKey = true; // wait for next rule map or end of sequence
        return;
    }
    if (auto f = dynamic_cast<FunctionDecl*>(node.get())) {
        LOG_DEBUG("ClassBuilder received method: " + f->name);
        classNode->methods.push_back(std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl*>(node.release())));
        expectingKey = true; // wait for next map or end of sequence
    }
}

std::unique_ptr<Node> ClassBuilder::finish() {
    LOG_DEBUG("ClassBuilder::finish called for class: " + classNode->name);
    return std::move(classNode);
}

} // namespace AST
} // namespace AIML
