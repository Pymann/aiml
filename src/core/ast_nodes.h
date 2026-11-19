#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <yaml-cpp/yaml.h>

namespace AIML {
namespace AST {

// Base class for all AST nodes
class Node {
public:
    virtual ~Node() = default;
    
    // For debugging/printing the tree
    virtual std::string toString() const = 0;
    
    // Original YAML node for exact line numbers/error reporting
    YAML::Node sourceNode;
    // 1-based line number in the AIML source file (-1 = unknown)
    int sourceLine = -1;
    // Spalte zur sourceLine, 1-basiert (spec/49 F49-100).  Ohne sie konnte
    // eine Diagnose aus einem AST-Durchlauf nur die Zeile nennen; spec/49
    // fuehrte das als offenen Punkt "the column is partial".
    int sourceColumn = -1;
};

// --- Expressions (Evaluate to a value) ---

class Expr : public Node {
public:
    virtual ~Expr() = default;
};

class LiteralExpr : public Expr {
public:
    std::string value; // e.g. "42", "true", "\"hello\""
    std::string type;  // e.g. "i32", "bool", "string"

    LiteralExpr(std::string v, std::string t) : value(v), type(t) {}
    std::string toString() const override { return "Literal(" + type + ": " + value + ")"; }
};

class VariableExpr : public Expr {
public:
    std::string name;

    VariableExpr(std::string n) : name(n) {}
    std::string toString() const override { return "Variable(" + name + ")"; }
};

class BinaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;

    BinaryExpr(std::string op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op(op), lhs(std::move(l)), rhs(std::move(r)) {}
    std::string toString() const override { return "Binary(" + op + ", " + lhs->toString() + ", " + rhs->toString() + ")"; }
};

// Unary operators: '?' (deref), '??' (double-deref), '&' (addr-of), '&&' (addr-of-addr), '!' (logical not)
class UnaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(std::string op, std::unique_ptr<Expr> operand)
        : op(op), operand(std::move(operand)) {}
    std::string toString() const override { return "Unary(" + op + ", " + operand->toString() + ")"; }
};

class CallExpr : public Expr {
public:
    std::string functionName;
    std::vector<std::unique_ptr<Expr>> args;

    CallExpr(std::string name) : functionName(name) {}
    std::string toString() const override { return "Call(" + functionName + ")"; }
};

class StringInterpolationExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> parts;

    std::string toString() const override { return "StringInterpolation(...)"; }
};

class NewExpr : public Expr {
public:
    std::string typeName;
    std::map<std::string, std::unique_ptr<Expr>> initValues;

    NewExpr(std::string tName) : typeName(tName) {}
    std::string toString() const override { return "New(" + typeName + ")"; }
};

// dyn_array<T> heap allocation: { new_array: [ElemType, count] }
class NewArrayExpr : public Expr {
public:
    std::string elemTypeName;
    std::unique_ptr<Expr> countExpr;
    NewArrayExpr(std::string t) : elemTypeName(t) {}
    std::string toString() const override { return "NewArray(" + elemTypeName + ")"; }
};

class SpawnExpr : public Expr {
public:
    std::string functionName;
    std::vector<std::unique_ptr<Expr>> args;

    SpawnExpr(std::string name) : functionName(name) {}
    std::string toString() const override { return "Spawn(" + functionName + ")"; }
};

// Index expression: { at: [container, idx1, idx2, ...] }
// Works for array<T:N> (GEP), list<T> (bounds-checked get), tensor (tensor_get).
class AtExpr : public Expr {
public:
    std::unique_ptr<Expr> container;
    std::vector<std::unique_ptr<Expr>> indices;
    // Optional trailing member path: { at: [container, index, "field", "field2"] } reads
    // element[index].field.field2 — the dash-free form of `container-index.field.field2`.
    std::vector<std::string> memberPath;

    AtExpr() {}
    std::string toString() const override { return "At(...)"; }
};

// Sequence literal used as buffer<T> initializer: decl: [buf, buffer<f32>, [v1, v2, ...]]
// Compiler expands each element into a BufferImpl::write call.
class BufferLiteralExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;
    BufferLiteralExpr() {}
    std::string toString() const override { return "BufferLiteral(...)"; }
};

