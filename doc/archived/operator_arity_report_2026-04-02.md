# AIML Operator Arity Report

Date: 2026-04-02

## Short conclusion

The relevant remaining problem is a binary arity problem.

- `UnaryExpr` is designed strictly single-operand in the AST: [ast_nodes.h](../../src/core/ast_nodes.h#L60)
- `BinaryExpr` is designed strictly two-operand in the AST: [ast_nodes.h](../../src/core/ast_nodes.h#L49)
- The builder only creates unary at `size()==1` and binary only at `size()==2`: [ast_builder.cpp](../../src/core/ast_builder.cpp#L80), [expression_builder.cpp](../../src/core/ast/expression_builder.cpp#L143)
- The compiler treats `||` and `&&` explicitly as binary: [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L580)

Unary-only operators with more than one operand are now already rejected cleanly as a compiler error. For binary the following now applies:

- `+` and `*` may have more than 2 operands and are folded to the left
- all other known binary operators produce a compiler error with more than 2 operands

## Affected operators

### Binary

Operators visibly evaluated as binary:

- arithmetic: `+`, `-`, `*`, `/`, `%`
- comparison: `eq`, `neq`, `lt`, `gt`, `le`, `lte`, `ge`, `gte`, `peq`, `pneq`
- logic: `and`, `&&`, `or`, `||`

Reference: [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L459)

Assessment:

- In the current AST/compiler model these operators are all binary.
- `+` and `*` are now the only deliberately variadic special case.
- All other known binary operators stay strictly 2-ary.
- That prevents silent miscompilation for operators like `eq`, `||`, `lt` or `pneq`.

## Effort per option

### Implemented policy

- Unary-only operators: exactly 1 operand
- `+`, `*`, `add`, `mul`: 2 or more operands, folded to the left
- all other known binary operators: exactly 2 operands

References:

- [expression_builder.cpp](../../src/core/ast/expression_builder.cpp#L23)
- [expression_builder.cpp](../../src/core/ast/expression_builder.cpp#L190)
- [ast_builder.cpp](../../src/core/ast_builder.cpp#L121)

Verified:

- `{ +: [1, 2, 3] }` compiles and yields `6`
- `{ eq: [1, 1, 1] }` now aborts with `Binary operator 'eq' expects exactly 2 operands, got 3.`

## Effort as of today

### Already done

- Unary arity errors
- Binary arity errors for all known operators except `+` and `*`
- Left folding for `+` and `*`

Estimated effort:

- done

### Optional later: further variadic operators

If later `eq`, `&&`, `||` or comparison chains should become variadic as well, that is a deliberate language design step.

Estimated effort:

- about 0.5 to 1.5 days depending on scope and test depth

## Concrete answer to the question

- The unary part is now done: unary-only operators with more than one operand produce a compiler error.
- Binary is now deliberately restricted: only `+` and `*` may have more than 2 operands.
- For all other known binary operators there is now a compiler error at `>2`.
