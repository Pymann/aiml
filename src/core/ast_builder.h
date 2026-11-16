#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/eventhandler.h>
#include "ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;

// spec/04 "Operator-Schluessel zitieren": `{ op: [args] }` ist eine YAML-Flow-Map, und
// ein Teil der symbolischen Operatoren kann dort nicht blank stehen -- `*` `%` `>` `>=`
// `||` `?` werfen, `&` `&&` `!` `~` werfen NICHT und liefern einen leeren bzw.
// `null`-Schluessel.  Liefert den ersten blanken reservierten Operatorschluessel in
// Flow-Schluesselposition (`{ op:` oder `, op:`), sonst "".  Rein lexikalisch, darum nur
// zum ERKLAEREN eines schon feststehenden Fehlschlags verwenden -- in Daten in
// Anfuehrungszeichen trifft die Suche auch (`"{ *: [a, b] }"` als Testeingabe).
std::string bareOperatorKey(const std::string& text);

// Die Zeile, die der Ereignis-Parser gerade liefert (1-basiert, 0 = unbekannt).
// `parseInterpolatedString` bekommt nur die Zeichenkette und kommt sonst ohne
// Fundort aus -- der Fundort ist bei einem 10.000-Zeilen-Modul aber die halbe
// Meldung.  Thread-lokal, weil mehrere Quellen parallel uebersetzt werden.
extern thread_local int g_builderLine;

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);
std::unique_ptr<Expr> parseScalarExpr(const std::string& val, const std::string& tag);
std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node);

// Datei des gerade geparsten Stroms.  Die freien Helfer dieser Einheit
// (nodeToExpr & Co.) haben keinen Builder, konnten also keine Datei nennen und
// schrieben "line 42: " als Prosa (spec/49 F49-100).  Der Compiler setzt den
// Namen vor dem Parsen; thread-lokal, weil Importe im selben Thread rekursiv
// parsen und jeder Lauf seinen eigenen Namen braucht.
void setCurrentParseFile(const std::string& file);
std::string currentParseFile();
// "datei:zeile:spalte: " aus der YAML-Marke, oder "line N: " ohne Datei.
std::string locOf(const YAML::Node& node);

// Base class for object-oriented states in the streaming parser
class StateHandler {
protected:
    Builder* builder;
public:
    StateHandler(Builder* b) : builder(b) {}
    virtual ~StateHandler() = default;

    // Return true if the child handled it, false if it's invalid
    virtual bool OnScalar(const std::string& value, const std::string& tag) { return false; }
    virtual bool OnSequenceStart() { return false; }
    virtual bool OnSequenceEnd() { return false; }
    virtual bool OnMapStart() { return false; }
    virtual bool OnMapEnd() { return false; }
    
    // Called by the Builder when a child state finishes and returns a built node
    virtual void receiveChildNode(std::unique_ptr<Node> node) {}
    
    // Called when this state handler is popped off the stack
    virtual std::unique_ptr<Node> finish() { return nullptr; }
};

class Builder : public YAML::EventHandler {
public:
    Builder();
    std::unique_ptr<Program> takeProgram();
    void addDeclaration(std::unique_ptr<Node> node);

    void pushState(std::unique_ptr<StateHandler> state);
    void popState();

    // Current 1-based line in the AIML source (updated on every YAML event).
    // State handlers can read this to record source locations on AST nodes.
    int currentLine = 0;
    // Spalte dazu, 1-basiert.  Die YAML-Marke trug sie immer; ohne sie kann ein
    // Werkzeug einen Befund nur der ZEILE zuordnen, nicht dem Token
    // (spec/49 F49-100).
    int currentColumn = 0;

    // Quelldatei, gegen die gerade geparst wird.  Der Parser lief bisher ohne
    // Dateinamen, darum konnte eine Meldung aus einem Builder nur den Text
    // nennen, nicht die Fundstelle (gemessen am Listen-Default-Check: der
    // Autor stand vor "field 'liste' has a non-empty list default" ohne zu
    // wissen, in WELCHER Datei).  Leer bei synthetisierten Schnipseln.
    std::string sourceFile;

    // "datei:zeile:spalte: " fuer eine Meldung aus einem Builder, oder "" ohne
    // Datei (spec/49 F49-100: der Ort ist ein Praefix in GENAU einer Form --
    // nie Prosa wie "line 9: ", die kein Werkzeug lesen kann).
    std::string fmtLoc() const { return fmtLoc(currentLine, currentColumn); }

    // Dieselbe Form fuer eine Stelle, die ihre Position frueher festgehalten hat
    // (StatementBuilder::capturedLine/-Column): ein Builder meldet oft erst beim
    // Schliessen des Knotens, und currentLine steht dann schon weiter.
    std::string fmtLoc(int line, int col) const {
        if (sourceFile.empty()) return "";
        if (line <= 0) return sourceFile + ": ";
        std::string loc = sourceFile + ":" + std::to_string(line);
        if (col > 0) loc += ":" + std::to_string(col);
        return loc + ": ";
    }

    // YAML::EventHandler Overrides
    void OnDocumentStart(const YAML::Mark& mark) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark& mark, YAML::anchor_t anchor) override;
    void OnAlias(const YAML::Mark& mark, YAML::anchor_t anchor) override {}
    void OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, const std::string& value) override;
    void OnSequenceStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnSequenceEnd() override;
    void OnMapStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnMapEnd() override;

private:
    std::unique_ptr<Program> program;
    std::stack<std::unique_ptr<StateHandler>> stateStack;
};


} // namespace AST
} // namespace AIML
