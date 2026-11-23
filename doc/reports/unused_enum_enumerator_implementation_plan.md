# Implementation plan: detecting unused enumerators in AIML

## Goal

The static AIML analysis should produce a non-blocking compiler diagnostic when enum members are declared, but never used within the current build.

The output should appear as a compiler diagnostic **just like other static problems**, not as a runtime `log.*` call.

Example direction:

```text
models/enums.aiml:12: warning: enum member 'PassiveAbility.armored' is never referenced in this build
```

## Desired semantics for V1

### What counts as a use

In the first stage only **explicit symbolic references** to enum members count, in particular:

- `EnumName.Member`
- fully qualified enum accesses, as far as they go through the same resolver

### What does not yet count as a use in V1

These cases should **not** be counted as a safe use at first:

- raw integer values that happen to correspond to an enum member
- implicit use only via persistence/network data
- pure enum type use without reference to concrete members
- UI bindings such as `enum: SomeEnum` on dropdowns or radios

This is deliberately conservative: V1 only reports what the compiler sees with high certainty as a **concrete member use**.

## Why this fits the existing architecture

The existing compiler/static analysis already has a very similar pattern for **whole-program** checks:

- prefab declarations and prefab uses are collected in
  [context.h](../../src/core/context.h#L299)
- the analysis runs in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L269)
- the function is called both for entry and for `libraryMode` modules in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L968)
  and
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1207)

Enum resolution also already exists centrally:

- enum registry in
  [context.h](../../src/core/context.h#L262)
- enum registration in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1922)
- explicit enum reference resolution in
  [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L301)
  and
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L2911)

## Implementation strategy

### Phase 1: capture declared enum members

#### Goal

For every enum, the compiler should register all declared members including their source location.

#### Affected places

