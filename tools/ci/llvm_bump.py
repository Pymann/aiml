#!/usr/bin/env python3
"""llvm_bump.py — move aiml's exodus configurations to another LLVM major
(TEST_SUITES.md §4.2, the LLVM update workflow of masterv2).

    llvm_bump.py rewrite --major 23 --packages <apt Packages index> exodus*.json
    llvm_bump.py libname --major 23 --cache __exodus_cache exodus*.json
    llvm_bump.py current exodus.json

`rewrite`: every apt package of the LLVM toolchain at the old major
(llvm-18, llvm-18-dev, …, libllvm18, libclang-cpp18 — a name of the LLVM family
whose number is the old major) becomes the same package at the new one, with
the version and the sha256 the Packages index names; every cache path into such
a package follows (`apt/<name>/<arch>/<version>/`), and inside it
`llvm-18` → `llvm-23`, `llvm-c-18` → `llvm-c-23`; the library `LLVM-18` becomes
`LLVM-23`.  Packages of another major (libllvm20, which Mesa needs) are not
touched.  A toolchain package the index does not have is an error naming it,
unless the entry is `required: false` — then it is dropped with a line.

`libname`: after the packages were fetched, `LLVM-<major>` is kept when the dev
package has `libLLVM-<major>.so`, else `LLVM` when it has `libLLVM.so` (the
packaging changed the name over the years).

`current`: prints the toolchain major an exodus.json uses (from llvm-<N>-dev).

The files are written back as json.dump(indent=4) with a trailing newline — the
format they are in, so the diff shows the bump and nothing else.
"""

import argparse
import json
import os
import re
import sys

FAMILY = re.compile(r"^(llvm|libllvm|libclang|clang|lld|liblld|libmlir|mlir|libpolly|libomp|libc\+\+|libunwind)")


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save(path, data):
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps(data, indent=4, ensure_ascii=False) + "\n")


def current_major(configs):
    """From llvm-<N>-dev, else from the library LLVM-<N> (libllvm<N> would be
    ambiguous: Mesa's libllvm20 sits next to the toolchain's)."""
    for path in configs:
        for p in load(path).get("apt_packages", []):
            m = re.fullmatch(r"llvm-(\d+)-dev", p.get("name", ""))
            if m:
                return int(m.group(1))
    for path in configs:
        for lib in load(path).get("linker", {}).get("libraries", []):
            m = re.fullmatch(r"LLVM-(\d+)", lib)
            if m:
                return int(m.group(1))
    return None


def at_major(name, major):
    """`name` with its toolchain number replaced, or None if it is no package
    of the LLVM family at that major."""
    if not FAMILY.match(name):
        return None
    rx = re.compile(r"(?<![0-9])" + str(major) + r"(?![0-9])")
    return rx if rx.search(name) else None


