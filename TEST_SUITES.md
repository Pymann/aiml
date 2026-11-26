# AIML test suites — status & overview

What exists, what each suite actually covers, which failures are expected, and
what nothing covers yet. Every figure here is measured and carries its date and
commit; a number without those is worse than none.

A green run is not the same as a covered feature, and this document exists
because the suite cannot tell an expected failure from a regression. Until it
can, that separation lives in §3.

Earlier revisions of this file were a session report for the `mem-v3-compiler`
branch (as of 2026-09-23). That narrative is in the git history; what follows is
the current state.

## 1. The runners

| Runner | What it covers | Size | Gates? |
|--------|----------------|------|--------|
| `run_all_tests.sh` | the corpus: every `tests/*.aiml` plus the 23 `tests/*/run.sh` drivers | 1092 entries | **no** — see §5 |
| `run_aot_tests.sh` | AOT smoke: compile to an object, link, run | 3 cases | yes (its own exit code) |
| `run_wasm_tests.sh` | the Emscripten path under node | 12 cases | yes |
| `tests/run_memory_sanitizers.sh` | valgrind memcheck: **memory safety only**, leaks are counted but not judged | 111 probes | yes |
| `tests/run_lifetime_leak_gates.sh` | valgrind leak **counts** (spec/61 §7), AOT-compiled and linked against `libaiml` | 182 probes | yes |
| `tests/lifetime/run_lifetime.sh` | the spec/61 **lab** — acceptance targets written before the implementation | lab probes | no (red by design) |

The two valgrind runners answer different questions and must not be conflated:
the sanitizer run passes `--errors-for-leak-kinds=none`, so it fails only on
invalid free / use-after-free / invalid read-write / signal death, while the leak
gate fails on `definite` or `indirect` bytes > 0. A probe can be memory-safe and
still leak.

**Never run two suites at once.** A sanitizer gate and the normal suite
interfere, and two `run_all_tests.sh` runs overwrite each other's
`test_results.txt`.

Running everything, in the order that keeps the measurements apart:

```bash
exodus build --all                          # library, compiler, wasm runtime, asan, zim
DISPLAY=:99 script -qec "bash run_all_tests.sh" /dev/null
bash run_aot_tests.sh
bash run_wasm_tests.sh
bash tests/run_memory_sanitizers.sh
bash tests/run_lifetime_leak_gates.sh
```

Display tests need **both** an X display and a TTY (`run_all_tests.sh` checks
`tty -s` and then `xdpyinfo`); without a TTY they are silently skipped, which is
how two of them stayed broken for months (§4). `Xvfb :99 -screen 0 1280x1024x24`
plus `script -qec` satisfies both.

## 2. How a test declares what it expects

In the test's own header, read by `run_all_tests.sh`:

| Marker | Meaning |
|--------|---------|
| `# Expected exit code: N` | the run passes iff the program exits with `N`. 400 of the 680 top-level tests carry one; 49 of those expect non-zero. |
| `# Requires: display` | needs an X/Wayland display **and** a TTY, else skipped (41 tests) |
| `# Requires: library` | not a test — imported by other tests, never run standalone (40 files) |
| `# Requires: driver` | belongs to the `run.sh` of its own directory; running it alone would look like a failure, because a negative case is *supposed* not to compile (125 files) |
| `# Requires: wasm` | only meaningful in the Emscripten build; `run_wasm_tests.sh` runs these (3) |
| `# Requires: peer` | needs a paired server/client process; always fails solo (4) |
| `# Requires: manual` | opens a window and waits for a human; run with `--manual` (3) |
| `# AssetRoots: …` | run through `tests/_asset_helper/run_asset_test.sh`, which passes `--asset-roots` |
| `# SPEC: <chapter>` | which spec chapter the test holds to account |

A hard-coded list in `run_all_tests.sh` **overrides** the header for 36 file
patterns. That list is older than the header mechanism and is kept so no
existing entry silently changes meaning. Most of it is negative tests
(`*_error.aiml`, `test_asset_neg_*`, `test_classgroup_neg_*` → exit 1); two are
programs whose own exit code is the assertion (`test.aiml` → 42,
`test_collections.aiml` → 30).

When you add a negative test, give it an `# Expected exit code: 1` header rather
than extending that list.

A test that is expected to **fail as a whole** declares nothing in its header --
there is no `xfail` marker. It is registered in the table in §3, which
`tools/check_expected_failures.sh` parses and the suite exits on. Registering a
test is therefore a visible, reviewable act, and un-registering is forced: once
it passes, the gate fails until the row is gone.

## 3. Known failures

Measured 2026-11-24 on `15ff4ddc` plus the two fixes below, whole corpus under
Xvfb with a TTY:

```
Total: 1092 · Passed: 917 · Failed: 7 · Skipped: 168
```

**All 7 failures are the expected ones listed below** — there is no unexplained
red. The other runners on the same build: AOT 3/3, WASM 12/12, memory sanitizer
PASS (control double-free detected, every probe correct and memory-safe),
lifetime leak gate 0 required failures.

