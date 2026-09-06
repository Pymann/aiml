#include "compiler.h"
#include "exec_impl.h"
#include "src/core/builtin_registry.h"
#include "src/core/asset_scanner.h"
#include "src/sys/asset_runtime.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <yaml-cpp/yaml.h>
#include <sys/time.h>
#include <unistd.h>
#include <dlfcn.h>
#include "src/sys/time_runtime.h"
#include "src/sys/process_runtime.h"
#include "src/sys/mem_runtime.h"
#include "src/core/path_runtime.h"
#include "src/sys/yaml_runtime.h"
#include "src/sys/string_runtime.h"
#include "src/sys/net_runtime.h"
#include "src/sys/tls_runtime.h"
#include "src/sys/ws_runtime.h"
#include "src/sys/db_runtime.h"
#include "src/sys/http_runtime.h"
#include "src/sys/math_runtime.h"
#include "src/sys/hash_runtime.h"
#include "src/sys/storage_runtime.h"
#include "src/sys/compress_runtime.h"
#include "src/sys/gltf_runtime.h"
#include "src/sys/gl_runtime.h"
#include "src/sys/engine_runtime.h"
#include "src/sys/prefab_runtime.h"
#include "src/sys/input_runtime.h"
#include "src/sys/audio_runtime.h"
#include "src/sys/mirror_runtime.h"
#include "src/sys/ai_runtime.h"
#include "src/sys/ui_runtime.h"
#include "src/sys/trace_runtime.h"
#include "src/sys/tensor_runtime.h"
#include "src/sys/arg_dump_runtime.h"
#include "src/sys/binary_io_runtime.h"

// LLVM includes
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/IR/Verifier.h>
#include <optional>

// Core compiler headers (pulled up so CompilerContext is available in
// the constructor, destructor and all methods below)
#include "src/core/context.h"
#include "src/core/ir_generator.h"
#include "src/core/ast_builder.h"
#include "src/core/ast_compiler.h"
#include <unordered_set>

namespace {

constexpr int kAimlFileWarningLines = 1000;
constexpr int kAimlFileErrorLines = 1500;
constexpr int kAimlFunctionErrorLines = 301;
constexpr int kAimlFunctionMaxArgs = 5;
// Per-file structural limit: how many top-level model/class/interface
// declarations are allowed in a single AIML source file.  Forces module
// splitting at the declaration-level instead of the file-line-level alone.
constexpr int kAimlMaxModelsPerFile = 3;

// Effektive Zeilen: ignoriert reine Kommentar-Zeilen (`# ...`) und
// komplett leere Zeilen.  Doku ist gratis, der Limit zählt nur Code.
// Eine Zeile mit Code + trailing comment (`set: [x, 1]  # note`) wird
// gezählt — das ist Code.
int countFileLines(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return -1;
    int lines = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;            // leer
        if (line[first] == '#') continue;                    // reiner Kommentar
        lines++;
    }
    return lines;
}

std::string canonicalizeDiagPath(const std::string& path);

// Count top-level `- model:` / `- class:` / `- interface:` declarations
// in a raw AIML YAML file.  We do NOT parse YAML — we scan line-prefix
// patterns, which is robust enough because top-level list items always
// start at column 0 with "- " followed by the keyword.
// Returns {n_models, n_classes, n_interfaces}.
struct DeclCounts { int models = 0; int classes = 0; int interfaces = 0; };
DeclCounts countTopLevelDeclsInFile(const std::string& path) {
    DeclCounts c;
    std::ifstream in(path);
    if (!in.is_open()) return c;
    std::string line;
    while (std::getline(in, line)) {
        // Strip CR for CRLF files.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Top-level YAML list items start at column 0.  Indented items
        // (members, methods within a class) are skipped automatically.
        if (line.rfind("- model:",     0) == 0) { c.models++;     continue; }
        if (line.rfind("- class:",     0) == 0) { c.classes++;    continue; }
        if (line.rfind("- interface:", 0) == 0) { c.interfaces++; continue; }
    }
    return c;
}

/// True if the file's first ~20 lines carry a comment opting out of the static
/// architecture checks.  Any of these markers works:
///   # static-check: off
///   # static-check: relaxed
///   # Requires: library
/// Library files are imported by other tests and can genuinely exceed the
/// per-file type cap.
static bool fileOptsOutOfStaticChecks(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    int scanned = 0;
    while (std::getline(in, line) && scanned++ < 20) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Strip leading whitespace.
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        std::string rest = line.substr(s);
        if (rest.rfind("# static-check: off",     0) == 0) return true;
        if (rest.rfind("# static-check: relaxed", 0) == 0) return true;
        if (rest.rfind("# Requires: library",     0) == 0) return true;
    }
    return false;
}

void logAimlDeclCountDiagnostic(const std::string& path) {
    static std::mutex s_mu;
    static std::unordered_set<std::string> s_seen;
    {
        std::lock_guard<std::mutex> lk(s_mu);
        if (!s_seen.insert(canonicalizeDiagPath(path)).second) return;
    }
    if (fileOptsOutOfStaticChecks(path)) return;
    DeclCounts c = countTopLevelDeclsInFile(path);
    int total = c.models + c.classes + c.interfaces;
    if (total > kAimlMaxModelsPerFile) {
        LOG_ERROR(path + ": file declares " + std::to_string(total) +
                  " top-level types (" + std::to_string(c.models) + " model, " +
                  std::to_string(c.classes) + " class, " +
                  std::to_string(c.interfaces) + " interface). " +
                  "Maximum is " + std::to_string(kAimlMaxModelsPerFile) +
                  "; split this file into smaller modules.");
    }
}

// Helpers for the "main composes external class" rule (3+ project files).
// We do a raw text scan instead of an AST walk to keep this independent
// of compile passes.  Top-level `- class:` blocks have their `name: X` on
// the next non-empty line at column 4 (YAML two-space indent applied to
// list item).  Same shape for `- model:` and `- interface:`.

struct TopLevelDecl { std::string name; std::string kind; }; // kind = class|model|interface|classgroup
std::vector<TopLevelDecl> scanTopLevelDecls(const std::string& path) {
    std::vector<TopLevelDecl> out;
    std::ifstream in(path);
    if (!in.is_open()) return out;
    std::string line;
    std::string pendingKind;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("- model:",      0) == 0) { pendingKind = "model";      continue; }
        if (line.rfind("- class:",      0) == 0) { pendingKind = "class";      continue; }
        if (line.rfind("- interface:",  0) == 0) { pendingKind = "interface";  continue; }
        // A classgroup synthesizes a container class of the group's name; a
        // `{new: <Group>}` from another file is external composition (spec/58).
        if (line.rfind("- classgroup:", 0) == 0) { pendingKind = "classgroup"; continue; }
        if (!pendingKind.empty()) {
            // Look for "name: X" (any indent — usually 4 spaces).
            auto pos = line.find("name:");
            if (pos != std::string::npos) {
                size_t i = pos + 5;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
                std::string name;
                while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) {
                    name += line[i++];
                }
                if (!name.empty()) out.push_back({name, pendingKind});
                pendingKind.clear();
            } else if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
                // Next top-level item arrived without `name:` — bail.
                pendingKind.clear();
            }
        }
    }
    return out;
}

// Look for `- def:` ... `name: main` pattern in raw text.  Returns true
// if the file declares a top-level `main` function.
bool fileDeclaresMain(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string line;
    bool inDef = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("- def:", 0) == 0) { inDef = true; continue; }
        if (inDef) {
            auto pos = line.find("name:");
            if (pos != std::string::npos) {
                size_t i = pos + 5;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
                std::string name;
                while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) {
                    name += line[i++];
                }
                if (name == "main") return true;
                inDef = false;
            } else if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
                inDef = false;
            }
        }
    }
    return false;
}

// Collect all class names instantiated via `{ new: ClassName }` in the
// given file.  We grep for the substring "new:" and read the following
// identifier — that's the class name being instantiated.
std::set<std::string> findNewExprClassNames(const std::string& path) {
    std::set<std::string> out;
    std::ifstream in(path);
    if (!in.is_open()) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t pos = 0;
        while ((pos = line.find("new:", pos)) != std::string::npos) {
            size_t i = pos + 4;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            std::string name;
            while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) {
                name += line[i++];
            }
            if (!name.empty()) out.insert(name);
            pos += 4;
        }
    }
    return out;
}

void logMainComposesExternalClassDiagnostic(
        const std::string& entryFile,
        const std::vector<std::string>& importedFiles)
{
    size_t projectFileCount = 1 + importedFiles.size();
    if (projectFileCount < 3) return;
    if (fileOptsOutOfStaticChecks(entryFile)) return;

    if (!fileDeclaresMain(entryFile)) {
        LOG_ERROR(entryFile + ": project spans " + std::to_string(projectFileCount) +
                  " AIML files but does not declare a `main` function. " +
                  "Projects with 3+ files must use a `main` entry point.");
        return;
    }

    // Build {className -> sourceFile} for class declarations across all files.
    std::map<std::string, std::string> classToFile;
    auto absorb = [&](const std::string& f) {
        for (auto& d : scanTopLevelDecls(f)) {
            if (d.kind == "class" || d.kind == "classgroup") classToFile[d.name] = f;
        }
    };
    absorb(entryFile);
    for (const auto& f : importedFiles) absorb(f);

    // Find all `new: X` in the entry file.
    auto news = findNewExprClassNames(entryFile);
    std::string entryCanonical = canonicalizeDiagPath(entryFile);
    bool composedExternal = false;
    for (const auto& cls : news) {
        auto it = classToFile.find(cls);
        if (it == classToFile.end()) continue;
        if (canonicalizeDiagPath(it->second) != entryCanonical) {
            composedExternal = true;
            break;
        }
    }
    if (!composedExternal) {
        LOG_ERROR(entryFile + ": project spans " + std::to_string(projectFileCount) +
                  " AIML files; `main` must instantiate at least one class " +
                  "declared in another file. Without external composition the " +
                  "extra files are dead weight — split the entry point and " +
                  "wire it from a separate module.");
    }
}

std::string canonicalizeDiagPath(const std::string& path) {
    try {
        return std::filesystem::weakly_canonical(path).string();
    } catch (...) {
        return path;
    }
}

bool emitAimlDiagOnce(const std::string& key) {
    static std::mutex s_mu;
    static std::unordered_set<std::string> s_seen;
    std::lock_guard<std::mutex> lk(s_mu);
    return s_seen.insert(key).second;
}

void logAimlFileSizeDiagnostic(const std::string& path,
                               std::unordered_set<std::string>* = nullptr) {
    static std::mutex s_mu;
    static std::unordered_set<std::string> s_seen;
    {
        std::lock_guard<std::mutex> lk(s_mu);
        if (!s_seen.insert(canonicalizeDiagPath(path)).second) return;
    }
    int lines = countFileLines(path);
    if (lines < 0) return;
    if (lines >= kAimlFileErrorLines) {
        LOG_ERROR(path + ": AIML file too large (" + std::to_string(lines) +
                  " lines). Maximum is 1499 lines; 1500+ is a hard error.");
    } else if (lines >= kAimlFileWarningLines) {
        LOG_WARNING(path + ": AIML file large (" + std::to_string(lines) +
                    " lines). 1000+ lines should be split.");
    }
}

int maxStmtSourceLine(const AIML::AST::Stmt* stmt);

int maxBlockSourceLine(const AIML::AST::BlockStmt* block) {
    if (!block) return -1;
    int maxLine = block->sourceLine;
    for (const auto& stmt : block->statements) {
        maxLine = std::max(maxLine, maxStmtSourceLine(stmt.get()));
    }
    return maxLine;
}

int maxStmtSourceLine(const AIML::AST::Stmt* stmt) {
    if (!stmt) return -1;
    int maxLine = stmt->sourceLine;
    if (auto ifStmt = dynamic_cast<const AIML::AST::IfStmt*>(stmt)) {
        maxLine = std::max(maxLine, maxBlockSourceLine(ifStmt->thenBranch.get()));
        maxLine = std::max(maxLine, maxBlockSourceLine(ifStmt->elseBranch.get()));
    } else if (auto whileStmt = dynamic_cast<const AIML::AST::WhileStmt*>(stmt)) {
        maxLine = std::max(maxLine, maxBlockSourceLine(whileStmt->body.get()));
    } else if (auto tryStmt = dynamic_cast<const AIML::AST::TryStmt*>(stmt)) {
        maxLine = std::max(maxLine, maxBlockSourceLine(tryStmt->tryBlock.get()));
        maxLine = std::max(maxLine, maxBlockSourceLine(tryStmt->catchBlock.get()));
    }
    return maxLine;
}

// Zähle Code-Zeilen (keine reinen Kommentare, keine leeren) im Bereich
// [startLine, endLine] inklusive einer Datei.  Spiegelbild von
// countFileLines aber für einen Subrange.  Falls Datei fehlt → roher span.
int countCodeLinesInRange(const std::string& path, int startLine, int endLine) {
    std::ifstream in(path);
    if (!in.is_open()) return endLine - startLine + 1;
    int curLine = 0;
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++curLine;
        if (curLine < startLine) continue;
        if (curLine > endLine) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (line[first] == '#') continue;
        ++count;
    }
    return count;
}

void logAimlFunctionSizeDiagnostic(const std::string& path, const AIML::AST::FunctionDecl* func) {
    if (!func || func->sourceLine <= 0 || !func->body) return;
    // Respect static_off: ["lines"] — skip line-count check for this function
    for (const auto& opt : func->staticOff) {
        if (opt == "lines") return;
    }
    int endLine = maxBlockSourceLine(func->body.get());
    if (endLine < func->sourceLine) endLine = func->sourceLine;
    int lines = countCodeLinesInRange(path, func->sourceLine, endLine);
    if (lines >= kAimlFunctionErrorLines) {
        std::string key = "fn_lines|" + canonicalizeDiagPath(path) + "|" +
                          std::to_string(func->sourceLine) + "|" + func->name;
        if (!emitAimlDiagOnce(key)) return;
        LOG_ERROR(path + ":" + std::to_string(func->sourceLine) +
                  ": AIML function '" + func->name + "' too large (" +
                  std::to_string(lines) +
                  " lines). Maximum is 300 lines; 301+ is a hard error.");
    }
}

void logAimlFunctionArgCountDiagnostic(const std::string& path, const AIML::AST::FunctionDecl* func) {
    if (!func || func->sourceLine <= 0) return;
    for (const auto& opt : func->staticOff) {
        if (opt == "args") return;
    }
    int argc = static_cast<int>(func->args.size());
    if (argc > kAimlFunctionMaxArgs) {
        std::string key = "fn_args|" + canonicalizeDiagPath(path) + "|" +
                          std::to_string(func->sourceLine) + "|" + func->name;
        if (!emitAimlDiagOnce(key)) return;
        LOG_ERROR(path + ":" + std::to_string(func->sourceLine) +
                  ": AIML function '" + func->name + "' has too many arguments (" +
                  std::to_string(argc) + "). Maximum is " +
                  std::to_string(kAimlFunctionMaxArgs) +
                  ". Use class/model-based programming to group related state and behavior.");
    }
}

}

AIMLCompiler::AIMLCompiler() {
    context_ = std::make_unique<llvm::LLVMContext>();
    module_ = std::make_unique<llvm::Module>("AIML_Module", *context_);

    // Initialize Target
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    
    std::string error;
    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    
    if (!target) {
        LOG_ERROR("Failed to lookup target: " + error);
        return;
    }
    
    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    auto targetMachine = target->createTargetMachine(targetTriple, "generic", "", opt, rm);
    
    module_->setDataLayout(targetMachine->createDataLayout());
    module_->setTargetTriple(targetTriple);
}

AIMLCompiler::~AIMLCompiler() {
    // Destroy in reverse-construction order:
    // CompilerContext holds refs to module + builder → destroy first.
    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;
    // engine_ is only set by compileChunkAndGetAddress (hot-reload path).
    // The ExecutionEngine took ownership of module_ via std::move, so do NOT
    // delete module_ separately when engine_ is set.
    if (engine_) { delete engine_; engine_ = nullptr; }
}

const llvm::Module& AIMLCompiler::getModule() const {
    return *module_;
}

// ... (Functions compile, compileToObject, etc.)

