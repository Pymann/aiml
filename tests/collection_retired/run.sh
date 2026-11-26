#!/usr/bin/env bash
# spec/71 §8 — every retired collection name is a compile error that names its replacement.
# Checked with --check: the diagnostic matters, not a program run.
set -uo pipefail
cd "$(dirname "$0")/../.."
BIN="./out/aiml/aiml"
DIR="tests/collection_retired"
fails=0
want() {
    local file="$1" needle="$2" out rc
    out=$("$BIN" --check "$DIR/$file" 2>&1); rc=$?
    if [ $rc -ne 0 ] && printf '%s\n' "$out" | grep -qF -- "$needle"; then
        echo "  ok   $file: rejected, names '$needle'"
    else
        echo "  FAIL $file: expected a compile error naming '$needle' (rc=$rc)"
        printf '%s\n' "$out" | grep -iE "error|unknown" | sed 's/^/        /' | head -3
        fails=$((fails+1))
    fi
}
want len_call.aiml ".len"
want empty_call.aiml ".empty"
want list_size.aiml ".len"
want list_length.aiml ".len"
want list_count.aiml "count_of"
want list_get.aiml "at:"
want list_set.aiml "idx:"
want hashmap_size.aiml ".len"
want buffer_count.aiml ".len"
want buffer_get.aiml "at:"
want map_put.aiml "set("
want hashmap_put.aiml "set("
want map_contains.aiml "has("
want hashmap_contains.aiml "has("
want map_remove.aiml "delete("
want hashmap_remove.aiml "delete("
want storage_remove.aiml "delete("
want hashmap_add.aiml "incr("
want buffer_append.aiml "extend("
want store_insert.aiml "add("
want vecstore_insert.aiml "set("
want timeseries_at.aiml "at_time("
want tensor_size.aiml ".len"
want tensor_get.aiml "at:"
want tensor_set.aiml "idx:"
want yaml_size.aiml "yaml.len"
want gl_delete.aiml "free("
want code_size.aiml ".len"
want buffer_size_prop.aiml ".len"
want list_count_prop.aiml ".len"
want session_remove.aiml "session_delete("
want len_readonly.aiml "read-only"
want buffer_capacity_readonly.aiml "read-only"
want get_default_handle.aiml "has(k)"
want typed_buffer_append.aiml "extend("
if [ $fails -ne 0 ]; then echo "collection_retired FAIL ($fails)"; exit 1; fi
echo "collection_retired PASS"
