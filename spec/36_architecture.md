# AIML Compiler Architecture (Command Pattern)

To ensure modularity and extensibility, each AIML instruction is encapsulated in its own class.
The Compiler traverses the YAML, looks up the corresponding Command Class, and instantiates/executes it.

## 1. Core Abstractions

### `AIMLCommand` (Abstract Base Class)
- **Responsibility**: Represents a single executable instruction or definition.
- **Interface**:
    - `virtual void compile(CompilerContext& ctx, const YAML::Node& node) = 0;`

### `CommandRegistry`
- **Responsibility**: Maps string keys (e.g. "set", "decl") to `AIMLCommand` factories.
- **Behavior**:
    - `getCommand("set")` -> returns `SetCommand` instance.
    - If key not found -> **Compiler Error**: "Unknown command 'foo'".

## 2. Command Implementations

Each supported YAML key gets a class:

- **`DeclCommand`**: Handles `decl: [...]`. Creates `AllocaInst`.
- **`SetCommand`**: Handles `set: [...]`. Generates `StoreInst`.
- **`PrintCommand`**: Handles `print: [...]`. Generates `call printf`.
- **`IfCommand`**: Handles `if: ...`. Manages BasicBlocks.
- **`FuncCommand`**: Handles `def: ...`. Creates `Function`.

## 3. Compiler Loop (`compiler.cpp`)

Refactored to be generic:

```cpp
for (const auto& node : yamlNodes) {
    if (node.IsMap()) {
        std::string commandName = node.begin()->first.as<string>();
        auto cmd = registry.create(commandName);
        if (!cmd) {
            LOG_ERROR("Unknown command: " + commandName);
            return;
        }
        cmd->compile(context, node);
    }
}
```

## 4. Directory Structure
```
src/
  commands/
    command.h        (Base Class)
    decl_command.cpp
    set_command.cpp
    print_command.cpp
    registry.cpp     (Factory)
  core/
    context.h        (LLVM Context, Builder, SymbolTable)
    compiler.cpp     (Main Loop)
```