// --prefab-report: format the merged wiring facts (tab-separated records
// D/I/S/C, see checkPrefabUsage) into a per-prefab report and print to stdout.
// The report is the deliverable, so it goes to std::cout, not the logger.
static void printPrefabReport(const std::vector<std::string>& rawLines,
                              const std::string& entry,
                              const std::string& filter) {
    // Case-insensitive substring match (empty filter matches everything).
    auto icontains = [](const std::string& hay, const std::string& needle) -> bool {
        if (needle.empty()) return true;
        auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        return lower(hay).find(lower(needle)) != std::string::npos;
    };
    struct Child { std::string name, type, spawnTgt, channel, attrs; };
    struct PInfo {
        bool declared = false;
        bool drag = false;                       // whole-prefab draggable
        std::string declLoc;
        std::vector<Child> children;             // in declaration order
        std::vector<std::string> inst;           // engine.instantiate sites (roots)
        std::map<std::string, std::vector<std::string>> childAccess;  // set_* per child
        std::set<std::string> buttons;           // child names of type=button
    };
    auto split = [](const std::string& s) {
        std::vector<std::string> out; size_t i = 0;
        for (;;) {
            size_t t = s.find('\t', i);
            if (t == std::string::npos) { out.push_back(s.substr(i)); break; }
            out.push_back(s.substr(i, t - i)); i = t + 1;
        }
        return out;
    };
    auto trimLoc = [](std::string l) {           // fmtLoc yields "file:line: "
        while (!l.empty() && (l.back() == ' ' || l.back() == ':')) l.pop_back();
        return l;
    };
    // Order-preserving dedup (diamond imports can merge a module twice) — keeps
    // child declaration order, which the recursive tree relies on.
    std::set<std::string> seenSet;
    std::vector<std::string> lines;
    for (const auto& l : rawLines) if (seenSet.insert(l).second) lines.push_back(l);
    std::map<std::string, PInfo> P;
    std::set<std::string> clicked;               // button names polled via child_clicked
    std::set<std::string> spawnTargets;          // every prefab named by a spawn/payload_spawn
    // Behavioral wiring (F/O records).
    struct FieldOp { std::string op, bucket, def, arg0; };
    std::map<std::string, std::string> fieldPrefab;               // "class\tfield" -> prefab
    std::map<std::string, std::vector<FieldOp>> fieldOps;         // "class\tfield" -> ops
    std::map<std::string, std::vector<std::string>> prefabOwners; // prefab -> ["class\tfield", ...]
    // Collections (list<prefab_instance> fields): M declares, L binds element, T touches.
    struct Coll { std::string elem; std::set<std::string> touchedBy; };
    std::map<std::string, Coll> colls;                            // "class\tfield" (list only)
    std::map<std::string, std::vector<std::string>> prefabInColl; // prefab -> ["class.field", ...]
    std::map<std::string, std::set<std::string>> actions;         // "class\tdef" -> {net_* calls}
    std::map<std::string, std::set<std::string>> keyHandlers;     // key -> {"class\tdef"}
    for (const auto& line : lines) {
        auto f = split(line);
        if (f.empty()) continue;
        if      (f[0] == "D" && f.size() >= 3) { auto& p = P[f[1]]; p.declared = true; p.declLoc = trimLoc(f[2]); }
        else if (f[0] == "I" && f.size() >= 3) P[f[1]].inst.push_back(trimLoc(f[2]));
        else if (f[0] == "C" && f.size() >= 4) P[f[1]].childAccess[f[2]].push_back(trimLoc(f[3]));
        else if (f[0] == "B" && f.size() >= 3) P[f[1]].buttons.insert(f[2]);
        else if (f[0] == "G" && f.size() >= 3) { if (f[2] == "*") P[f[1]].drag = true; }
        else if (f[0] == "K" && f.size() >= 2) clicked.insert(f[1]);
        else if (f[0] == "F" && f.size() >= 4) {
            std::string key = f[1] + "\t" + f[2];
            fieldPrefab[key] = f[3];
            auto& own = prefabOwners[f[3]];
            if (std::find(own.begin(), own.end(), key) == own.end()) own.push_back(key);
        }
        else if (f[0] == "O" && f.size() >= 6) {
            FieldOp o; o.op = f[3]; o.bucket = f[4]; o.def = f[5]; o.arg0 = (f.size() >= 7) ? f[6] : "";
            fieldOps[f[1] + "\t" + f[2]].push_back(o);
        }
        else if (f[0] == "M" && f.size() >= 4) {                  // list<prefab_instance> member
            if (f[3] == "1") colls[f[1] + "\t" + f[2]];           // ensure entry exists
        }
        else if (f[0] == "L" && f.size() >= 5) {                  // list push: element prefab
            std::string key = f[1] + "\t" + f[2];
            auto& c = colls[key];
            if (c.elem.empty()) c.elem = f[3];
            c.touchedBy.insert(f[4]);
            std::string label = (f[1].empty() ? "" : f[1] + ".") + f[2];
            auto& v = prefabInColl[f[3]];
            if (std::find(v.begin(), v.end(), label) == v.end()) v.push_back(label);
        }
        else if (f[0] == "T" && f.size() >= 4) colls[f[1] + "\t" + f[2]].touchedBy.insert(f[3]);
        else if (f[0] == "N" && f.size() >= 4) actions[f[1] + "\t" + f[2]].insert(f[3]);
        else if (f[0] == "Y" && f.size() >= 4) keyHandlers[f[3]].insert(f[1] + "\t" + f[2]);
        else if (f[0] == "H" && f.size() >= 6) {
            Child c; c.name = f[2]; c.type = f[3]; c.spawnTgt = f[4]; c.channel = f[5];
            c.attrs = (f.size() >= 7) ? f[6] : "";
            if (!c.spawnTgt.empty()) spawnTargets.insert(c.spawnTgt);
            P[f[1]].children.push_back(std::move(c));
        }
    }
    // Helper: render the behavioral "driven by" block for a prefab into `os`.
    // Groups the operations on each owner field by bucket, then by driver method,
    // and appends completeness flags.  Returns nothing; prints indented lines.
    auto behavioralBlock = [&](std::ostringstream& out, const std::string& prefab) {
        auto oit = prefabOwners.find(prefab);
        if (oit == prefabOwners.end()) return;   // not held in a class field we could link
        static const char* order[] = {"create", "content", "gauge", "motion", "visibility", "interaction"};
        for (const std::string& key : oit->second) {
            size_t tab = key.find('\t');
            std::string cls = key.substr(0, tab), fld = key.substr(tab + 1);
            out << "    owner: " << (cls.empty() ? "(global)" : cls) << "." << fld << "\n";
            auto fit = fieldOps.find(key);
            std::set<std::string> buckets;
            if (fit != fieldOps.end())
                for (auto& o : fit->second) buckets.insert(o.bucket);
            for (const char* b : order) {
                if (!buckets.count(b)) continue;
                // collect defs -> ops for this bucket
                std::map<std::string, std::vector<std::string>> byDef;
                for (auto& o : fit->second)
                    if (o.bucket == b) {
                        std::string t = o.op;
                        if (!o.arg0.empty()) t += "(" + o.arg0 + ")";
                        auto& v = byDef[o.def.empty() ? "?" : o.def];
                        if (std::find(v.begin(), v.end(), t) == v.end()) v.push_back(t);
                    }
                out << "      " << b << ": ";
                bool firstDef = true;
                for (auto& dv : byDef) {
                    if (!firstDef) out << "; "; firstDef = false;
                    out << dv.first << " (";
                    for (size_t i = 0; i < dv.second.size(); ++i) { out << dv.second[i]; if (i + 1 < dv.second.size()) out << ", "; }
                    out << ")";
                }
                out << "\n";
            }
            // ── completeness flags for this owner field (only high-confidence
            // ones; content is often set via a helper that takes the instance as
            // a parameter, which the field-op scan cannot attribute, so we do NOT
            // flag "no content" to avoid false positives). ──
            bool hasGauge = buckets.count("gauge");
            bool anyOp = (fit != fieldOps.end() && !fit->second.empty());
            bool hasBarChild = false;
            auto pit = P.find(prefab);
            if (pit != P.end())
                for (auto& c : pit->second.children)
                    if (c.type == "bar") hasBarChild = true;
            if (!anyOp)
                out << "      (!) created but never driven — no set_*/positioning on this field\n";
            if (hasBarChild && !hasGauge)
                out << "      (!) has a bar/gauge child but set_bar_* is never called on this field\n";
        }
    };
    int total = 0, reached = 0, dead = 0;
    for (auto& kv : P) {
        if (!kv.second.declared) continue;
        ++total;
        if (!kv.second.inst.empty() || spawnTargets.count(kv.first)) ++reached; else ++dead;
    }
    std::ostringstream os;
    os << "\n===================== PREFAB WIRING TREE =====================\n";
    os << "entry:  " << entry << "\n";
    os << total << " declared  |  " << reached << " reached  |  " << dead << " DEAD\n";
    os << "(roots = engine.instantiate sites, grouped by scene/source; children\n";
    os << " recurse through spawn: / payload_spawn:)\n";
    os << "owner: = class field holding the instance; below it the methods that\n";
    os << " drive it, grouped by effect: content|gauge|motion|visibility|interaction.\n";
    os << " NB: content set via a helper (populate(inst, ...)) is not attributed to\n";
    os << " the field, so an empty 'content:' line is not proof of 'not wired'.\n";
    if (!filter.empty())
        os << "focused on scene: \"" << filter << "\" (roots by instantiate source; "
              "collections by class; actions shown in full)\n";

    // Scene index (navigation): each instantiate source with its root count.
    {
        std::map<std::string, int> sceneCount;
        for (auto& kv : P) if (kv.second.declared && !kv.second.inst.empty()) sceneCount[kv.second.inst.front()]++;
        os << "\nscenes (use --prefab-report=<substr> to focus one):\n";
        for (auto& sc : sceneCount)
            os << "  " << (icontains(sc.first, filter) ? "* " : "  ") << sc.first
               << "   (" << sc.second << " roots)\n";
    }

    // Recursive child renderer.  `vis` guards against spawn cycles.
    std::function<void(const std::string&, const std::string&, std::set<std::string>&)> tree;
    tree = [&](const std::string& pf, const std::string& prefix, std::set<std::string>& vis) {
        auto it = P.find(pf);
        if (it == P.end()) return;
        auto& kids = it->second.children;
        for (size_t i = 0; i < kids.size(); ++i) {
            const Child& c = kids[i];
            bool last = (i + 1 == kids.size());
            os << prefix << (last ? "\\-- " : "|-- ") << c.name << " (" << c.type << ")";
            if (c.type == "button")
                os << (clicked.count(c.name) ? "  [click]" : "  [click: NO HANDLER]");
            if (it->second.childAccess.count(c.name)) os << "  [set_*]";
            if (!c.attrs.empty()) os << "  " << c.attrs;
            os << "\n";
            std::string deep = prefix + (last ? "    " : "|   ");
            if (!c.spawnTgt.empty()) {
                if (vis.count(c.spawnTgt)) {
                    os << deep << "\\-> " << c.spawnTgt << "  (" << c.channel << ", recursion cut)\n";
                } else if (P.count(c.spawnTgt) && P[c.spawnTgt].declared) {
                    os << deep << "\\-> " << c.spawnTgt << "  (" << c.channel << ")   ["
                       << P[c.spawnTgt].declLoc << "]\n";
                    vis.insert(c.spawnTgt);
                    tree(c.spawnTgt, deep + "    ", vis);
                    vis.erase(c.spawnTgt);
                } else {
                    os << deep << "\\-> " << c.spawnTgt << "  (" << c.channel
                       << ", NOT declared in this build)\n";
                }
            }
        }
    };

    // Roots grouped BY SCENE — as (instantiate-site, root) pairs across ALL of a
    // root's instantiate sites, so a prefab used in several screens (e.g. CardFull
    // in the board AND the card editor) appears under each.  A scene filter keeps
    // only the sites whose source matches; because every field-owned prefab is
    // instantiated in its owner class's source, a source substring like "board"
    // captures the whole scene without needing a separate class match.
    std::set<std::pair<std::string, std::string>> sceneRoots;   // (site, root)
    for (auto& kv : P)
        if (kv.second.declared)
            for (auto& s : kv.second.inst) sceneRoots.insert({s, kv.first});
    std::string curSite;
    for (const auto& pr : sceneRoots) {
        const std::string& site = pr.first;
        const std::string& r = pr.second;
        if (!filter.empty() && !icontains(site, filter)) continue;
        if (site != curSite) {
            curSite = site;
            os << "\n==== scene/source: " << site << " ====\n";
        }
        os << "\n# " << r;
        if (P[r].drag) os << "  [draggable]";
        os << "   [" << P[r].declLoc << "]\n";
        behavioralBlock(os, r);
        auto cit = prefabInColl.find(r);
        if (cit != prefabInColl.end()) {
            os << "    in collection: ";
            for (size_t i = 0; i < cit->second.size(); ++i) { os << cit->second[i] << " [list]"; if (i + 1 < cit->second.size()) os << ", "; }
            os << "\n";
        }
        std::set<std::string> vis; vis.insert(r);
        tree(r, "", vis);
    }

    // ── Collections: list<prefab_instance> fields, their element prefab (if a
    // push binds one) and the methods that build/update them. ──
    if (!colls.empty()) {
        os << "\n==== COLLECTIONS (list<prefab_instance> fields) ====\n";
        for (auto& kv : colls) {
            size_t tab = kv.first.find('\t');
            std::string cls = kv.first.substr(0, tab), fld = kv.first.substr(tab + 1);
            if (!filter.empty() && !icontains(cls, filter)) continue;
            os << (cls.empty() ? "" : cls + ".") << fld << "   element: "
               << (kv.second.elem.empty() ? "? (filled via helper/param — not resolved)" : kv.second.elem)
               << "   touched by: ";
            bool first = true;
            for (auto& d : kv.second.touchedBy) { if (!first) os << ", "; first = false; os << d; }
            if (kv.second.touchedBy.empty()) os << "(none)";
            os << "\n";
        }
    }

    // DEAD prefabs: declared, never instantiated AND never a spawn target.
    std::vector<std::string> deadList;
    for (auto& kv : P)
        if (kv.second.declared && kv.second.inst.empty() && !spawnTargets.count(kv.first))
            deadList.push_back(kv.first);
    if (!deadList.empty()) {
        os << "\n==== DEAD (declared, never instantiated or spawned) ====\n";
        for (auto& d : deadList) os << "# " << d << "   [" << P[d].declLoc << "]\n";
    }

    // ── Actions: which method fires which server RPC (net_*).  Surfaces the
    // functional wiring behind interactions (e.g. handle_drop / dispatch ->
    // net_play_card) that the structural tree alone cannot show. ──
    if (!actions.empty()) {
        os << "\n==== ACTIONS (server RPCs fired, by method) ====\n";
        // ACTIONS is a cross-cutting overview (server RPCs often live in a
        // dispatch class, not the scene class) — always shown in full, even under
        // a scene filter, so e.g. net_play_card is never hidden from a board view.
        for (auto& kv : actions) {
            size_t tab = kv.first.find('\t');
            std::string cls = kv.first.substr(0, tab), def = kv.first.substr(tab + 1);
            os << "  " << (cls.empty() ? "" : cls + ".") << def << "  ->  ";
            bool first = true;
            for (auto& n : kv.second) { if (!first) os << ", "; first = false; os << n; }
            os << "\n";
        }
    }

    // ── Key bindings: keyboard keys handled via input.key_pressed("<key>"),
    // and the method that reacts to each.  Filtered by class like collections
    // (keys are scene-local), so a scene focus shows only that scene's keys. ──
    if (!keyHandlers.empty()) {
        std::vector<std::pair<std::string, std::vector<std::string>>> rows;
        for (auto& kv : keyHandlers) {
            std::vector<std::string> hs;
            for (auto& h : kv.second) {
                size_t tab = h.find('\t');
                std::string cls = h.substr(0, tab), def = h.substr(tab + 1);
                if (!filter.empty() && !icontains(cls, filter)) continue;
                hs.push_back((cls.empty() ? "" : cls + ".") + def);
            }
            if (!hs.empty()) rows.push_back({kv.first, hs});
        }
        if (!rows.empty()) {
            os << "\n==== KEY BINDINGS (input.key_pressed) ====\n";
            for (auto& r : rows) {
                os << "  \"" << r.first << "\""
                   << std::string(r.first.size() < 6 ? 6 - r.first.size() : 1, ' ') << "->  ";
                for (size_t i = 0; i < r.second.size(); ++i) { os << r.second[i]; if (i + 1 < r.second.size()) os << ", "; }
                os << "\n";
            }
        }
    }

    // ── Interaction summary: buttons and drag-and-drop across the build ──
    int btnTotal = 0, btnUnwired = 0, dragPrefabs = 0;
    std::vector<std::string> unwired;   // "Prefab.button" never polled anywhere
    for (auto& kv : P) {
        if (!kv.second.declared) continue;
        if (kv.second.drag) ++dragPrefabs;
        for (auto& b : kv.second.buttons) {
            ++btnTotal;
            if (!clicked.count(b)) { ++btnUnwired; unwired.push_back(kv.first + "." + b); }
        }
    }
    os << "\n===================== INTERACTION SUMMARY ====================\n";
    os << "buttons: " << btnTotal << " total, " << btnUnwired
       << " with NO child_clicked handler anywhere\n";
    for (auto& u : unwired) os << "    (!) " << u << "\n";
    os << "drag sources: " << dragPrefabs << " draggable prefab(s) "
          "(drop targets are handled imperatively in board logic)\n";
    os << "note: a button name polled via child_clicked anywhere counts as wired "
          "(names are not prefab-unique — heuristic; NEVER polled = definite dead button).\n";
    os << "=============================================================\n";
    std::cout << os.str() << std::endl;
}

