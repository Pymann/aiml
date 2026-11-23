# Auto-generation of AIML unit tests (mock & stub builder)

This document assesses the complexity and the theoretical approach for automatically generating fully functional, isolated unit test files (with mocks and cut-off imports) from existing `.aiml` source files.

## 1. Feasibility & difficulty
The difficulty is surprisingly **low**.
Since AIML structures its own abstract syntax tree (AST) purely as YAML, we do not have to develop a lexical analyzer or text parser (as for C/C++ or Rust) from scratch.

A simple Python script using `pyyaml` can load the file, modify it as a nested dictionary and write it back to disk as perfect AIML.

## 2. The algorithm (Python-based test generator)

The tooling flow would happen in these 6 clear steps:

### Step 1: parse & isolate the YAML AST
The script loads the original file (e.g. `core/decoder.aiml`) into a Python list.
- **Cut off imports**: all top-level list items of type `{ "import": ... }` are removed via `del list[i]`, to cut file dependencies completely.

### Step 2: inventory the "existing symbols"
The script walks the top-level nodes to understand what is _really_ there:
- every `- class: { name: "X" }` found $\rightarrow$ register "X"
- every `- def: { name: "Y" }` found $\rightarrow$ register "Y"

### Step 3: find the "missing symbols" (semantic pass)
Now the script walks recursively through all `body` blocks and variable declarations of the AST:
- Looks for calls of the kind: `[call, FunctionX, ...]` or `[obj.FunctionX]`.
- Looks for declarations of the kind: `[decl, var, TypeX]`.
- **Logic**: if `FunctionX` or `TypeX` is neither in the inventory from step 2 NOR part of the AIML standard library (`sys`, `math`, `list`, `string`, `i32` ...), it is a cut-off dependency.

### Step 4: build in mocks & stubs automatically
For every _missing_ symbol the script now inserts dynamic mock code (dummy nodes) at the top of the YAML list:

- **For missing classes**:
  ```yaml
  - class:
      name: MissingDependency  # dummy, so that the LLVM compiler knows the pointer type
      members:
        - [dummy, i32] # helper field, in case the struct size is checked
  ```
- **For missing functions**:
  ```yaml
  - def:
      name: missing_function
      args: [] # types can mostly be omitted/defined as 'any'
      return: void
      body:
        - print: ["Mocked missing_function executed"]
  ```

### Step 5: generate test skeletons
For _every_ `def` or `method` inventoried in step 2 the script appends a test case at the end of the YAML file:
```yaml
- def:
    name: test_Decoder_decode_step
    return: void
    body:
      - print: ["--- Running: test_Decoder_decode_step ---"]
      # TODO: add a manual assert
```

### Step 6: generate the runner (`main`)
As the last element the `main` routine is inserted, which forms a test suite, satisfies the compiler (because `main` is required) and fires all `test_*` functions:
```yaml
- def:
    name: main
    return: i32
    body:
      - call: [test_Decoder_decode_step]
      - call: [test_xyz]
      - print: ["All generated tests executed successfully."]
      - return: [0]
```

## 3. Benefits for our phases 1 to 3
1. **100% compiler safety without context**: since missing types are stubbed by empty dummy `class` nodes, the type checker of `tulaiml` does not complain. It compiles immediately.
2. **Lightning-fast TDD**: you can develop algorithms (e.g. matrix multiplication) inside a large agent class, run the generator, and evaluate the isolated `test_tensor_minimal.aiml` in milliseconds via `out/aiml`.
3. **CI/CD hook-in**: Python can turn 50 .aiml files into self-contained test suites in seconds via the script.

## Conclusion: is it worth it as a Python script?
**Definitely.** What would need an enormously complex Clang AST matcher in classic C++ is in AIML just a **50-100 line Python script** that has to search and modify a simple JSON/YAML object recursively. That would be a gigantic productivity boost for our project.

---

## 4. The premier class: native compiler support (`--generate-tests`)

Your consideration is absolutely spot-on! Instead of maintaining an external Python script, it is paradoxically even **cleaner and more powerful** to build this generator directly into the `aiml` C++ compiler.

### Why the compiler is the perfect place for it:
1. **Perfect knowledge of the standard library:** a Python script would have to maintain a hard-coded "whitelist" to know that `[sys.env]` or `[math.vec2]` must not be mocked. The C++ compiler already has this knowledge built in flawlessly in the `SymbolResolutionPass`!
2. **Catching undefined references:** normally the compiler aborts with `Error: Undefined Type X` when it finds a `[decl, my_var, MissingClass]`. For test mode we activate a `TestGenVisitor` instead: as soon as the compiler notices that a type is missing, it does not spit out an error abort but simply injects a new dummy node into the internal C++ AST (abstract syntax tree).
3. **Native export:** thanks to the implementation of `yyjson` (from phase 4) the AIML compiler has an extremely fast JSON writer on board anyway. Since *valid JSON = valid YAML*, the C++ compiler can simply dump the internal AST, extended by the stubs and `- def: [test_...]` skeletons, to disk at the end as formatted source code!

**The C++ CLI interface would then look fantastically simple:**
```bash
./out/aiml --generate-tests src/tul/model/decoder.aiml -o tests/test_decoder_minimal.aiml
```

The development effort in C++ for this `TestGeneratorPass` would probably be a realistic **200–400 lines**. The compiler iterates once over all nodes, creates dummies on the fly in the system heap when something is missing, appends `test_` routines and writes the tree back to the file.

**Conclusion:** the compiler approach is technically superior, because it makes optimal use of the *single source of truth* (its own type system), without provoking tooling breaks in Python!
