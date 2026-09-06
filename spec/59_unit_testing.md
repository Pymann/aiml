# 59 - Auto-Generated Unit-Test Stubs [PROPOSAL]

## 1. Goal

AIML unit tests belong to the function they exercise. A `def` with a `test:` field does not declare its dependencies manually. The compiler finds every direct function call in that `def` and creates all required stubs automatically for the generated unit-test harness.

The test author describes only inputs, configured stub returns, and expected results. There is no `stub:` field and no separate stub declaration syntax.

## 2. Function Syntax

```yaml
- def:
    name: <function_name>
    args: [[<arg_name>, <type>], ...]
    return: <type>                 # optional; defaults to void
    test: [<test_case>, ...]       # optional; defaults to []
    body: [...]
```

A `def` without `test:` is compiled normally. For each test case of a `def` with `test:`, the compiler invokes an instrumented test version of the function. Every direct call made by that function is routed to its generated stub; the normal production call graph remains unchanged.

## 3. Automatic Stub Discovery

The compiler walks the body of the tested function and assigns an identifier to each direct call site in source order:

```yaml
- def:
    name: price_with_tax
    args: [[sku, string], [customer_id, i32]]
    return: i32
    test: [...]
    body:
      - decl: [net, i32, { call: [catalog.price, sku] }] # catalog.price#1
      - decl: [gross, i32, { +: [net, 19] }]
      - call: [audit.write, customer_id, sku, gross]     # audit.write#1
      - return: gross
```

The identifier is `<function>#<occurrence>`. If the same function appears twice in the body, both call sites get separate stubs, for example `catalog.price#1` and `catalog.price#2`.

A call site executed repeatedly at runtime, for example in a loop, is still one stub. It records its total call count and the arguments of its most recent call.

## 4. Test-Case Syntax

```yaml
- def:
    name: price_with_tax
    args: [[sku, string], [customer_id, i32]]
    return: i32
    test:
      - name: regular_customer
        args: ["book-7", 42]
        set:
          catalog.price#1:
            return: 100
        expect:
          return: 119
          catalog.price#1:
            calls: 1
            args: ["book-7"]
          audit.write#1:
            calls: 1
            args: [42, "book-7", 119]

      - name: missing_price
        args: ["missing", 42]
        set:
          catalog.price#1:
            return: 0
        expect:
          return: 0
          catalog.price#1:
            calls: 1
            args: ["missing"]
          audit.write#1:
            calls: 0
    body:
      - decl: [net, i32, { call: [catalog.price, sku] }]
      - if:
          condition: { eq: [net, 0] }
          then: [{ return: 0 }]
      - decl: [gross, i32, { +: [net, 19] }]
      - call: [audit.write, customer_id, sku, gross]
      - return: gross
```

`args` supplies positional input for the function under test. `set` configures generated return variables. `expect.return` checks the return value of the function under test, and every other `expect` entry checks a generated call site.

- `calls` is the exact expected call count.
- `args` compares the arguments of the last call at that call site.
- A non-`void` call site may receive `return: <value>`.
- A `void` call site has no return variable.
- An omitted call-site entry creates no assertion for that call site.

## 5. Generated State

For every call site, the compiler generates private test-harness variables. For `catalog.price#1(sku: string) -> i32`, the conceptual variables are:

| Purpose | Conceptual generated variable |
|---|---|
| Configure the result | `__aiml_test_price_with_tax_catalog_price_1_return` |
| Count calls | `__aiml_test_price_with_tax_catalog_price_1_calls` |
| Inspect the last `sku` | `__aiml_test_price_with_tax_catalog_price_1_arg_sku` |

The generated stub increments `calls`, copies every parameter into its corresponding argument variable, and returns its current return variable. Before each test case, all generated state is reset to the type default; then `set`, the tested call, and `expect` run in that order.

Generated names are compiler-reserved. Test source refers to stable call-site identifiers such as `catalog.price#1`, never to implementation variable names.

## 6. Validation and Diagnostics

The compiler reports a test-definition error when:

- `set` or `expect` references an unknown call-site identifier;
- a return is configured for a `void` call site;
- a configured return or expected argument has an incompatible type;
- an expected argument list has the wrong count;
- `calls: 0` is combined with `args`;
- test-case names repeat within the same function.

A failed assertion reports the function, test-case name, call-site identifier, and expected and observed values.

```text
FAIL price_with_tax.regular_customer: audit.write#1 argument 3 expected 119, got 0
```

## 7. Status

This is a language-design proposal. It specifies automatic stub discovery and test syntax; parser, AST, compiler, and test-runner support are not part of this chapter.