void AIMLCompiler::compile(const std::string& filename, const std::string& outputFilename) {
    LOG_DEBUG("Compiling " + filename);
    // Asset Registry: scan roots once at the top of the compile.  Stored in
    // a singleton so builtin_registry can validate string-literal lookups and
    // the JIT-runtime can resolve stems via the same in-memory tables.
    AIML::AssetScanner::instance().scan(assetRootsStr);
    YAML::Node config = YAML::LoadFile(filename);
    auto seenFileSizeDiagnostics = std::make_shared<std::unordered_set<std::string>>();
    logAimlFileSizeDiagnostic(filename, seenFileSizeDiagnostics.get());
    logAimlDeclCountDiagnostic(filename);

    // 1. (Legacy __aiml_init creation removed. AST::Compiler now handles it)

    // 2. Initialize Compiler Context — heap-allocated so it survives into jitRun()
    //    allowing aiml_exec_code to access the symbol table during JIT execution.
    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;

    builder_inst_ = new llvm::IRBuilder<>(*context_);
    auto& builder  = *static_cast<llvm::IRBuilder<>*>(builder_inst_);

    compiler_ctx_ = new CompilerContext(*context_, *module_, builder);
    CompilerContext& ctx = *static_cast<CompilerContext*>(compiler_ctx_);

    ctx.compilerPath = compilerPath;
    ctx.libraryMode = libraryMode;
    ctx.reportPrefabs = reportPrefabsMode;   // --prefab-report: collect wiring facts
    ctx.ir_gen = AIML::createLLVMGenerator(ctx);

    // Register builtins (classes, interfaces, namespace fns) BEFORE the
    // sibling-import / header-scan phase parses any user code.  Otherwise a
    // class header that says `implements: [mirror.RpcHandler]` fires
    // "unknown interface" during sibling registration — that early visit
    // runs registerTypes but never reaches the AST::Compiler::compile()
    // call that normally seeds builtins.
    registerBuiltins(ctx);

    // 3. Setup Globals: __argc, __argv for sys commands
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());

    // Per-instance set: each compiler gets its own header-propagation guard.
    // Shared via shared_ptr so the lambda can capture by value.
    auto headerDone = std::make_shared<std::unordered_set<std::string>>();

    // Deferred import compilation: record paths during registerTypes, compile
    // to .o AFTER all types from all imports are known.  This prevents errors
    // when file A references types defined in sibling import file B.
    auto deferredImports = std::make_shared<std::vector<std::string>>();

    // Per-run guard for the *parser* phase (not the .o cache; that uses
    // compiledKeys).  Shared via a member shared_ptr so sub-compilers within
    // the same top-level run reuse the dedup set (prevents exponential
    // re-parsing of diamond imports — the prior code used a process-wide
    // static set; this is the same idea but scoped to one compile root).
    auto parsedPaths = this->parsedPathsShared;
    ctx.resolveImportCallback = [this, &ctx, headerDone, deferredImports, parsedPaths, seenFileSizeDiagnostics](const std::string& rawPath) {
        // Resolve relative imports: try cwd first (legacy behaviour),
        // then walk up from the importing file's directory looking for a
        // base where <base>/<rawPath> exists.  This makes "models/enums.aiml"
        // work whether the build is run from project root or from elsewhere.
        std::string path = rawPath;
        if (!std::filesystem::exists(path) && !ctx.currentSourceFile.empty()) {
            try {
                std::filesystem::path cur =
                    std::filesystem::path(ctx.currentSourceFile).parent_path();
                while (true) {
                    std::filesystem::path candidate = cur / rawPath;
                    if (std::filesystem::exists(candidate)) {
                        path = candidate.string();
                        break;
                    }
                    if (cur.empty() || cur == cur.root_path()) break;
                    cur = cur.parent_path();
                }
            } catch (...) {}
        }

        // Normalize ONLY for dedup. Keep the original (resolved-but-not-canonical)
        // path string when pushing to deferredImports / using as the import path,
        // because exodus's directory scan derives the .o filename from the
        // sanitized form of this string and expects the relative-style spelling
        // (e.g. "client/board_layout.aiml" → "client_board_layout_aiml.o").
        std::string normPath;
        try { normPath = std::filesystem::weakly_canonical(path).string(); }
        catch (...) { normPath = path; }
        if (parsedPaths->insert(normPath).second) {
            deferredImports->push_back(path);
        }

        // Register the lib init function name (deterministic, based on path).
        // Must match the name generated in ast_compiler.cpp library mode.
        {
            std::string sanitized;
            for (char c : path)
                sanitized += (std::isalnum(c) ? c : '_');
            std::string libInitName = "__aiml_lib_init_" + sanitized;
            if (std::find(ctx.libInitFuncNames.begin(), ctx.libInitFuncNames.end(), libInitName)
                == ctx.libInitFuncNames.end()) {
                ctx.libInitFuncNames.push_back(libInitName);
            }
        }

        // Per-instance guard: header-propagate into THIS context at most once per file.
        // Each sub-compiler gets its own headerDone set, so it always sees all imported types.
        if (headerDone->count(path)) return;
        headerDone->insert(path);

        // Header-Only Mode for current compiler: Parse the import for signatures!
        std::string prevNs = ctx.currentNamespace;
        bool prevHeaderOnly = ctx.headerOnlyMode;
        ctx.headerOnlyMode = true;
        // Track the importing file for nested-import path resolution: while
        // we recursively process this import's own import directives, the
        // callback needs to walk up from THIS file, not the top-level entry.
        std::string prevSrcFile = ctx.currentSourceFile;
        ctx.currentSourceFile = path;

        AIML::AST::Builder subBuilder;
        std::ifstream fin(path);
        if (fin.is_open()) {
            try {
                logAimlFileSizeDiagnostic(path, seenFileSizeDiagnostics.get());
                logAimlDeclCountDiagnostic(path);
                YAML::Parser parser(fin);
                parser.HandleNextDocument(subBuilder);
                auto subProg = subBuilder.takeProgram();
                if (subProg) {
                    AIML::AST::Compiler subCompiler(ctx);
                    subCompiler.registerTypes(subProg.get());
                    // spec/58 §11: synthesize any classgroup DEFINED in this
                    // imported file so its container + subclass TYPES are visible
                    // here.  This appends the synthesized ClassDecls to
                    // subProg->declarations; the loop below visitTopLevel's them in
                    // header-only mode (forward-declaring __ctor + group methods).
                    // Bodies are emitted only in the defining unit's own compile().
                    subCompiler.synthesizeImportedGroups(subProg.get());
                    for (const auto& decl : subProg->declarations) {
                        if (auto func = dynamic_cast<AIML::AST::FunctionDecl*>(decl.get())) {
                            logAimlFunctionSizeDiagnostic(path, func);
                            logAimlFunctionArgCountDiagnostic(path, func);
                            subCompiler.visitFunction(func);
                        } else if (auto cls = dynamic_cast<AIML::AST::ClassDecl*>(decl.get())) {
                            // Register class method signatures (header-only
                            // mode will skip bodies in visitFunction).
                            subCompiler.visitTopLevel(cls);
                        } else if (auto mod = dynamic_cast<AIML::AST::ModelDecl*>(decl.get())) {
                            subCompiler.visitTopLevel(mod);
                        } else if (auto d = dynamic_cast<AIML::AST::DeclStmt*>(decl.get())) {
                            // Extern decl matching ast_compiler.cpp Pass 1 naming:
                            // {extern: true} → plain __lib_<varName>
                            // otherwise      → file-qualified __lib_<sanitized_path>_<varName>
                            if (ctx.symbol_table.count(d->varName)) continue;
                            AIML::IRType* ty = ctx.ir_gen->resolveType(d->typeName);
                            if (!ty) continue;
                            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
                            std::string symName;
                            if (d->is_extern) {
                                symName = "__lib_" + d->varName;
                            } else {
                                std::string importSuffix;
                                for (char c : path) importSuffix += (std::isalnum(c) ? c : '_');
                                symName = "__lib_" + importSuffix + "_" + d->varName;
                            }
                            llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
                            if (!gv) {
                                gv = new llvm::GlobalVariable(
                                    ctx.module, llvmTy, false,
                                    llvm::GlobalValue::ExternalLinkage,
                                    nullptr,
                                    symName);
                            }
                            Symbol sym;
                            sym.ir_alloca_inst = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
                            sym.ir_type        = ctx.ir_gen->getPointerTy();
                            sym.ir_element_type = ty;
                            sym.typeName       = d->typeName;
                            sym.is_const       = d->is_const;
                            sym.is_volatile    = d->is_volatile;
                            ctx.symbol_table[d->varName] = sym;
                        }
                    }
                    // spec/58 §11: keep this imported AST alive for the rest of
                    // the compilation — ctx.pendingGroupMethods holds raw pointers
                    // into it that mergeGroupMethods folds into their target group
                    // later (in the unit that also has the classgroup).
                    ctx.astKeepAlive.push_back(std::shared_ptr<void>(
                        subProg.release(),
                        [](void* p){ delete static_cast<AIML::AST::Program*>(p); }));
                }
            } catch (...) {
                LOG_ERROR(path + ": Failed to parse import signatures");
            }
        }
        ctx.currentNamespace = prevNs;
        ctx.headerOnlyMode = prevHeaderOnly;
        ctx.currentSourceFile = prevSrcFile;
    };

    // 4. Parse YAML to AST & Compile (Two-Pass)
    AIML::AST::Builder astBuilder;
    try {
        std::ifstream fin(filename);
        if (!fin.is_open()) {
             LOG_ERROR(filename + ": Could not open file");
             return;
        }
        YAML::Parser parser(fin);
        parser.HandleNextDocument(astBuilder);

        auto programAST = astBuilder.takeProgram();
        if (programAST) {
            // Assign sourceNode to each top-level declaration for sys.bundle.
            // Load the file as a YAML tree and zip entries with AST nodes.
            try {
                YAML::Node rawSeq = YAML::LoadFile(filename);
                if (rawSeq.IsSequence()) {
                    size_t n = std::min(rawSeq.size(), programAST->declarations.size());
                    for (size_t i = 0; i < n; ++i)
                        programAST->declarations[i]->sourceNode = rawSeq[i];
                }
            } catch (...) {}

            // Track current source file for log.* location injection (Spec 46)
            ctx.currentSourceFile = filename;

            AIML::AST::Compiler astCompiler(ctx);

            // Sibling import injection (sub-compiler only):
            // Two-phase: first register types from ALL siblings + self,
            // then register function signatures + class methods.
            if (!siblingImports.empty()) {
                bool prevHeaderOnly = ctx.headerOnlyMode;
                ctx.headerOnlyMode = true;

                // Collect parsed programs (reused in both phases)
                struct SibProg { std::string path; std::unique_ptr<AIML::AST::Program> prog; };
                std::vector<SibProg> sibProgs;
                for (const auto& sibPath : siblingImports) {
                    AIML::AST::Builder sb;
                    std::ifstream sf(sibPath);
                    if (!sf.is_open()) continue;
                    try {
                        logAimlFileSizeDiagnostic(sibPath, seenFileSizeDiagnostics.get());
                        logAimlDeclCountDiagnostic(sibPath);
                        YAML::Parser sp(sf);
                        sp.HandleNextDocument(sb);
                        auto p = sb.takeProgram();
                        if (p) sibProgs.push_back({sibPath, std::move(p)});
                    } catch (...) {}
                }

                // Phase A: register type definitions (class/model/enum) from
                // self + all siblings.  No function forward-declarations yet.
                // Uses fixed-point iteration: repeat until no new types are
                // registered in a full pass.  This handles cross-file model
                // inheritance (e.g. "model HandCardView inherits Card" where
                // Card lives in a sibling file).
                {
                    bool progress = true;
                    while (progress) {
                        size_t before = ctx.classes.size();
                        // Sibling types
                        for (auto& sp : sibProgs) {
                            AIML::AST::Compiler sc(ctx);
                            for (const auto& decl : sp.prog->declarations) {
                                if (dynamic_cast<AIML::AST::ClassDecl*>(decl.get()) ||
                                    dynamic_cast<AIML::AST::ModelDecl*>(decl.get()) ||
                                    dynamic_cast<AIML::AST::EnumDecl*>(decl.get()) ||
                                    dynamic_cast<AIML::AST::InterfaceDecl*>(decl.get()) ||
                                    dynamic_cast<AIML::AST::GroupMethodsDecl*>(decl.get()) ||
                                    dynamic_cast<AIML::AST::ImportDecl*>(decl.get()))
                                    sc.registerTypeDecl(decl.get());
                            }
                        }
                        // Self types
                        for (const auto& decl : programAST->declarations) {
                            if (dynamic_cast<AIML::AST::ClassDecl*>(decl.get()) ||
                                dynamic_cast<AIML::AST::ModelDecl*>(decl.get()) ||
                                dynamic_cast<AIML::AST::EnumDecl*>(decl.get()) ||
                                dynamic_cast<AIML::AST::InterfaceDecl*>(decl.get()) ||
                                dynamic_cast<AIML::AST::ImportDecl*>(decl.get()))
                                astCompiler.registerTypeDecl(decl.get());
                        }
                        progress = (ctx.classes.size() > before);
                    }
                }

                // Phase B: full registerTypes on self (now knows all types
                // from siblings, so function forward-declarations resolve).
                astCompiler.registerTypes(programAST.get());

                // Phase C: register function signatures + class methods from siblings.
                // Also register top-level DeclStmts as ExternalLinkage globals so
                // cross-file references (e.g. g_selected_deckset_id declared in
                // deckset_picker.aiml but used in board_screen.aiml) resolve correctly.
                //
                // Pre-scan: collect this file's own decl names so we skip same-name
                // globals from siblings (own definition takes precedence, avoids
                // linker conflicts for private variables with identical names).
                std::unordered_set<std::string> ownDeclVars;
                for (const auto& decl : programAST->declarations) {
                    if (auto d = dynamic_cast<AIML::AST::DeclStmt*>(decl.get()))
                        ownDeclVars.insert(d->varName);
                }

                for (auto& sp : sibProgs) {
                    AIML::AST::Compiler sc(ctx);
                    for (const auto& decl_node : sp.prog->declarations) {
                        if (auto func = dynamic_cast<AIML::AST::FunctionDecl*>(decl_node.get())) {
                            logAimlFunctionSizeDiagnostic(sp.path, func);
                            logAimlFunctionArgCountDiagnostic(sp.path, func);
                            sc.visitFunction(func);
                        } else if (auto cls = dynamic_cast<AIML::AST::ClassDecl*>(decl_node.get()))
                            sc.visitTopLevel(cls);
                        else if (auto mod = dynamic_cast<AIML::AST::ModelDecl*>(decl_node.get()))
                            sc.visitTopLevel(mod);
                        else if (auto d = dynamic_cast<AIML::AST::DeclStmt*>(decl_node.get())) {
                            // Skip: current file owns this name (its Pass-1 def takes precedence).
                            if (ownDeclVars.count(d->varName)) continue;
                            // Skip: already registered from an earlier sibling.
                            if (ctx.symbol_table.count(d->varName)) continue;
                            AIML::IRType* ty = ctx.ir_gen->resolveType(d->typeName);
                            if (!ty) continue;
                            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
                            // Match the name used in the sibling's Pass-1 definition.
                            std::string symName;
                            if (d->is_extern) {
                                symName = "__lib_" + d->varName;
                            } else {
                                std::string sibSuffix;
                                for (char c : sp.path) sibSuffix += (std::isalnum(c) ? c : '_');
                                symName = "__lib_" + sibSuffix + "_" + d->varName;
                            }
                            llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
                            if (!gv) {
                                gv = new llvm::GlobalVariable(
                                    ctx.module, llvmTy, false,
                                    llvm::GlobalValue::ExternalLinkage,
                                    nullptr,
                                    symName);
                            }
                            Symbol sym;
                            sym.ir_alloca_inst = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
                            sym.ir_type        = ctx.ir_gen->getPointerTy();
                            sym.ir_element_type = ty;
                            sym.typeName       = d->typeName;
                            sym.is_const       = d->is_const;
                            sym.is_volatile    = d->is_volatile;
                            ctx.symbol_table[d->varName] = sym;
                        }
                    }
                }

                // spec/58 §11: keep sibling ASTs alive for the rest of this
                // compile — ctx.pendingGroupMethods holds raw pointers into them
                // (from Phase A) that mergeGroupMethods folds into this file's
                // classgroup below in astCompiler.compile().
                for (auto& sp : sibProgs)
                    ctx.astKeepAlive.push_back(std::shared_ptr<void>(
                        sp.prog.release(),
                        [](void* p){ delete static_cast<AIML::AST::Program*>(p); }));

                ctx.headerOnlyMode = prevHeaderOnly;
            } else {
                // Normal (non-sub-compiler) path: just register types
                astCompiler.registerTypes(programAST.get());
            }

            for (const auto& decl : programAST->declarations) {
                if (auto func = dynamic_cast<AIML::AST::FunctionDecl*>(decl.get())) {
                    logAimlFunctionSizeDiagnostic(filename, func);
                    logAimlFunctionArgCountDiagnostic(filename, func);
                } else if (auto cls = dynamic_cast<AIML::AST::ClassDecl*>(decl.get())) {
                    for (const auto& method : cls->methods) {
                        logAimlFunctionSizeDiagnostic(filename, method.get());
                        logAimlFunctionArgCountDiagnostic(filename, method.get());
                    }
                }
            }

            // Pass 1.5: Compile deferred imports to .o files.
            // Now all types from ALL imports are registered in ctx, so
            // sub-compilers can resolve cross-import type references.
            this->siblingImports = *deferredImports;
            for (const auto& importPath : *deferredImports) {
                this->resolveImport(importPath);
            }
            // Propagate linked objects accumulated during deferred compilation.
            for (const auto& obj : this->linkedObjects) {
                if (std::find(ctx.objectFilesToLink.begin(), ctx.objectFilesToLink.end(), obj)
                    == ctx.objectFilesToLink.end())
                    ctx.objectFilesToLink.push_back(obj);
            }

            // Hard OOP rule: if a compile spans >= 3 AIML project files,
            // at least one real user class must exist somewhere in the project.
            // Builtins and models do not count: the point is to force behavior-
            // carrying object structure once a project grows beyond tiny size.
            if (!fileOptsOutOfStaticChecks(filename)) {
                size_t projectFileCount = 1 + this->importedSourceFiles.size();
                size_t userClassCount = 0;
                for (const auto& [name, ci] : ctx.classes) {
                    if (!ci.isBuiltin && !ci.isModel) ++userClassCount;
                }
                if (projectFileCount >= 3 && userClassCount == 0) {
                    LOG_ERROR(filename + ": project spans " + std::to_string(projectFileCount) +
                              " AIML files but defines no class. Projects with 3+ files must define at least one class.");
                }
            }

            // Additional rule for 3+ file projects: the entry file must
            // expose a `main` function AND that function must instantiate
            // (via `{ new: ClassName }`) a class declared in a DIFFERENT
            // project file.  Without this the entry point is unnecessarily
            // monolithic and the extra files are not actually wired in.
            {
                std::vector<std::string> imports(this->importedSourceFiles.begin(),
                                                 this->importedSourceFiles.end());
                logMainComposesExternalClassDiagnostic(filename, imports);
            }

            // Pass 2: Full Code Generation
            astCompiler.compile(programAST.get());

            // Tier-3 dead-prefab (spec/44_8): fold THIS module's prefab decls
            // and instantiation sites into the merged sets.  Sub-compilers have
            // already merged their (transitive) sets up via resolveImport, so at
            // the top level these unions are whole-build complete.
            for (const auto& kv : ctx.prefabDecls) allPrefabDecls.insert(kv.first);
            allPrefabUses.insert(ctx.prefabUses.begin(), ctx.prefabUses.end());

            // Unused-enumerator (spec/49): fold THIS module's declared/used enum
            // members + decl locations into the whole-build sets, per enum.
            for (const auto& kv : ctx.enumDeclMembers)
                allEnumDeclMembers[kv.first].insert(kv.second.begin(), kv.second.end());
            for (const auto& kv : ctx.enumUsedMembers)
                allEnumUsedMembers[kv.first].insert(kv.second.begin(), kv.second.end());
            for (const auto& kv : ctx.enumDeclLoc)
                allEnumDeclLoc.emplace(kv.first, kv.second);   // first decl wins

            // --prefab-report: fold THIS module's wiring facts in, then (at the
            // top-level entry) print the whole-build report and skip warnings —
            // the report supersedes the dead-prefab hint.
            if (reportPrefabsMode) {
                allPrefabReport.insert(allPrefabReport.end(),
                    ctx.prefabReportLines.begin(), ctx.prefabReportLines.end());
                if (!libraryMode)
                    printPrefabReport(allPrefabReport, filename, prefabReportFilter);
            }

            // Warn once, at the top-level entry, on prefabs that are declared
            // somewhere in the build but never instantiated.  Gate on a complete
            // use-set: a real object/exe build (noCache) compiles every import
            // fresh (no cache-hit skips), and a lone single file has no imports —
            // both give a complete picture.  A warm-cache multi-file JIT run can
            // skip imports, so we stay silent there to avoid false "dead" noise.
            if (!libraryMode && !reportPrefabsMode && (noCache || importedSourceFiles.empty())) {
                for (const auto& pname : allPrefabDecls) {
                    if (!allPrefabUses.count(pname)) {
                        LOG_WARNING("prefab '" + pname + "' is declared but never "
                            "instantiated in this build — no engine.instantiate(\"" +
                            pname + "\") anywhere. Remove the dead `- prefab:` "
                            "declaration or wire it up.");
                    }
                }

                // Unused-enumerator (spec/49): warn once, at the top-level entry,
                // on enum members declared somewhere in the build but never
                // referenced as `Enum.Member`.  Same complete-use-set gate as
                // dead-prefab.  V1 counts only explicit symbolic references (not
                // raw ints, persistence, or `enum:`-typed UI bindings), so the
                // decl-location prefix points at the enum block.
                for (const auto& kv : allEnumDeclMembers) {
                    const std::string& enumName = kv.first;
                    auto usedIt = allEnumUsedMembers.find(enumName);
                    for (const auto& member : kv.second) {
                        if (usedIt != allEnumUsedMembers.end() && usedIt->second.count(member))
                            continue;
                        auto locIt = allEnumDeclLoc.find(enumName);
                        std::string loc = (locIt != allEnumDeclLoc.end()) ? locIt->second : "";
                        LOG_WARNING(loc + "enum member '" + enumName + "." + member +
                            "' is never referenced in this build — remove the unused "
                            "enumerator or wire it up.");
                    }
                }
            }
        } else {
            LOG_ERROR(filename + ": AST Builder failed to produce a valid program.");
        }
    } catch (const YAML::Exception& e) {
        LOG_ERROR(filename + ": YAML Parsing Error during AST Build: " + std::string(e.what()));
    }
    
    // Propagate linked objects from pass 1
    this->linkedObjects = ctx.objectFilesToLink;
    
    // 5. (Legacy __aiml_init terminator removed)
    // 6. Output IR (Module Checkpoint)
    // Note: We do NOT generate 'main' here anymore. 
    // 'main' should be generated only if this is the entry point file.
    
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        LOG_ERROR(filename + ": Could not open file: " + EC.message());
        return;
    }
    module_->print(dest, nullptr);
    // Save State for JIT/Linker
    linkedObjects = ctx.objectFilesToLink;
    entryPointName = ctx.config.entryPointName;

    LOG_DEBUG("Compiled to " + outputFilename);
}

// ── compileFromString ─────────────────────────────────────────────────────────
// Like compile() but parses from an in-memory YAML string instead of a file.
// No IR file is written; the compiled module stays in module_ for jitRun().
void AIMLCompiler::compileFromString(const std::string& yaml_source) {
    LOG_DEBUG("compileFromString: " + std::to_string(yaml_source.size()) + " bytes");

    delete static_cast<CompilerContext*>(compiler_ctx_);
    compiler_ctx_ = nullptr;
    delete static_cast<llvm::IRBuilder<>*>(builder_inst_);
    builder_inst_ = nullptr;

    builder_inst_ = new llvm::IRBuilder<>(*context_);
    auto& builder  = *static_cast<llvm::IRBuilder<>*>(builder_inst_);

    compiler_ctx_ = new CompilerContext(*context_, *module_, builder);
    CompilerContext& ctx = *static_cast<CompilerContext*>(compiler_ctx_);

    ctx.compilerPath = compilerPath;
    ctx.ir_gen = AIML::createLLVMGenerator(ctx);
    ctx.currentSourceFile = "<memory>";

    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());

    try {
        YAML::Node rawSeq = YAML::Load(yaml_source);

        AIML::AST::Builder astBuilder;
        // Feed each node to the AST builder via a synthesised YAML stream
        std::istringstream ss(yaml_source);
        YAML::Parser parser(ss);
        parser.HandleNextDocument(astBuilder);

        auto programAST = astBuilder.takeProgram();
        if (programAST) {
            // Assign sourceNodes (zip raw sequence with AST declarations)
            if (rawSeq.IsSequence()) {
                size_t n = std::min(rawSeq.size(), programAST->declarations.size());
                for (size_t i = 0; i < n; ++i)
                    programAST->declarations[i]->sourceNode = rawSeq[i];
            }

            AIML::AST::Compiler astCompiler(ctx);
            astCompiler.registerTypes(programAST.get());
            astCompiler.compile(programAST.get());
        } else {
            LOG_ERROR("<memory>: compileFromString: AST builder produced no program");
        }
    } catch (const YAML::Exception& e) {
        LOG_ERROR("<memory>: compileFromString: YAML parse error: " + std::string(e.what()));
    }

    linkedObjects = ctx.objectFilesToLink;
    entryPointName = ctx.config.entryPointName;
}

int AIMLCompiler::compileAndRun(const std::string& yaml_source) {
    compileFromString(yaml_source);
    generateEntryPoint();
    return jitRun();
}

uint64_t AIMLCompiler::compileChunkAndGetAddress(const std::string& yaml_source,
                                                   const std::string& funcName) {
    // Compile the YAML (may contain class/model/def nodes — full dependency context).
    compileFromString(yaml_source);

    // Create a JIT engine without running any entry point.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    std::string errStr;
    module_->setTargetTriple(llvm::sys::getDefaultTargetTriple());

    llvm::EngineBuilder eb(std::move(module_));
    eb.setErrorStr(&errStr);
    eb.setEngineKind(llvm::EngineKind::JIT);

    llvm::ExecutionEngine* eng = eb.create();
    if (!eng) {
        LOG_ERROR("compileChunkAndGetAddress: engine creation failed: " + errStr);
        return 0;
    }
    eng->finalizeObject();

    // Try to look up the function by name.
    uint64_t addr = eng->getFunctionAddress(funcName);
    if (addr) {
        engine_ = eng;
        return addr;
    }

    // No callable function found — check if it's a class/model (sentinel 1 = success).
    CompilerContext* ctx = static_cast<CompilerContext*>(compiler_ctx_);
    if (ctx && ctx->classes.count(funcName)) {
        engine_ = eng;
        return 1;  // sentinel: class/model compiled successfully
    }

    LOG_WARNING("compileChunkAndGetAddress: '" + funcName + "' not found in compiled chunk");
    delete eng;
    return 0;
}

