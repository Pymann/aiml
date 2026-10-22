# AIML module and package management

Date: 2026-10-12
Status: backlog

## Summary

AIML has no notion of a reusable, versioned module. Code is shared by importing
a file path, and `exodus` has no mechanism to depend on another local project.
This became concrete while building the first cross-project module in this
workspace (`test/meshkit`, a generic glTF writer shared by `scanner` and, later,
`shaper`): the only way to consume it is

```yaml
- import: "../../meshkit/src/glb_writer.aiml"
```

That works, but it hard-codes the consumer's position in the filesystem relative
to the module, carries no version, and gives the module no way to state its own
dependencies. What is wanted is closer to what PyPI or Conan provide: a named,
versioned package with a manifest, resolvable by name rather than by relative
path, with a local cache and transitive resolution.

## Current state, measured

### Imports resolve only against the importing file

`spec/21_namespaces.md` §5: "Import Paths: Relative to the directory of the
importing file." The `search_paths` of an exodus config become `-I` flags
(`exodus/tools/build/build.py`, the "Includes" block), but those do not
participate in AIML import resolution — they are C/C++ include paths for the
compiler's own needs. So there is no search-path mechanism an import could use,
and `import: "glb_writer.aiml"` cannot be made to resolve to a module.

### exodus dependencies are a placeholder

`exodus/models/project.py`:

```python
class Dependency(BaseModel):
    name: str
    version: Optional[str] = None
    url: Optional[str] = None
    # Add other dependency fields as needed
```

Nothing consumes this for local projects. `apt_packages`, `conan_packages` and
git-cloned dependencies exist for native libraries, but there is no path for
"this AIML project depends on that AIML project at version X".

### Linking already works, which is the good news

The compiler compiles each imported file into its own object and writes a
`.deps` sidecar that exodus links (`spec/21` §4). An out-of-tree import is
compiled into the consumer's build directory with a path-mangled object name.
So the build machinery does not need to change for modules to work — only
identity, resolution and versioning are missing.

## Sketch: a `package` statement next to `import`

A module needs to state its own identity, and a consumer needs to name it
without knowing where it sits on disk. That is two statements, not one.

Declaring side, top level in the module's files (or once in an entry file that
the others are reached from):

```yaml
- package: { name: meshkit, version: "0.1.0" }
```

Consuming side, naming the package instead of a path:

```yaml
- import: { package: meshkit, version: ">=0.1 <0.2", file: "glb_writer.aiml" }
```

with today's `- import: "types/tri_mesh.aiml"` unchanged for files inside the
same package, because that is the common case and it already works.

### Where a package lives

Resolution order, first hit wins, every step reported in the build log:

1. **Workspace override.** An explicit local path, for the case this workspace
   is in now: developing `meshkit` and `scanner` together and wanting edits in
   one to be seen immediately by the other. Either in the exodus config
   (`packages: {meshkit: {path: "../meshkit"}}`) or a workspace file listing
   sibling packages. Without this, local module development means publishing to
   a cache on every edit, which nobody will do.
2. **Local cache**, keyed by name and version, under the existing
   `__exodus_cache` convention.
3. **Remote registry**, fetched into that cache. Out of scope for a first
   version, but the manifest and the resolution order should not have to change
   when it arrives.

### What the two statements must carry

- `package`: name, version, and the files it exports. Whether non-exported files
  are importable by a consumer is a real decision: hiding them gives the module
  an interface, but AIML has no visibility mechanism across files today, so the
  honest first version is that everything is reachable and the spec says which
  files are the interface.
- `import`: the package name, a version constraint, and the file. The resolved
  concrete version belongs in the build log and in any SBOM output, so a binary
  can be traced back to exact sources.

### Interaction with the compiler

A `package` statement is also the natural place to hang the symbol namespacing
that point 4 below needs, and the natural unit for the file-count rule in point
5: files reached through an `import` with a `package:` key are a dependency's
files, not the consumer's.

## What a design has to settle

1. **Module identity and resolution.** A manifest (name, version, license,
   exported entry files) plus a resolution order: workspace override, local
   cache, remote. Imports would name a module and a path inside it, for example
   `import: "meshkit:glb_writer.aiml"`, keeping today's relative form for
   intra-project files.
2. **Versioning and compatibility.** AIML has no stable ABI story for compiled
   objects across compiler versions; modules are consumed as source, so the
   cache key must include the compiler version, the defines and the target
   architecture, not just the module version.
3. **Transitive dependencies and diamonds.** Two modules pulling different
   versions of a third must either be refused with a located error or resolved
   by a documented rule. Silent duplicate class names across versions would be
   worse than a hard failure, because classgroup instances are wired by name.
4. **Object and symbol naming.** Class and function symbols are currently global
   (`MeshProbe_build`, `ScannerRuntime_photo`). Two versions of one module in a
   single binary would collide. Either forbid that case or namespace symbols by
   module and version.
5. **The three-file rule.** `compiler.cpp` refuses a project that "spans 3 AIML
   files" unless `main` instantiates a class declared in another file. That rule
   is useful against dead weight, but a module-aware build must not count a
   dependency's files as the consumer's, or every small program that uses one
   module trips it.
6. **Where tests live.** A module needs its own test program (`meshkit` has one)
   that the consumer does not compile. Today that is a separate exodus config
   per project, which is fine, but a module manifest should declare it so a
   `exodus test --with-deps` could run a dependency's tests before trusting it.

## Interim convention

Until this exists, cross-project modules live as sibling directories under
`test/` and are consumed by relative import, each with its own spec, its own
exodus test config and no dependencies of its own. `test/meshkit` is the first
one and is deliberately dependency-free so that it cannot be a test case for
transitive resolution.
