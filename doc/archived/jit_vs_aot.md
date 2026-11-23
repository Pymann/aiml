# JIT vs AOT

## Summary

The compiler currently has no uniform semantics between the JIT and AOT paths.

There are at least two clearly different compile/link pipelines:

- the file/AOT path via `compile(...)`
- the string/JIT/chunk path via `compileFromString(...)` and `compileChunkAndGetAddress(...)`

These paths do not set up the same compiler context, handle imports differently and do not load external symbols/object files the same way.

This is very probably a real cause of:

- differences between a normal build and `sys.bundle` / hot reload
- import/extern/global problems only in the JIT path
- differences with transitive imports, sibling imports and object linking

## 1. Biggest difference: `compile()` vs `compileFromString()`

Affected places:

- [compiler.cpp](../../compiler.cpp#L104)
- [compiler.cpp](../../compiler.cpp#L431)

### AOT / file path

In [compiler.cpp](../../compiler.cpp#L104) to [compiler.cpp](../../compiler.cpp#L426), `compile(...)` among other things:

- sets `ctx.libraryMode`
- sets `ctx.compilerPath`
- installs `ctx.resolveImportCallback`
- collects `deferredImports`
- does header propagation for imports
- registers `libInitFuncNames`
- does `siblingImports` injection
- registers external globals from imported files
- propagates `ctx.objectFilesToLink`
- sets `ctx.currentSourceFile`

### JIT / string path

In [compiler.cpp](../../compiler.cpp#L431) to [compiler.cpp](../../compiler.cpp#L480), `compileFromString(...)` does considerably less by comparison:

- no `ctx.libraryMode`
- no `ctx.resolveImportCallback`
- no `deferredImports`
- no `siblingImports`
- no header-only import propagation
- no separate registration of imported globals
- no `ctx.currentSourceFile`

### Consequence

The same AIML content does not have the same compile context from a file and from a string.

That is not a small difference, but a semantic split.

## 2. Import handling is not symmetric

Affected places:

- [compiler.cpp](../../compiler.cpp#L135)
- [compiler.cpp](../../compiler.cpp#L263)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L417)

### AOT

In the file path, import resolution is actively prepared:

- [compiler.cpp](../../compiler.cpp#L135) to [compiler.cpp](../../compiler.cpp#L205)
- [compiler.cpp](../../compiler.cpp#L263) to [compiler.cpp](../../compiler.cpp#L398)

In the process:

- types from imports are registered ahead of time
- sibling imports are taken into account
- global `decl`s from imports are lifted into the symbol table
- object files for imports are prepared

### JIT

`compileFromString(...)` only does:

- `registerTypes(programAST.get())`
- `compile(programAST.get())`

without the import preparation.

### Consequence

Everything that depends on the import context can react differently in the JIT path:

- global variables from imports
- `extern`-like symbols
- transitive models/classes
- sibling imports

## 3. JIT runtime binding is not uniform

Affected places:

- [compiler.cpp](../../compiler.cpp#L723)
- [compiler.cpp](../../compiler.cpp#L1614)
- [compiler.cpp](../../compiler.cpp#L489)

### `jitRun()`

The normal JIT run in [compiler.cpp](../../compiler.cpp#L723) to [compiler.cpp](../../compiler.cpp#L1668):

- loads host process symbols via `LoadLibraryPermanently(nullptr)`
- preloads libraries from `exodus.json`
- registers a great many runtime functions via `DynamicLibrary::AddSymbol`
- loads `linkedObjects` via `engine->addObjectFile(...)`
- sets `aiml_set_active_context(...)`

### `compileChunkAndGetAddress()`

The chunk/hot reload path in [compiler.cpp](../../compiler.cpp#L489) to [compiler.cpp](../../compiler.cpp#L530):

- compiles via `compileFromString(...)`
- builds a JIT engine directly
- does no visible import object loading
- does no broad `AddSymbol` registration like `jitRun()`
- uses no equivalent setup path for external runtime dependencies

### Consequence

A symbol can be available in the normal JIT run, but be missing or resolved differently in the chunk/bundle path.

## 4. `currentSourceFile` only in the file path

Affected places:

- [compiler.cpp](../../compiler.cpp#L259)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L166)

In [compiler.cpp](../../compiler.cpp#L247) to [compiler.cpp](../../compiler.cpp#L256), `ctx.currentSourceFile` is set in the file path.

`compileFromString(...)` does not do that.

This affects at least:

- `log.*` location injection in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L151)

### Consequence

JIT/bundle code has different or missing source locations in logs.

That is not a core runtime bug, but further evidence that both paths do not have the same invariants.

## 5. `libraryMode` is not set consistently

Affected places:

- [compiler.cpp](../../compiler.cpp#L124)
- [compiler.cpp](../../compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L25)

In the file path, `ctx.libraryMode = libraryMode` is set in [compiler.cpp](../../compiler.cpp#L123).

This takeover is missing in the string path.

That is relevant because `libraryMode` influences global symbol names and import/lib initialisation behaviour, in particular for:

- imported `decl`s
- `__aiml_lib_init_*`
- extern/file-specific symbol prefixes

### Consequence

The same code fragment can emit different global names or get different linkage semantics in string/JIT and file/AOT.

## 6. Object file linking only visible in the normal JIT path

Affected places:

- [compiler.cpp](../../compiler.cpp#L1614)
- [compiler.cpp](../../compiler.cpp#L489)

In [compiler.cpp](../../compiler.cpp#L1614) to [compiler.cpp](../../compiler.cpp#L1634), `jitRun()` explicitly loads `linkedObjects` into the engine.

`compileChunkAndGetAddress()` does not do this step.

### Consequence

If a JIT chunk depends on import code or imported object files, it does not behave like the normal JIT/AOT path.

This is particularly relevant for:

- `sys.bundle`
- hot reload
- transitive function/type use
- imported global state

## 7. AOT has a real linker phase, JIT only simulates parts of it

Affected places:

- [compiler.cpp](../../compiler.cpp#L642)
- [compiler.cpp](../../compiler.cpp#L723)

The AOT path via `compileToObject(...)` in [compiler.cpp](../../compiler.cpp#L642) to [compiler.cpp](../../compiler.cpp#L720) ends in real object files.

The JIT path, on the other hand, has to:

- register host symbols itself
- register runtime symbols itself
- load import objects itself

That is fundamentally normal, but here it is not centralised. Instead there is:

- a more comprehensive `jitRun()` path
- a shortened `compileChunkAndGetAddress()` path

### Consequence

JIT is currently not a clean mirror of AOT, but a mode of its own with different infrastructure.

## 8. `libraryMode` changes the entire top-level codegen

Affected places:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L153)

In [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20) to [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L183) there are two fundamentally different codegen modes:

- `ctx.libraryMode == true`
- `ctx.libraryMode == false`

### In library mode

- top-level `decl`s become LLVM globals
- `__aiml_lib_init_<file>` is generated
- initialisation runs via this lib init function
- no normal `__aiml_init` is generated
- `emitFptable(ctx)` does not run
- `emitSchemaRegistry(ctx)` does not run

### In normal mode

- `__aiml_init` is generated
- imported `libInitFuncNames` are called there
- afterwards `emitFptable(ctx)` is generated
- afterwards `emitSchemaRegistry(ctx)` is generated

### Consequence

If `compileFromString(...)` does not take over `libraryMode` correctly, that is not a side detail, but leads to a completely different module structure:

- different global names
- a different initialisation order
- missing `__aiml_init`
- missing fptable/schema emission

## 9. Hot reload/fptable infrastructure is not equally present everywhere

Affected places:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L298)
- [fptable.cpp](../../src/core/fptable.cpp#L11)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L182)

The indirect call infrastructure depends on `__aiml_fptable_current`:

- call site use in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L298) to [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L337)
- emission in [fptable.cpp](../../src/core/fptable.cpp#L11)

Since `emitFptable(ctx)` is only generated in the non-library path, the presence of the hot reload tables depends directly on the mode choice.

### Consequence

String/JIT code that accidentally ends up in a different mode can:

- generate direct calls instead of fptable calls
- have no `__aiml_fptable_current` global
- have different hot reload semantics than the normal build

## 10. Entry point generation is not symmetric between the paths

Affected places:

- [compiler.cpp](../../compiler.cpp#L483)
- [compiler.cpp](../../compiler.cpp#L530)
- [compiler.cpp](../../compiler.cpp#L489)

`compileAndRun(...)` does:

- `compileFromString(...)`
- `generateEntryPoint()`
- `jitRun()`

See [compiler.cpp](../../compiler.cpp#L483) to [compiler.cpp](../../compiler.cpp#L486)

`compileChunkAndGetAddress(...)`, on the other hand, does:

- `compileFromString(...)`
- build the engine immediately
- `finalizeObject()`
- symbol lookup

See [compiler.cpp](../../compiler.cpp#L489) to [compiler.cpp](../../compiler.cpp#L530)

### Consequence

The chunk path never runs through the same entry point logic as the normal JIT run:

- no standardised `main`
- no `__argc`/`__argv` store via `generateEntryPoint()`
- no guaranteed `__aiml_init()` call via the entry point

This is particularly relevant if code implicitly depends on top-level init or CLI globals.

## 11. `aiml_set_active_context(...)` only in the normal JIT run

Affected places:

- [compiler.cpp](../../compiler.cpp#L1657)
- [exec_impl.cpp](../../exec_impl.cpp#L53)

In [compiler.cpp](../../compiler.cpp#L1657) to [compiler.cpp](../../compiler.cpp#L1659), `jitRun()` sets:

- `aiml_set_active_context(compiler_ctx_, engine)`

`compileChunkAndGetAddress()` does not do that.

### Consequence

Everything that works at runtime via the active compiler/engine context can work differently or not at all in the chunk/bundle path.

The comment in [compiler.cpp](../../compiler.cpp#L1656) explicitly names:

- `aiml_exec_code`

That makes the difference directly relevant for `sys.exec`/code object paths.

## 12. Module verification only runs explicitly in the AOT object emit

Affected places:

- [compiler.cpp](../../compiler.cpp#L695)
- [compiler.cpp](../../compiler.cpp#L705)

In [compiler.cpp](../../compiler.cpp#L695) to [compiler.cpp](../../compiler.cpp#L715), `compileToObject(...)` verifies:

- every function
- the entire module
- writes `/tmp/aiml_bad_module.ll` on errors

The normal JIT path does not have this explicit diagnostic to the same extent.

### Consequence

There can be IR that fails diffusely later in the JIT, but stands out early and with better diagnostics in the AOT path.

That is a difference in debuggability and error surface, even though the same source code is compiled.

## 13. The caching/object strategy differs by mode

Affected places:

- [compiler.cpp](../../compiler.cpp#L1739)
- [compiler.cpp](../../compiler.cpp#L1757)
- [compiler.cpp](../../compiler.cpp#L1774)

In [resolveImport](../../compiler.cpp#L1739) to [resolveImport](../../compiler.cpp#L1825):

- `noCache == false`: an internal SHA-based cache in `__aiml_cache`
- `noCache == true`: a build system/OutputDir-oriented object path

`compileFromString(...)`, however, practically does not use this import/cache path at all.

### Consequence

The artefact strategy differs too:

- file/AOT/JIT with imports builds real object files and caches them
- the string/JIT chunk works much more directly on the module and bypasses this stage

That is another reason why import/link bugs can be visible in only one of the two modes.

## 14. `sys.exec` depends hard on the active JIT context

Affected places:

- [exec_impl.cpp](../../exec_impl.cpp#L43)
- [exec_impl.cpp](../../exec_impl.cpp#L303)
- [compiler.cpp](../../compiler.cpp#L1657)

The runtime in [exec_impl.cpp](../../exec_impl.cpp#L43) to [exec_impl.cpp](../../exec_impl.cpp#L108) works with global state:

- `g_active_context`
- `g_active_ee`
- cached fptable globals

This state is only set explicitly in the normal JIT run:

- [compiler.cpp](../../compiler.cpp#L1657)

`compileChunkAndGetAddress()` does not set this active context.

### Consequence

`sys.exec`-related paths are not only "JIT instead of AOT", but additionally depend on whether an active engine/compiler context was set at all.

This particularly affects:

- fptable lookup
- hot reload
- `compileYamlChunk(...)`
- direct engine lookups in [exec_impl.cpp](../../exec_impl.cpp#L455)

## 15. `sys.bundle` depends on source metadata state collected on the compiler side

Affected places:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L886)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1424)

`sys.bundle` uses:

- `FunctionInfo.sourceYaml`
- `FunctionInfo.referencedTypes`
- `ClassInfo.sourceYaml`
- `ClassInfo.referencedTypes`

See:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L431)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L886)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L939)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1365)

Since `compileFromString(...)` sets less context and has less import preparation, it is very plausible that the closure for `sys.bundle` is more incomplete in JIT/string cases than in the file/AOT path.

### Consequence

Even if the same function name is bundled, the content of the bundle can differ:

- missing imported types
- missing transitive references
- missing or different `sourceYaml` assignment

## 16. Isolated `sys.exec` uses yet another compiler path

Affected places:

- [exec_impl.cpp](../../exec_impl.cpp#L225)
- [exec_impl.cpp](../../exec_impl.cpp#L276)
- [compiler.cpp](../../compiler.cpp#L483)

`aiml_isolated_exec(...)` in [exec_impl.cpp](../../exec_impl.cpp#L225) to [exec_impl.cpp](../../exec_impl.cpp#L301) builds new YAML from `code_chunks` and then calls:

- `AIMLCompiler iso;`
- `iso.compileAndRun(yaml_source);`

That means:

- shared-context `sys.exec` uses the active context + fptable + hot reload
- isolated-context `sys.exec` compiles and runs via a fresh compiler with `compileFromString(...)`

### Consequence

In practice there is not only JIT vs AOT, but at least three relevant semantic paths:

- file/AOT
- shared JIT
- isolated JIT via `compileFromString(...)`

## 17. Exception/invoke behaviour depends on the compile-time context and runtime symbols

Affected places:

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L634)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L322)

Exception handling in the compiler uses:

- `ctx.catchBlock`
- `CreateInvoke(...)`
- `__gxx_personality_v0`
- `__cxa_throw`
- `__cxa_allocate_exception`
- `__cxa_begin_catch`
- `__cxa_end_catch`

See:

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L634)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L322)

Additionally:

- the personality function is only set on definition paths, not in header-only, see [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)

### Consequence

Exception semantics are particularly sensitive to path differences:

- header-only / import context
- missing or different personality setups
- JIT symbol availability for C++ runtime functions

That is another area in which JIT and AOT should not automatically be considered equivalent.

## 18. `compileChunkAndGetAddress()` completely bypasses the `jitRun()` initialisation logic

Affected places:

- [compiler.cpp](../../compiler.cpp#L489)
- [compiler.cpp](../../compiler.cpp#L723)

`jitRun()` additionally does:

- library preload from `exodus.json`
- broad `AddSymbol(...)` registration for runtime functions
- import object loading
- `aiml_set_active_context(...)`
- calling `main`

`compileChunkAndGetAddress()` only does:

- `compileFromString(...)`
- create the engine
- `finalizeObject()`
- function lookup

### Consequence

The chunk/bundle/hot reload path is not simply "JIT without main", but a clearly reduced runtime environment.

Many problems there are therefore not local bugs of individual features, but consequences of a fundamentally different execution setup.

## Prioritised problem places

### 1. `compileFromString(...)`

- [compiler.cpp](../../compiler.cpp#L431)

Main problem:

- too little context compared to `compile(...)`

### 2. `compileChunkAndGetAddress(...)`

- [compiler.cpp](../../compiler.cpp#L489)

Main problem:

- uses the shortened string path and does not load the imported objects/runtime symbols equivalently to `jitRun()`

### 3. `resolveImport(...)`

- [compiler.cpp](../../compiler.cpp#L1739)

Main problem:

- this path is central for file/AOT and the normal JIT, but practically outside the string/chunk path

### 4. `libraryMode` / `ast_compiler.cpp`

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L20)

Main problem:

- the mode bit decides on a different module structure, but is not set consistently by all compile paths

### 5. `compileChunkAndGetAddress(...)`

- [compiler.cpp](../../compiler.cpp#L489)

Additional main problem:

- no equivalent entry point, context and object link setup as in the normal JIT run

### 6. `exec_impl.cpp`

- [exec_impl.cpp](../../exec_impl.cpp#L43)

Main problem:

- `sys.exec` semantics depend on the set active engine/compiler context and additionally distinguish shared vs isolated JIT

### 7. Exception handling / personality

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L520)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L871)

Main problem:

- invoke/exception paths are sensitive to header-only, JIT symbol and personality differences

## Most likely real effects

The differences probably already explain observed or plausible errors with:

- `sys.bundle` / `sys.exec`
- hot reload
- imported globals
- `extern` symbols
- imported classes/models that are resolved differently in the JIT
- logging/debugging with a missing `currentSourceFile`
- hot reload/fptable behaviour
- the `sys.exec` / `aiml_exec_code` context
- top-level initialisation via `__aiml_init` vs `__aiml_lib_init_*`
- JIT-only or AOT-only IR errors due to different verification paths
- differences in the `sys.bundle` content closure
- differences in `sys.exec` shared-vs-isolated behaviour
- differences with exceptions, `invoke` and personality functions

## Recommendation

The sustainable direction would be:

1. create a shared IR setup path for file and string
2. extract the import/header/sibling logic from `compile()` into a reusable shared setup
3. bring `compileChunkAndGetAddress()` onto the same symbol/object loading pipeline as `jitRun()`
4. set `libraryMode`, `currentSourceFile`, `resolveImportCallback`, `objectFilesToLink` consistently in both paths

As long as that does not happen, JIT and AOT should not be treated as semantically equivalent.