- [ast_nodes.h](../../src/core/ast_nodes.h#L359)
- [context.h](../../src/core/context.h#L262)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1922)

#### Concrete changes

1. Create new structures in `CompilerContext`, analogous to `prefabDecls` / `prefabUses`.

Proposal:

```cpp
std::map<std::string, std::set<std::string>> enumDeclMembers;
std::map<std::string, std::set<std::string>> enumUsedMembers;
std::map<std::string, YAML::Node> enumDeclNodes;
std::map<std::string, std::map<std::string, YAML::Node>> enumMemberDeclNodes;
```

Minimally sufficient for V1:

```cpp
std::map<std::string, std::set<std::string>> enumDeclMembers;
std::map<std::string, std::set<std::string>> enumUsedMembers;
std::map<std::string, std::map<std::string, YAML::Node>> enumMemberDeclNodes;
```

2. When registering an `EnumDecl` in `visitTopLevel(...)`, additionally enter all `values` into `enumDeclMembers[enumName]`.

3. In doing so, also store the best possible source location for every member.

#### Important note

`EnumDecl::values` currently stores only `name -> int`, not the source location per individual member
([ast_nodes.h](../../src/core/ast_nodes.h#L362)).

Therefore there are two options:

- **V1 pragmatic:** emit the warning at the enum block position
- **V1.5 cleaner:** extend the AST for enum members with member-specific `YAML::Node` marks

Recommendation:

- pragmatically with the enum block position first
- retrofit the per-member source location later

## Phase 2: capture used enum members

#### Goal

Every explicit symbolic reference such as `GamePhase.combat` should be marked as a use.

#### Affected places

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L301)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L2911)

#### Concrete changes

1. Build a hook directly before or in `resolveEnum(baseName, propName)`:

```cpp
ctx.enumUsedMembers[resolvedEnumName].insert(propName);
```

2. If namespaces / qualification play a role, the **canonical enum ID** must be used, not just `baseName`.

That is why the best place is **inside** `resolveEnum(...)`, as soon as it is certain which enum was actually resolved.

3. If `resolveEnum(...)` is used both for global and namespace-local enums, the storage must use the same qualification logic as `ctx.enums`.

#### Why this place is good

- only real, successfully resolved enum members are counted
- no duplicate AST walk logic needed
- low false positive risk

## Phase 3: whole-program merge as for dead-prefab

#### Goal

The member uses from imported/libraryMode modules should flow back into the top-level compile.

#### Affected places

The exact merge places for `allPrefabUses` must be mirrored in the compiler driver/import pipeline. The plan explicitly follows the already existing `dead-prefab` path, which is referenced in the comments of `CompilerContext`
([context.h](../../src/core/context.h#L303)).

#### Concrete changes

1. Create new aggregate structures in the parent compiler/driver, analogous to:

- `allPrefabDecls`
- `allPrefabUses`

2. When merging a sub-compiler back:

- unite the declared enum members
- unite the used enum members

3. The merge key must be the fully qualified enum name.

#### Risk

If these merges do not happen cleanly along the import chain, false positives arise for modules that import enums whose use only takes place in sibling modules.

## Phase 4: emit the top-level diagnostic

#### Goal

At the end of the top-level build, exactly one warning should be emitted for every declared but never used enum member.

#### Affected places

- the same top-level post-pass that already emits whole-program warnings such as `dead-prefab`
- the diagnostic format via `fmtLoc(...)`-like paths from
  [context.h](../../src/core/context.h#L377)

#### Concrete changes

1. Compute the set difference:

```cpp
unused = enumDeclMembers[enumName] - enumUsedMembers[enumName]
```

2. Emit a warning for every `unused` member.

Proposal:

```text
<loc>: enum member '<Enum>.<Member>' is never referenced in this build
```

3. The diagnostic should only appear in the **complete build context**, analogous to the existing whole-program gating idea for `dead-prefab`.

#### Recommendation for gating

Only emit the warning if the use set can be considered complete:

- a real build without a warm import shortcut
- or a single file without imports

That keeps the behaviour consistent with
[44_8_prefab_static.md](../../spec/44_8_prefab_static.md#L131).

## Phase 5: extend the spec

#### Affected files

- [49_static_analysis.md](../../spec/49_static_analysis.md#L449)
- optionally a new dedicated spec file, if the rule is to be described at greater length

#### Content

The spec should record:

- that unused enumerators are a warning
- that it is a whole-program check
- that the statement `in this build` is deliberately build-specific
- which kinds of use count in V1
- which are deliberately not counted yet

## Phase 6: tests

### Test group A: basic cases

Create new AIML tests, for example under `tests/`:

- an enum with 3 values, 1 of them used -> 2 warnings
- an enum whose values are all used -> no warning
- two enums, only one partially used -> warnings only for that one

### Test group B: import cases

- enum defined in an imported file, used in the entry
- enum defined in the entry, used in an imported file
- enum defined in an imported file and only used in another imported file

### Test group C: non-counting cases for V1

- enum type present as a field type, but no member referenced
- widget with `enum: SomeEnum`, but no `SomeEnum.member`
- integer value `3` in place of an enum member

These tests should document that such cases do **not yet** count as a use.

## Recommended implementation order

1. data structures for `enumDeclMembers` / `enumUsedMembers` in `CompilerContext`
2. member capture at enum registration
3. use capture in `resolveEnum(...)`
4. merge in the import/sub-compiler pipeline
5. top-level warning output
6. tests
7. spec update

## Effort per sub-package

### Package 1: V1 core

- collect declared and used enum members
- emit the top-level warning
- 2 to 4 small tests

Estimate:

- about 0.75 to 1.25 days

### Package 2: clean whole-program expansion

- make merge/gating robust
- secure the import cases
- update the spec cleanly

Estimate:

- an additional 0.5 to 1 day

### Package 3: member-exact source location

- extend the AST/builder for per-member source locations

Estimate:

- an additional 0.25 to 0.5 days

## Future: warning for serialize/deserialize mismatch with enums

## Problem

Later there should be a warning when enum values are serialised, but are **not correctly treated as an enum again** on deserialisation.

This is relevant because enums are currently treated like integers on the serialisation side:

- serialisation of enums as integers in
  [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1102)
- `sys.deserialize` knows the target type via `deserializeTargetType` in
  [context.h](../../src/core/context.h#L451)
- deserialisation path in
  [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1150)

## Goal of the future check

Produce a warning for patterns such as:

- enum-typed data is serialised
- but later deserialised back into `i32`, `string` or a structurally wrong target

Example direction:

```text
state.aiml:44: warning: serialized enum value from 'GamePhase' is deserialized into 'i32' instead of 'GamePhase'
```

## Recommended V2 strategy

### 1. Diagnostic at compiler level, not at runtime

The check should use the same diagnostic class as other static analysis cases, not `log.warn`.

### 2. Tracking points

When compiling `sys.serialize(...)`, the following should be recorded:

- source type `typeName`
- if `ctx.enums.count(typeName)` holds: it is an enum serialisation
- ideally the source location of the serialisation

When compiling `sys.deserialize(...)`, the following should be recorded:

- target type `typeName`
- source location of the deserialisation

### 3. Mismatch rules

Warning if:

- source enum, target non-enum
- source enum `A`, target enum `B`

No warning if:

- source enum `A`, target enum `A`

### 4. Difficulty

This is considerably harder than `unused enumerator`, because serialize/deserialize do not always occur locally as a pair in the same expression.

It needs either:

- a local heuristic for direct patterns
  `set: [x, { call: [sys.deserialize, { call: [sys.serialize, y] }, T] }]`
- or later a data flow analysis over temporal values

## Concrete V2 plan for a first heuristic check

### Affected places

- [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1098)
- [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1150)
- call dispatch/statement compile paths in which `sys.deserialize` target types are known

### Heuristic V2 rule

At first only check direct AST patterns:

- `sys.deserialize(sys.serialize(expr), TargetType)`
- `decl/set` with a clearly recognisable enum-typed origin and an unsuitable target type

### Expected diagnostic

Non-blocking warning:

```text
...: serialized enum value from 'Status' is deserialized as 'i32'; prefer deserializing back into 'Status'
```

## Recommendation

The two topics should be implemented separately:

1. **now**: `unused enumerator` as a simple, robust whole-program check
2. **later**: serialize/deserialize mismatch as a heuristic check of its own

That keeps the first delivery small, clear and low-risk.

## Conclusion

Detecting unused enumerators is architecturally a very good fit for the existing AIML static analysis. The cleanest V1 is:

- use capture in `resolveEnum(...)`
- decl capture at enum registration
- whole-program evaluation analogous to `dead-prefab`
- output as a compiler warning with the build context `in this build`

The additional serialize/deserialize check makes sense too, but should be planned as a second feature of its own, because it goes beyond simple symbol collection and needs at least a small data flow or pattern analysis.
