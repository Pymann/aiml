# Loops

## while

### Canonical Form

```yaml
- set: [counter, 0]
- set: [limit, 5]

- while:
    condition: { lt: [counter, limit] }
    body:
      - print: [counter]
      - set: [counter, { +: [counter, 1] }]

- print: [counter]  # 5
```

### Short Form

```yaml
- while: [{ lt: [counter, limit] }]
  body:
    - print: [counter]
    - set: [counter, { +: [counter, 1] }]
```

The first (and only) list element is the condition. Both forms are equivalent.