// Inline option map: {key: value, ...} used as call argument (e.g. {context: true}).
// Values are kept as strings; callers parse booleans/numbers as needed.
// Für map<K:V>-Init in decl: die typed Expr-Values werden zusätzlich in
// entriesExpr abgelegt (Reihenfolge-erhaltend); Compiler emittiert dann .put-Calls.
class DictLiteralExpr : public Expr {
public:
    std::map<std::string, std::string> entries;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> entriesExpr;
    explicit DictLiteralExpr(std::map<std::string, std::string> e) : entries(std::move(e)) {}
    std::string toString() const override { return "Dict(...)"; }
};

// --- Statements (Perform an action) ---

class Stmt : public Node {
public:
    virtual ~Stmt() = default;
};

class BlockStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;

    std::string toString() const override { return "Block(...)"; }
};

class DeclStmt : public Stmt {
public:
    std::string varName;
    std::string typeName;
    std::unique_ptr<Expr> initialValue;
    bool is_const = false;
    // spec/21 §6.2 F21-S1: erklaerter Anker eines Rueckrufs — keine
    // Globals-Warnung, dafuer die Pruefung aus F21-S2.
    bool is_singleton = false;
    bool is_volatile = false;
    bool is_extern = false;   // {extern: true} → cross-file ExternalLinkage (plain __lib_<name>)
    std::string lifetime = "scope";  // {lifetime: scope|persistent|consumed} — spec/61.
                                     // Ownership metadata; drop emission is a later step.

    // spec/65 F65-19/F65-34: 4.-Positions-Modifier `{ level: N }`.  -1 = keine
    // Annotation (NICHT Level 0).  Hier nur getragen, nicht durchgesetzt.
    int level = -1;

    DeclStmt(std::string n, std::string t) : varName(n), typeName(t) {}
    std::string toString() const override { return "Decl(" + typeName + " " + varName + ")"; }
};

class SetStmt : public Stmt {
public:
    std::string target; // Simple variable for now, later LHS expression
    std::unique_ptr<Expr> value;

    SetStmt(std::string t, std::unique_ptr<Expr> v) : target(t), value(std::move(v)) {}
    std::string toString() const override { return "Set(" + target + " = ...)"; }
};

// store: [ptr_var, value, (optional)depth]  — deref-write through pointer.
// depth 1 (default):  *ptr = value      (analog zu C `*p = v`)
// depth N:            *(*...*ptr) = value
// Komplement zu `set: [p, v]` (rebind / C `p = v`) und `{ '?': [p] }` (read).
// Spec/12_pointers.md beschreibt die Semantik.
class StoreStmt : public Stmt {
public:
    std::string target;             // pointer-variable name
    std::unique_ptr<Expr> value;
    int depth;                      // dereference count (>=1)

    StoreStmt(std::string t, std::unique_ptr<Expr> v, int d = 1)
        : target(t), value(std::move(v)), depth(d) {}
    std::string toString() const override {
        return "Store(" + std::string(depth, '*') + target + " = ...)";
    }
};

// idx: [container, idx1, ..., idxN, value]  — indexed write
// Last arg is the value, first is container, rest are indices.
class IdxStmt : public Stmt {
public:
    std::string container;
    std::vector<std::unique_ptr<Expr>> indices;
    // Optional trailing member path: idx: [container, index, .field, .f2, value] writes
    // element[index].field.f2 = value (dash-free `set: [container-index.field.f2, value]`).
    std::vector<std::string> memberPath;
    std::unique_ptr<Expr> value;

    IdxStmt(std::string c) : container(c) {}
    std::string toString() const override { return "Idx(" + container + "[...] = ...)"; }
};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;

    ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
    std::string toString() const override { return "ExprStmt(" + expr->toString() + ")"; }
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBranch;
    std::unique_ptr<BlockStmt> elseBranch; // Optional

    std::string toString() const override { return "If(...)"; }
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;

    std::string toString() const override { return "While(...)"; }
};

class RetStmt : public Stmt {
public:
    std::unique_ptr<Expr> value; // Optional

    std::string toString() const override { return "Ret(...)"; }
};

class ThrowStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;

    ThrowStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
    std::string toString() const override { return "Throw(" + value->toString() + ")"; }
};

