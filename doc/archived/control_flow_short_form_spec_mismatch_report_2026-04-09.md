# AIML Control-Flow Short Form Spec Mismatch Report

Datum: 2026-04-09
Status: **GESCHLOSSEN** — beide Befunde behoben

## Behobene Befunde

### Befund 1: ControlFlowBuilder Kurzform (`if: [cond, true]`)

**Fix**: `receiveChildNode` behandelt `currentKey.empty()` korrekt; `OnNull` leitet null-Literale weiter; `OnSequenceEnd` setzt `expectingKey` zurück.

Kurzform `if: [{ ... }, true]` und `while: [{ ... }]` sind damit stabil implementiert und spec-konform.

### Befund 2: `map.contains(...)` fehlender Pointer-bitcast

**Fix** in `src/core/ast_compiler_call.cpp`:

```cpp
} else if (methodName == "contains" && argVals.size() > 0) {
    llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
    if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
        keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
    }
    return ctx.ir_gen->wrap(MapImpl::contains(ctx, ..., keyVal, mapStructTy));
```

`contains` ist damit konsistent zu `put/get/remove`.

**Regressionstest**: `tests/test_map_contains.aiml` — deckt Szenarien A–G ab, inklusive der report-spezifischen Kurzformen:

```aiml
- if: [{ call: [m.contains, "k"] }, true]
- if: [{ eq: [{ call: [m.contains, "k"] }, 1] }, true]
```

## Offene Punkte

Keine. Alle Befunde behoben, Spec aktualisiert.
