#!/usr/bin/env bash
# Diagnostic runner: preserves compiler, program and Valgrind logs, fails on any error.
set -u
cd "$(dirname "$0")/../../.." || exit 2
artifacts=$(mktemp -d /tmp/aiml-cascade-params-XXXXXX) || exit 2
echo "Artifacts: $artifacts"
compiler=${AIML_BIN:-out/aiml/aiml}
export AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1
failed=0
for name in direct dispatch indirect rebind owner_rebind; do
  dir="$artifacts/$name"
  mkdir -p "$dir"
  if ! "$compiler" "tests/lifetime/cascade_params/$name.aiml" -o "$dir/main.o" --log-level ERROR >"$dir/build.log" 2>&1 || grep -q '\[ERROR\]' "$dir/build.log"; then
    echo "$name BUILD-FAIL"
    failed=1
    continue
  fi
  if ! g++ "$dir"/*.o -o "$dir/bin" -Lout/libaiml -Wl,-rpath,"$PWD/out/libaiml" -laiml -lyaml-cpp -lpthread -lssl -lcrypto -ldl >"$dir/link.log" 2>&1; then
    echo "$name LINK-FAIL"
    failed=1
    continue
  fi
  timeout 30 valgrind --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99 --log-file="$dir/valgrind.log" "$dir/bin" >"$dir/run.log" 2>&1
  rc=$?
  echo "$name exit=$rc"
  if [ "$rc" -ne 0 ]; then failed=1; fi
done
exit "$failed"