// spec/22: ein `catch:` ist eine LISTE von Zweigen.  Bis 18.10.2026 hielt der AST
// nur einen, und jeder weitere ueberschrieb ihn wortlos.
struct CatchClause {
    std::string type;                  // leer = faengt alles
    std::string var;
    std::unique_ptr<BlockStmt> body;
};

class TryStmt : public Stmt {
public:
    std::unique_ptr<BlockStmt> tryBlock;
    std::vector<CatchClause> catches;  // in geschriebener Reihenfolge

    // Alle Zweig-Rumpfe — fuer die Analysen, die jeden Pfad sehen muessen.
    std::vector<BlockStmt*> catchBodies() const {
        std::vector<BlockStmt*> out;
        for (const auto& c : catches) if (c.body) out.push_back(c.body.get());
        return out;
    }

    std::string toString() const override { return "Try(...) Catch(...)"; }
};

// --- Extern "C" Declarations ---

struct ExternFuncDecl {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> args; // name, type
};

class ExternBlock : public Node {
public:
    std::string abi;  // "C" (only supported ABI for now)
    std::vector<ExternFuncDecl> funcs;

    std::string toString() const override { return "ExternBlock(\"" + abi + "\")"; }
};

// --- Top-Level Declarations ---

// Per-parameter modifiers (spec/61 §3.1). Parallel to FunctionDecl::args (same index).
// Kept as a separate vector so the many existing `args[i].first/.second` readers stay
// valid; only ownership-aware code consults argMods.
struct ParamMod {
    std::string lifetime = "scope";  // "scope" (borrow, default) | "consumed" | "persistent"
    bool is_const = false;           // read-only parameter (spec/61 §3.1, const params — later)
};

class FunctionDecl : public Node {
public:
    std::string name;
    std::string returnType;
    // spec/61 return-ownership: `owns_return: true` marks this function as returning an
    // OWNED heap box (a ptr<T> whose pointee it freshly allocated, e.g. get_base's
    // `{ new: <Type>Card }`). A caller's `decl: [x, ptr<T>, { call: this-fn }]` local then
    // gets a deep scope-drop (kind 4), exactly like a `{ new: T }` local — unless it escapes
    // (return/store/push → consumeLocalDrop removes the drop). Default false = borrow/view.
    bool        ownsReturn = false;
    // spec/61 §8: for a `ref<T>` return, the parameter name the borrow is anchored to
    // (`return: { type: ref<T>, borrows: <param> }`). Recorded for the future modular borrow
    // lifetime check; not enforced yet (a ref-return behaves like a ptr-return today).
    std::string returnBorrowsParam;
    uint64_t    version = 1;  // Hot-Reload version (default 1)
    std::vector<std::pair<std::string, std::string>> args; // name, type
    std::vector<ParamMod> argMods;   // parallel to args; default-constructed = plain borrow
    std::unique_ptr<BlockStmt> body;
    std::vector<std::string> staticOff; // disabled static checks, e.g. "lines"
    // Function decorators — right-to-left applied at compile time.  Built-in
    // registry currently: "vocab" (wraps string returns with vocab.lookup).
    // Empty list = no decoration.  See /spec/55_decorators.md.
    std::vector<std::string> decorators;
    // spec/66 §2 — endpoint annotations.  Only meaningful on a `def` inside a
    // `- service:`; the service pass reads them, an ordinary def ignores them.
    std::vector<std::string> http;    // [verb, path]; empty = no REST transport
    std::vector<std::string> roles;   // required roles; empty list + hasRoles = open
    bool hasRoles = false;            // was `roles:` written at all?  (§5.3)
    std::vector<std::string> cli;     // [subcommand, ...]; empty = no CLI transport
    // spec/66 §5.6: von der Service-Synthese erzeugt.  Ein Aufruf AUS so einem
    // Rumpf zaehlt nicht als Benutzung des Service.
    bool serviceSynth = false;
    // Free-form metadata tags.  Recognised today: "deprecated" — every call
    // site of this function/method gets a compile-time warning.  Unknown tags
    // are ignored (forward-compatible).  Parsed from `meta: [...]`.
    std::vector<std::string> meta;

