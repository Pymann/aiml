# AIML Control-Flow Short Form Spec Mismatch Report

Date: 2026-04-09
Status: **CLOSED** — both findings fixed

## Fixed findings

### Finding 1: ControlFlowBuilder short form (`if: [cond, true]`)

**Fix**: `receiveChildNode` handles `currentKey.empty()` correctly; `OnNull` forwards null literals; `OnSequenceEnd` resets `expectingKey`.

The short forms `if: [{ ... }, true]` and `while: [{ ... }]` are thus implemented stably and conform to the spec.

### Finding 2: `map.contains(...)` missing pointer bitcast

**Fix** in `src/core/ast_compiler_call.cpp`:

```cpp
} else if (methodName == "contains" && argVals.size() > 0) {
    llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
    if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
        keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
    }
    return ctx.ir_gen->wrap(MapImpl::contains(ctx, ..., keyVal, mapStructTy));
```

`contains` is thus consistent with `put/get/remove`.

**Regression test**: `tests/test_map_contains.aiml` — covers scenarios A–G, including the report-specific short forms:

```aiml
- if: [{ call: [m.contains, "k"] }, true]
- if: [{ eq: [{ call: [m.contains, "k"] }, 1] }, true]
```

## Open items

None. All findings fixed, spec updated.
