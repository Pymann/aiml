#!/usr/bin/env bash
# run.sh — which tests a CI run selects from its git diff (TEST_SUITES.md §4.1):
# tools/ci/select_tests.py (what the changed files need) and
# tools/ci/changed_files.sh (which files changed, against which base).
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

# sel <file>… — the selection for these changed files, as key=value lines
sel() { printf '%s\n' "$@" | python3 tools/ci/select_tests.py 2> "$WORK/why"; }
val() { echo "$1" | sed -n "s/^$2=//p"; }
# the tests run_all_tests.sh would run for an ERE: .aiml files and drivers
corpus_paths() {
    { find tests/ -name "*.aiml" -not -path "*/__aiml_cache/*" -not -path "*/build/*" -not -path "*/out/*" \
        -not -path "*/_*" -not -path "tests/lifetime/neg/*"; ls tests/*/run.sh; } | sort | grep -E -- "$1" |
    while read -r p; do grep -q "# Requires: driver" "$p" || echo "$p"; done   # a driver's own file is skipped
}

echo "── select_tests.py (TEST_SUITES.md §4.1) ──"
out=$(sel src/core/venv.cpp)
check "aiml venv alone: partial, no valgrind, no AOT" \
  '[ "$(val "$out" scope)" = partial ] && [ "$(val "$out" valgrind)" = false ] && [ "$(val "$out" aot)" = false ]'
re=$(val "$out" corpus)
check "and the corpus is the venv and build drivers, nothing else" \
  '[ "$(corpus_paths "$re" | tr "\n" " ")" = "tests/build/run.sh tests/venv/run.sh " ]'
out=$(sel src/core/build.h tests/venv/kontor_stub.aiml)
re=$(val "$out" corpus)
check "aiml build and a venv fixture: the same two drivers" \
  '[ "$(val "$out" scope)" = partial ] && corpus_paths "$re" | grep -x tests/build/run.sh > /dev/null && corpus_paths "$re" | grep -x tests/venv/run.sh > /dev/null'
out=$(sel compiler.cpp)
check "a compiler source: everything" '[ "$(val "$out" scope)" = full ] && [ "$(val "$out" corpus)" = all ] && [ "$(val "$out" valgrind)" = true ] && [ "$(val "$out" aot)" = true ]'
check "and says why" 'grep -q "compiler.cpp" "$WORK/why"'
for f in src/sys/http_runtime.cpp main.cpp .github/workflows/ci.yml tools/ci/package_release.sh run_all_tests.sh \
         tools/check_expected_failures.sh exodus2.json somewhere/new_thing.txt; do
    out=$(sel "$f")
    check "$f: everything" '[ "$(val "$out" scope)" = full ]'
done
out=$(sel spec/68_venv.md doc/notes.md README.md LICENSE)
check "docs only: nothing beyond build, smoke and packaging" \
  '[ "$(val "$out" scope)" = partial ] && [ "$(val "$out" corpus)" = none ] && [ "$(val "$out" valgrind)" = false ] && [ "$(val "$out" aot)" = false ]'
out=$(sel)
check "no changed file: nothing beyond build, smoke and packaging" '[ "$(val "$out" corpus)" = none ]'
out=$(sel tests/test_input_cursor.aiml)
re=$(val "$out" corpus)
check "one test: that test" '[ "$(corpus_paths "$re")" = tests/test_input_cursor.aiml ] && [ "$(val "$out" valgrind)" = false ]'
out=$(sel tests/test_kv_sidx_3_modify.aiml)
re=$(val "$out" corpus)
check "one test of a numbered series: the whole series, in its order" \
  '[ "$(corpus_paths "$re" | wc -l)" -eq "$(ls tests/test_kv_sidx_[0-9]*_*.aiml | wc -l)" ] && [ "$(corpus_paths "$re" | sed -n 1p)" = tests/test_kv_sidx_1_write.aiml ] && ! corpus_paths "$re" | grep -v "^tests/test_kv_sidx_" > /dev/null'
out=$(sel tests/closures/run.sh)
re=$(val "$out" corpus)
check "a driver directory: its run.sh and its tests" \
  'corpus_paths "$re" | grep -x tests/closures/run.sh > /dev/null && ! corpus_paths "$re" | grep -v "^tests/closures/" > /dev/null'
out=$(sel tests/_asset_fixtures/basic/resources/textures/hammer.png)
check "a helper or fixture under tests/_*: the whole corpus" '[ "$(val "$out" corpus)" = all ] && [ "$(val "$out" scope)" = partial ]'
out=$(sel tests/anim_legs.glb)
check "a fixture directly in tests/: the whole corpus" '[ "$(val "$out" corpus)" = all ]'
out=$(sel TEST_SUITES.md)
check "the register: the whole corpus, no valgrind" '[ "$(val "$out" corpus)" = all ] && [ "$(val "$out" valgrind)" = false ]'
out=$(sel tests/test_regex.aiml)
check "a test the memory gate names: that test plus the valgrind gates" \
  '[ "$(val "$out" valgrind)" = true ] && [ "$(corpus_paths "$(val "$out" corpus)")" = tests/test_regex.aiml ]'
out=$(sel tests/_mem_sanitize/ctl_double_free.aiml)
check "a probe under tests/_mem_sanitize: the corpus and the valgrind gates" '[ "$(val "$out" valgrind)" = true ] && [ "$(val "$out" corpus)" = all ]'
out=$(sel tests/lifetime/container_leaks/map_model_control.aiml)
check "tests/lifetime: the valgrind gates" '[ "$(val "$out" valgrind)" = true ]'
out=$(sel tests/run_lifetime_leak_gates.sh)
check "a gate script: the valgrind gates" '[ "$(val "$out" valgrind)" = true ] && [ "$(val "$out" scope)" = partial ]'
out=$(sel tests/test_aot_hello.aiml)
check "a test run_aot_tests.sh names: the AOT smoke tests" '[ "$(val "$out" aot)" = true ]'
out=$(sel run_aot_tests.sh)
check "the AOT script: the AOT smoke tests" '[ "$(val "$out" aot)" = true ] && [ "$(val "$out" scope)" = partial ]'
out=$(sel src/core/venv.cpp tests/test_input_cursor.aiml spec/44_0_input.md)
re=$(val "$out" corpus)
check "needs add up" \
  '[ "$(corpus_paths "$re" | tr "\n" " ")" = "tests/build/run.sh tests/test_input_cursor.aiml tests/venv/run.sh " ]'
out=$(sel tests/venv/run.sh src/core/venv.cpp src/core/ast_compiler.cpp)
check "one file that needs everything makes it full" '[ "$(val "$out" scope)" = full ] && [ "$(val "$out" corpus)" = all ]'
out=$(printf '#full a v* tag\n' | python3 tools/ci/select_tests.py 2>/dev/null)
check "#full from changed_files.sh: everything, with its reason" \
  '[ "$(val "$out" scope)" = full ] && val "$out" why | grep -q "tag"'
out=$(printf '#base abc123\nsrc/core/venv.cpp\n' | python3 tools/ci/select_tests.py 2>/dev/null)
check "the base is passed on" '[ "$(val "$out" base)" = abc123 ]'
out=$(sel "tests/a b.aiml")
check "the ERE escapes what a path may hold" '[ "$(val "$out" corpus)" = "^tests/a b\\.aiml\$" ]'

echo "── changed_files.sh: the base (TEST_SUITES.md §4.1) ──"
if command -v git >/dev/null; then
    R="$WORK/repo"; mkdir -p "$R"; cd "$R"
    git init -q -b master . && g() { git -c user.name=t -c user.email=t@t "$@"; }
    echo a > a.txt; g add .; g commit -qm A; A=$(git rev-parse HEAD)
    mkdir -p src/core; echo b > src/core/venv.cpp; g add .; g commit -qm B; B=$(git rev-parse HEAD)
    echo c > c.md; g add .; g commit -qm C; C=$(git rev-parse HEAD)
    mkdir -p "$WORK/bin"
    # a stand-in gh: prints the head_sha list of $FAKE_RUNS for the runs query
    cat > "$WORK/bin/gh" <<'EOT'
#!/bin/sh
case "$*" in *actions/workflows/ci.yml/runs*) printf '%s\n' $FAKE_RUNS ;; *) exit 1 ;; esac
EOT
    chmod +x "$WORK/bin/gh"
    cf() { env PATH="$WORK/bin:$PATH" GITHUB_EVENT_NAME="$1" GITHUB_REF="$2" GITHUB_REF_NAME="${2##*/}" \
             GITHUB_REPOSITORY=o/r FAKE_RUNS="$3" bash "$ROOT/tools/ci/changed_files.sh" 2>&1; }
    out=$(cf push refs/heads/master "$A")
    check "a push: the diff against the newest successful ancestor" \
      '[ "$(echo "$out" | head -1)" = "#base $A" ] && [ "$(echo "$out" | tail -n +2 | tr "\n" " ")" = "c.md src/core/venv.cpp " ]'
    out=$(cf push refs/heads/master "$B $A")
    check "the newest one is taken (the list comes newest first)" '[ "$(echo "$out" | head -1)" = "#base $B" ] && [ "$(echo "$out" | tail -n +2)" = c.md ]'
    out=$(cf push refs/heads/master "1111111111111111111111111111111111111111 $A")
    check "a successful run that is no ancestor (a force push) is skipped" '[ "$(echo "$out" | head -1)" = "#base $A" ]'
    out=$(cf push refs/heads/master "")
    check "no successful run: everything" 'echo "$out" | head -1 | grep -q "^#full"'
    out=$(cf push refs/tags/v1.2.3 "$B")
    check "a tag: everything" 'echo "$out" | head -1 | grep -q "^#full"'
    out=$(cf workflow_dispatch refs/heads/master "$B")
    check "workflow_dispatch: everything" 'echo "$out" | head -1 | grep -q "^#full"'
    g checkout -q -b pr "$A"; mkdir -p src/core; echo p > src/core/build.cpp; g add .; g commit -qm P
    g checkout -q master; g merge -q --no-ff pr -m "merge pr"
    out=$(cf pull_request refs/pull/1/merge "")
    check "a pull request: its merge commit against the base tip" \
      '[ "$(echo "$out" | head -1)" = "#base $C" ] && [ "$(echo "$out" | tail -n +2)" = src/core/build.cpp ]'
    cat > "$WORK/bin/gh" <<'EOT'
#!/bin/sh
exit 1
EOT
    out=$(cf push refs/heads/master "$A")
    check "the run history cannot be read: everything" 'echo "$out" | head -1 | grep -q "^#full"'
    cd "$ROOT"
else
    echo "  skip changed_files.sh (no git)"
fi

[ $fail -eq 0 ] && echo "ci_select: PASS" || echo "ci_select: FAIL"
exit $fail