    // spec/65 F65-19/F65-10: `level:`/`grants:` als def-Schluessel.  -1 = absent.
    int level = -1;
    int grants = -1;

    std::string toString() const override { return "FunctionDecl(" + name + ")"; }
};

struct StructMember {
    std::string name;
    std::string type;
    // spec/15: zweiter Name NACH AUSSEN (Serialisierung).  Leer = der echte
    // Name gilt.  Noetig fuer fremde Formate, deren Schluessel aimls
    // Drei-Zeichen-Regel nicht erfuellen (OCI: `id`).
    std::string alias;
    std::unique_ptr<Expr> initialValue; // Applicable for classes/models
    uint64_t version = 1;
    std::string access = "public";
    bool is_const = false;
    int level = -1;              // spec/65 F65-33: Bindung, nicht Typ.  -1 = absent.
    bool is_volatile = false;
    // classgroup sees-member: a wired COLLABORATOR pointer, not owned data.
    // Serialization skips it (spec/58 §9: skip / re-inject on load), which also
    // breaks the infinite ser-schema recursion of bidirectional sees cycles.
    bool is_collaborator = false;

    // AionDB db_indexed modifier — see CompilerContext::StructMember mirror.
    // Parsed from 4th-position dict in model field declaration.
    bool db_indexed = false;
    std::string db_indexed_mode;   // "" | "lower"
    bool db_indexed_unique = false;
    bool db_indexed_skip_empty = false;
};

struct MethodSignature {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> args;
    std::vector<std::string> argLifetimes;  // spec/61 §3.1, parallel to args ("scope" default)
};

// Function-cascade rule context slot (spec/57).  One `Mask` per path stage:
// a selection of allowed member/type tokens, or the wildcard.  Tokens are raw
// strings at parse time; a later milestone resolves them to i32 member values
// against the stage enum.
struct Mask {
    std::vector<std::string> options;   // raw tokens (member/type names)
    bool is_wildcard = false;           // true = any element ("*")
};

// A single α A β rewrite rule inside a function-cascade member class.
class RuleDecl : public Node {
public:
    std::vector<Mask> before;                 // preceding history (prefix TAIL)
    Mask              on;                      // the handled element
    std::vector<Mask> after;                  // following history (suffix HEAD)
    bool              hasAfter = false;        // true even for `after: []` (terminal)
    int               weight = 0;
    bool              hasWeight = false;
    std::unique_ptr<FunctionDecl> def;         // the handler method

    std::string toString() const override { return "RuleDecl"; }
};

class ClassDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;
    std::string parentName; // for "extends"
    uint64_t version = 1;
    std::vector<std::string> models;
    std::vector<std::string> interfaces;
    std::vector<StructMember> members;
    std::vector<std::unique_ptr<FunctionDecl>> methods;
    std::vector<std::string> syncFields;   // mirror: sync: [...]
    std::vector<std::string> rpcMethods;   // mirror: rpc:  [...]
    // Function-cascade member class (spec/57).  `renders` = the cascade|enum this
    // class holds rules for; `rules` = its α A β rewrite rules.
    std::string renders;
    std::vector<std::unique_ptr<RuleDecl>> rules;

    std::string toString() const override { return "ClassDecl(" + name + ")"; }
};

class ModelDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;
    std::string parentName;   // for "inherits"
    uint64_t version = 1;
    std::string primaryKey;   // field name used as entity ID; empty = slot-index (i32)
    std::vector<StructMember> fields;
    std::vector<std::string> syncFields;   // mirror: sync: [...]
    std::vector<std::string> rpcMethods;   // mirror: rpc:  [...]

    std::string toString() const override { return "ModelDecl(" + name + ")"; }
};

class InterfaceDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;
    std::vector<MethodSignature> methods;
    // spec/58 §12: a `groupinterface:` parses to an interface whose methods are the
    // export accessors `__gi_<inst>` (returning ref<Iface>).  `groupExports` keeps
    // the declared [instance, interface] pairs (duplicates included, for GI-3).
    bool groupInterface = false;
    std::vector<std::pair<std::string, std::string>> groupExports;

    std::string toString() const override { return "InterfaceDecl(" + name + ")"; }
};

class EnumDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    // spec/49 §18.2a: Vokabular einer Schnittstelle — alle Mitglieder gelten als
    // verwendet, auch wenn DIESER Build sie nicht benennt.
    bool wire = false;
    std::string name;
    std::vector<std::pair<std::string, int>> values; // enum name, integer value
    // Optional map: id → display label.  If empty, labels are derived from
    // value names (snake_case → Title Case) at consumer sites.
    std::map<int, std::string> labels;

    std::string toString() const override { return "EnumDecl(" + name + ")"; }
};

/// A single field within a cascade node.  Field-type may reference another
/// enum, cascade, model, primitive, or alias.
struct CascadeField {
    std::string name;      // auto-generated if empty (from type name)
    std::string typeName;  // "DeckType", "ValueType", "i32", "VALUE" (alias), ...
    std::string defaultValue; // literal, empty if none
};

/// A single node in a cascade.  Bytecode id + payload fields.  Layout maps
/// directly to `list<i32>` at serialize time: [id, field0, field1, ...] where
/// nested cascade-fields expand recursively in place.
struct CascadeNode {
    std::string name;                    // "draw_card"
    int id = -1;                         // bytecode id (== list[0] at wire)
    std::vector<CascadeField> fields;
    std::string label;                   // optional display label
};

class CascadeDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;                    // "EffectCode", "ValueType", ...
    std::string opcode;                  // optional: enum typing element 0 (node id).
                                         // When set, node ids/names are resolved and
                                         // verified against this enum's members.
    std::vector<CascadeNode> nodes;
    // Overrides that apply after fields[]/nodes[] were parsed.  `labels`
    // maps node-name → display label.  IDs and defaults live on the nodes.
    std::map<std::string, std::string> labelOverrides;

    std::string toString() const override { return "CascadeDecl(" + name + ")"; }
};

// Function-cascade method pool (spec/57).  Bundles the rule-holding member
// classes that together render one cascade, and pins the pool's carrier
// endpoints.  M1a: top-level declaration only (rules/Mask live on member
// classes, wired in a later milestone).
class CascadeClassDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;
    std::string bindsCascade;   // exactly one of bindsCascade / bindsEnum is set
    std::string bindsEnum;
    std::string carrier;        // carrier type at entry  (render's carry param)
    std::string taker;          // carrier type at exit   (render's return)
    std::vector<std::string> members;   // rule-holding class names

    std::string toString() const override { return "CascadeClassDecl(" + name + ")"; }
};

// One instance in a classgroup (spec/58).  A base class specialized into a
// synthesized subclass: the `sees` siblings become `ptr<>` members and the
// `functions` become methods; an optional `constructor` names a base method run
// in phase 0.  The instance name is the group-unique role identifier.
struct ClassGroupInstance {
    std::string className;      // base class to specialize
    std::string instanceName;   // group-unique instance/role name
    std::vector<std::string> sees;   // sibling instance names it may reference
    // Interfaces this instance implements — satisfied by its base methods AND its
    // group `functions`.  Lets an interface-typed sees edge call the instance's
    // coordination functions across the edge (spec/58 §5.2, cross-edge coord).
    std::vector<std::string> implements;
    std::vector<std::unique_ptr<FunctionDecl>> functions;   // coordination methods
    // Optional base-class method run in PHASE 0 (sees-blind).  [0] = method name,
    // [1..] = args (each a group param name → <.param, else a literal token).
    std::vector<std::string> constructor;
};

