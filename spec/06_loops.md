# Loops Proposal

```yaml
- set: [counter, 0]
- set: [limit, 5]

# While loop
- while:
    condition: [lt, counter, limit]
    body:
      - print: [counter]
      - add: [counter, 1]

- print: [counter] # Should be 5
```

# While loop
- while:
    condition: [lt, counter, limit]
    body:
      - print: [counter]
      - add: [counter, 1]

- print: [counter] # Should be 5