Of the 168 skips, 125 are driver-owned files (the driver is counted instead), 40
are libraries imported by other tests, 3 need wasm, 4 need a peer process, 3 are
manual-only. With no display and no TTY, 41 more would be skipped — that is what
happens in CI (§4).

**2026-11-25:** six of those seven were one bug, not missing chapters.
`sys.exec` of an isolated bundle never called its start function
(`aiml_isolated_exec`, fixed in `84e5a2b`), so `test_bundle_exec_semantics` and
the five spec/62 pipeline tests left the register; a fresh CI checkout exposed it,
where a developer's `logs/` still held the files an earlier run had written.

### Expected — acceptance targets written before the implementation

These are red because the chapter they test is not implemented. They are the
spec-first working method, not breakage, and they must stay red until the
respective chapter lands.

<!-- expected-failures:begin -- read by tools/check_expected_failures.sh.
     One corpus entry per row, its path first and in backticks.  This table is
     the only register: people read it, the gate parses it.  A row that starts
     passing fails the gate too, so it cannot rot here unnoticed. -->

| Test | Chapter | Why |
|------|---------|-----|
| `tests/closures/run.sh` (16 negative cases) | spec/64 | `callable` is an unknown top-level key and `capture` an unknown operator — the feature does not exist in this build, so no negative case can be rejected *for the right reason* |

<!-- expected-failures:end -->

`tests/lifetime/run_lifetime.sh` belongs here by nature but not in the table: it
is a runner of its own, never executed by the corpus, and its own header says it
stays red until the spec/61 rewrite lands (§1).

### Expected — the leak gate's RED list

`tests/run_lifetime_leak_gates.sh` keeps one probe outside the gate:

| Probe | State on 2026-11-24 |
|-------|---------------------|
| `p2_array_field_leak_RED.aiml` | genuinely red: `rc=99`, 6,780 bytes definitely lost |

`50_share_semantics.aiml` reported clean (`rc=0`, 0/0) and moved into the gate on
2026-11-25.

### Fixed on 2026-11-24 — and why nobody had seen them

Both had been failing for a long time behind `# Requires: display`: every run
without a TTY skipped them, and the skip was silent.

| Test | Cause |
|------|-------|
| `tests/test_gl_vfx.aiml` | real runtime bug. In `aiml_gl_buffer_reserve_bytes` the RAII guard `GlPufferZiel` binds the replacement buffer to `GL_COPY_WRITE_BUFFER`, but its scope closed one line before the `glCopyBufferSubData` that needs that binding; the copy went into buffer 0, `GL_INVALID_OPERATION`, and **every grow of a non-empty vertex buffer returned false**. Index buffers passed because they skip the copy. |
| `tests/test_layout_helpers.aiml` | broken test. It never called `engine.prefab_scene_init`, so `engine.instantiate` returned `nullptr` for all three tiles, `layout_hflow` skipped them via `if (!pi) continue`, and `get_x` returned 0. It asserted layout behaviour it never exercised. |

The second one is the more useful lesson: a test can be green-looking, skipped,
or failing for a reason that has nothing to do with its subject. Hence the
baseline steps in newer tests — `tests/test_prefab_input_claim.aiml` proves
before each probe that the thing it is about to block is reachable at all, so a
green run cannot be green for the wrong reason.

## 4. What the CI covers

`.github/workflows/ci.yml`, on push to `master`, on tags `v*`, on every pull
request, and via `workflow_dispatch` (input `only` →
`run_all_tests.sh --only`). Which gates a run executes follows from its git diff
(§4.1); a tag and `workflow_dispatch` run all of them.

**Hard gates** — a failure fails the job:

* system packages, the pinned `.deb` fetch, `exodus pkg install`;
* the three builds (`exodus.json`, `exodus2.json`, `exodus-zim.json`);
* the **smoke test**: `tests/test.aiml` must exit 42;
* the **test corpus** — `run_all_tests.sh --no-build`, `TEST_TIMEOUT=300`, run
  under `xvfb-run -a script`, which gives it both an X display and a TTY, so the
  41 `Requires: display` tests actually run. The suite judges itself against the
  register in §3 and exits with that verdict, so an unregistered failure is a
  regression **and** a registered one that starts passing is a finding too;
* **AOT** (`run_aot_tests.sh`, 3 smoke cases);
* **memory safety** (`tests/run_memory_sanitizers.sh`, valgrind);
* the **leak gate** (`tests/run_lifetime_leak_gates.sh`, valgrind, spec/61 §7);
* packaging, artifact upload, and on a `v*` tag a draft release — **after** the
  gates, not before them. They used to run first, so a tag published a release
  while the tests had not been asked yet.

### 4.1 Which tests a run selects — from the git diff (2026-11-26)

aiml is **always built** (compiler, runtime, ZIM, wasm32), the smoke test always
runs, and packaging always runs. What else runs follows from the files the run
changes, `tools/ci/changed_files.sh` (which files) and `tools/ci/select_tests.py`
(what they need):