// Static component-wiring group (spec/58).  Each instance desugars to a subclass
// of its base with sees→members + functions→methods; a synthesized container
// class named after the group holds all instances and wires them (phase 0 alloc
// + phase 1 sees back-patch) via a synthesized __ctor invoked on {new: <group>}.
class ClassGroupDecl : public Node {
public:
    int level = -1;   // spec/65 F65-19: Typform traegt `level:`.  -1 = absent.
    std::string name;
    // External config the group needs (name, type) — becomes container members,
    // set at {new: <group>, param: val}, and readable by constructors in phase 0.
    std::vector<std::pair<std::string, std::string>> params;
    // Required external instances — PORTS (spec/58 §6, `requires:`).  Each is a
    // (port name, base class) pair.  A subgroup declares the parent instances it
    // consumes; the embedding parent BINDS each port to one of its own instances
    // (see `subgroupPorts`).  Inside the group, a `sees: [port]` edge resolves to
    // a base-typed member wired to the bound parent instance (child→parent).
    std::vector<std::pair<std::string, std::string>> ports;
    // Embedded child groups (member name, group type) — owned + constructed by
    // this group's container in phase 0.
    std::vector<std::pair<std::string, std::string>> subgroups;
    // Port bindings per embedded subgroup: subgroup member name → list of
    // (child port name, THIS group's instance name).  Populated from the optional
    // 3rd map element of a `subgroups:` entry (`[fx, EffectRuntime, {cards: cards}]`).
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> subgroupPorts;
    // Instance names visible outside this group (its public surface).  Empty =
    // all instances exported (backwards-compatible with M1).
    std::vector<std::string> exports;
    std::vector<ClassGroupInstance> instances;
    // Group interfaces this group implements (spec/58 §12, group-level
    // `implements:`).  Distinct from an instance's `implements:` (§5.2).
    std::vector<std::string> implementsGroups;

    std::string toString() const override { return "ClassGroupDecl(" + name + ")"; }
};

// One endpoint of a service (spec/66 §2): an ordinary `def` plus its transport
// annotations, which live on the FunctionDecl itself.
struct ServiceEndpoint {
    std::unique_ptr<FunctionDecl> fn;   // owned until synthesis lifts it into the program
    // Synthesis runs BEFORE type registration (so the lifted function and the
    // description model are known like any other declaration), the checks run
    // AFTER it (so models are registered).  The metadata the checks need is
    // therefore captured here while `fn` is still owned.
    std::string name, verb, path, requestType, returnType;
    std::vector<std::string> roles;
    std::vector<std::string> cli;   // §4.3 Unterkommando(s)
    bool hasRoles = false;
    int  argCount = 0;
    bool captured = false;
};

// A service declaration (spec/66).  One statement of an endpoint — name, request
// and response model, verb and path, required roles — from which the compiler
// synthesizes the RPC handler, the HTTP route, the client stub and the
// description table.  The body is written once and reached through every
// transport.
class ServiceDecl : public Node {
public:
    std::string name;
    std::string base;                        // HTTP path prefix, e.g. "/api/v1"
    std::string auth;                        // auth class, or "none"/"" = no gate
    std::vector<std::string> transports;     // rpc | rest | cli
    std::vector<ServiceEndpoint> endpoints;

    std::string toString() const override { return "ServiceDecl(" + name + ")"; }
};

// One target of an external `groupmethods:` block: the coordination methods to
// attach to a named instance of the target classgroup.
struct GroupMethodsTarget {
    std::string instanceName;
    std::vector<std::unique_ptr<FunctionDecl>> methods;
};

// External `groupmethods:` block (spec/58) — lets an instance's coordination
// methods live in their OWN file instead of inline in the classgroup body.  It
// names a target `group:` and, per `instance:`, a `methods:` list.  Merged into
// the target ClassGroupInstance.functions before checkClassGroups/synthesize, so
// the methods are lifted onto the synthesized subclass exactly like inline ones.
class GroupMethodsDecl : public Node {
public:
    std::string group;   // target classgroup name
    std::vector<GroupMethodsTarget> targets;
    std::string toString() const override { return "GroupMethodsDecl(" + group + ")"; }
};

/// Alias — a named alias for a primitive type with UI-metadata attached.
/// Used for cascade payload "role" names like VALUE / COUNT / SIDES that
/// share the same wire-type (i32) but carry different widget hints.
class AliasDecl : public Node {
public:
    std::string name;         // "VALUE", "COUNT", ...
    std::string typeName;     // Underlying type (usually "i32"/"f32"/"string")
    std::string label;        // Widget label (auto-derived from name if empty)
    std::string minValue;     // as literal ("0")
    std::string maxValue;
    std::string defaultValue;

    std::string toString() const override { return "AliasDecl(" + name + ")"; }
};