void AIMLCompiler::generateEntryPoint() {
    // This method generates the 'int main(int argc, char** argv)' entry point.
    // It should only be called if we are building an executable or running JIT.
    
    LOG_DEBUG("Generating Entry Point...");
    llvm::IRBuilder<> builder(*context_);
    
    // 1. Check for User-Defined 'main' (or custom entry point)
    llvm::Function* userMain = module_->getFunction(entryPointName);
    if (userMain) {
        LOG_DEBUG("User defined entry point found. Renaming to __aiml_user_" + entryPointName);
        userMain->setName("__aiml_user_" + entryPointName);
    }

    // 2. Create Standard Main: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = { builder.getInt32Ty(), builder.getInt8Ty()->getPointerTo()->getPointerTo() };
    llvm::FunctionType* funcType = llvm::FunctionType::get(builder.getInt32Ty(), mainArgs, false);
    llvm::Function* mainFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main", *module_);
    
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", mainFunc);
    builder.SetInsertPoint(entry);
    
    // 3. Setup Globals: __argc, __argv
    // ExternalLinkage damit andere Compilation-Units (z.B. core/cfg_loader.aiml,
    // die sys.get_arg aufrufen) auf dasselbe Symbol referenzieren koennen.
    module_->getOrInsertGlobal("__argc", builder.getInt32Ty());
    llvm::GlobalVariable* argcGlobal = module_->getNamedGlobal("__argc");
    argcGlobal->setLinkage(llvm::GlobalValue::ExternalLinkage);
    argcGlobal->setInitializer(builder.getInt32(0));

    module_->getOrInsertGlobal("__argv", builder.getInt8Ty()->getPointerTo()->getPointerTo());
    llvm::GlobalVariable* argvGlobal = module_->getNamedGlobal("__argv");
    argvGlobal->setLinkage(llvm::GlobalValue::ExternalLinkage);
    argvGlobal->setInitializer(llvm::ConstantPointerNull::get(builder.getInt8Ty()->getPointerTo()->getPointerTo()));

    // Store Arguments
    llvm::Function::arg_iterator args = mainFunc->arg_begin();
    llvm::Value* argcVal = args++;
    argcVal->setName("argc");
    llvm::Value* argvVal = args++;
    argvVal->setName("argv");
    
    builder.CreateStore(argcVal, argcGlobal);
    builder.CreateStore(argvVal, argvGlobal);

    // 3.5. Disable stdout buffering: setvbuf(stdout, NULL, _IONBF, 0)
    {
        llvm::Type* i8p = builder.getInt8Ty()->getPointerTo();
        llvm::GlobalVariable* stdoutGV = module_->getGlobalVariable("stdout");
        if (!stdoutGV) {
            stdoutGV = new llvm::GlobalVariable(
                *module_, i8p, false,
                llvm::GlobalValue::ExternalLinkage,
                nullptr, "stdout");
        }
        llvm::Value* stdoutVal = builder.CreateLoad(i8p, stdoutGV, "stdout_ptr");
        // size_t width follows the target's pointer width (i64 on x86_64, i32 on wasm32).
        llvm::Type* sizeTy = builder.getIntNTy(
            module_->getDataLayout().getPointerSizeInBits());
        llvm::FunctionType* setvbufTy = llvm::FunctionType::get(
            builder.getInt32Ty(),
            {i8p, i8p, builder.getInt32Ty(), sizeTy},
            false);
        llvm::FunctionCallee setvbufFn = module_->getOrInsertFunction("setvbuf", setvbufTy);
        builder.CreateCall(setvbufFn, {
            stdoutVal,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
            builder.getInt32(2),               // _IONBF
            llvm::ConstantInt::get(sizeTy, 0)  // size (ignored for _IONBF)
        });
    }

    // 3.9. Initialize AIML_TRACE.
    // If AIML_TRACE was set in the build environment (e.g. via exodus.json env section),
    // bake the value into the binary so the AOT binary traces without needing the env var at runtime.
    // Otherwise fall back to reading the env var at runtime (JIT default).
    {
        const char* baked_trace = getenv("AIML_TRACE");
        if (baked_trace && *baked_trace) {
            // Bake the compile-time AIML_TRACE value as a string literal into the binary.
            llvm::Value* bakedStr = builder.CreateGlobalStringPtr(baked_trace, "__aiml_trace_baked");
            std::vector<llvm::Type*> traceInitBakedArgs = {builder.getPtrTy()};
            llvm::FunctionType* ft = llvm::FunctionType::get(builder.getVoidTy(), traceInitBakedArgs, false);
            llvm::FunctionCallee traceInitFn = module_->getOrInsertFunction("aiml_trace_init_baked", ft);
            builder.CreateCall(traceInitFn, {bakedStr});
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
            llvm::FunctionCallee traceInitFn = module_->getOrInsertFunction("aiml_trace_init", ft);
            builder.CreateCall(traceInitFn);
        }
    }

    // 3.95. Initialize Asset Registry (spec/44_7).  The compile-time
    // AssetScanner state lives only in the compiler process; the wasm/aot
    // binary starts with an empty singleton.  std::filesystem walks of
    // emscripten's preloaded VFS are unreliable, so we emit one explicit
    // register call per known (kind, stem, path) entry.  Path is computed
    // cwd-relative (`assets/items/hammer.png`) which works as a relative
    // path in both native (cwd=project root) and WASM (cwd=/).
    {
        llvm::Type* i8p  = builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32t = builder.getInt32Ty();
        llvm::FunctionType* regFt = llvm::FunctionType::get(
            builder.getVoidTy(), {i32t, i8p, i8p}, false);
        llvm::FunctionCallee regFn = module_->getOrInsertFunction(
            "aiml_asset_runtime_register", regFt);
        std::error_code rec;
        std::filesystem::path cwd = std::filesystem::current_path(rec);
        auto kindIdx = [](AIML::AssetKind k) -> int {
            switch (k) {
                case AIML::AssetKind::Texture: return 0;
                case AIML::AssetKind::Audio:   return 1;
                case AIML::AssetKind::Shader:  return 2;
                case AIML::AssetKind::Font:    return 3;
                case AIML::AssetKind::Data:    return 4;
                default: return -1;
            }
        };
        for (const auto& [kind, store] : AIML::AssetScanner::instance().all_stores()) {
            int kidx = kindIdx(kind);
            if (kidx < 0) continue;
            for (const auto& [stem, entry] : store) {
                std::error_code rel_ec;
                std::filesystem::path rel = std::filesystem::relative(entry.abs_path, cwd, rel_ec);
                std::string path_str = rel_ec ? entry.abs_path : rel.string();
                llvm::Value* stemV = builder.CreateGlobalStringPtr(stem, "__a_stem");
                llvm::Value* pathV = builder.CreateGlobalStringPtr(path_str, "__a_path");
                builder.CreateCall(regFn, {builder.getInt32(kidx), stemV, pathV});
            }
        }
    }

    // 4. Call __aiml_init()
    llvm::Function* initFunc = module_->getFunction("__aiml_init");
    if (initFunc) {
        builder.CreateCall(initFunc);
    } else {
        LOG_WARNING("__aiml_init not found, skipping initialization.");
    }
    
    // 5. Call User Main (if exists)
    if (userMain) {
        // Check signature of user main
        // We support:
        // - void main()
        // - int main()
        // - int main(int argc, char** argv)
        // - void main(int argc, char** argv)
        
        // Actually, LLVM Function Type check is strict.
        llvm::FunctionType* userType = userMain->getFunctionType();
        size_t numParams = userType->getNumParams();
        
        std::vector<llvm::Value*> callArgs;
        if (numParams == 2) {
             callArgs.push_back(argcVal);
             callArgs.push_back(argvVal);
        }
        // If 0 params, callArgs empty.
        
        llvm::Value* result = builder.CreateCall(userMain, callArgs);
        
        // Return Result
        if (result->getType()->isIntegerTy()) {
            builder.CreateRet(result);
        } else {
             builder.CreateRet(builder.getInt32(0));
        }
        
    } else {
        // No user main, just return 0 (Script mode finished)
        builder.CreateRet(builder.getInt32(0));
    }
    
    LOG_DEBUG("Entry Point generated.");
}

void AIMLCompiler::setTargetTriple(const std::string& triple) {
    // Re-register all targets so non-native triples (e.g. wasm32) are available.
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    std::string err;
    auto* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        LOG_ERROR("setTargetTriple: " + err);
        return;
    }
    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    auto* tm = target->createTargetMachine(triple, "generic", "", opt, rm);
    module_->setDataLayout(tm->createDataLayout());
    module_->setTargetTriple(triple);
    targetTripleStr = triple;
    LOG_DEBUG("compiler target set to " + triple);
}

void AIMLCompiler::setAssetRoots(const std::string& commaList) {
    assetRootsStr = commaList;
}

void AIMLCompiler::saveModule(const std::string& outputFilename) {
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        LOG_ERROR("saveModule: could not open " + outputFilename + ": " + EC.message());
        return;
    }
    module_->print(dest, nullptr);
    LOG_DEBUG("Saved module IR to " + outputFilename);
}

void AIMLCompiler::compileToObject(const std::string& /*filename*/, const std::string& outputFilename) {
    // Reuse compile logic to generate IR in module_
    // TODO: Refactor compile() to separate IR generation from file output to avoid double work or side effects
    
    // IMPLEMENTATION:
    // 1. Initialize targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module_->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        LOG_ERROR("Target not found: " + error);
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    // Set Relocation Model to PIC (Position Independent Code)
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module_->setDataLayout(targetMachine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);

    if (EC) {
        LOG_ERROR("Could not open file: " + EC.message());
        return;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        LOG_ERROR("TargetMachine can't emit a file of this type");
        return;
    }

    // Verify module before running passes to catch null-type GEPs early.
    {
        bool anyFail = false;
        // Per-function verification to identify the failing function.
        for (auto& F : *module_) {
            std::string errMsg;
            llvm::raw_string_ostream errStream(errMsg);
            if (llvm::verifyFunction(F, &errStream)) {
                LOG_ERROR("Function '" + F.getName().str() + "' verification FAILED: " + errMsg);
                anyFail = true;
            }
        }
        std::string errMsg;
        llvm::raw_string_ostream errStream(errMsg);
        if (llvm::verifyModule(*module_, &errStream)) {
            LOG_ERROR("Module verification FAILED before compileToObject: " + errMsg);
            anyFail = true;
        }
        if (anyFail) {
            // Dump IR to a temp file for inspection.
            std::error_code ec;
            llvm::raw_fd_ostream irDump("/tmp/aiml_bad_module.ll", ec, llvm::sys::fs::OF_None);
            if (!ec) module_->print(irDump, nullptr);
            LOG_ERROR("IR dumped to /tmp/aiml_bad_module.ll");
            return; // Do not run passes on invalid IR — they would crash or corrupt output.
        }
    }

    pass.run(*module_);
    LOG_INFO("Object code written to " + outputFilename);
}

// ── Uncaught-exception message capture ───────────────────────────────────────
// AIML `throw` records its exception's `message` here right before __cxa_throw,
// so the top-level try/catch in jitRun can report it when nothing catches the
// exception (spec/22: unhandled → print message to stderr + sys.exit(2)).
// Without this the exception would unwind out of runFunction() and std::terminate
// the process (core dump, no diagnostic).
static thread_local std::string g_aiml_last_thrown_msg;
extern "C" void aiml_note_thrown_message(const char* msg) {
    g_aiml_last_thrown_msg = msg ? msg : "";
}

