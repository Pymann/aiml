#!/usr/bin/env python3
"""select_tests.py — what a CI run has to test, from the files it changes.

    tools/ci/changed_files.sh | python3 tools/ci/select_tests.py >> "$GITHUB_OUTPUT"

Reads the changed paths, one per line (`#full <reason>` and `#base <sha>` lines
come from changed_files.sh), and writes key=value lines:

    scope=full|partial    full: every gate, as before this script existed
    corpus=all|none|<ERE> the argument of run_all_tests.sh --only ("all": no filter)
    valgrind=true|false   the two valgrind gates and the lifetime lab
    aot=true|false        run_aot_tests.sh
    base=<sha>            the commit the diff was taken against ("" when none)
    why=<one line>        the reason, for the job summary

One reason per changed path goes to stderr.  The rules are TEST_SUITES.md
§4.1; a path that no rule names needs everything, so a new kind of file is
tested fully until somebody decides otherwise.  aiml is always built and the
smoke test always runs — that is not this script's business.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The gate scripts name their test files; a changed one needs that gate.
VALGRIND_SCRIPTS = ["tests/run_memory_sanitizers.sh", "tests/run_lifetime_leak_gates.sh"]
AOT_SCRIPTS = ["run_aot_tests.sh"]

# Paths that need everything, whatever else the rules say.
FULL = [
    r"^\.github/",
    r"^tools/ci/",
    r"^run_all_tests\.sh$",
    r"^tools/check_expected_failures\.sh$",
    r"^exodus[^/]*\.json$",
]

# src/core/venv.* and src/core/build.* are `aiml venv` and `aiml build`: what
# they do is tested by these two drivers, and no compiled program runs their code.
TOOL_DRIVERS = {
    r"^src/core/(venv|build)\.(cpp|h)$": [r"^tests/venv/", r"^tests/build/"],
}

NOTHING = [
    r"\.md$",            # spec, doc, README — after TEST_SUITES.md, which is the register
    r"^LICENSE$",
    r"^\.gitignore$",
]


def ere(text):
    """A literal for grep -E (POSIX ERE) — re.escape writes Python's syntax."""
    return re.sub(r"([.\[\]{}()*+?^$|\\])", r"\\\1", text)


def named_in(scripts, path):
    for s in scripts:
        try:
            with open(os.path.join(ROOT, s), encoding="utf-8", errors="replace") as f:
                if path in f.read():
                    return True
        except OSError:
            continue
    return False


def needs(path):
    """(full, corpus, valgrind, aot, reason) for one changed path.
    corpus: None (nothing), "all", or a list of EREs."""
    for rx in FULL:
        if re.search(rx, path):
            return True, "all", True, True, "CI or build configuration"
    for rx, drivers in TOOL_DRIVERS.items():
        if re.search(rx, path):
            return False, drivers, False, False, "aiml venv / aiml build: their drivers"

    valgrind = path in VALGRIND_SCRIPTS or path.startswith("tests/lifetime/") or named_in(VALGRIND_SCRIPTS, path)
    aot = path in AOT_SCRIPTS or named_in(AOT_SCRIPTS, path)
    extra = (" + valgrind" if valgrind else "") + (" + AOT" if aot else "")

    if path in VALGRIND_SCRIPTS or path in AOT_SCRIPTS:
        return False, None, valgrind, aot, "a gate script" + extra
    if path == "TEST_SUITES.md":
        return False, "all", False, False, "the expected-failure register: the whole corpus"
    if path.startswith("tests/"):
        rest = path[len("tests/"):]
        if "/" in rest:
            top = rest.split("/", 1)[0]
            if top.startswith("_"):
                return False, "all", valgrind, aot, "a helper or fixture under tests/_*: the whole corpus" + extra
            return False, ["^" + ere("tests/" + top + "/")], valgrind, aot, "tests/" + top + "/ and its driver" + extra
        if rest.endswith(".aiml"):
            return False, ["^" + ere(path) + "$"], valgrind, aot, "this test" + extra
        return False, "all", valgrind, aot, "a fixture in tests/: the whole corpus" + extra
    for rx in NOTHING:
        if re.search(rx, path):
            return False, None, False, False, "documentation: nothing to test"
    return True, "all", True, True, "no rule names it: everything"


def main():
    full, full_why, base = False, "", ""
    corpus, valgrind, aot = None, False, False
    paths = []
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        if line.startswith("#full"):
            full, full_why = True, line[len("#full"):].strip() or "asked for"
            continue
        if line.startswith("#base"):
            base = line[len("#base"):].strip()
            continue
        paths.append(line)

    for p in paths:
        f, c, v, a, why = needs(p)
        print(f"  {p}: {why}", file=sys.stderr)
        if f and not full:
            full, full_why = True, f"{p} ({why})"
        valgrind |= v
        aot |= a
        if c == "all":
            corpus = "all"
        elif c is not None and corpus != "all":
            corpus = (corpus or []) + [x for x in c if x not in (corpus or [])]

    if full:
        corpus, valgrind, aot = "all", True, True
        why = "everything: " + full_why
    else:
        parts = []
        if corpus == "all":
            parts.append("the whole corpus")
        elif corpus:
            parts.append(f"{len(corpus)} corpus pattern(s)")
        if valgrind:
            parts.append("the valgrind gates")
        if aot:
            parts.append("AOT")
        why = f"{len(paths)} changed file(s): " + (", ".join(parts) if parts else "build, smoke test and packaging only")

    if corpus is None:
        corpus_out = "none"
    elif corpus == "all":
        corpus_out = "all"
    else:
        corpus_out = "|".join(corpus)
    print(f"scope={'full' if full else 'partial'}")
    print(f"corpus={corpus_out}")
    print(f"valgrind={'true' if valgrind else 'false'}")
    print(f"aot={'true' if aot else 'false'}")
    print(f"base={base}")
    print(f"why={why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