struct PrefabChildInfo {
    // Fundstelle des Kindes in der Quelle (spec/49 F49-100).  Tier 4 meldet AM
    // KIND, nicht an der Prefab-Zeile: ein Prefab hat oft zwanzig Kinder, und
    // ein Befund, der auf die Deklarationszeile zeigt, laesst den Leser suchen.
    int src_line = -1;
    int src_col  = -1;
    std::string name;
    std::string type;      // "sprite", "label", "rect", "button", "text_input"
    float x = 0, y = 0, width = 0, height = 0;
    float font_size = 12;
    uint32_t color = 0xFFFFFF;
    std::string texture;
    std::string anchor;    // "left", "center", "right"
    std::string valign;    // "top", "center" (default), "bottom"
    std::string font;      // "" = scene font; else a name from engine.prefab_add_font
    bool gutter = false;   // multi-line text_input: line-number column (spec/44_6 §5g)

    // UI-widget style (button, text_input, rect)
    uint32_t bg_color      = 0;
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;
    uint32_t active_color  = 0;
    // Label outline — cheap 8-tap stroke around text glyphs.  Renders
    // outline_color at ±outline_px offsets, main color on top.  Only
    // applied when outline_px > 0 and outline_color has non-zero alpha.
    uint32_t outline_color = 0;
    float    outline_px    = 0.0f;
    // Tileable background texture for `rect` — stem name resolved to a
    // GL id at instantiate time.  `tile_size` gives the logical pixel
    // period at which the pattern repeats (default 128 px).
    std::string bg_texture;
    float    tile_size     = 128.0f;
    // Slider skin: `track_texture` replaces the SDF track/border with a
    // stretched sprite; `handle_texture` replaces the knob.  Both are
    // stem names resolved to GL ids at instantiate time.
    // `handle_gem_texture` is an optional overlay sprite drawn on top of
    // handle_texture, tinted with fill_color so the metal grip stays
    // neutral while the inset gem takes the widget's mana color.
    std::string track_texture;
    std::string handle_texture;
    std::string handle_gem_texture;
    // Tileable border overlay: one small strip repeated along each edge of
    // the button/rect.  `frame_border_size` is the on-screen edge thickness
    // (also acts as tile pitch so the pattern stays uniform per pixel).
    std::string frame_border_texture;
    float       frame_border_size = 6.0f;
    // Mirror this child's sprite horizontally at draw time.
    bool flip_h = false;
    // Slider snap step (0 = continuous).  When > 0 the slider quantizes
    // its value to multiples of `snap` during drag — useful for discrete
    // ranges like "mana amount 0..3".
    float slider_snap = 0.0f;
    // Slider lower bound (spec/44_6).  `bar` ignores it and always treats 0
    // as the lower bound.
    float slider_min  = 0.0f;
    // Start state of the child (spec/44_6).  `false` means instantiate hidden;
    // set_visible reveals it later.
    bool  is_visible  = true;
    // container: engine-managed row window + native scrollbar (spec/44_6 §5c).
    bool  scrollable  = false;
    // textbox: keep the view pinned to the newest line (spec/44_6 §5f).
    bool  tail        = false;
    /* spec/44_6 §5f: `wrap: false` = eine Zeile bleibt eine Zeile, die Box
     * scrollt seitwaerts (Bau-Protokoll, Quelltext). */
    bool  wrap        = true;
    std::string label;        // button label / static label text
    bool     is_secret     = false; // text_input password mode

    // Bar/gauge (type: "bar"): track uses bg_color/border/corner_radius;
    // the filled portion uses fill_color, sized value/max of the width.
    float    bar_value     = 0.0f;
    float    bar_max       = 1.0f;
    uint32_t fill_color    = 0;

    // Callbacks (function names, resolved at runtime via fptable)
    std::string on_click;
    std::string on_change;
    std::string group;   // radio: exklusive Auswahl
    std::string options; // dropdown: pipe-separierte Optionen

    // Material reference (looked up by name at instantiate time)
    std::string material;

    // ── Model-binding / cascade-widget attributes (Phase 6) ────────────
    // `bind: <field>` — this widget's value mirrors a scalar field of the
    // Model that owns the enclosing prefab instance.  Two-way sync at
    // runtime.  Empty when the widget carries no model binding.
    std::string bind;

    // `bind_list: <field>` — this widget (usually type=container) spawns
    // one Prefab-instance per element of the named list<T> field.  The
    // spawned prefab's `binds:` type must match T.
    std::string bind_list;