int AIMLCompiler::jitRun() {
    // EXTREMELY IMPORTANT: Load host process symbols so JIT can find malloc, printf, etc.
    LOG_DEBUG("LoadLibraryPermanently...");
    bool loadFailed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    if (loadFailed) {
        LOG_ERROR("LoadLibraryPermanently failed!");
        return 1;
    }

    // Pre-load user libraries declared in exodus.json linker.libraries so that
    // extern "C" symbols are resolvable in JIT mode (AOT lets the linker handle it).
    {
        std::string exodusPath = "exodus.json";
        std::ifstream ef(exodusPath);
        if (ef.good()) {
            try {
                YAML::Node ecfg = YAML::LoadFile(exodusPath);

                // Collect library_paths for fallback search
                std::vector<std::string> libPaths;
                if (ecfg["linker"] && ecfg["linker"]["library_paths"]) {
                    for (const auto& lp : ecfg["linker"]["library_paths"])
                        libPaths.push_back(lp.as<std::string>());
                }

                // Helper: try to load soName; if that fails, search libPaths for
                // soName or soName.N (versioned) and load the first match found.
                auto tryLoad = [&](const std::string& soName) {
                    std::string err;
                    if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(soName.c_str(), &err)) {
                        LOG_DEBUG("JIT: loaded " + soName);
                        return;
                    }
                    // Fallback: search library_paths
                    for (const auto& dir : libPaths) {
                        // Try exact name first
                        std::string full = dir + "/" + soName;
                        if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(full.c_str(), &err)) {
                            LOG_DEBUG("JIT: loaded " + full);
                            return;
                        }
                        // Try versioned names: libfoo.so.3, .2, .1
                        for (int v = 9; v >= 1; --v) {
                            std::string vfull = full + "." + std::to_string(v);
                            if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(vfull.c_str(), &err)) {
                                LOG_DEBUG("JIT: loaded " + vfull);
                                return;
                            }
                        }
                    }
                    LOG_DEBUG("JIT: could not load " + soName + ": " + err);
                };

                if (ecfg["linker"] && ecfg["linker"]["libraries"]) {
                    for (const auto& lib : ecfg["linker"]["libraries"]) {
                        std::string libName = lib.as<std::string>();
                        // Convert short name → full .so name: "SDL2" → "libSDL2.so"
                        tryLoad("lib" + libName + ".so");
                    }
                }
            } catch (...) {
                LOG_DEBUG("JIT: could not parse exodus.json for library preloading");
            }
        }
        // Also expose dlopen/dlsym so sys.load_lib/load_sym work at JIT time
        llvm::sys::DynamicLibrary::AddSymbol("dlopen",  (void*)(intptr_t)dlopen);
        llvm::sys::DynamicLibrary::AddSymbol("dlsym",   (void*)(intptr_t)dlsym);
    }
    
    // Explicitly add malloc/free
    llvm::sys::DynamicLibrary::AddSymbol("malloc", (void*)(intptr_t)malloc);
    llvm::sys::DynamicLibrary::AddSymbol("free", (void*)(intptr_t)free);
    llvm::sys::DynamicLibrary::AddSymbol("strlen", (void*)(intptr_t)strlen);
    llvm::sys::DynamicLibrary::AddSymbol("strcpy", (void*)(intptr_t)strcpy);
    llvm::sys::DynamicLibrary::AddSymbol("strcat", (void*)(intptr_t)strcat);
    llvm::sys::DynamicLibrary::AddSymbol("sprintf", (void*)(intptr_t)sprintf);
    llvm::sys::DynamicLibrary::AddSymbol("strcmp", (void*)(intptr_t)strcmp);
    llvm::sys::DynamicLibrary::AddSymbol("atoi",   (void*)(intptr_t)atoi);
    llvm::sys::DynamicLibrary::AddSymbol("fdopen", (void*)(intptr_t)fdopen);
    
    // Trace runtime symbols (AIML_TRACE)
    aiml_trace_init();  // read AIML_TRACE env var now, before any JIT code runs
    llvm::sys::DynamicLibrary::AddSymbol("aiml_trace_init_baked", (void*)(intptr_t)aiml_trace_init_baked);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_trace_active",     (void*)(intptr_t)aiml_trace_active);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_trace",            (void*)(intptr_t)aiml_trace);

    // sys.arg_dump runtime
    llvm::sys::DynamicLibrary::AddSymbol("aiml_arg_dump_impl", (void*)(intptr_t)aiml_arg_dump_impl);

    // binary io runtime
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_open_binary", (void*)(intptr_t)aiml_io_open_binary);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_close",       (void*)(intptr_t)aiml_io_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_tell",        (void*)(intptr_t)aiml_io_tell);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_seek",        (void*)(intptr_t)aiml_io_seek);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_skip",        (void*)(intptr_t)aiml_io_skip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_eof",         (void*)(intptr_t)aiml_io_eof);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_u8",     (void*)(intptr_t)aiml_io_read_u8);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_bool",   (void*)(intptr_t)aiml_io_read_bool);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_i32_le", (void*)(intptr_t)aiml_io_read_i32_le);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_u32_le", (void*)(intptr_t)aiml_io_read_u32_le);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_i64_le", (void*)(intptr_t)aiml_io_read_i64_le);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_f32_le", (void*)(intptr_t)aiml_io_read_f32_le);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_f64_le", (void*)(intptr_t)aiml_io_read_f64_le);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_cstring",(void*)(intptr_t)aiml_io_read_cstring);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_bytes",          (void*)(intptr_t)aiml_io_read_bytes);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_at",             (void*)(intptr_t)aiml_io_read_at);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_buf_free",            (void*)(intptr_t)aiml_io_buf_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_f32_array_fill", (void*)(intptr_t)aiml_io_read_f32_array_fill);

    // Runtime Library Symbols
    llvm::sys::DynamicLibrary::AddSymbol("time_now_ms", (void*)(intptr_t)time_now_ms);
    llvm::sys::DynamicLibrary::AddSymbol("time_sleep_ms", (void*)(intptr_t)time_sleep_ms);
    llvm::sys::DynamicLibrary::AddSymbol("sys_exe_runtime", (void*)(intptr_t)sys_exe_runtime);

    // Memory limit / usage
    llvm::sys::DynamicLibrary::AddSymbol("aiml_sys_mem_limit", (void*)(intptr_t)aiml_sys_mem_limit);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_sys_mem_usage", (void*)(intptr_t)aiml_sys_mem_usage);

    // Path runtime symbols
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_intern",      (void*)(intptr_t)aiml_path_intern);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_join",        (void*)(intptr_t)aiml_path_join);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_parent",      (void*)(intptr_t)aiml_path_parent);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_name",        (void*)(intptr_t)aiml_path_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_stem",        (void*)(intptr_t)aiml_path_stem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_extension",   (void*)(intptr_t)aiml_path_extension);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_root",        (void*)(intptr_t)aiml_path_root);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_absolute", (void*)(intptr_t)aiml_path_is_absolute);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_relative", (void*)(intptr_t)aiml_path_is_relative);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_is_empty",    (void*)(intptr_t)aiml_path_is_empty);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_depth",       (void*)(intptr_t)aiml_path_depth);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_relative_to", (void*)(intptr_t)aiml_path_relative_to);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_safe_join",   (void*)(intptr_t)aiml_path_safe_join);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_to_string",   (void*)(intptr_t)aiml_path_to_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_home",        (void*)(intptr_t)aiml_path_home);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_temp_dir",    (void*)(intptr_t)aiml_path_temp_dir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_cwd",         (void*)(intptr_t)aiml_path_cwd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_current_exe", (void*)(intptr_t)aiml_path_current_exe);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_path_config_dir",  (void*)(intptr_t)aiml_path_config_dir);
    // YAML document symbols
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_parse", (void*)(intptr_t)aiml_yaml_parse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_parse_file", (void*)(intptr_t)aiml_yaml_parse_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_error", (void*)(intptr_t)aiml_yaml_error);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_free", (void*)(intptr_t)aiml_yaml_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_root", (void*)(intptr_t)aiml_yaml_root);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_kind", (void*)(intptr_t)aiml_yaml_kind);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_size", (void*)(intptr_t)aiml_yaml_size);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_scalar", (void*)(intptr_t)aiml_yaml_scalar);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_line", (void*)(intptr_t)aiml_yaml_line);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_column", (void*)(intptr_t)aiml_yaml_column);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_dump", (void*)(intptr_t)aiml_yaml_dump);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_item", (void*)(intptr_t)aiml_yaml_item);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_key", (void*)(intptr_t)aiml_yaml_key);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_value", (void*)(intptr_t)aiml_yaml_value);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_yaml_get", (void*)(intptr_t)aiml_yaml_get);

    // IO / filesystem symbols
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_exists",     (void*)(intptr_t)aiml_io_exists);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_is_dir",     (void*)(intptr_t)aiml_io_is_dir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_is_file",    (void*)(intptr_t)aiml_io_is_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_file_size",  (void*)(intptr_t)aiml_io_file_size);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_mkdir",      (void*)(intptr_t)aiml_io_mkdir);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_mkdir_all",  (void*)(intptr_t)aiml_io_mkdir_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_remove",     (void*)(intptr_t)aiml_io_remove);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_remove_all", (void*)(intptr_t)aiml_io_remove_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_rename",     (void*)(intptr_t)aiml_io_rename);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_read_file",      (void*)(intptr_t)aiml_io_read_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_write_file",     (void*)(intptr_t)aiml_io_write_file);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_list_dir_fill",  (void*)(intptr_t)aiml_io_list_dir_fill);

    // String builtins (lower, trim, replace, split)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_lower",         (void*)(intptr_t)aiml_str_lower);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_trim",          (void*)(intptr_t)aiml_str_trim);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_replace",       (void*)(intptr_t)aiml_str_replace);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_split_fill",    (void*)(intptr_t)aiml_str_split_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_parse_i32",    (void*)(intptr_t)aiml_str_parse_i32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_str_parse_f32",    (void*)(intptr_t)aiml_str_parse_f32);

    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_read",       (void*)(intptr_t)aiml_io_can_read);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_write",      (void*)(intptr_t)aiml_io_can_write);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_io_can_exec",       (void*)(intptr_t)aiml_io_can_exec);

    // net namespace (POSIX sockets)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_socket",       (void*)(intptr_t)aiml_net_socket);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_connect",      (void*)(intptr_t)aiml_net_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_bind",         (void*)(intptr_t)aiml_net_bind);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_listen",       (void*)(intptr_t)aiml_net_listen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_accept",       (void*)(intptr_t)aiml_net_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_send",         (void*)(intptr_t)aiml_net_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_recv",         (void*)(intptr_t)aiml_net_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_send_string",  (void*)(intptr_t)aiml_net_send_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_recv_string",  (void*)(intptr_t)aiml_net_recv_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_close",        (void*)(intptr_t)aiml_net_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_fd",           (void*)(intptr_t)aiml_net_fd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_net_set_timeout",  (void*)(intptr_t)aiml_net_set_timeout);

    // tls namespace (OpenSSL TLS sockets)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_socket",          (void*)(intptr_t)aiml_tls_socket);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_connect",         (void*)(intptr_t)aiml_tls_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_bind",            (void*)(intptr_t)aiml_tls_bind);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_listen",          (void*)(intptr_t)aiml_tls_listen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_accept",          (void*)(intptr_t)aiml_tls_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_send",            (void*)(intptr_t)aiml_tls_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_recv",            (void*)(intptr_t)aiml_tls_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_send_string",     (void*)(intptr_t)aiml_tls_send_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_recv_string",     (void*)(intptr_t)aiml_tls_recv_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_close",           (void*)(intptr_t)aiml_tls_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_fd",              (void*)(intptr_t)aiml_tls_fd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_timeout",     (void*)(intptr_t)aiml_tls_set_timeout);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_cert",        (void*)(intptr_t)aiml_tls_set_cert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_ca",          (void*)(intptr_t)aiml_tls_set_ca);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_set_verify",      (void*)(intptr_t)aiml_tls_set_verify);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_peer_cert_cn",    (void*)(intptr_t)aiml_tls_peer_cert_cn);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tls_peer_cert_valid", (void*)(intptr_t)aiml_tls_peer_cert_valid);

    // ws/wss namespace (WebSocket — RFC 6455, from scratch)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_connect",      (void*)(intptr_t)aiml_ws_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_wss_connect",     (void*)(intptr_t)aiml_wss_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_server",       (void*)(intptr_t)aiml_ws_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_wss_server",      (void*)(intptr_t)aiml_wss_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_accept",       (void*)(intptr_t)aiml_ws_accept);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_send",         (void*)(intptr_t)aiml_ws_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_send_binary",  (void*)(intptr_t)aiml_ws_send_binary);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_recv",         (void*)(intptr_t)aiml_ws_recv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_ping",         (void*)(intptr_t)aiml_ws_ping);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_close",        (void*)(intptr_t)aiml_ws_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_fd",              (void*)(intptr_t)aiml_ws_fd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_poll",            (void*)(intptr_t)aiml_ws_poll);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ws_poll_listener",   (void*)(intptr_t)aiml_ws_poll_listener);

    // db namespace (AionDB)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open",          (void*)(intptr_t)aiml_db_store_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_insert",        (void*)(intptr_t)aiml_db_store_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_update",        (void*)(intptr_t)aiml_db_store_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get",           (void*)(intptr_t)aiml_db_store_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get_by_key",    (void*)(intptr_t)aiml_db_store_get_by_key);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_delete",        (void*)(intptr_t)aiml_db_store_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_len",           (void*)(intptr_t)aiml_db_store_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open_persist",  (void*)(intptr_t)aiml_db_store_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open_typed_persist",(void*)(intptr_t)aiml_db_store_open_typed_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_checkpoint",    (void*)(intptr_t)aiml_db_store_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_checkpoint_all",      (void*)(intptr_t)aiml_db_checkpoint_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_open_col",      (void*)(intptr_t)aiml_db_store_open_col);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open",             (void*)(intptr_t)aiml_db_kv_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open_persist",     (void*)(intptr_t)aiml_db_kv_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open_typed",        (void*)(intptr_t)aiml_db_kv_open_typed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_open_typed_persist",(void*)(intptr_t)aiml_db_kv_open_typed_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_checkpoint",       (void*)(intptr_t)aiml_db_kv_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_reload",           (void*)(intptr_t)aiml_db_kv_reload);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_set",              (void*)(intptr_t)aiml_db_kv_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_get",              (void*)(intptr_t)aiml_db_kv_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_get_copy",         (void*)(intptr_t)aiml_db_kv_get_copy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ser_deep_copy",          (void*)(intptr_t)aiml_ser_deep_copy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_has",              (void*)(intptr_t)aiml_db_kv_has);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_len",              (void*)(intptr_t)aiml_db_kv_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_delete",           (void*)(intptr_t)aiml_db_kv_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_clear",            (void*)(intptr_t)aiml_db_kv_clear);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_evict",           (void*)(intptr_t)aiml_db_kv_evict);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_set_path",           (void*)(intptr_t)aiml_db_set_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_get_path",           (void*)(intptr_t)aiml_db_get_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_set_blob",        (void*)(intptr_t)aiml_db_kv_set_blob);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_kv_get_blob",        (void*)(intptr_t)aiml_db_kv_get_blob);
    // Secondary index (db_indexed)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_open_for_kv",  (void*)(intptr_t)aiml_db_index_open_for_kv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_lookup",       (void*)(intptr_t)aiml_db_index_lookup);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_add",          (void*)(intptr_t)aiml_db_index_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_remove",       (void*)(intptr_t)aiml_db_index_remove);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_findings",     (void*)(intptr_t)aiml_db_index_findings);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_finding",      (void*)(intptr_t)aiml_db_index_finding);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_index_backfill",     (void*)(intptr_t)aiml_db_index_backfill);
    // Asset Registry (spec/22)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_get_texture",     (void*)(intptr_t)aiml_asset_get_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_get_sfx",         (void*)(intptr_t)aiml_asset_get_sfx);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_get_music",       (void*)(intptr_t)aiml_asset_get_music);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_read_data",       (void*)(intptr_t)aiml_asset_read_data);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_get_font",        (void*)(intptr_t)aiml_asset_get_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_init_runtime",    (void*)(intptr_t)aiml_asset_init_runtime);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_asset_runtime_register",(void*)(intptr_t)aiml_asset_runtime_register);
    // Iterator
    llvm::sys::DynamicLibrary::AddSymbol("aiml_iter_create_kv",        (void*)(intptr_t)aiml_iter_create_kv);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_iter_next",             (void*)(intptr_t)aiml_iter_next);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_iter_has_next",         (void*)(intptr_t)aiml_iter_has_next);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_iter_reset",            (void*)(intptr_t)aiml_iter_reset);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_iter_free",             (void*)(intptr_t)aiml_iter_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_open",            (void*)(intptr_t)aiml_db_vec_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_open_persist",   (void*)(intptr_t)aiml_db_vec_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_insert",          (void*)(intptr_t)aiml_db_vec_insert);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_search",          (void*)(intptr_t)aiml_db_vec_search);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_checkpoint",     (void*)(intptr_t)aiml_db_vec_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_vec_len",             (void*)(intptr_t)aiml_db_vec_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_open",             (void*)(intptr_t)aiml_db_ts_open);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_open_persist",     (void*)(intptr_t)aiml_db_ts_open_persist);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_checkpoint",       (void*)(intptr_t)aiml_db_ts_checkpoint);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_append",           (void*)(intptr_t)aiml_db_ts_append);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_at",               (void*)(intptr_t)aiml_db_ts_at);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_range",            (void*)(intptr_t)aiml_db_ts_range);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_ts_len",              (void*)(intptr_t)aiml_db_ts_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_total",         (void*)(intptr_t)aiml_db_store_total);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_is_active",     (void*)(intptr_t)aiml_db_store_is_active);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_get_ptr",       (void*)(intptr_t)aiml_db_store_get_ptr);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_db_store_query",         (void*)(intptr_t)aiml_db_store_query);

    // HTTP runtime
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_parse",              (void*)(intptr_t)aiml_url_parse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_to_string",          (void*)(intptr_t)aiml_url_to_string);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_is_https",           (void*)(intptr_t)aiml_url_is_https);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_protocol",           (void*)(intptr_t)aiml_url_protocol);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_host",               (void*)(intptr_t)aiml_url_host);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_path",               (void*)(intptr_t)aiml_url_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_fragment",           (void*)(intptr_t)aiml_url_fragment);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_port",               (void*)(intptr_t)aiml_url_port);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_get_param",          (void*)(intptr_t)aiml_url_get_param);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_credential",         (void*)(intptr_t)aiml_url_credential);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_cred_user",          (void*)(intptr_t)aiml_url_cred_user);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_url_cred_pass",          (void*)(intptr_t)aiml_url_cred_pass);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_new",           (void*)(intptr_t)aiml_http_map_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_put",           (void*)(intptr_t)aiml_http_map_put);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_get",           (void*)(intptr_t)aiml_http_map_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_len",           (void*)(intptr_t)aiml_http_map_len);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_map_free",          (void*)(intptr_t)aiml_http_map_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_get",               (void*)(intptr_t)aiml_http_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_post",              (void*)(intptr_t)aiml_http_post);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_put",               (void*)(intptr_t)aiml_http_put);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_delete",            (void*)(intptr_t)aiml_http_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_head",              (void*)(intptr_t)aiml_http_head);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_new_request",       (void*)(intptr_t)aiml_http_new_request);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_new",        (void*)(intptr_t)aiml_http_client_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_send",       (void*)(intptr_t)aiml_http_client_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_status",       (void*)(intptr_t)aiml_http_resp_status);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_ok",           (void*)(intptr_t)aiml_http_resp_ok);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_headers",      (void*)(intptr_t)aiml_http_resp_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_body",         (void*)(intptr_t)aiml_http_resp_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_free",         (void*)(intptr_t)aiml_http_resp_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_send",         (void*)(intptr_t)aiml_http_resp_send);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_write",        (void*)(intptr_t)aiml_http_resp_write);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_write_bytes",  (void*)(intptr_t)aiml_http_resp_write_bytes);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_flush",        (void*)(intptr_t)aiml_http_resp_flush);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_close",        (void*)(intptr_t)aiml_http_resp_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_status",   (void*)(intptr_t)aiml_http_resp_set_status);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_body",     (void*)(intptr_t)aiml_http_resp_set_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_set_body_bytes",(void*)(intptr_t)aiml_http_resp_set_body_bytes);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_resp_get_headers",  (void*)(intptr_t)aiml_http_resp_get_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_method",        (void*)(intptr_t)aiml_http_req_method);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_path",          (void*)(intptr_t)aiml_http_req_path);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_body",          (void*)(intptr_t)aiml_http_req_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_url",           (void*)(intptr_t)aiml_http_req_url);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_params",        (void*)(intptr_t)aiml_http_req_params);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_cookies",       (void*)(intptr_t)aiml_http_req_cookies);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_header",        (void*)(intptr_t)aiml_http_req_header);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_req_cookie",        (void*)(intptr_t)aiml_http_req_cookie);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_body",   (void*)(intptr_t)aiml_http_client_set_body);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_timeout",(void*)(intptr_t)aiml_http_client_set_timeout);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_set_stream", (void*)(intptr_t)aiml_http_client_set_stream);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_client_get_headers",(void*)(intptr_t)aiml_http_client_get_headers);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server",            (void*)(intptr_t)aiml_http_server);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_tls",        (void*)(intptr_t)aiml_http_server_tls);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_route",      (void*)(intptr_t)aiml_http_server_route);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_use",        (void*)(intptr_t)aiml_http_server_use);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_group",      (void*)(intptr_t)aiml_http_server_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_set_header", (void*)(intptr_t)aiml_http_server_set_header);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_start",      (void*)(intptr_t)aiml_http_server_start);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_start_async",(void*)(intptr_t)aiml_http_server_start_async);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_stop",       (void*)(intptr_t)aiml_http_server_stop);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_server_filesystem", (void*)(intptr_t)aiml_http_server_filesystem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_route",       (void*)(intptr_t)aiml_http_route_route);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_use",         (void*)(intptr_t)aiml_http_route_use);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_group",       (void*)(intptr_t)aiml_http_route_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_http_route_filesystem",  (void*)(intptr_t)aiml_http_route_filesystem);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_new",         (void*)(intptr_t)aiml_fs_options_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_index",   (void*)(intptr_t)aiml_fs_options_set_index);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_spa",     (void*)(intptr_t)aiml_fs_options_set_spa);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_fs_options_set_cache_max_age",(void*)(intptr_t)aiml_fs_options_set_cache_max_age);

    // llvm::sys::DynamicLibrary::AddSymbol("stderr", (void*)(intptr_t)stderr); // Removed to avoid macro issues for now

    // ── tensor namespace (tensor_runtime.h) ─────────────────────────────
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_arena_init",       (void*)(intptr_t)aiml_tensor_arena_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_arena_free",       (void*)(intptr_t)aiml_tensor_arena_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_kv_evict",        (void*)(intptr_t)aiml_tensor_kv_evict);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_zeros",            (void*)(intptr_t)aiml_tensor_zeros);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_ones",             (void*)(intptr_t)aiml_tensor_ones);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_randn",            (void*)(intptr_t)aiml_tensor_randn);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_from_data",        (void*)(intptr_t)aiml_tensor_from_data);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_clone",            (void*)(intptr_t)aiml_tensor_clone);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_free",             (void*)(intptr_t)aiml_tensor_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_get_flat",         (void*)(intptr_t)aiml_tensor_get_flat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_set_flat",         (void*)(intptr_t)aiml_tensor_set_flat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_item",             (void*)(intptr_t)aiml_tensor_item);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_shape",            (void*)(intptr_t)aiml_tensor_shape);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_ndim",             (void*)(intptr_t)aiml_tensor_ndim);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_size",             (void*)(intptr_t)aiml_tensor_size);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_data_ptr",         (void*)(intptr_t)aiml_tensor_data_ptr);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_reshape",          (void*)(intptr_t)aiml_tensor_reshape);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_transpose",        (void*)(intptr_t)aiml_tensor_transpose);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_slice",            (void*)(intptr_t)aiml_tensor_slice);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_add",              (void*)(intptr_t)aiml_tensor_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_sub",              (void*)(intptr_t)aiml_tensor_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_mul",              (void*)(intptr_t)aiml_tensor_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_mul_scalar",       (void*)(intptr_t)aiml_tensor_mul_scalar);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_neg",              (void*)(intptr_t)aiml_tensor_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_matmul",           (void*)(intptr_t)aiml_tensor_matmul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_sum",              (void*)(intptr_t)aiml_tensor_sum);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_mean",             (void*)(intptr_t)aiml_tensor_mean);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_exp",              (void*)(intptr_t)aiml_tensor_exp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_log_op",           (void*)(intptr_t)aiml_tensor_log_op);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_tanh_op",          (void*)(intptr_t)aiml_tensor_tanh_op);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_relu",             (void*)(intptr_t)aiml_tensor_relu);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_gelu",             (void*)(intptr_t)aiml_tensor_gelu);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_softmax",          (void*)(intptr_t)aiml_tensor_softmax);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_layer_norm",       (void*)(intptr_t)aiml_tensor_layer_norm);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_embedding_lookup", (void*)(intptr_t)aiml_tensor_embedding_lookup);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_cross_entropy",    (void*)(intptr_t)aiml_tensor_cross_entropy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_weighted_cross_entropy", (void*)(intptr_t)aiml_tensor_weighted_cross_entropy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_fma_inplace",      (void*)(intptr_t)aiml_tensor_fma_inplace);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_fma_sq_inplace",  (void*)(intptr_t)aiml_tensor_fma_sq_inplace);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_fma_abs_inplace", (void*)(intptr_t)aiml_tensor_fma_abs_inplace);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_abs",             (void*)(intptr_t)aiml_tensor_abs);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_sign",            (void*)(intptr_t)aiml_tensor_sign);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_step",            (void*)(intptr_t)aiml_tensor_step);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_argmax_scalar",    (void*)(intptr_t)aiml_tensor_argmax_scalar);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_argmax",           (void*)(intptr_t)aiml_tensor_argmax);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_set_requires_grad",(void*)(intptr_t)aiml_tensor_set_requires_grad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_requires_grad",    (void*)(intptr_t)aiml_tensor_requires_grad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_grad",             (void*)(intptr_t)aiml_tensor_grad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_zero_grad",        (void*)(intptr_t)aiml_tensor_zero_grad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_begin_graph",      (void*)(intptr_t)aiml_tensor_begin_graph);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_backward",         (void*)(intptr_t)aiml_tensor_backward);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_thread_cleanup",   (void*)(intptr_t)aiml_tensor_thread_cleanup);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_no_grad_begin",    (void*)(intptr_t)aiml_tensor_no_grad_begin);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_no_grad_end",      (void*)(intptr_t)aiml_tensor_no_grad_end);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_adam_step",        (void*)(intptr_t)aiml_tensor_adam_step);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_print",            (void*)(intptr_t)aiml_tensor_print);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_fill",             (void*)(intptr_t)aiml_tensor_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_sha256",           (void*)(intptr_t)aiml_tensor_sha256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_concat",           (void*)(intptr_t)aiml_tensor_concat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_causal_mask",      (void*)(intptr_t)aiml_tensor_causal_mask);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_save",              (void*)(intptr_t)aiml_tensor_save);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_tensor_load",              (void*)(intptr_t)aiml_tensor_load);

    // ── math namespace C runtime ─────────────────────────────────────────
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_add",       (void*)(intptr_t)aiml_math_vec2_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_sub",       (void*)(intptr_t)aiml_math_vec2_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_mul",       (void*)(intptr_t)aiml_math_vec2_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_scale",     (void*)(intptr_t)aiml_math_vec2_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_div",       (void*)(intptr_t)aiml_math_vec2_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_neg",       (void*)(intptr_t)aiml_math_vec2_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_dot",       (void*)(intptr_t)aiml_math_vec2_dot);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_length",    (void*)(intptr_t)aiml_math_vec2_length);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_length_sq", (void*)(intptr_t)aiml_math_vec2_length_sq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_normalize", (void*)(intptr_t)aiml_math_vec2_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_lerp",      (void*)(intptr_t)aiml_math_vec2_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_distance",  (void*)(intptr_t)aiml_math_vec2_distance);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_angle",     (void*)(intptr_t)aiml_math_vec2_angle);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_perp",      (void*)(intptr_t)aiml_math_vec2_perp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_eq",        (void*)(intptr_t)aiml_math_vec2_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_add",       (void*)(intptr_t)aiml_math_vec3_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_sub",       (void*)(intptr_t)aiml_math_vec3_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_mul",       (void*)(intptr_t)aiml_math_vec3_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_scale",     (void*)(intptr_t)aiml_math_vec3_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_div",       (void*)(intptr_t)aiml_math_vec3_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_neg",       (void*)(intptr_t)aiml_math_vec3_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_dot",       (void*)(intptr_t)aiml_math_vec3_dot);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_cross",     (void*)(intptr_t)aiml_math_vec3_cross);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_length",    (void*)(intptr_t)aiml_math_vec3_length);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_length_sq", (void*)(intptr_t)aiml_math_vec3_length_sq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_normalize", (void*)(intptr_t)aiml_math_vec3_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_lerp",      (void*)(intptr_t)aiml_math_vec3_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_distance",  (void*)(intptr_t)aiml_math_vec3_distance);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_reflect",   (void*)(intptr_t)aiml_math_vec3_reflect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_refract",   (void*)(intptr_t)aiml_math_vec3_refract);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec3_eq",        (void*)(intptr_t)aiml_math_vec3_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_add",       (void*)(intptr_t)aiml_math_vec4_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_sub",       (void*)(intptr_t)aiml_math_vec4_sub);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_mul",       (void*)(intptr_t)aiml_math_vec4_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_scale",     (void*)(intptr_t)aiml_math_vec4_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_div",       (void*)(intptr_t)aiml_math_vec4_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_neg",       (void*)(intptr_t)aiml_math_vec4_neg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_lerp",      (void*)(intptr_t)aiml_math_vec4_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec4_eq",        (void*)(intptr_t)aiml_math_vec4_eq);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_identity",     (void*)(intptr_t)aiml_math_quat_identity);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_axis_angle",   (void*)(intptr_t)aiml_math_quat_axis_angle);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_euler",        (void*)(intptr_t)aiml_math_quat_euler);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_mul",          (void*)(intptr_t)aiml_math_quat_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_normalize",    (void*)(intptr_t)aiml_math_quat_normalize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_inverse",      (void*)(intptr_t)aiml_math_quat_inverse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_rotate_vec3",  (void*)(intptr_t)aiml_math_quat_rotate_vec3);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_lerp",         (void*)(intptr_t)aiml_math_quat_lerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_slerp",        (void*)(intptr_t)aiml_math_quat_slerp);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_quat_to_mat4",      (void*)(intptr_t)aiml_math_quat_to_mat4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_to_quat",      (void*)(intptr_t)aiml_math_mat4_to_quat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_identity",     (void*)(intptr_t)aiml_math_mat4_identity);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_mul",          (void*)(intptr_t)aiml_math_mat4_mul);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_mul_vec4",     (void*)(intptr_t)aiml_math_mat4_mul_vec4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_translate",    (void*)(intptr_t)aiml_math_mat4_translate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate",       (void*)(intptr_t)aiml_math_mat4_rotate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_x",     (void*)(intptr_t)aiml_math_mat4_rotate_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_y",     (void*)(intptr_t)aiml_math_mat4_rotate_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_rotate_z",     (void*)(intptr_t)aiml_math_mat4_rotate_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_scale",        (void*)(intptr_t)aiml_math_mat4_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_perspective",  (void*)(intptr_t)aiml_math_mat4_perspective);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_ortho",        (void*)(intptr_t)aiml_math_mat4_ortho);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_lookat",       (void*)(intptr_t)aiml_math_mat4_lookat);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_inverse",      (void*)(intptr_t)aiml_math_mat4_inverse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_transpose",    (void*)(intptr_t)aiml_math_mat4_transpose);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_to_buffer",    (void*)(intptr_t)aiml_math_mat4_to_buffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_get",          (void*)(intptr_t)aiml_math_mat4_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_mat4_set",          (void*)(intptr_t)aiml_math_mat4_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_add",         (void*)(intptr_t)aiml_math_ivec2_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_div",         (void*)(intptr_t)aiml_math_ivec2_div);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec2_to_vec2",     (void*)(intptr_t)aiml_math_ivec2_to_vec2);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_vec2_to_ivec2",     (void*)(intptr_t)aiml_math_vec2_to_ivec2);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_ivec3_add",         (void*)(intptr_t)aiml_math_ivec3_add);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_fmin_list",         (void*)(intptr_t)aiml_math_fmin_list);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_fmax_list",         (void*)(intptr_t)aiml_math_fmax_list);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_random_f32",        (void*)(intptr_t)aiml_math_random_f32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_math_random_int",        (void*)(intptr_t)aiml_math_random_int);
    // hash namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha256",         (void*)(intptr_t)aiml_hash_sha256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha512",         (void*)(intptr_t)aiml_hash_sha512);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha3_256",       (void*)(intptr_t)aiml_hash_sha3_256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_keccak256",      (void*)(intptr_t)aiml_hash_keccak256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_ripemd160",      (void*)(intptr_t)aiml_hash_ripemd160);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2b",        (void*)(intptr_t)aiml_hash_blake2b);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2s",        (void*)(intptr_t)aiml_hash_blake2s);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_md5",            (void*)(intptr_t)aiml_hash_md5);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_crc32",          (void*)(intptr_t)aiml_hash_crc32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha256",    (void*)(intptr_t)aiml_hash_hmac_sha256);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha512",    (void*)(intptr_t)aiml_hash_hmac_sha512);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha256_raw",     (void*)(intptr_t)aiml_hash_sha256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha512_raw",     (void*)(intptr_t)aiml_hash_sha512_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_sha3_256_raw",   (void*)(intptr_t)aiml_hash_sha3_256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_keccak256_raw",  (void*)(intptr_t)aiml_hash_keccak256_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_ripemd160_raw",  (void*)(intptr_t)aiml_hash_ripemd160_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_blake2b_raw",    (void*)(intptr_t)aiml_hash_blake2b_raw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_hash_hmac_sha256_raw",(void*)(intptr_t)aiml_hash_hmac_sha256_raw);
    // compress namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_gzip",                (void*)(intptr_t)aiml_compress_gzip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_gunzip",               (void*)(intptr_t)aiml_compress_gunzip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_gunzip_str",           (void*)(intptr_t)aiml_compress_gunzip_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_deflate",              (void*)(intptr_t)aiml_compress_deflate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_inflate",              (void*)(intptr_t)aiml_compress_inflate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_inflate_str",          (void*)(intptr_t)aiml_compress_inflate_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zlib",                 (void*)(intptr_t)aiml_compress_zlib);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zlib_decompress",      (void*)(intptr_t)aiml_compress_zlib_decompress);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zlib_decompress_str",  (void*)(intptr_t)aiml_compress_zlib_decompress_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_brotli",               (void*)(intptr_t)aiml_compress_brotli);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_brotli_decompress",    (void*)(intptr_t)aiml_compress_brotli_decompress);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_brotli_decompress_str",(void*)(intptr_t)aiml_compress_brotli_decompress_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zstd",                 (void*)(intptr_t)aiml_compress_zstd);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zstd_decompress",      (void*)(intptr_t)aiml_compress_zstd_decompress);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zstd_decompress_str",  (void*)(intptr_t)aiml_compress_zstd_decompress_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_lz4",                  (void*)(intptr_t)aiml_compress_lz4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_lz4_decompress",       (void*)(intptr_t)aiml_compress_lz4_decompress);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_lz4_decompress_str",   (void*)(intptr_t)aiml_compress_lz4_decompress_str);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_tar_gz",               (void*)(intptr_t)aiml_compress_tar_gz);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_untar_gz",             (void*)(intptr_t)aiml_compress_untar_gz);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zip",                  (void*)(intptr_t)aiml_compress_zip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_unzip",                (void*)(intptr_t)aiml_compress_unzip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_zip_entries_fill",     (void*)(intptr_t)aiml_compress_zip_entries_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_compress_tar_entries_fill",     (void*)(intptr_t)aiml_compress_tar_entries_fill);
    // ai (Spec 51 — native ML acceleration)
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_embedding_lookup",       (void*)(intptr_t)aiml_ai_embedding_lookup);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_embedding_update",       (void*)(intptr_t)aiml_ai_embedding_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_dot",                    (void*)(intptr_t)aiml_ai_dot);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_bpe_train_merges",          (void*)(intptr_t)aiml_bpe_train_merges);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_bpe_merge_result_free",     (void*)(intptr_t)aiml_bpe_merge_result_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_encode_dense",           (void*)(intptr_t)aiml_ai_encode_dense);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_encode_dense_weighted",  (void*)(intptr_t)aiml_ai_encode_dense_weighted);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_triplet_loss",           (void*)(intptr_t)aiml_ai_triplet_loss);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_triplet_grad",           (void*)(intptr_t)aiml_ai_triplet_grad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ai_sample_logits",          (void*)(intptr_t)aiml_ai_sample_logits);
    // gltf
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_load",                         (void*)(intptr_t)aiml_gltf_load);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_valid",                  (void*)(intptr_t)aiml_gltf_scene_valid);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_error",                  (void*)(intptr_t)aiml_gltf_scene_error);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_free",                   (void*)(intptr_t)aiml_gltf_scene_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_nodes_fill",             (void*)(intptr_t)aiml_gltf_scene_nodes_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_meshes_fill",            (void*)(intptr_t)aiml_gltf_scene_meshes_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_materials_fill",         (void*)(intptr_t)aiml_gltf_scene_materials_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_animations_fill",        (void*)(intptr_t)aiml_gltf_scene_animations_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_skins_fill",             (void*)(intptr_t)aiml_gltf_scene_skins_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_scene_images_fill",            (void*)(intptr_t)aiml_gltf_scene_images_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_name",                    (void*)(intptr_t)aiml_gltf_node_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_translation_x",           (void*)(intptr_t)aiml_gltf_node_translation_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_translation_y",           (void*)(intptr_t)aiml_gltf_node_translation_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_translation_z",           (void*)(intptr_t)aiml_gltf_node_translation_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_rotation_x",              (void*)(intptr_t)aiml_gltf_node_rotation_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_rotation_y",              (void*)(intptr_t)aiml_gltf_node_rotation_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_rotation_z",              (void*)(intptr_t)aiml_gltf_node_rotation_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_rotation_w",              (void*)(intptr_t)aiml_gltf_node_rotation_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_scale_x",                 (void*)(intptr_t)aiml_gltf_node_scale_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_scale_y",                 (void*)(intptr_t)aiml_gltf_node_scale_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_scale_z",                 (void*)(intptr_t)aiml_gltf_node_scale_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_mesh",                    (void*)(intptr_t)aiml_gltf_node_mesh);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_skin",                    (void*)(intptr_t)aiml_gltf_node_skin);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_children_fill",           (void*)(intptr_t)aiml_gltf_node_children_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_node_world_matrix",            (void*)(intptr_t)aiml_gltf_node_world_matrix);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mesh_name",                    (void*)(intptr_t)aiml_gltf_mesh_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mesh_primitive_count",         (void*)(intptr_t)aiml_gltf_mesh_primitive_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mesh_primitive",               (void*)(intptr_t)aiml_gltf_mesh_primitive);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_positions",               (void*)(intptr_t)aiml_gltf_prim_positions);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_normals",                 (void*)(intptr_t)aiml_gltf_prim_normals);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_tangents",                (void*)(intptr_t)aiml_gltf_prim_tangents);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_uvs",                     (void*)(intptr_t)aiml_gltf_prim_uvs);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_colors",                  (void*)(intptr_t)aiml_gltf_prim_colors);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_joints",                  (void*)(intptr_t)aiml_gltf_prim_joints);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_weights",                 (void*)(intptr_t)aiml_gltf_prim_weights);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_indices",                 (void*)(intptr_t)aiml_gltf_prim_indices);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_vertex_count",            (void*)(intptr_t)aiml_gltf_prim_vertex_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_index_count",             (void*)(intptr_t)aiml_gltf_prim_index_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_material",                (void*)(intptr_t)aiml_gltf_prim_material);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_name",                     (void*)(intptr_t)aiml_gltf_mat_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_base_color_r",             (void*)(intptr_t)aiml_gltf_mat_base_color_r);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_base_color_g",             (void*)(intptr_t)aiml_gltf_mat_base_color_g);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_base_color_b",             (void*)(intptr_t)aiml_gltf_mat_base_color_b);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_base_color_a",             (void*)(intptr_t)aiml_gltf_mat_base_color_a);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_metallic",                 (void*)(intptr_t)aiml_gltf_mat_metallic);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_roughness",                (void*)(intptr_t)aiml_gltf_mat_roughness);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_emissive_r",               (void*)(intptr_t)aiml_gltf_mat_emissive_r);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_emissive_g",               (void*)(intptr_t)aiml_gltf_mat_emissive_g);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_emissive_b",               (void*)(intptr_t)aiml_gltf_mat_emissive_b);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_base_color_texture",       (void*)(intptr_t)aiml_gltf_mat_base_color_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_normal_texture",           (void*)(intptr_t)aiml_gltf_mat_normal_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_metallic_roughness_texture",(void*)(intptr_t)aiml_gltf_mat_metallic_roughness_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_emissive_texture",         (void*)(intptr_t)aiml_gltf_mat_emissive_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_occlusion_texture",        (void*)(intptr_t)aiml_gltf_mat_occlusion_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_alpha_mode",               (void*)(intptr_t)aiml_gltf_mat_alpha_mode);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_alpha_cutoff",             (void*)(intptr_t)aiml_gltf_mat_alpha_cutoff);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_mat_double_sided",             (void*)(intptr_t)aiml_gltf_mat_double_sided);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_tex_image",                    (void*)(intptr_t)aiml_gltf_tex_image);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_tex_wrap_s",                   (void*)(intptr_t)aiml_gltf_tex_wrap_s);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_tex_wrap_t",                   (void*)(intptr_t)aiml_gltf_tex_wrap_t);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_tex_min_filter",               (void*)(intptr_t)aiml_gltf_tex_min_filter);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_tex_mag_filter",               (void*)(intptr_t)aiml_gltf_tex_mag_filter);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_img_pixels_rgba",              (void*)(intptr_t)aiml_gltf_img_pixels_rgba);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_img_width",                    (void*)(intptr_t)aiml_gltf_img_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_img_height",                   (void*)(intptr_t)aiml_gltf_img_height);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_img_to_gl_texture",            (void*)(intptr_t)aiml_gltf_img_to_gl_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_name",                    (void*)(intptr_t)aiml_gltf_anim_name);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_duration",                (void*)(intptr_t)aiml_gltf_anim_duration);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_translation_x",    (void*)(intptr_t)aiml_gltf_anim_sample_translation_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_translation_y",    (void*)(intptr_t)aiml_gltf_anim_sample_translation_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_translation_z",    (void*)(intptr_t)aiml_gltf_anim_sample_translation_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_rotation_x",       (void*)(intptr_t)aiml_gltf_anim_sample_rotation_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_rotation_y",       (void*)(intptr_t)aiml_gltf_anim_sample_rotation_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_rotation_z",       (void*)(intptr_t)aiml_gltf_anim_sample_rotation_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_anim_sample_rotation_w",       (void*)(intptr_t)aiml_gltf_anim_sample_rotation_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_skin_joint_count",             (void*)(intptr_t)aiml_gltf_skin_joint_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_skin_inverse_bind_matrices",   (void*)(intptr_t)aiml_gltf_skin_inverse_bind_matrices);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_skin_joints_fill",             (void*)(intptr_t)aiml_gltf_skin_joints_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_img_to_gl_texture",            (void*)(intptr_t)aiml_gltf_img_to_gl_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_to_gl_mesh",              (void*)(intptr_t)aiml_gltf_prim_to_gl_mesh);
    // gl
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_init",                           (void*)(intptr_t)aiml_gl_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_viewport",                       (void*)(intptr_t)aiml_gl_viewport);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_clear",                          (void*)(intptr_t)aiml_gl_clear);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_clear_depth",                    (void*)(intptr_t)aiml_gl_clear_depth);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_clear_all",                      (void*)(intptr_t)aiml_gl_clear_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_enable_depth_test",              (void*)(intptr_t)aiml_gl_enable_depth_test);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_disable_depth_test",             (void*)(intptr_t)aiml_gl_disable_depth_test);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_enable_blend",                   (void*)(intptr_t)aiml_gl_enable_blend);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_disable_blend",                  (void*)(intptr_t)aiml_gl_disable_blend);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_enable_cull_face",               (void*)(intptr_t)aiml_gl_enable_cull_face);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_cull_back",                      (void*)(intptr_t)aiml_gl_cull_back);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_cull_front",                     (void*)(intptr_t)aiml_gl_cull_front);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_wireframe",                      (void*)(intptr_t)aiml_gl_wireframe);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_polygon_offset",                 (void*)(intptr_t)aiml_gl_polygon_offset);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_enable_polygon_offset_fill",     (void*)(intptr_t)aiml_gl_enable_polygon_offset_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_disable_polygon_offset_fill",    (void*)(intptr_t)aiml_gl_disable_polygon_offset_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_buffer",                  (void*)(intptr_t)aiml_gl_create_buffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_buffer_dynamic",          (void*)(intptr_t)aiml_gl_create_buffer_dynamic);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_index_buffer",            (void*)(intptr_t)aiml_gl_create_index_buffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_buffer_update",                  (void*)(intptr_t)aiml_gl_buffer_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_buffer_delete",                  (void*)(intptr_t)aiml_gl_buffer_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_vao",                     (void*)(intptr_t)aiml_gl_create_vao);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_vao_bind_vbo",                   (void*)(intptr_t)aiml_gl_vao_bind_vbo);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_vao_bind_ibo",                   (void*)(intptr_t)aiml_gl_vao_bind_ibo);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_vao_attrib",                     (void*)(intptr_t)aiml_gl_vao_attrib);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_vao_attrib_i",                   (void*)(intptr_t)aiml_gl_vao_attrib_i);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_vao_delete",                     (void*)(intptr_t)aiml_gl_vao_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_compile_shader",                 (void*)(intptr_t)aiml_gl_compile_shader);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_load_shader",                    (void*)(intptr_t)aiml_gl_load_shader);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_use",                     (void*)(intptr_t)aiml_gl_shader_use);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_f32",             (void*)(intptr_t)aiml_gl_shader_uniform_f32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_i32",             (void*)(intptr_t)aiml_gl_shader_uniform_i32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_vec2",            (void*)(intptr_t)aiml_gl_shader_uniform_vec2);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_vec3",            (void*)(intptr_t)aiml_gl_shader_uniform_vec3);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_vec4",            (void*)(intptr_t)aiml_gl_shader_uniform_vec4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_mat4",            (void*)(intptr_t)aiml_gl_shader_uniform_mat4);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_uniform_mat4_array",      (void*)(intptr_t)aiml_gl_shader_uniform_mat4_array);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_shader_delete",                  (void*)(intptr_t)aiml_gl_shader_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_load_texture",                   (void*)(intptr_t)aiml_gl_load_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_texture",                 (void*)(intptr_t)aiml_gl_create_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_texture_set_wrap",               (void*)(intptr_t)aiml_gl_texture_set_wrap);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_texture_set_filter",             (void*)(intptr_t)aiml_gl_texture_set_filter);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_texture_gen_mipmaps",            (void*)(intptr_t)aiml_gl_texture_gen_mipmaps);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_texture_delete",                 (void*)(intptr_t)aiml_gl_texture_delete);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_bind_texture",                   (void*)(intptr_t)aiml_gl_bind_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_draw_arrays",                    (void*)(intptr_t)aiml_gl_draw_arrays);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_draw_elements",                  (void*)(intptr_t)aiml_gl_draw_elements);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_draw_lines",                     (void*)(intptr_t)aiml_gl_draw_lines);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_draw_arrays_mode",               (void*)(intptr_t)aiml_gl_draw_arrays_mode);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_framebuffer",             (void*)(intptr_t)aiml_gl_create_framebuffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_bind_framebuffer",               (void*)(intptr_t)aiml_gl_bind_framebuffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_bind_default_framebuffer",       (void*)(intptr_t)aiml_gl_bind_default_framebuffer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_framebuffer_color_texture",      (void*)(intptr_t)aiml_gl_framebuffer_color_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_framebuffer_resize",             (void*)(intptr_t)aiml_gl_framebuffer_resize);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_framebuffer_delete",             (void*)(intptr_t)aiml_gl_framebuffer_delete);
    // gl_mesh
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_create_mesh",                    (void*)(intptr_t)aiml_gl_create_mesh);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_mesh_draw",                      (void*)(intptr_t)aiml_gl_mesh_draw);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_mesh_draw_count",                (void*)(intptr_t)aiml_gl_mesh_draw_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_mesh_index_count",               (void*)(intptr_t)aiml_gl_mesh_index_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gl_mesh_delete",                    (void*)(intptr_t)aiml_gl_mesh_delete);
    // gltf prim.to_gl_mesh
    llvm::sys::DynamicLibrary::AddSymbol("aiml_gltf_prim_to_gl_mesh",              (void*)(intptr_t)aiml_gltf_prim_to_gl_mesh);
    // engine namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_create_window",              (void*)(intptr_t)aiml_engine_create_window);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_create_window_fullscreen",   (void*)(intptr_t)aiml_engine_create_window_fullscreen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_destroy_window",             (void*)(intptr_t)aiml_engine_destroy_window);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_set_title",                  (void*)(intptr_t)aiml_engine_set_title);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_maximize_window",            (void*)(intptr_t)aiml_engine_maximize_window);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_window_size_x",              (void*)(intptr_t)aiml_engine_window_size_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_window_size_y",              (void*)(intptr_t)aiml_engine_window_size_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_make_current",               (void*)(intptr_t)aiml_engine_make_current);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_set_vsync",                  (void*)(intptr_t)aiml_engine_set_vsync);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_run",                        (void*)(intptr_t)aiml_engine_run);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_running",                    (void*)(intptr_t)aiml_engine_running);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_swap",                       (void*)(intptr_t)aiml_engine_swap);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_quit",                       (void*)(intptr_t)aiml_engine_quit);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_dt",                         (void*)(intptr_t)aiml_engine_dt);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_time",                       (void*)(intptr_t)aiml_engine_time);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_fps",                        (void*)(intptr_t)aiml_engine_fps);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_frame",                      (void*)(intptr_t)aiml_engine_frame);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_target_fps",                 (void*)(intptr_t)aiml_engine_target_fps);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_platform",                   (void*)(intptr_t)aiml_engine_platform);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_screen_size_x",              (void*)(intptr_t)aiml_engine_screen_size_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_screen_size_y",              (void*)(intptr_t)aiml_engine_screen_size_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_dpi",                        (void*)(intptr_t)aiml_engine_dpi);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_is_mobile",                  (void*)(intptr_t)aiml_engine_is_mobile);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_is_web",                     (void*)(intptr_t)aiml_engine_is_web);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_glfw_window",                (void*)(intptr_t)aiml_engine_glfw_window);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_register_input_update",      (void*)(intptr_t)aiml_engine_register_input_update);
    // prefab namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_scene_init",                 (void*)(intptr_t)aiml_prefab_scene_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_register",                   (void*)(intptr_t)aiml_prefab_register);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_instantiate",                (void*)(intptr_t)aiml_prefab_instantiate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_destroy",                    (void*)(intptr_t)aiml_prefab_destroy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_pos",                    (void*)(intptr_t)aiml_prefab_set_pos);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_pos_rel",                (void*)(intptr_t)aiml_prefab_set_pos_rel);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_x",                      (void*)(intptr_t)aiml_prefab_get_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_y",                      (void*)(intptr_t)aiml_prefab_get_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_scale",                  (void*)(intptr_t)aiml_prefab_set_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_scale",                  (void*)(intptr_t)aiml_prefab_get_scale);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_rotation",               (void*)(intptr_t)aiml_prefab_set_rotation);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_rotation",               (void*)(intptr_t)aiml_prefab_get_rotation);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_z",                      (void*)(intptr_t)aiml_prefab_set_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_z",                      (void*)(intptr_t)aiml_prefab_get_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_visible",                (void*)(intptr_t)aiml_prefab_set_visible);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_visible",                (void*)(intptr_t)aiml_prefab_get_visible);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_alpha",                  (void*)(intptr_t)aiml_prefab_set_alpha);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_alpha",                  (void*)(intptr_t)aiml_prefab_get_alpha);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_draggable",              (void*)(intptr_t)aiml_prefab_set_draggable);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_layout_hflow",               (void*)(intptr_t)aiml_engine_layout_hflow);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_layout_vflow",               (void*)(intptr_t)aiml_engine_layout_vflow);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_engine_layout_stack",               (void*)(intptr_t)aiml_engine_layout_stack);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_width",                  (void*)(intptr_t)aiml_prefab_get_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_height",                 (void*)(intptr_t)aiml_prefab_get_height);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_text",                   (void*)(intptr_t)aiml_prefab_set_text);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_texture",                (void*)(intptr_t)aiml_prefab_set_texture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_color",                  (void*)(intptr_t)aiml_prefab_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_bg_color",               (void*)(intptr_t)aiml_prefab_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_border_color",           (void*)(intptr_t)aiml_prefab_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_child_visible",          (void*)(intptr_t)aiml_prefab_set_child_visible);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_container_set_compact",      (void*)(intptr_t)aiml_prefab_container_set_compact);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_note_thrown_message",               (void*)(intptr_t)aiml_note_thrown_message);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_child",                      (void*)(intptr_t)aiml_prefab_child);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_dock_x",                     (void*)(intptr_t)aiml_prefab_dock_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_dock_y",                     (void*)(intptr_t)aiml_prefab_dock_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_grid_cell_rect",                 (void*)(intptr_t)aiml_ui_grid_cell_rect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_draw_scene",                 (void*)(intptr_t)aiml_prefab_draw_scene);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_hit_test",                   (void*)(intptr_t)aiml_prefab_hit_test);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_update_hover",               (void*)(intptr_t)aiml_prefab_update_hover);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_hovered",                (void*)(intptr_t)aiml_prefab_get_hovered);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_is_hovered",                 (void*)(intptr_t)aiml_prefab_is_hovered);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_was_clicked",                (void*)(intptr_t)aiml_prefab_was_clicked);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_update_drag",                (void*)(intptr_t)aiml_prefab_update_drag);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_dragged",                (void*)(intptr_t)aiml_prefab_get_dragged);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_cancel_drag",                (void*)(intptr_t)aiml_prefab_cancel_drag);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_add_child",                  (void*)(intptr_t)aiml_prefab_add_child);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_remove_child",               (void*)(intptr_t)aiml_prefab_remove_child);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_clip",                   (void*)(intptr_t)aiml_prefab_set_clip);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_scroll",                 (void*)(intptr_t)aiml_prefab_set_scroll);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_focus",                  (void*)(intptr_t)aiml_prefab_set_focus);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_scroll_to_end",              (void*)(intptr_t)aiml_prefab_scroll_to_end);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_scroll_x",               (void*)(intptr_t)aiml_prefab_get_scroll_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_scroll_y",               (void*)(intptr_t)aiml_prefab_get_scroll_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_bar_value",              (void*)(intptr_t)aiml_prefab_set_bar_value);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_bar_max",                (void*)(intptr_t)aiml_prefab_set_bar_max);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_bar_fill",               (void*)(intptr_t)aiml_prefab_set_bar_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_get_slider_value",           (void*)(intptr_t)aiml_prefab_get_slider_value);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_slider_value",           (void*)(intptr_t)aiml_prefab_set_slider_value);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_slider_min",             (void*)(intptr_t)aiml_prefab_set_slider_min);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_slider_max",             (void*)(intptr_t)aiml_prefab_set_slider_max);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_connect",                    (void*)(intptr_t)aiml_prefab_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_colors",              (void*)(intptr_t)aiml_connector_set_colors);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_highlight",           (void*)(intptr_t)aiml_connector_set_highlight);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_width",               (void*)(intptr_t)aiml_connector_set_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_bow",                 (void*)(intptr_t)aiml_connector_set_bow);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_visible",             (void*)(intptr_t)aiml_connector_set_visible);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_z",                   (void*)(intptr_t)aiml_connector_set_z);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_set_port",                (void*)(intptr_t)aiml_connector_set_port);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_connector_destroy",                 (void*)(intptr_t)aiml_connector_destroy);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_connector_anchor",       (void*)(intptr_t)aiml_prefab_set_connector_anchor);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_font",                   (void*)(intptr_t)aiml_prefab_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_register_callback",          (void*)(intptr_t)aiml_prefab_register_callback);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_material_register",                 (void*)(intptr_t)aiml_material_register);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_material_register",                 (void*)(intptr_t)aiml_material_register);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_text_value",                 (void*)(intptr_t)aiml_prefab_text_value);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_child_clicked",              (void*)(intptr_t)aiml_prefab_child_clicked);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_child_focused",              (void*)(intptr_t)aiml_prefab_child_focused);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_child_checked",              (void*)(intptr_t)aiml_prefab_child_checked);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_prefab_set_checked",                (void*)(intptr_t)aiml_prefab_set_checked);
    // storage namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_get",                       (void*)(intptr_t)aiml_storage_get);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_set",                       (void*)(intptr_t)aiml_storage_set);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_remove",                    (void*)(intptr_t)aiml_storage_remove);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_has",                       (void*)(intptr_t)aiml_storage_has);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_clear",                     (void*)(intptr_t)aiml_storage_clear);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_storage_host",                      (void*)(intptr_t)aiml_storage_host);
    // input namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_type",                  (void*)(intptr_t)aiml_input_event_type);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_key",                   (void*)(intptr_t)aiml_input_event_key);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_btn",                   (void*)(intptr_t)aiml_input_event_btn);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_pos_x",                 (void*)(intptr_t)aiml_input_event_pos_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_pos_y",                 (void*)(intptr_t)aiml_input_event_pos_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_scroll_x",              (void*)(intptr_t)aiml_input_event_scroll_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_scroll_y",              (void*)(intptr_t)aiml_input_event_scroll_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_char_",                 (void*)(intptr_t)aiml_input_event_char_);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_init",                        (void*)(intptr_t)aiml_input_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_update",                      (void*)(intptr_t)aiml_input_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_set_capture",                 (void*)(intptr_t)aiml_input_set_capture);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_key_down",                    (void*)(intptr_t)aiml_input_key_down);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_key_pressed",                 (void*)(intptr_t)aiml_input_key_pressed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_key_released",                (void*)(intptr_t)aiml_input_key_released);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_pos_x",                 (void*)(intptr_t)aiml_input_mouse_pos_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_pos_y",                 (void*)(intptr_t)aiml_input_mouse_pos_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_delta_x",               (void*)(intptr_t)aiml_input_mouse_delta_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_delta_y",               (void*)(intptr_t)aiml_input_mouse_delta_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_button",                (void*)(intptr_t)aiml_input_mouse_button);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_pressed",               (void*)(intptr_t)aiml_input_mouse_pressed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_released",              (void*)(intptr_t)aiml_input_mouse_released);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_scroll_x",              (void*)(intptr_t)aiml_input_mouse_scroll_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_mouse_scroll_y",              (void*)(intptr_t)aiml_input_mouse_scroll_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_gamepad_connected",           (void*)(intptr_t)aiml_input_gamepad_connected);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_gamepad_axis",                (void*)(intptr_t)aiml_input_gamepad_axis);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_gamepad_button",              (void*)(intptr_t)aiml_input_gamepad_button);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_gamepad_pressed",             (void*)(intptr_t)aiml_input_gamepad_pressed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_has_event",                   (void*)(intptr_t)aiml_input_has_event);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_next_event",                  (void*)(intptr_t)aiml_input_next_event);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_clear_events",                (void*)(intptr_t)aiml_input_clear_events);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_axis",                    (void*)(intptr_t)aiml_input_map_axis);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_axis_mouse",              (void*)(intptr_t)aiml_input_map_axis_mouse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_axis_gamepad",            (void*)(intptr_t)aiml_input_map_axis_gamepad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_action",                  (void*)(intptr_t)aiml_input_map_action);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_action_mouse",            (void*)(intptr_t)aiml_input_map_action_mouse);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_map_action_gamepad",          (void*)(intptr_t)aiml_input_map_action_gamepad);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_unmap",                       (void*)(intptr_t)aiml_input_unmap);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_clear_mappings",              (void*)(intptr_t)aiml_input_clear_mappings);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_axis",                        (void*)(intptr_t)aiml_input_axis);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_action_down",                 (void*)(intptr_t)aiml_input_action_down);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_action_pressed",              (void*)(intptr_t)aiml_input_action_pressed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_action_released",             (void*)(intptr_t)aiml_input_action_released);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_delta_x",              (void*)(intptr_t)aiml_input_event_delta_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_event_delta_y",              (void*)(intptr_t)aiml_input_event_delta_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_active",                (void*)(intptr_t)aiml_input_drag_active);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_started",               (void*)(intptr_t)aiml_input_drag_started);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_ended",                 (void*)(intptr_t)aiml_input_drag_ended);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_origin_x",              (void*)(intptr_t)aiml_input_drag_origin_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_origin_y",              (void*)(intptr_t)aiml_input_drag_origin_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_delta_x",               (void*)(intptr_t)aiml_input_drag_delta_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_delta_y",               (void*)(intptr_t)aiml_input_drag_delta_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_set_payload",           (void*)(intptr_t)aiml_input_drag_set_payload);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_get_payload",           (void*)(intptr_t)aiml_input_drag_get_payload);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_input_drag_set_threshold",         (void*)(intptr_t)aiml_input_drag_set_threshold);
    // audio namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_init",                              (void*)(intptr_t)aiml_audio_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_shutdown",                          (void*)(intptr_t)aiml_audio_shutdown);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_set_volume",                        (void*)(intptr_t)aiml_audio_set_volume);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_load",                              (void*)(intptr_t)aiml_audio_load);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_load_stream",                       (void*)(intptr_t)aiml_audio_load_stream);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_create_group",                      (void*)(intptr_t)aiml_audio_create_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_play",                        (void*)(intptr_t)aiml_audio_sound_play);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_pause",                       (void*)(intptr_t)aiml_audio_sound_pause);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_stop",                        (void*)(intptr_t)aiml_audio_sound_stop);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_loop",                    (void*)(intptr_t)aiml_audio_sound_set_loop);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_volume",                  (void*)(intptr_t)aiml_audio_sound_set_volume);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_pitch",                   (void*)(intptr_t)aiml_audio_sound_set_pitch);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_pan",                     (void*)(intptr_t)aiml_audio_sound_set_pan);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_seek",                        (void*)(intptr_t)aiml_audio_sound_seek);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_free",                        (void*)(intptr_t)aiml_audio_sound_free);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_is_playing",                  (void*)(intptr_t)aiml_audio_sound_is_playing);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_is_finished",                 (void*)(intptr_t)aiml_audio_sound_is_finished);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_duration",                    (void*)(intptr_t)aiml_audio_sound_duration);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_position",                    (void*)(intptr_t)aiml_audio_sound_position);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_group",                   (void*)(intptr_t)aiml_audio_sound_set_group);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_position",                (void*)(intptr_t)aiml_audio_sound_set_position);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_velocity",                (void*)(intptr_t)aiml_audio_sound_set_velocity);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_attenuation",             (void*)(intptr_t)aiml_audio_sound_set_attenuation);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_spatialization",          (void*)(intptr_t)aiml_audio_sound_set_spatialization);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_sound_set_directional_attenuation", (void*)(intptr_t)aiml_audio_sound_set_directional_attenuation);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_group_set_volume",                  (void*)(intptr_t)aiml_audio_group_set_volume);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_group_pause_all",                   (void*)(intptr_t)aiml_audio_group_pause_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_group_resume_all",                  (void*)(intptr_t)aiml_audio_group_resume_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_group_stop_all",                    (void*)(intptr_t)aiml_audio_group_stop_all);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_set_listener",                      (void*)(intptr_t)aiml_audio_set_listener);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_audio_set_listener_velocity",             (void*)(intptr_t)aiml_audio_set_listener_velocity);
    // mirror namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_listen",                     (void*)(intptr_t)aiml_mirror_listen);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_listen_max",                 (void*)(intptr_t)aiml_mirror_listen_max);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_connect",                    (void*)(intptr_t)aiml_mirror_connect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_connect_timeout",            (void*)(intptr_t)aiml_mirror_connect_timeout);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_close",              (void*)(intptr_t)aiml_mirror_session_close);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_update",             (void*)(intptr_t)aiml_mirror_session_update);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_is_connected",       (void*)(intptr_t)aiml_mirror_session_is_connected);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_peer_count",         (void*)(intptr_t)aiml_mirror_session_peer_count);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_register",            (void*)(intptr_t)aiml_mirror_session_register);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_register_id",         (void*)(intptr_t)aiml_mirror_session_register_id);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_register_typed",      (void*)(intptr_t)aiml_mirror_session_register_typed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_register_id_typed",   (void*)(intptr_t)aiml_mirror_session_register_id_typed);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_unregister",         (void*)(intptr_t)aiml_mirror_session_unregister);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_has_event",          (void*)(intptr_t)aiml_mirror_session_has_event);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_next_event",         (void*)(intptr_t)aiml_mirror_session_next_event);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_set_sync_rate",      (void*)(intptr_t)aiml_mirror_session_set_sync_rate);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_sync_now",           (void*)(intptr_t)aiml_mirror_session_sync_now);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_set_sync_mode",      (void*)(intptr_t)aiml_mirror_session_set_sync_mode);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_set_rpc_target",     (void*)(intptr_t)aiml_mirror_session_set_rpc_target);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_event_type",                 (void*)(intptr_t)aiml_mirror_event_type);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_type_enum",                  (void*)(intptr_t)aiml_mirror_type_enum);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_event_peer",                 (void*)(intptr_t)aiml_mirror_event_peer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_event_net_id",               (void*)(intptr_t)aiml_mirror_event_net_id);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_send_rpc",           (void*)(intptr_t)aiml_mirror_session_send_rpc);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_send_rpc_model",     (void*)(intptr_t)aiml_mirror_session_send_rpc_model);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_send_and_await",     (void*)(intptr_t)aiml_mirror_session_send_and_await);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_rpc_event_decode_into",             (void*)(intptr_t)aiml_rpc_event_decode_into);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_has_rpc",            (void*)(intptr_t)aiml_mirror_session_has_rpc);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_next_rpc",           (void*)(intptr_t)aiml_mirror_session_next_rpc);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_poll_event",         (void*)(intptr_t)aiml_mirror_session_poll_event);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_mirror_session_poll_rpc",           (void*)(intptr_t)aiml_mirror_session_poll_rpc);
    // ui namespace
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_init",                           (void*)(intptr_t)aiml_ui_init);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_begin",                          (void*)(intptr_t)aiml_ui_begin);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_end",                            (void*)(intptr_t)aiml_ui_end);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_begin_grid",                     (void*)(intptr_t)aiml_ui_begin_grid);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_end_grid",                       (void*)(intptr_t)aiml_ui_end_grid);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_set_gap",                        (void*)(intptr_t)aiml_ui_set_gap);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_begin_cell",                     (void*)(intptr_t)aiml_ui_begin_cell);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_end_cell",                       (void*)(intptr_t)aiml_ui_end_cell);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_set_align",                      (void*)(intptr_t)aiml_ui_set_align);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_set_scroll",                     (void*)(intptr_t)aiml_ui_set_scroll);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label",                          (void*)(intptr_t)aiml_ui_label);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_colored",                  (void*)(intptr_t)aiml_ui_label_colored);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_text_input",                     (void*)(intptr_t)aiml_ui_text_input);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_text_area",                      (void*)(intptr_t)aiml_ui_text_area);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button",                         (void*)(intptr_t)aiml_ui_button);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_icon",                    (void*)(intptr_t)aiml_ui_button_icon);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_checkbox",                       (void*)(intptr_t)aiml_ui_checkbox);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_spinbox",                        (void*)(intptr_t)aiml_ui_spinbox);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_toggle",                         (void*)(intptr_t)aiml_ui_toggle);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_radio",                          (void*)(intptr_t)aiml_ui_radio);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider",                         (void*)(intptr_t)aiml_ui_slider);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_int",                     (void*)(intptr_t)aiml_ui_slider_int);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_drag_f32",                       (void*)(intptr_t)aiml_ui_drag_f32);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_bar",                   (void*)(intptr_t)aiml_ui_progress_bar);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_bar_colored",           (void*)(intptr_t)aiml_ui_progress_bar_colored);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_image",                          (void*)(intptr_t)aiml_ui_image);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_image_sized",                    (void*)(intptr_t)aiml_ui_image_sized);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_rect",                           (void*)(intptr_t)aiml_ui_rect);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_rect_rounded",                   (void*)(intptr_t)aiml_ui_rect_rounded);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_separator",                      (void*)(intptr_t)aiml_ui_separator);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_spacer",                         (void*)(intptr_t)aiml_ui_spacer);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_same_line",                      (void*)(intptr_t)aiml_ui_same_line);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_newline",                        (void*)(intptr_t)aiml_ui_newline);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_begin_notebook",                (void*)(intptr_t)aiml_ui_begin_notebook);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_notebook_tab",                  (void*)(intptr_t)aiml_ui_notebook_tab);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_end_notebook",                  (void*)(intptr_t)aiml_ui_end_notebook);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_notebook_remove_clicked",       (void*)(intptr_t)aiml_ui_notebook_remove_clicked);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_push_style",                     (void*)(intptr_t)aiml_ui_push_style);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_pop_style",                      (void*)(intptr_t)aiml_ui_pop_style);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_set_default_style",              (void*)(intptr_t)aiml_ui_set_default_style);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_copy_current_style",             (void*)(intptr_t)aiml_ui_copy_current_style);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_load_font",                      (void*)(intptr_t)aiml_ui_load_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_load_font_default",              (void*)(intptr_t)aiml_ui_load_font_default);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_set_default_font",               (void*)(intptr_t)aiml_ui_set_default_font);
    // Widget style preset factories
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_new",               (void*)(intptr_t)aiml_ui_slider_style_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_new",               (void*)(intptr_t)aiml_ui_button_style_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_new",                (void*)(intptr_t)aiml_ui_label_style_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_new",             (void*)(intptr_t)aiml_ui_progress_style_new);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_new",                (void*)(intptr_t)aiml_ui_panel_style_new);
    // Slider style setters
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_thumb",         (void*)(intptr_t)aiml_ui_slider_style_set_thumb);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_track",         (void*)(intptr_t)aiml_ui_slider_style_set_track);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_color",         (void*)(intptr_t)aiml_ui_slider_style_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_bg_color",      (void*)(intptr_t)aiml_ui_slider_style_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_hover_color",   (void*)(intptr_t)aiml_ui_slider_style_set_hover_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_active_color",  (void*)(intptr_t)aiml_ui_slider_style_set_active_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_border_color",  (void*)(intptr_t)aiml_ui_slider_style_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_corner_radius", (void*)(intptr_t)aiml_ui_slider_style_set_corner_radius);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_border_width",  (void*)(intptr_t)aiml_ui_slider_style_set_border_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_font",          (void*)(intptr_t)aiml_ui_slider_style_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_padding_x",     (void*)(intptr_t)aiml_ui_slider_style_set_padding_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_padding_y",     (void*)(intptr_t)aiml_ui_slider_style_set_padding_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_size_w",        (void*)(intptr_t)aiml_ui_slider_style_set_size_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_slider_style_set_size_h",        (void*)(intptr_t)aiml_ui_slider_style_set_size_h);
    // Button style setters
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_bg",            (void*)(intptr_t)aiml_ui_button_style_set_bg);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_hover",         (void*)(intptr_t)aiml_ui_button_style_set_hover);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_active",        (void*)(intptr_t)aiml_ui_button_style_set_active);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_color",         (void*)(intptr_t)aiml_ui_button_style_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_bg_color",      (void*)(intptr_t)aiml_ui_button_style_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_hover_color",   (void*)(intptr_t)aiml_ui_button_style_set_hover_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_active_color",  (void*)(intptr_t)aiml_ui_button_style_set_active_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_border_color",  (void*)(intptr_t)aiml_ui_button_style_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_corner_radius", (void*)(intptr_t)aiml_ui_button_style_set_corner_radius);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_border_width",  (void*)(intptr_t)aiml_ui_button_style_set_border_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_font",          (void*)(intptr_t)aiml_ui_button_style_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_padding_x",     (void*)(intptr_t)aiml_ui_button_style_set_padding_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_padding_y",     (void*)(intptr_t)aiml_ui_button_style_set_padding_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_size_w",        (void*)(intptr_t)aiml_ui_button_style_set_size_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_button_style_set_size_h",        (void*)(intptr_t)aiml_ui_button_style_set_size_h);
    // Label style setters
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_color",          (void*)(intptr_t)aiml_ui_label_style_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_bg_color",       (void*)(intptr_t)aiml_ui_label_style_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_hover_color",    (void*)(intptr_t)aiml_ui_label_style_set_hover_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_active_color",   (void*)(intptr_t)aiml_ui_label_style_set_active_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_border_color",   (void*)(intptr_t)aiml_ui_label_style_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_corner_radius",  (void*)(intptr_t)aiml_ui_label_style_set_corner_radius);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_border_width",   (void*)(intptr_t)aiml_ui_label_style_set_border_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_font",           (void*)(intptr_t)aiml_ui_label_style_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_padding_x",      (void*)(intptr_t)aiml_ui_label_style_set_padding_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_padding_y",      (void*)(intptr_t)aiml_ui_label_style_set_padding_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_size_w",         (void*)(intptr_t)aiml_ui_label_style_set_size_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_label_style_set_size_h",         (void*)(intptr_t)aiml_ui_label_style_set_size_h);
    // Progress style setters
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_fill",        (void*)(intptr_t)aiml_ui_progress_style_set_fill);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_bg_tex",      (void*)(intptr_t)aiml_ui_progress_style_set_bg_tex);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_color",       (void*)(intptr_t)aiml_ui_progress_style_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_bg_color",    (void*)(intptr_t)aiml_ui_progress_style_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_hover_color", (void*)(intptr_t)aiml_ui_progress_style_set_hover_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_active_color",(void*)(intptr_t)aiml_ui_progress_style_set_active_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_border_color",(void*)(intptr_t)aiml_ui_progress_style_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_corner_radius",(void*)(intptr_t)aiml_ui_progress_style_set_corner_radius);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_border_width",(void*)(intptr_t)aiml_ui_progress_style_set_border_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_font",        (void*)(intptr_t)aiml_ui_progress_style_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_padding_x",   (void*)(intptr_t)aiml_ui_progress_style_set_padding_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_padding_y",   (void*)(intptr_t)aiml_ui_progress_style_set_padding_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_size_w",      (void*)(intptr_t)aiml_ui_progress_style_set_size_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_progress_style_set_size_h",      (void*)(intptr_t)aiml_ui_progress_style_set_size_h);
    // Panel style setters
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_bg_tex",         (void*)(intptr_t)aiml_ui_panel_style_set_bg_tex);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_color",          (void*)(intptr_t)aiml_ui_panel_style_set_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_bg_color",       (void*)(intptr_t)aiml_ui_panel_style_set_bg_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_hover_color",    (void*)(intptr_t)aiml_ui_panel_style_set_hover_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_active_color",   (void*)(intptr_t)aiml_ui_panel_style_set_active_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_border_color",   (void*)(intptr_t)aiml_ui_panel_style_set_border_color);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_corner_radius",  (void*)(intptr_t)aiml_ui_panel_style_set_corner_radius);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_border_width",   (void*)(intptr_t)aiml_ui_panel_style_set_border_width);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_font",           (void*)(intptr_t)aiml_ui_panel_style_set_font);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_padding_x",      (void*)(intptr_t)aiml_ui_panel_style_set_padding_x);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_padding_y",      (void*)(intptr_t)aiml_ui_panel_style_set_padding_y);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_size_w",         (void*)(intptr_t)aiml_ui_panel_style_set_size_w);
    llvm::sys::DynamicLibrary::AddSymbol("aiml_ui_panel_style_set_size_h",         (void*)(intptr_t)aiml_ui_panel_style_set_size_h);
    // libm scalar functions (resolved from libc.so at runtime, but AddSymbol ensures JIT finds them)
    llvm::sys::DynamicLibrary::AddSymbol("sinf",   (void*)(intptr_t)sinf);
    llvm::sys::DynamicLibrary::AddSymbol("cosf",   (void*)(intptr_t)cosf);
    llvm::sys::DynamicLibrary::AddSymbol("tanf",   (void*)(intptr_t)tanf);
    llvm::sys::DynamicLibrary::AddSymbol("asinf",  (void*)(intptr_t)asinf);
    llvm::sys::DynamicLibrary::AddSymbol("acosf",  (void*)(intptr_t)acosf);
    llvm::sys::DynamicLibrary::AddSymbol("sqrtf",  (void*)(intptr_t)sqrtf);
    llvm::sys::DynamicLibrary::AddSymbol("logf",   (void*)(intptr_t)logf);
    llvm::sys::DynamicLibrary::AddSymbol("log2f",  (void*)(intptr_t)log2f);
    llvm::sys::DynamicLibrary::AddSymbol("floorf", (void*)(intptr_t)floorf);
    llvm::sys::DynamicLibrary::AddSymbol("ceilf",  (void*)(intptr_t)ceilf);
    llvm::sys::DynamicLibrary::AddSymbol("roundf", (void*)(intptr_t)roundf);
    llvm::sys::DynamicLibrary::AddSymbol("fabsf",  (void*)(intptr_t)fabsf);
    llvm::sys::DynamicLibrary::AddSymbol("atan2f", (void*)(intptr_t)atan2f);
    llvm::sys::DynamicLibrary::AddSymbol("powf",   (void*)(intptr_t)powf);
    llvm::sys::DynamicLibrary::AddSymbol("fmodf",  (void*)(intptr_t)fmodf);

    std::string errStr;
    if (!module_) {
        LOG_ERROR("Module is empty. Did you compile first?");
        return 1;
    }

    // Verify IR before JIT execution — invalid IR causes hard crashes inside LLVM.
    {
        bool anyFail = false;
        for (auto& F : *module_) {
            std::string msg;
            llvm::raw_string_ostream s(msg);
            if (llvm::verifyFunction(F, &s)) {
                LOG_ERROR("Function '" + F.getName().str() + "' verification FAILED: " + msg);
                anyFail = true;
            }
        }
        std::string msg;
        llvm::raw_string_ostream s(msg);
        if (llvm::verifyModule(*module_, &s)) {
            LOG_ERROR("Module verification FAILED before JIT: " + msg);
            anyFail = true;
        }
        if (anyFail) {
            std::error_code ec;
            llvm::raw_fd_ostream irDump("/tmp/aiml_bad_module.ll", ec, llvm::sys::fs::OF_None);
            if (!ec) module_->print(irDump, nullptr);
            LOG_ERROR("IR dumped to /tmp/aiml_bad_module.ll");
            return 1;
        }
    }

    // Set Target Triple
    module_->setTargetTriple(llvm::sys::getDefaultTargetTriple());

    LOG_DEBUG("Creating EngineBuilder...");
    llvm::EngineBuilder builder(std::move(module_));
    builder.setErrorStr(&errStr);
    builder.setEngineKind(llvm::EngineKind::JIT);
    
    llvm::ExecutionEngine* engine = builder.create();
    if (!engine) {
        LOG_ERROR("Failed to create ExecutionEngine: " + errStr);
        return 1;
    }
    LOG_DEBUG("Engine created.");

    llvm::Function* mainFunc = engine->FindFunctionNamed("main");
    if (!mainFunc) {
        LOG_ERROR("Function 'main' not found in module.");
        return 1;
    }

    // Load External Object Files (Imports) into Engine
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> keptBuffers;
    for (const auto& objPath : linkedObjects) {
        auto bufferOrErr = llvm::MemoryBuffer::getFile(objPath);
        if (std::error_code EC = bufferOrErr.getError()) {
             LOG_ERROR("Failed to load object file for JIT: " + objPath + " -> " + EC.message());
        } else {
             std::unique_ptr<llvm::MemoryBuffer> buf = std::move(bufferOrErr.get());
             llvm::MemoryBufferRef bufRef = buf->getMemBufferRef();
             auto objOrErr = llvm::object::ObjectFile::createObjectFile(bufRef);
             
             if (!objOrErr) {
                 LOG_ERROR("Failed to create ObjectFile from buffer: " + objPath);
                 llvm::logAllUnhandledErrors(objOrErr.takeError(), llvm::errs(), "[JIT Error] ");
             } else {
                 engine->addObjectFile(std::move(objOrErr.get()));
                 keptBuffers.push_back(std::move(buf));
                 LOG_DEBUG("Loaded object file into Engine: " + objPath);
             }
        }
    }

    LOG_DEBUG("Found main. Running...");

    // Explicitly add printf
    llvm::sys::DynamicLibrary::AddSymbol("printf", (void*)(intptr_t)printf);

    LOG_DEBUG("Running main with fixed argv...");

    // Create dummy argv for JIT safety
    // We need to keep the memory alive during execution
    static char* dummyArgv[] = { (char*)"aiml_jit", nullptr };

    std::vector<llvm::GenericValue> args;
    args.push_back(llvm::GenericValue()); // argc
    args[0].IntVal = llvm::APInt(32, 1);
    args.push_back(llvm::GenericValue()); // argv
    args[1].PointerVal = dummyArgv;

    // Expose CompilerContext + engine to aiml_exec_code (called from JIT)
    aiml_set_active_context(compiler_ctx_, engine);
    // Top-level guard: an unhandled AIML exception (thrown deep in any function,
    // no matching catch) unwinds out of runFunction.  Catch it here so the
    // process reports the message and exits cleanly (code 2) instead of
    // std::terminate/core-dumping (spec/22 "Exception Propagation").
    llvm::GenericValue result;
    try {
        result = engine->runFunction(mainFunc, args);
    } catch (...) {
        aiml_set_active_context(nullptr, nullptr);
        std::fprintf(stderr, "Unhandled AIML exception: %s\n",
                     g_aiml_last_thrown_msg.empty() ? "(no message)" : g_aiml_last_thrown_msg.c_str());
        return 2;
    }
    aiml_set_active_context(nullptr, nullptr);

    LOG_DEBUG("Main finished. Result: " + std::to_string(result.IntVal.getSExtValue()));

    return result.IntVal.getSExtValue();
}

