#!/usr/bin/env python3
"""Replace deprecated operators (==, !=, <>) with their canonical forms (eq, neq)
in all .aiml files under the current directory."""

import re
import sys
from pathlib import Path

# Patterns to replace:
#   { "==": [...] }  →  { eq: [...] }
#   { "!=": [...] }  →  { neq: [...] }
#   { "<>": [...] }  →  { neq: [...] }
# Also handles without quotes where yaml-cpp might parse them.

REPLACEMENTS = [
    # Quoted forms: { "==": ... }
    (re.compile(r'\{\s*"=="\s*:'), '{ eq:'),
    (re.compile(r'\{\s*"!="\s*:'), '{ neq:'),
    (re.compile(r'\{\s*"<>"\s*:'), '{ neq:'),
    # Unquoted forms: { ==: ... }
    (re.compile(r'\{\s*==\s*:'), '{ eq:'),
    (re.compile(r'\{\s*!=\s*:'), '{ neq:'),
    (re.compile(r'\{\s*<>\s*:'), '{ neq:'),
    # "==" as a standalone quoted value (rare but possible)
    (re.compile(r'"=="'), 'eq'),
    (re.compile(r'"!="'), 'neq'),
    (re.compile(r'"<>"'), 'neq'),
]


def fix_file(path: Path) -> int:
    text = path.read_text()
    new_text = text
    for pattern, replacement in REPLACEMENTS:
        new_text = pattern.sub(replacement, new_text)
    if new_text != text:
        path.write_text(new_text)
        return 1
    return 0


def main():
    root = Path(".") if len(sys.argv) < 2 else Path(sys.argv[1])
    files = sorted(root.rglob("*.aiml"))
    changed = 0
    for f in files:
        n = fix_file(f)
        if n:
            print(f"  fixed: {f}")
            changed += 1
    print(f"\n{changed}/{len(files)} files changed.")


if __name__ == "__main__":
    main()