* **The base.** A pull request: its merge commit against the base branch's tip
  (`HEAD^1`), i.e. exactly what the PR changes. A push to a branch: the newest
  commit of this branch whose CI run **succeeded** and that is an ancestor of
  `HEAD`. Not `github.event.before`: a run that was cancelled by the next push
  (`cancel-in-progress`) or that failed has verified nothing, so its changes stay
  in the diff until a run passes with them. Every successful run verified its
  own diff on top of a verified base, so the chain holds. No such commit, a
  `v*` tag, `workflow_dispatch`, any other event, or any error on the way →
  **everything** runs. A release is never made from a partial run.
* **What a file needs** (`--no-renames`, so a move counts with both names):

  | changed | runs |
  |---|---|
  | `.github/`, `tools/ci/`, `run_all_tests.sh`, `tools/check_expected_failures.sh`, `exodus*.json`, every source of the compiler or runtime, and **any path no rule names** | everything |
  | `src/core/venv.*`, `src/core/build.*` (aiml venv, aiml build) | the drivers `tests/venv/` and `tests/build/` |
  | `tests/<name>.aiml` | that test |
  | `tests/<dir>/…` | that directory's tests and its `run.sh` |
  | `tests/_<helper>/…`, other files directly in `tests/` (fixtures) | the whole corpus |
  | `TEST_SUITES.md` (the register) | the whole corpus |
  | a test file named in `tests/run_memory_sanitizers.sh` or `tests/run_lifetime_leak_gates.sh`, `tests/lifetime/`, those two scripts | in addition the two valgrind gates and the lifetime lab |
  | a test file named in `run_aot_tests.sh`, that script | in addition the AOT smoke tests |
  | any other `*.md` (spec, doc, README), `LICENSE`, `.gitignore` | nothing beyond build, smoke test and packaging |

  The needs of all changed files add up; one file that needs everything makes
  the run a full one.
* **The corpus filter** is `run_all_tests.sh --only <ERE>`, and `--only` now
  filters the drivers (`tests/*/run.sh`) as well as the `.aiml` files — before,
  every driver ran regardless of the filter.
* The job summary says which scope was taken, from which base, and why.

Measured on a full run (b83a55d, 2026-11-26): 68 min, of which the two valgrind
gates take 53, the corpus 8, the four builds 4. A change to `aiml venv` alone
now costs the builds, the smoke test, two drivers and packaging.

Tests: `tests/ci_select/run.sh` — the selection for each row of the table, the
ERE matching the real paths and nothing else, a sum of needs, the full scope
markers, and the base: in a scratch repository with a stand-in `gh`, the newest
successful ancestor is taken, a successful run that is no ancestor (a force
push) is skipped, and no such run means everything.

**Advisory** (`continue-on-error: true`, reported but never blocking):

* the **lifetime lab** (`tests/lifetime/run_lifetime.sh`), red by design (§3);
* a step that prints the last 150 lines of every failed test in a collapsed
  group, so a red corpus is readable from the job log alone.

What the CI still does **not** run: `run_wasm_tests.sh`. It needs an `emsdk`
installation, and an untested CI step is a guess, so the wasm32 target remains
local-only (§5).

exodus has a CI of its own (`Pymann/exodus`, `.github/workflows/ci.yml`): wheel
install, an import check from outside the repository, `pytest`,
`flake8 --select=F`, `black --check` and `mypy`. aiml's CI installs exodus
straight from that repository's `main`, so a broken exodus fails aiml's build
too — the pair is verified from both ends.

## 5. Gaps, stated plainly

* **`run_aot_tests.sh` is 3 smoke cases**, not an AOT corpus. The name promises
  more than the script does, and nothing runs the corpus through AOT. It is a
  gate in CI now, but a narrow one.
* **wasm32 is not in CI** (see §4), so a wasm-only regression is found only by a
  local `run_wasm_tests.sh`.
* **No language check.** The sources were translated from German in one pass
  (2026-11-24); nothing stops German from coming back, and a check would have to
  split CamelCase identifiers to catch cases like `GlPufferZiel`.
* **4 peer tests and 3 manual tests never run** in any automated context.

## 6. Keeping this document honest

When you add a runner, a `Requires:` marker, or a test that is expected to fail,
change this file in the same commit. When a number here no longer matches a run,
re-measure rather than adjust it by hand: a figure without a date and a commit
is worse than none.

See also [BUILDING.md](BUILDING.md) §5 for the commands and §6 for the other
targets.

## 7. Reproducing

```bash
source ../../exvenv/bin/activate            # the exodus venv
exodus build --all                          # library, compiler, wasm runtime, asan, zim
Xvfb :99 -screen 0 1280x1024x24 &           # display tests need a display AND a TTY
DISPLAY=:99 script -qec "bash run_all_tests.sh --no-build" /dev/null
bash run_aot_tests.sh
bash run_wasm_tests.sh                      # needs emsdk + node
bash tests/run_memory_sanitizers.sh         # valgrind, ~30 min
bash tests/run_lifetime_leak_gates.sh       # valgrind, ~15 min
bash tests/lifetime/run_lifetime.sh         # the spec/61 lab (red by design)
```

Only ever one suite at a time.
