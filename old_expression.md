# Old Expression Path

## Summary

The AIML project currently has two expression-related implementation layers:

- the active AST-based path
- an older YAML-expression evaluator path

The active path is the one used for normal compilation of AIML code.
The older path appears to be effectively unused in normal compilation.

## Active Path

The productive expression pipeline is:

1. parse YAML into AST expression nodes
2. compile those AST nodes via `visitExpression(...)`

Relevant files:

- `src/core/ast_builder.cpp`
- `src/core/ast/expression_builder.cpp`
- `src/core/ast_nodes.h`
- `src/core/ast_compiler_expr.cpp`

Key evidence:

- `nodeToExpr(...)` in `src/core/ast_builder.cpp` builds `UnaryExpr` and `BinaryExpr`
- `Compiler::visitExpression(...)` in `src/core/ast_compiler_expr.cpp` dispatches to:
  - `visitBinary(...)`
  - `visitUnary(...)`
- statement compilation uses `visitExpression(...)`, not the old evaluator

## Old Path

The old expression evaluator is:

- `src/core/expression.cpp`

This file still contains a YAML-node based evaluator with:

- `Expression::evaluate(...)`
- `evaluateMap(...)`
- `handleBinary(...)`
- `handleCast(...)`
- address-of / dereference helpers

However, this path appears to be legacy.

## Why It Looks Unused

Search results show no real call sites for `Expression::evaluate(...)` outside `src/core/expression.cpp` itself.

By contrast, the AST compiler is used broadly:

- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_call.cpp`
- `src/core/ast_compiler.cpp`

These files consistently use:

- `visitExpression(...)`

not the old YAML evaluator.

So the practical compiler path is:

- AST build
- AST compile

not:

- YAML node evaluation through `expression.cpp`

## Unary And Binary Operators

The real operator handling in the active path is AST-based.

AST node types:

- `BinaryExpr`
- `UnaryExpr`

Defined in:

- `src/core/ast_nodes.h`

Constructed in:

- `src/core/ast_builder.cpp`
- `src/core/ast/expression_builder.cpp`

Compiled in:

- `src/core/ast_compiler_expr.cpp`

## Legacy Fallback Inside ExpressionBuilder

There is also a smaller legacy-looking fallback in:

- `src/core/ast/expression_builder.cpp`

Relevant logic:

- normal operator form sets `hadSequence = true` for `{ op: [args] }`
- later, `finish()` first handles the normal sequence-based path
- after dict-literal handling, it still contains a fallback:
  - if `args.size() == 2` -> build `BinaryExpr`
  - if `args.size() == 1` -> build `UnaryExpr`

This fallback is marked in code as:

- `Unary/binary fallback for args that didn't come via sequence`

That strongly suggests an older compatibility branch.

## Why That Fallback Also Looks Practically Unused

The current AIML syntax in specs, tests, and 9rooms code is overwhelmingly:

- `{ op: [args] }`

Examples:

- `{ +: [a, b] }`
- `{ -: [x, y] }`
- `{ eq: [a, b] }`
- `{ call: [fn, ...] }`

That means the normal path sets `hadSequence = true`, so the fallback branch should usually not be needed.

## About `{ binary: [lhs, op, rhs] }`

There are a few spec references to a wrapper form such as:

- `{ binary: [a, ==, b] }`

But in the current codebase scan:

- this form appears in documentation/spec files
- it does not appear as a normal implementation pattern in `tests/` or `9rooms/`
- there is no dedicated compiler branch like `op == "binary"` or `op == "unary"`

So this wrapper form looks non-productive or incomplete relative to the active compiler path.

## Practical Conclusion

The effective situation is:

- the real expression pipeline is AST-based
- `src/core/expression.cpp` appears to be an old expression path that is practically not used
- `ExpressionBuilder` still contains a smaller legacy unary/binary fallback branch that also appears to be rarely or never reached in normal `{ op: [args] }` code

## Safe Refactoring Direction

Before removal, validate:

- whether any non-AST tooling still calls `Expression::evaluate(...)`
- whether any YAML event edge case still relies on the `ExpressionBuilder` fallback

But based on the current code scan, the old expression path is very likely removable or at least should be clearly marked deprecated.