int AIMLCompiler::jitRun(int argc, char** argv) {
    // Same as jitRun() but passes real argc/argv to the AIML program's main().
    // argc/argv must remain valid for the entire duration of execution.
    llvm::Function* mainFunc = module_->getFunction("main");
    if (!mainFunc) {
        LOG_ERROR("No main function found in module.");
        return -1;
    }
    std::string errStr;
    llvm::EngineBuilder builder(std::move(module_));
    builder.setErrorStr(&errStr);
    builder.setEngineKind(llvm::EngineKind::JIT);
    llvm::ExecutionEngine* engine = builder.create();
    if (!engine) {
        LOG_ERROR("Failed to create ExecutionEngine: " + errStr);
        return -1;
    }
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    llvm::sys::DynamicLibrary::AddSymbol("printf", (void*)(intptr_t)printf);

    std::vector<llvm::GenericValue> gvArgs;
    gvArgs.push_back(llvm::GenericValue());
    gvArgs[0].IntVal = llvm::APInt(32, argc);
    gvArgs.push_back(llvm::GenericValue());
    gvArgs[1].PointerVal = argv;

    aiml_set_active_context(compiler_ctx_, engine);
    // Top-level guard — see the no-arg jitRun() above.
    llvm::GenericValue result;
    try {
        result = engine->runFunction(mainFunc, gvArgs);
    } catch (...) {
        aiml_set_active_context(nullptr, nullptr);
        std::fprintf(stderr, "Unhandled AIML exception: %s\n",
                     g_aiml_last_thrown_msg.empty() ? "(no message)" : g_aiml_last_thrown_msg.c_str());
        return 2;
    }
    aiml_set_active_context(nullptr, nullptr);

    return result.IntVal.getSExtValue();
}