def read_index(path):
    """Debian Packages (deb822): {(name, arch): {"version", "sha256"}} — the
    highest version when a name appears more than once (the index lists the
    newest last, but do not rely on it: compare)."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    for stanza in re.split(r"\n\s*\n", text):
        fields = {}
        key = None
        for line in stanza.splitlines():
            if line[:1] in (" ", "\t") and key:
                continue
            if ":" in line:
                key, _, val = line.partition(":")
                fields[key.strip()] = val.strip()
        name, ver = fields.get("Package"), fields.get("Version")
        if not name or not ver:
            continue
        arch = fields.get("Architecture", "amd64")
        entry = {"version": ver, "sha256": fields.get("SHA256", "")}
        prev = out.get((name, arch))
        if prev is None or version_key(ver) > version_key(prev["version"]):
            out[(name, arch)] = entry
    return out


def version_key(v):
    """Good enough to pick the newer of two builds of one package: epoch, then
    the digit runs in order."""
    epoch, _, rest = v.partition(":") if ":" in v else ("0", "", v)
    return [int(epoch) if epoch.isdigit() else 0] + [int(x) for x in re.findall(r"\d+", rest)]


def rewrite_strings(node, fn):
    if isinstance(node, dict):
        return {k: rewrite_strings(v, fn) for k, v in node.items()}
    if isinstance(node, list):
        return [rewrite_strings(v, fn) for v in node]
    if isinstance(node, str):
        return fn(node)
    return node


def cmd_rewrite(args):
    old = args.from_major or current_major(args.configs)
    if old is None:
        print("no llvm-<N>-dev in the configurations — nothing to move from", file=sys.stderr)
        return 1
    new = args.major
    if old == new:
        print(f"the configurations are on LLVM {old} already")
        return 0
    index = read_index(args.packages)
    problems = []
    # Pass 1, over all configurations: which packages move where.  One config
    # may point into a package another one pins (exodus2.json uses the headers
    # of exodus.json's llvm-18-dev), so the map is shared.
    moved = {}          # "apt/<old>/<arch>/<oldver>/" → "apt/<new>/<arch>/<newver>/"
    plans = {}
    for path in args.configs:
        cfg = load(path)
        kept, changed = [], 0
        for p in cfg.get("apt_packages", []):
            name, arch = p.get("name", ""), p.get("arch", "amd64")
            rx = at_major(name, old)
            if rx is None:
                kept.append(p)
                continue
            new_name = rx.sub(str(new), name)
            hit = index.get((new_name, arch)) or index.get((new_name, "all"))
            if hit is None:
                if p.get("required", True) is False:
                    print(f"  {path}: {name} → {new_name} is not in the index; optional, dropped")
                    continue
                problems.append(f"{path}: {new_name} ({arch}) is not in {args.packages}")
                kept.append(p)
                continue
            moved[f"apt/{name}/{arch}/{p.get('version', '')}/"] = f"apt/{new_name}/{arch}/{hit['version']}/"
            q = dict(p)
            q["name"], q["version"] = new_name, hit["version"]
            if hit["sha256"]:
                q["digest"] = hit["sha256"]
            else:
                q.pop("digest", None)
            kept.append(q)
            changed += 1
        cfg["apt_packages"] = kept
        plans[path] = (cfg, changed)
    if problems:
        for p in problems:
            print("  " + p, file=sys.stderr)
        print(f"{len(problems)} toolchain package(s) missing from the index — nothing written", file=sys.stderr)
        return 1

    def fix(s):
        out = s
        for a, b in moved.items():
            if a not in out:
                continue
            head, _, tail = out.partition(a)
            tail = re.sub(r"(?<![0-9A-Za-z])llvm-c-" + str(old) + r"(?![0-9])", f"llvm-c-{new}", tail)
            tail = re.sub(r"(?<![0-9A-Za-z])llvm-" + str(old) + r"(?![0-9])", f"llvm-{new}", tail)
            out = head + b + tail
        if out == f"LLVM-{old}":
            out = f"LLVM-{new}"
        return out

    # Pass 2: every string of every configuration, then write.
    left = []
    for path, (cfg, changed) in plans.items():
        cfg = rewrite_strings(cfg, fix)
        save(path, cfg)
        text = json.dumps(cfg)
        if re.search(r"apt/(llvm-|libllvm|libclang-cpp)" + str(old) + r"(?![0-9])", text):
            left.append(path)
        print(f"  {path}: {changed} package(s) pinned, LLVM {old} → {new}")
    for path in left:
        print(f"  {path}: still points into an LLVM {old} package no configuration pins", file=sys.stderr)
    return 1 if left else 0


def dev_package(configs, major):
    for path in configs:
        for p in load(path).get("apt_packages", []):
            if p.get("name") == f"llvm-{major}-dev":
                return p
    return None


def cmd_libname(args):
    new = args.major
    dev = dev_package(args.configs, new)
    if dev is None:
        print(f"no configuration pins llvm-{new}-dev", file=sys.stderr)
        return 1
    lib = os.path.join(args.cache, "apt", dev["name"], dev.get("arch", "amd64"), dev["version"],
                       "payload", "usr", "lib", f"llvm-{new}", "lib")
    if os.path.exists(os.path.join(lib, f"libLLVM-{new}.so")):
        name = f"LLVM-{new}"
    elif os.path.exists(os.path.join(lib, "libLLVM.so")):
        name = "LLVM"
    else:
        print(f"neither libLLVM-{new}.so nor libLLVM.so in {lib}", file=sys.stderr)
        return 1
    for path in args.configs:
        cfg = load(path)
        libs = cfg.get("linker", {}).get("libraries", [])
        if not any(l in (f"LLVM-{new}", "LLVM") for l in libs):
            continue
        cfg["linker"]["libraries"] = [name if l in (f"LLVM-{new}", "LLVM") else l for l in libs]
        save(path, cfg)
        print(f"  {path}: links -l{name}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("rewrite")
    r.add_argument("--major", type=int, required=True)
    r.add_argument("--from", dest="from_major", type=int)
    r.add_argument("--packages", required=True)
    r.add_argument("configs", nargs="+")
    ln = sub.add_parser("libname")
    ln.add_argument("--major", type=int, required=True)
    ln.add_argument("--cache", required=True)
    ln.add_argument("configs", nargs="+")
    c = sub.add_parser("current")
    c.add_argument("configs", nargs="+")
    args = ap.parse_args()
    if args.cmd == "rewrite":
        return cmd_rewrite(args)
    if args.cmd == "libname":
        return cmd_libname(args)
    m = current_major(args.configs)
    if m is None:
        return 1
    print(m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
