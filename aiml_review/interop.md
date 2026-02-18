# Interoperability Strategy

How AIML talks to the world (C++, Python, Rust, Java, etc.).

## The Golden Rule: C-ABI is King
Almost every language on earth can talk to **C**.
Therefore, AIML targets full **C-ABI Compatibility**.

## 1. Talking to C++
**Problem:** C++ has "Name Mangling" and complex Classes/Templates that are hard to bind to.
**Solution:** Write a thin `extern "C"` wrapper in C++.
```cpp
// math_wrapper.cpp
extern "C" int MyClass_Add(MyClass* c, int a) { return c->add(a); }
```
**AIML:**
```yaml
- extern: "C"
  defs:
    - def: { name: MyClass_Add, ... }
```

## 2. Talking to Python
**Strategy:** Use Python's C-API via FFI.
We can load `libpython.so` and call functions like `PyRun_SimpleString`.
*Future Goal:* Generate Python Bindings (ctypes) automatically from AIML structs.

## 3. Talking to Rust
Rust and AIML are friends. Both speak C-ABI fluently.
**Rust:** `#[no_mangle] public extern "C" fn foo() { ... }`
**AIML:** Just call `foo`.

## 4. Talking to Java / C#
Use their native FFI mechanisms (JNI / P/Invoke) to call AIML functions compiled as a Shared Library (`.so` / `.dll`).
**Requirement:** AIML must be able to compile to a **Shared Library** (`-shared`).

## Summary
We don't need specific features for every language.
We strictly implement:
1.  **C-Compatible Struct Layouts** (`packed`, standard alignment).
2.  **C-Calling Convention** (System V AMD64 ABI).
3.  **Shared Library Export** (creating `.so`/`.dll` files).
