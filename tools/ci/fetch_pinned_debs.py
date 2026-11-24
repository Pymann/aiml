#!/usr/bin/env python3
"""Fetch every pinned apt package of the given Exodus configs into __exodus_cache/.

`exodus pkg install` downloads with `apt-get download`, which only finds versions
that are still in the mirror index. Ubuntu drops superseded point releases from
the index, so older pins stop resolving. This script fills the cache first:

1. `apt-get download name:arch=version` (fast, from the mirror),
2. otherwise the permanent Launchpad copy
   https://launchpad.net/ubuntu/+archive/primary/+files/<name>_<version>_<arch>.deb

Every .deb is checked against the sha256 digest pinned in the config; a mismatch
is an error. The cache layout is the one Exodus uses
(apt/<name>/<arch>/<version>/{_download,payload}), so a later
`exodus pkg install` finds the packages and only does its path/symlink work.

Usage: fetch_pinned_debs.py exodus.json [exodus2.json ...]
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

LP = "https://launchpad.net/ubuntu/+archive/primary/+files/"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def from_launchpad(name: str, arch: str, version: str, dest: Path) -> bool:
    bare = version.split(":", 1)[1] if ":" in version else version
    for a in dict.fromkeys([arch, "all"]):
        url = f"{LP}{name}_{bare}_{a}.deb"
        for attempt in range(3):
            try:
                with urllib.request.urlopen(url, timeout=120) as r:
                    data = r.read()
                (dest / f"{name}_{bare}_{a}.deb").write_bytes(data)
                return True
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    break
                time.sleep(2 + attempt * 3)
            except OSError:
                time.sleep(2 + attempt * 3)
    return False


def fetch(cache: Path, name: str, arch: str, version: str, digest: str) -> str:
    cdir = cache / "apt" / name / arch / version
    dl, payload = cdir / "_download", cdir / "payload"
    debs = sorted(dl.glob("*.deb")) if dl.exists() else []
    if payload.exists() and debs and (not digest or sha256(debs[0]) == digest):
        if not digest:
            print(f"::notice title=No pinned digest::{name}={version} sha256={sha256(debs[0])}")
        return "cached"
    if cdir.exists():
        shutil.rmtree(cdir)
    dl.mkdir(parents=True)
    src = "apt"
    rc = subprocess.run(["apt-get", "download", f"{name}:{arch}={version}"],
                        cwd=dl, capture_output=True, text=True).returncode
    if rc != 0 or not list(dl.glob("*.deb")):
        src = "launchpad"
        if not from_launchpad(name, arch, version, dl):
            raise RuntimeError(f"{name}:{arch}={version}: not on the mirror and not on Launchpad")
    deb = sorted(dl.glob("*.deb"))[0]
    got = sha256(deb)
    if digest and got != digest:
        shutil.rmtree(cdir)
        raise RuntimeError(f"{name}={version}: sha256 {got} != pinned {digest}")
    if not digest:
        print(f"::notice title=No pinned digest::{name}={version} sha256={got}")
    payload.mkdir()
    subprocess.run(["dpkg-deb", "-x", str(deb), str(payload)], check=True)
    return src


def main(configs: list[str]) -> int:
    cache = Path(os.environ.get("EXODUS_CACHE", "__exodus_cache")).resolve()
    pins = {}
    for c in configs:
        for p in json.loads(Path(c).read_text()).get("apt_packages", []):
            key = (p["name"], p.get("arch", "amd64"), p["version"])
            pins.setdefault(key, p.get("digest") or "")
    counts, errors = {}, []
    for (name, arch, version), digest in sorted(pins.items()):
        try:
            how = fetch(cache, name, arch, version, digest)
            counts[how] = counts.get(how, 0) + 1
            if how == "launchpad":
                print(f"launchpad: {name}={version}")
        except (RuntimeError, subprocess.CalledProcessError) as e:
            errors.append(str(e))
            print(f"ERROR: {e}", file=sys.stderr)
    print(f"{len(pins)} pinned packages: " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or ["exodus.json"]))
