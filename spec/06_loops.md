# Loops

## while

### Kanonische Form

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

### Kurzform

```yaml
- while: [{ lt: [counter, limit] }]
  body:
    - print: [counter]
    - set: [counter, { +: [counter, 1] }]
```

Das erste (und einzige) Listenelement ist die Condition. Beide Formen sind äquivalent.
