# LLVM 18 Compatibility Fixes

This document records the changes made to ensure compatibility with LLVM 18 (and Opaque Pointers).

## 1. Opaque Pointers & `getPointerElementType()` Removal

### Problem
In LLVM 18, `getPointerElementType()` is removed because pointers are opaque (`ptr`) and do not carry type information about what they point to.
The following code caused compilation errors:
```cpp
llvm::Type* structType = listPtr->getType()->getPointerElementType(); // ERROR
```

### Solution
We must track or reconstruct the element type explicitly.
For `ListImpl`, we introduced a helper function `getListStructType(CompilerContext& ctx)` that returns the expected structure layout `{i32, i32, ptr}`.

```cpp
// Old
llvm::Type* structType = listPtr->getType()->getPointerElementType();

// New
llvm::StructType* structType = getListStructType(ctx);
```

Where `getListStructType` creates or retrieves:
```cpp
llvm::StructType::create(ctx.context, {
    ctx.builder.getInt32Ty(), // size
    ctx.builder.getInt32Ty(), // capacity
    llvm::PointerType::get(ctx.context, 0) // data (opaque ptr)
}, "GenericList");
```

## 2. `Module::getTypeByName` Deprecation

### Problem
`module.getTypeByName("Name")` is no longer the standard way to retrieve struct types in newer LLVM C++ APIs (or was causing issues).

### Solution
Use the static method on `llvm::StructType`:

```cpp
// Old
llvm::StructType* listType = module.getTypeByName(structName);

// New
llvm::StructType* listType = llvm::StructType::getTypeByName(ctx.context, structName);
```

## 3. Strict Scope & Declarations

### Problem
Functions like `getFunction` were declared in `utils.h` but used in headers included *before* `utils.h` (circular dependency or include order issues), leading to "not declared in this scope" errors.

### Solution
Moved the `getFunction` helper directly into the `CompilerContext` class in `context.h`, making it globally available via the context object.

```cpp
// usage
ctx.getFunction("malloc", ...);
```
