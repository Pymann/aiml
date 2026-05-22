What is missing for sys.bundle / sys.exec
1. Current Implementation (Actual State)
Code type — fully implemented in code_impl.cpp:


Struct: { i8* id, i8* source, i64 src_len, i8* entry }
Methods: contains(), size(), toString(), merge() — all working.

sys.bundle — not implemented (no handler in builtin_registry.cpp, no intercept in ast_compiler_call.cpp)

sys.load_bundle — not implemented

sys.exec — not implemented

Infrastructure already exists in context.h:

FunctionInfo::sourceYaml — YAML dump of the function AST
FunctionInfo::referencedTypes — transitive type dependencies
ClassInfo::sourceYaml / ClassInfo::referencedTypes — same for classes
ctx.bundleTrackingTarget — set during compilation, tracks type dependencies
2. Spec Issues (29_serialize.md)
Inconsistency: Code struct definition does not match the implementation:

Spec says	Impl has
id: string (SHA-256)	id: i8* — always empty ""
start_call: map (AST node)	entry: i8* — simple function name as string
chunks: list<string>	source: i8* + src_len: i64 — no chunks array
Unclear semantics:

What happens with sys.bundle in AOT mode (not JIT)? Executing at runtime makes no sense there.
sys.load_bundle — "loads definitions into the current environment" — this means dynamic recompilation of YAML at runtime: extremely complex, probably only useful in JIT mode.
Enum definitions have no sourceYaml → can they be bundled at all?
What happens with sys.bundle on a function that uses external C libraries?
3. What Is Concretely Missing (Implementation)
sys.bundle (compile-time operation):

Handler in builtin_registry.cpp as FuncHandler
Selector argument: string literal at compile time
Dependency closure algorithm: target + referencedTypes recursively → collect all sourceYaml
Create code object with assembled YAML as source + entry = selector
sys.exec (JIT-only):

Handler in builtin_registry.cpp
Parse Code.source YAML + execute in a new AIML compiler context (via params.compileCallback)
Return value? Error handling?
sys.load_bundle — unclear whether really needed. Alternative: implement only sys.exec.

4. Realistic Plan
Before implementing, the spec should answer the following questions:

Should sys.exec / sys.load_bundle work only in JIT mode or also in AOT?
Should sys.bundle include enums?
What is the return value of sys.exec?
Do we need sys.load_bundle at all, or is sys.exec alone sufficient?
Should the Code type get the spec definition (with start_call: map, chunks) — or should the simpler implementation (source: string, entry: string) be documented in the spec?
Should I clarify the spec first and then implement?

Exactly, I did not understand why you implemented the Code Object differently than specified — the idea was that you can push aiml-pieces onto chunks. Of course you also need to be able to specify a start_call, so you know the entrypoint with parameters. A list of strings is not strictly necessary, but it essentially represents an aiml file that is also a list of instructions. In chunks they are then serialized, i.e. the original yaml string. We now have the problem that if a Code object e.g. uses class y, then any set values must of course also be stored in the Code object. Which is why we actually need 2 chunks, one code_chunk and one data_chunk. Or do you put everything in code_chunk with direct serialize commands? Of course this should also work in AOT mode, then we need to link against libaiml.so, which we have already built. Why do enum definitions have no source yaml? That can and must then be set. The final idea is that e.g. an agent receives a Code object directly streamed over the network and can execute it. Like e.g. a jenkins agent, except groovy and cps transformations are bizarre. When using external C libs they simply need to be present again at execution time, we are sending aiml code directly in the Code object back and forth, why is that a problem? Then analyze further with my input!
Very clarifying. Let me now analyze this systematically:

Architecture Analysis: Code Object & sys.bundle/exec
1. Code Struct: What the Spec Meant vs. Impl
The implementation incorrectly oversimplified the concept. Correct:


__aiml_code_t:
  id:          string   → SHA-256 of code_chunk (fingerprint)
  start_call:  string   → serialized YAML call node: '{"call":["worker_task",...]}'
  code_chunk:  string   → AIML definitions (enum, class, def — static)
  data_chunk:  string   → AIML set statements (runtime state, via sys.deserialize)
Why two chunks? Correctly identified:


# code_chunk contains:
- class: { name: Payload, members: [[id, i32]] }
- def: { name: worker_task, args: [[p, Payload]], body: [...] }

# data_chunk contains (serialized state):
- decl: [p, Payload]
- set: [p, { call: [sys.deserialize, '{"__type":"Payload","id":42}', "Payload"] }]

# start_call:
'{"call": ["worker_task", "p"]}'
When an agent receives this: compile code_chunk → execute data_chunk → call start_call. ✓

2. What Is Missing/Wrong in the Impl
code_impl.cpp — current struct:


// ACTUAL (wrong):
{ i8* id, i8* source, i64 src_len, i8* entry }

// SHOULD BE:
{ i8* id, i8* start_call, i8* code_chunk, i8* data_chunk }
Methods still missing:

push(chunk: string) → appends to code_chunk (for manual assembly)
push_data(chunk: string) → appends to data_chunk
set_entry(call_yaml: string) → sets start_call
Enum sourceYaml is completely missing:


// context.h has for enums only:
std::map<std::string, std::map<std::string, int>> enums;
// No place for the YAML source!

// We need:
std::map<std::string, std::string> enumSources;  // name → YAML string
In ast_compiler.cpp, when processing an enum: node, ctx.enumSources[name] = yamlDump must be set.

3. sys.bundle — What It Must Do

# Syntax options:
- set: [pkg, { call: [sys.bundle, "worker_task"] }]
# or with explicit call:
- set: [pkg, { call: [sys.bundle, "worker_task", "p", some_payload] }]
# or on instance (for data bundle):
- set: [pkg, { call: [sys.bundle, p] }]
Compile-time operation (intercept like sys.serialize):


1. Resolve selector → ctx.functions["worker_task"] or ctx.classes["Payload"]
2. Get sourceYaml
3. Collect transitive referencedTypes:
   - for each referenced type: sourceYaml + recursively their deps
   - enums: ctx.enumSources[name]
4. code_chunk = join(all sourceYamls)
5. data_chunk = "" (initially empty; for instance bundle: sys.serialize of fields)
6. id = SHA-256 of code_chunk (or simpler hash for now)
7. start_call = serialized YAML call
8. Create code object
Instance bundle (sys.bundle(p) where p: Payload):


code_chunk = Payload.sourceYaml + transitive deps
data_chunk = generated AIML code:
  "- decl: [p, Payload]\n- set: [p, {call: [sys.deserialize, '<json>', 'Payload']}]\n"
start_call = "" (no automatic entry point)
4. sys.exec — AOT via libaiml.so
The key insight is correct: we send AIML source code, not IR. The receiver only needs:

libaiml.so (AIML compiler+runtime as shared library)
The same C libraries (yaml-cpp, LLVM-18) as the sender
libaiml.so API (what we need to build):


// Public C API for AOT sys.exec:
int aiml_exec(const char* code_chunk, const char* data_chunk, const char* start_call);
int aiml_load(const char* code_chunk);   // for sys.load_bundle
sys.exec flow:


JIT mode:  ctx.params.compileCallback(code_chunk + "\n" + data_chunk + "\n" + start_call_as_instr)
AOT mode:  dlopen("libaiml.so") → aiml_exec(code, data, call) → dlclose()
5. Spec Must Clarify These Points
The current spec/29_serialize.md must clarify the following:

Code struct (the reflected definition does not match the impl):

- class:
    name: Code
    members:
      - [id,         string]   # SHA-256 of code_chunk
      - [start_call, string]   # YAML-serialized call node
      - [code_chunk, List<string>]   # AIML defs (enum/class/def)
      - [data_chunk, List<string>]   # AIML state inits (decl/set via deserialize)
    methods:
      - set_entry(call_yaml: string) → void   # sets start_call
      - contains(name: string)       → bool
      - toString()                   → string # = code_chunk + data_chunk
      - merge(other: Code)           → Code
sys.bundle:

Enums are included with their definition
External C libs: not in the bundle — receiver needs the same runtime environment
AOT mode: works via libaiml.so
sys.exec:

JIT: directly in the same process
AOT: via libaiml_exec() from libaiml.so
Return value: i32 (exit code)