    // `spawn: <PrefabName>` — companion to bind_list.  Names the prefab
    // to instantiate per element.
    std::string spawn;

    // `layout: <mode>` — layout mode for containers ("vflow", "hflow", ...).
    std::string layout;

    // `gap: <px>` — spacing between spawned children in the layout.
    float gap = 0.0f;

    // `compact: <bool>` — container only: hidden spawned rows take no layout
    // space; visible rows re-pack contiguously from the origin each frame.
    bool compact = false;

    // `cascade_type: <CascadeName>` — for type=cascade / type=dropdown widgets:
    // populate options from the registered cascade metadata and drive
    // reactive payload sub-widgets when the selection changes.
    std::string cascade_type;

    // `payload_spawn: <PrefabName>` — companion to cascade_type.  When the
    // dropdown's selection changes, the runtime destroys the previously-
    // mounted payload instances and spawns one fresh instance of this
    // prefab per field of the newly-selected cascade node.  Each spawned
    // instance can bind to the corresponding row slot via bound_index.
    std::string payload_spawn;

    // `cascade_flow: horizontal|vertical` (spec/44_6 §5h) — wohin die Payload-
    // Widgets gehen: in eine Zeile rechts des Dropdowns (Default) oder darunter.
    std::string cascade_flow;

    // `payload_scroll: <bool>` (spec/44_6 §5h) — die Payload-Zeile auf die
    // Prefab-Breite BESCHNEIDEN und mit waagerechtem Scrollbalken versehen,
    // statt eine tiefe Kaskade aus dem Prefab herauslaufen zu lassen.
    bool payload_scroll = false;

    // `enum: <EnumName>` — for type=dropdown/radio widgets: populate options
    // from the registered enum's labels and let `dropdown_enum` return the true
    // member value (see prefab_runtime widget-build + aiml_prefab_enum_labels).
    std::string enum_type;
};

struct MaterialUniformInfo {
    enum Kind { K_FLOAT, K_VEC2, K_VEC3, K_VEC4, K_INT, K_TEX } kind = K_FLOAT;
    float       scalar  = 0.0f;
    int32_t     integer = 0;
    std::vector<float> vec;   // 2..4 elements for vecN
    std::string tex_path;     // sampler2D
};

class MaterialDecl : public Node {
public:
    std::string name;
    std::string shader;       // path to fragment shader file
    std::vector<std::pair<std::string, MaterialUniformInfo>> uniforms;

    std::string toString() const override { return "MaterialDecl(" + name + ")"; }
};

class PrefabDecl : public Node {
public:
    std::string name;
    float width = 0, height = 0;
    std::string texture;
    bool draggable = false;
    /* spec/44_6: `input_transparent: true` am Prefab — gilt dann fuer JEDE
     * Instanz.  Pro Aufrufstelle zu setzen hiess, es beim naechsten Effekt zu
     * vergessen, und dann stiehlt die Dekoration wieder den Hover. */
    bool inputTransparent = false;
    /// `binds: <TypeName>` — declares which Model/cascade this prefab is a
    /// visual representation of.  Runtime uses it to route field bindings
    /// (`bind:` on children) to the correct Model-instance-per-Prefab-instance.
    std::string binds;
    std::vector<PrefabChildInfo> children;

    std::string toString() const override { return "PrefabDecl(" + name + ")"; }
};

// --- Modular Declarations ---

class NamespaceDecl : public Node {
public:
    std::string name;
    std::string toString() const override { return "NamespaceDecl(" + name + ")"; }
};

class ImportDecl : public Node {
public:
    std::string path;
    std::string toString() const override { return "ImportDecl(" + path + ")"; }
};

class ConfigDecl : public Node {
public:
    std::string entryPoint;
    size_t stackSize = 0;
    size_t heapSize = 0;
    std::string target;
    std::string version;
    std::string toString() const override { return "ConfigDecl(...)"; }
};

class Program : public Node {
public:
    std::vector<std::unique_ptr<Node>> declarations; // Functions, Classes, Global Stmts

    std::string toString() const override { return "Program(...)"; }
};

} // namespace AST
} // namespace AIML
