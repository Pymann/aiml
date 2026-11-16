# Third-Party Software

AIML itself is MIT licensed (see [LICENSE](LICENSE)). This file lists everything
else that ships in this repository or is linked into the binaries it produces.

---

## 1. Vendored source (redistributed with this repository)

These files are third-party code kept in-tree. Each one carries its own license
notice inside the file; the notices below are reproduced for convenience and do
not replace them.

### cgltf 1.15 — glTF 2.0 parser

* File: `src/vendor/cgltf.h`
* Upstream: https://github.com/jkuhlmann/cgltf
* License: MIT — `Copyright (c) 2018-2021 Johannes Kuhlmann`
* Notice: `src/vendor/cgltf.h`, line 7219 ff.

cgltf bundles **jsmn**, a minimal JSON parser:

* License: MIT — `Copyright (c) 2010 Serge A. Zaitsev`
* Notice: `src/vendor/cgltf.h`, line 6881 ff.

### miniaudio — audio playback and capture

* File: `src/vendor/miniaudio.h`
* Upstream: https://github.com/mackron/miniaudio
* License: choice of **public domain (Unlicense)** or **MIT-0** — `Copyright 2026 David Reid`
* Notice: end of `src/vendor/miniaudio.h` ("ALTERNATIVE 1 - Public Domain",
  "ALTERNATIVE 2 - MIT No Attribution")

### yyjson 0.12 — JSON reader/writer

* Files: `src/core/yyjson.cpp`, `src/core/yyjson.h`
* Upstream: https://github.com/ibireme/yyjson
* License: MIT — `Copyright (c) 2020 YaoYuan <ibireme@gmail.com>`
* Notice: head of `src/core/yyjson.cpp`

`src/core/yyjson_wrapper.{cpp,h}` is AIML code, not upstream yyjson, and is MIT
licensed as part of this project.

### MIT license text

The MIT notices above all carry this text:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 2. Linked system libraries (not redistributed)

The compiler and `libaiml` link against system libraries. They are **not** part
of this repository; `exodus.json` pins the exact Ubuntu 24.04 package versions
and digests it was built against (162 entries, see
[BUILDING.md](BUILDING.md)). The ones the `aiml` target links directly:

| Library | Package | License (upstream) |
| --- | --- | --- |
| LLVM 18 | `llvm-18-dev` | Apache-2.0 with LLVM exception |
| yaml-cpp | `libyaml-cpp-dev` | MIT |
| OpenSSL (`ssl`, `crypto`) | `libssl-dev` | Apache-2.0 |
| zlib | `zlib1g-dev` | Zlib |
| Brotli | `libbrotli-dev` | MIT |
| zstd | `libzstd-dev` | BSD-3-Clause / GPL-2.0 dual |
| xz / lzma | `liblzma5` | public domain / 0BSD |
| lz4 | `liblz4-dev` | BSD-2-Clause |
| libzip | `libzip-dev` | BSD-3-Clause |
| Assimp | `libassimp-dev` | BSD-3-Clause |
| OpenGL / GLEW / GLFW | `libgl-dev`, `libglew-dev`, `libglfw3-dev` | MIT / BSD-3-Clause / Zlib |
| libpng | `libpng-dev` | PNG Reference Library License v2 |
| libwebp, sharpyuv | `libwebp-dev` | BSD-3-Clause |
| OpenBLAS | `libopenblas-serial-dev` | BSD-3-Clause |
| X11, Xtst | `libx11-6`, `libxtst-dev` | MIT (X11) |
| libssh | `libssh-dev` | LGPL-2.1 |
| OpenLDAP (`ldap`, `lber`) | `libldap-dev` | OLDAP-2.8 |
| GnuTLS | `libgnutls28-dev` | LGPL-2.1-or-later |
| Cyrus SASL | `libsasl2-dev` | BSD-4-Clause-like |
| xmlsec1 | `libxmlsec1-dev` | MIT |
| libxml2, libxslt | `libxml2-dev`, `libxslt1-dev` | MIT |

### Copyleft note

Two entries deserve attention when **distributing built binaries**:

* **libzim** (`libzim-dev`, GPL-2.0-or-later) is declared in `exodus.json`'s
  `apt_packages`, but it is linked **only** by the optional, separately built
  `libaiml_zim` module (`exodus-zim.json`, see `zimmod/`). The `aiml` compiler
  and `libaiml` do not link it. Shipping `libaiml_zim` means shipping a
  GPL-encumbered artifact; shipping the compiler does not.
* **libssh** and **GnuTLS** are LGPL. Dynamic linking (what the build does)
  keeps them separable, but static linking would not.

This section is an inventory, not legal advice. License identifiers were taken
from the upstream projects and should be re-checked against the exact package
versions before a binary release.

---

## 3. Build and test tooling (not linked)

* **Exodus** — the build tool used to compile this project
  (https://github.com/Pymann/exodus), MIT, by the same author.
* **Emscripten / emsdk** — used for the `wasm32` target, MIT / NCSA.
* **Valgrind**, **AddressSanitizer**, **Xvfb** — used in test gates only.
