#!/usr/bin/env bash
# run.sh — package imports `@name/…` (spec/21 §3.1), from a file and from a
# context (spec/29_2 §14a).  Builds a throwaway project with aiml.packages.json
# pointing at tests/packages/demo_pkg, the way `exodus pkg install` would.
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
AIML="${AIML:-$ROOT/out/aiml/aiml}"
case "$AIML" in /*) ;; *) AIML="$ROOT/$AIML" ;; esac
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
cp tests/packages/main_file.aiml tests/packages/ctx_host.aiml tests/packages/bundle_pkg.aiml tests/packages/bundle_plain.aiml "$WORK/"
mkdir -p "$WORK/demo_pkg/lib"
# the table: relative path, relative to the table's directory
mkdir -p "$WORK/cache/demo"; cp -r tests/packages/demo_pkg/. "$WORK/cache/demo/"
printf '{"packages": {"demo": {"path": "cache/demo", "version": "1.0.0"}}}\n' > "$WORK/aiml.packages.json"
echo "$WORK/aiml.packages.json" > "$WORK/table_path.txt"
cp tests/packages/demo_pkg/lib/util.aiml "$WORK/demo_pkg/lib/"   # target of the refused plain import

run() { (cd "$WORK" && env AIML_MEM_V3=1 "$AIML" "$@" 2>&1); }
echo "── package imports (spec/21 §3.1) ──"
out=$(run main_file.aiml); rc=$?
if [ $rc -eq 0 ] && echo "$out" | grep -q "PASS package import from a file"; then echo "  ok   file imports @demo"; else echo "  FAIL file imports @demo (rc=$rc)"; echo "$out" | grep -E "ERROR|FAIL" | head -5; fail=1; fi
out=$(cd "$WORK/demo_pkg" && env AIML_MEM_V3=1 "$AIML" ../main_file.aiml 2>&1); rc=$?
if [ $rc -eq 0 ]; then echo "  ok   table found upward from the importing file (cwd elsewhere)"; else echo "  FAIL table lookup from another cwd (rc=$rc)"; echo "$out" | grep ERROR | head -3; fail=1; fi
out=$(cd "$WORK" && mv aiml.packages.json t.json && env AIML_MEM_V3=1 AIML_PACKAGES="$WORK/t.json" "$AIML" main_file.aiml 2>&1); rc=$?; mv "$WORK/t.json" "$WORK/aiml.packages.json"
if [ $rc -eq 0 ]; then echo "  ok   AIML_PACKAGES names the table"; else echo "  FAIL AIML_PACKAGES (rc=$rc)"; fail=1; fi
out=$(cd "$WORK" && mv aiml.packages.json t.json && env AIML_MEM_V3=1 "$AIML" main_file.aiml 2>&1); rc=$?; mv "$WORK/t.json" "$WORK/aiml.packages.json"
if [ $rc -ne 0 ] && echo "$out" | grep -q "exodus pkg install"; then echo "  ok   no table → error names the fix"; else echo "  FAIL missing table (rc=$rc)"; fail=1; fi
out=$(run ctx_host.aiml); rc=$?
echo "$out" | grep -E "^  (ok|FAIL)"
if [ $rc -ne 0 ]; then echo "  FAIL ctx_host (rc=$rc)"; echo "$out" | grep -E "CRASH|ERROR" | head -5; fail=1; fi
[ $fail -eq 0 ] && echo "packages: PASS" || echo "packages: FAIL"
exit $fail