llvm::Function* AIMLCompiler::getPrintfPrototype(llvm::Module& module, llvm::IRBuilderBase& builder) {
    std::vector<llvm::Type*> printfArgs;
    printfArgs.push_back(builder.getInt8Ty()->getPointerTo()); // char* format
    llvm::FunctionType* printfType = llvm::FunctionType::get(builder.getInt32Ty(), printfArgs, true); // true = varargs
    
    return llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module);
}
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>
#include <unistd.h>
#include <yaml-cpp/yaml.h>
#include "compiler.h"

namespace fs = std::filesystem;

static std::string getCacheDir() {
    const char* envCache = std::getenv("AIML_CACHE");
    if (envCache && std::strlen(envCache) > 0) {
        return std::string(envCache);
    }
    return "__aiml_cache";
}

static std::string computeSHA512(const std::string& path) {
    // Memoize by (path, size, mtime). Without this, computeImportCacheKey
    // re-fork+execs sha512sum on every transitive file for every cache check,
    // which on large projects (9rooms) blows up to multi-GB RSS and minutes
    // of CPU before main.aiml even finishes parsing.
    struct Entry { uintmax_t sz; long long mt; std::string hash; };
    static std::unordered_map<std::string, Entry> cache;

    uintmax_t sz = 0;
    long long mt = 0;
    try {
        if (fs::exists(path)) {
            sz = fs::file_size(path);
            mt = fs::last_write_time(path).time_since_epoch().count();
        }
    } catch (...) {}

    auto it = cache.find(path);
    if (it != cache.end() && it->second.sz == sz && it->second.mt == mt) {
        return it->second.hash;
    }

    std::string command = "sha512sum \"" + path + "\" 2>/dev/null";
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    size_t spacePos = result.find(' ');
    std::string hash = (spacePos != std::string::npos) ? result.substr(0, spacePos) : result;
    cache[path] = Entry{sz, mt, hash};
    return hash;
}

