#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

class ClassBuilder : public StateHandler {
    std::unique_ptr<ClassDecl> classNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideMembers = false;
    bool insideMethods = false;
    bool insideModels = false;
    bool insideImplements = false;
    bool insideRules = false;   // function-cascade `rules:` (spec/57)
    bool insideRpc = false;     // mirror `rpc: [method, …]` (spec/44_5 §7.1)
    int mapDepth = 0; // Start at 0
    int sequenceDepth = 0;
    int memberAttrIndex = 0; // Tracks which column [name, type, default, modifiers]
    
    // State for parsing member sequence entry
    std::string currentField;
    std::string currentType;
    std::unique_ptr<Expr> currentInitValue;
    uint64_t currentVersion = 1;
    std::string currentAccess = "public";
    int currentLevel = -1;                 // spec/65 F65-33; -1 = absent
    bool currentConst = false;
    bool currentVolatile = false;
    // spec/15 F15-40: ein `list<T>`-Member nimmt nur `[]` als Default.  Ein
    // gefuellter Default wurde hier still verworfen (der ExpressionBuilder baute
    // einen Ausdruck, den initClassContainerMembers fuer Listenfelder nie
    // ansieht) → das Feld existierte und war zur Laufzeit leer.  Der ModelBuilder
    // hatte den Check, der ClassBuilder nicht.
    int  defaultSeqDepth = 0;
    bool defaultSeqItems = false;
public:
    ClassBuilder(Builder* b) : StateHandler(b) { classNode = std::make_unique<ClassDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