// ── Import Cache (semantic key) ──────────────────────────────────────────────
//
// The cache key for an imported file must reflect EVERYTHING the compiler reads
// to produce its .o, not just the file's own bytes. The previous (path-only)
// key produced false hits whenever a sibling or transitive import changed.
//
// Key components:
//   1. source_hash         — sha512 of the file itself
//   2. transitive_hash     — sha512 over sorted [path|sha512] of all transitive imports
//   3. sibling_hash        — sha512 over sorted [path|sha512] of all siblingImports
//   4. library_mode        — bool flag (header-only / lib vs entrypoint)
//   5. compiler_rev        — mtime+size of the aiml binary itself; rebuilding
//                            the compiler invalidates all caches
//
// The keyed object file lives at <cache>/<sanitized>__<key12>.o so different
// semantic contexts of the same source path never collide.

static std::string normalizePath(const std::string& p) {
    try {
        // weakly_canonical handles non-existing tail, absolute-izes the rest
        return fs::weakly_canonical(fs::path(p)).string();
    } catch (...) {
        return p;
    }
}

static std::string sha512OfString(const std::string& data) {
    // Pipe through sha512sum.  Avoids pulling in OpenSSL just for this path.
    std::string cmd = "sha512sum 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) return "";
    // popen with "w" only — we need bidi.  Fall back to a temp file.
    pclose(pipe);

    char tmpl[] = "/tmp/aiml_keyXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return "";
    ssize_t wrote = write(fd, data.data(), data.size());
    (void)wrote;
    close(fd);
    std::string out = computeSHA512(tmpl);
    unlink(tmpl);
    return out;
}

static std::string compilerBinaryRev(const std::string& compilerPath) {
    if (compilerPath.empty() || !fs::exists(compilerPath)) return "unknown";
    try {
        auto sz = fs::file_size(compilerPath);
        auto mt = fs::last_write_time(compilerPath).time_since_epoch().count();
        std::stringstream ss;
        ss << sz << "_" << mt;
        return ss.str();
    } catch (...) {
        return "unknown";
    }
}

void AIMLCompiler::collectTransitiveImports(const std::string& path,
                                            std::set<std::string>& visited) const {
    std::string norm = normalizePath(path);
    if (visited.count(norm)) return;       // cycle / dup guard
    visited.insert(norm);

    if (!fs::exists(norm)) return;
    try {
        YAML::Node root = YAML::LoadFile(norm);
        if (!root.IsSequence()) return;
        // Resolve relative imports against the importing file's directory.
        fs::path baseDir = fs::path(norm).parent_path();
        for (const auto& entry : root) {
            if (!entry.IsMap()) continue;
            if (entry["import"]) {
                std::string ip = entry["import"].as<std::string>();
                fs::path resolved = ip;
                if (!fs::exists(resolved)) {
                    // Walk up from importing file's directory.
                    fs::path cur = baseDir;
                    while (true) {
                        fs::path cand = cur / ip;
                        if (fs::exists(cand)) { resolved = cand; break; }
                        if (cur.empty() || cur == cur.root_path()) break;
                        cur = cur.parent_path();
                    }
                }
                collectTransitiveImports(resolved.string(), visited);
            }
        }
    } catch (...) {
        // Bad YAML in a transitive file: leave it in `visited` so its presence
        // (or absence) still affects the key, but don't recurse further.
    }
}

std::string AIMLCompiler::computeImportCacheKey(const std::string& sourcePath,
                                                const std::vector<std::string>& siblings,
                                                bool libMode) const {
    std::stringstream keyText;
    keyText << "source=" << normalizePath(sourcePath) << "\n";
    keyText << "source_hash=" << computeSHA512(sourcePath) << "\n";

    // Transitive imports (sorted, normalized, hashed individually)
    std::set<std::string> transitive;
    collectTransitiveImports(sourcePath, transitive);
    transitive.erase(normalizePath(sourcePath));   // exclude self
    keyText << "transitive_count=" << transitive.size() << "\n";
    for (const auto& tp : transitive) {
        keyText << "  " << tp << "|" << computeSHA512(tp) << "\n";
    }

    // Sibling imports (sorted, normalized)
    std::set<std::string> sibSet;
    for (const auto& s : siblings) sibSet.insert(normalizePath(s));
    sibSet.erase(normalizePath(sourcePath));
    keyText << "sibling_count=" << sibSet.size() << "\n";
    for (const auto& sp : sibSet) {
        keyText << "  " << sp << "|" << computeSHA512(sp) << "\n";
    }

    keyText << "library_mode=" << (libMode ? 1 : 0) << "\n";
    keyText << "compiler_rev=" << compilerBinaryRev(compilerPath) << "\n";

    return sha512OfString(keyText.str());
}

static std::string keyedObjectPath(const std::string& cacheDir,
                                   const std::string& sourcePath,
                                   const std::string& cacheKey) {
    std::string sanitized;
    for (char c : sourcePath)
        sanitized += (std::isalnum(c) ? c : '_');
    std::string keyShort = cacheKey.substr(0, 12);
    return cacheDir + "/" + sanitized + "__" + keyShort + ".o";
}

bool AIMLCompiler::checkCache(const std::string& sourcePath,
                              const std::vector<std::string>& siblings,
                              bool libMode,
                              std::string& outCacheKey,
                              std::string& outObjPath) {
    if (!fs::exists(sourcePath)) {
        LOG_ERROR(sourcePath + ": Import source not found");
        return false;
    }

    outCacheKey = computeImportCacheKey(sourcePath, siblings, libMode);
    if (outCacheKey.empty()) return false;

    std::string cacheDir = getCacheDir();
    if (!fs::exists(cacheDir)) fs::create_directories(cacheDir);

    outObjPath = keyedObjectPath(cacheDir, sourcePath, outCacheKey);

    std::string manifestPath = cacheDir + "/compilations.yaml";
    YAML::Node manifest;
    if (fs::exists(manifestPath)) {
        try { manifest = YAML::LoadFile(manifestPath); } catch (...) {}
    }

    // New manifest schema: per-source map with cache_key + object_path.
    // Old scalar schema is implicitly invalidated (.as<map>() throws → miss).
    YAML::Node entry = manifest[sourcePath];
    if (entry && entry.IsMap() && entry["cache_key"]) {
        try {
            std::string storedKey = entry["cache_key"].as<std::string>();
            if (storedKey == outCacheKey && fs::exists(outObjPath)) {
                return true;
            }
        } catch (...) {}
    }
    return false;
}

void AIMLCompiler::resolveImport(const std::string& path) {
    // Track imported source file for .d dependency output
    if (std::find(importedSourceFiles.begin(), importedSourceFiles.end(), path) == importedSourceFiles.end())
        importedSourceFiles.push_back(path);

    // In object mode (noCache=true), the build tool (exodus) handles caching.
    // In JIT mode (noCache=false), use the internal semantic cache.
    std::string cacheKey;
    std::string objPath;

    if (!noCache) {
        if (checkCache(path, this->siblingImports, /*libMode=*/true, cacheKey, objPath)) {
            LOG_INFO("Cache HIT for import: " + path + " [" + cacheKey.substr(0,12) + "]");
            // Per-instance compiledKeys guard: avoid duplicate add to linkedObjects
            if (compiledKeys->insert(cacheKey).second) {
                linkedObjects.push_back(objPath);
            }
            return;
        }
        LOG_INFO("Cache MISS for import: " + path + " [" + cacheKey.substr(0,12) + "]. Compiling...");
    } else {
        LOG_INFO("Compiling import (no-cache): " + path);
    }

    // Output dir: use outputDir (build dir) when noCache, else __aiml_cache
    std::string outDir = (noCache && !outputDir.empty()) ? outputDir : getCacheDir();
    if (!fs::exists(outDir)) fs::create_directories(outDir);

    // Object filename: keyed in JIT mode (collision-safe across contexts),
    // sanitized-only in noCache mode (exodus owns the namespace there).
    // Suffix: `.ll` when emitting text IR for an external toolchain (emcc),
    // `.o` otherwise. The exodus build picks up the matching extension via
    // its expected-side-file logic.
    const char* importExt = emitImportsAsLL ? ".ll" : ".o";
    if (noCache) {
        std::string sanitized;
        for (char c : path)
            sanitized += (std::isalnum(c) ? c : '_');
        objPath = outDir + "/" + sanitized + importExt;
    } else {
        objPath = keyedObjectPath(outDir, path, cacheKey);
    }

    // Per-instance guard: skip if this exact key was already compiled in
    // this run (e.g. diamond imports A→B, A→C, B→D, C→D).
    if (!noCache && !compiledKeys->insert(cacheKey).second) {
        linkedObjects.push_back(objPath);
        return;
    }

    // Create isolated compiler instance for the imported file.
    // Pass sibling imports so the sub-compiler can resolve types from other
    // import files (e.g. GameState defined in models.aiml, used in effects.aiml).
    AIMLCompiler subCompiler;
    subCompiler.compilerPath = this->compilerPath;
    subCompiler.libraryMode = true;
    subCompiler.noCache = this->noCache;
    subCompiler.outputDir = this->outputDir;
    subCompiler.compiledKeys = this->compiledKeys;   // share guard within run
    subCompiler.parsedPathsShared = this->parsedPathsShared;   // share parser dedup within run
    subCompiler.emitImportsAsLL = this->emitImportsAsLL;
    subCompiler.reportPrefabsMode = this->reportPrefabsMode;
    if (!this->targetTripleStr.empty())
        subCompiler.setTargetTriple(this->targetTripleStr);
    // Asset Registry (spec/44_7): propagate the same --asset-roots into the
    // sub-compiler so imported files' prefab/gl.tex literals validate against
    // the same store.  Without this the sub-compiler's scan("") would clear
    // the singleton and every literal lookup would fail.
    subCompiler.assetRootsStr = this->assetRootsStr;
    // Give the sub-compiler all known sibling imports (excluding itself).
    for (const auto& sib : this->siblingImports) {
        if (sib != path)
            subCompiler.siblingImports.push_back(sib);
    }

    subCompiler.compile(path, "/dev/null");
    if (reportPrefabsMode) {
        // Analysis-only: skip object/IR emission — we only want the wiring
        // facts (merged below).
    } else if (emitImportsAsLL) {
        // Text IR for external toolchains (emcc). Library imports don't need
        // a main() entry point so we skip generateEntryPoint here.
        subCompiler.saveModule(objPath);
    } else {
        subCompiler.compileToObject(path, objPath);
    }

    // Propagate transitive linked objects from sub-compiler
    for (const auto& obj : subCompiler.linkedObjects) {
        if (std::find(linkedObjects.begin(), linkedObjects.end(), obj) == linkedObjects.end())
            linkedObjects.push_back(obj);
    }

    // Propagate transitive imported source files from sub-compiler
    for (const auto& src : subCompiler.importedSourceFiles) {
        if (std::find(importedSourceFiles.begin(), importedSourceFiles.end(), src) == importedSourceFiles.end())
            importedSourceFiles.push_back(src);
    }

    // Propagate transitive prefab decl/use sets for Tier-3 dead-prefab analysis.
    allPrefabDecls.insert(subCompiler.allPrefabDecls.begin(), subCompiler.allPrefabDecls.end());
    allPrefabUses.insert(subCompiler.allPrefabUses.begin(),  subCompiler.allPrefabUses.end());

    // Propagate transitive enum decl/use sets for unused-enumerator analysis.
    for (const auto& kv : subCompiler.allEnumDeclMembers)
        allEnumDeclMembers[kv.first].insert(kv.second.begin(), kv.second.end());
    for (const auto& kv : subCompiler.allEnumUsedMembers)
        allEnumUsedMembers[kv.first].insert(kv.second.begin(), kv.second.end());
    for (const auto& kv : subCompiler.allEnumDeclLoc)
        allEnumDeclLoc.emplace(kv.first, kv.second);
    if (reportPrefabsMode)
        allPrefabReport.insert(allPrefabReport.end(),
            subCompiler.allPrefabReport.begin(), subCompiler.allPrefabReport.end());

    // Update cache manifest (JIT mode only) — new structured schema.
    if (!noCache) {
        std::string manifestPath = getCacheDir() + "/compilations.yaml";
        YAML::Node manifest;
        if (fs::exists(manifestPath)) {
            try { manifest = YAML::LoadFile(manifestPath); } catch (...) {}
        }
        // If existing entry is the old scalar form, drop it implicitly by reassigning.
        YAML::Node entry;
        entry["cache_key"]   = cacheKey;
        entry["source_hash"] = computeSHA512(path);
        entry["object_path"] = objPath;
        entry["library_mode"] = 1;
        manifest[path] = entry;
        std::ofstream fout(manifestPath);
        fout << manifest;
    }

    linkedObjects.push_back(objPath);
}
