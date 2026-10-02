#!/usr/bin/env bash
# spec/61 §7 hartes Leak-Zahlen-Gate (doc/spec61_inline_value_model_release_plan_2026-09-24.md §11.2).
#
# Ergänzt run_memory_sanitizers.sh (das mit --errors-for-leak-kinds=none NUR Memory-Safety prüft,
# KEINE Leak-Abnahme). Hier: jede GREEN-Probe wird nativ AOT kompiliert, gegen libaiml gelinkt und
# unter Valgrind mit --error-exitcode=99 ausgeführt; definite ODER indirect > 0 (bzw. jeder valgrind-
# Fehler / Buildfehler / fehlendes valgrind) ist ein FAIL. Beide Flags aktiv (AIML_MEM_V3=1,
# AIML_LIFETIME_DROPS=1). RED-Ziele werden separat als bekannte offene Befunde gelistet (nicht
# gatend), bis der jeweilige Fix steht — dann in GREEN verschieben.
#
# Nutzung:  bash tests/run_lifetime_leak_gates.sh
# CI/Abnahme ruft dieses Skript UND run_memory_sanitizers.sh.
set -u
cd "$(dirname "$0")/.."
AIML="${AIML_BIN:-out/aiml/aiml}"
LIB="out/libaiml"
RPATH="$(pwd)/$LIB"
VG="valgrind --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99 --num-callers=20"
command -v valgrind >/dev/null 2>&1 || { echo "FAIL: valgrind not installed (mandatory for the leak gate)"; exit 2; }
[ -x "$AIML" ] || { echo "FAIL: compiler not found at $AIML"; exit 2; }

# GREEN: must be 0 definite + 0 indirect lost, native exit 0, no valgrind error.
GREEN=(
  tests/lifetime/container_leaks/map_model_control.aiml
  tests/lifetime/container_leaks/map_string_control.aiml
  tests/lifetime/container_leaks/map_shared_string_key_control.aiml
  tests/lifetime/container_leaks/list_nested_control.aiml
  tests/lifetime/container_leaks/list_pop_discard.aiml
  tests/lifetime/container_leaks/map_nested_control.aiml
  tests/lifetime/container_leaks/map_nested_self_put.aiml
  tests/lifetime/container_leaks/map_shared_control.aiml
  tests/lifetime/container_leaks/list_string_insert.aiml
  tests/lifetime/container_leaks/map_nested_remove.aiml
  tests/lifetime/container_leaks/map_model_remove.aiml
  tests/lifetime/container_leaks/map_model_replace.aiml
  tests/lifetime/container_leaks/list_pop_bound.aiml
  tests/lifetime/container_leaks/list_pop_arg.aiml
  tests/lifetime/container_leaks/map_model_get_borrow.aiml
  tests/lifetime/61_member_handle_share.aiml
  tests/lifetime/62_member_share_recursive_release.aiml
  tests/lifetime/63_deserialize_carrier_no_leak.aiml
  tests/lifetime/64_local_new_box_no_leak.aiml
  tests/lifetime/65_value_model_at_field_replace.aiml
  tests/lifetime/66_value_model_field_reassign_no_leak.aiml
  tests/lifetime/67_push_producer_temp_no_leak.aiml
  tests/lifetime/68_view_copy_string_field_writeback_no_leak.aiml
  tests/lifetime/69_producer_defined_after_caller_no_leak.aiml
  tests/lifetime/70_view_copy_model_field_writeback_no_leak.aiml
  tests/lifetime/71_local_model_set_copy_no_leak.aiml
  tests/lifetime/72_local_model_set_overwrite_no_leak.aiml
  tests/lifetime/75_local_model_set_producer_no_leak.aiml
  tests/lifetime/76_method_rebind_producer_no_leak.aiml
  tests/lifetime/77_member_container_from_producer_no_leak.aiml
  tests/lifetime/78_producer_returns_container_element.aiml
  tests/lifetime/79_producer_fresh_nested_element.aiml
  tests/lifetime/82_copy_of_bound_element.aiml
  tests/lifetime/83_clone_of_bound_element.aiml
  tests/lifetime/80_copy_of_nested_element.aiml
  tests/lifetime/81_clone_of_nested_element.aiml
  tests/lifetime/84_container_return_shared_or_fresh.aiml
  tests/lifetime/85_container_return_unbound_and_exits.aiml
  tests/lifetime/86_container_return_method_and_builtin_temps.aiml
  tests/lifetime/87_producer_model_result_discarded_or_nested.aiml
  tests/lifetime/88_conditional_move_and_borrow_in_loop.aiml
  tests/lifetime/89_kv_get_copy_service_patterns.aiml
  tests/lifetime/90_map_nested_values_matrix.aiml
  tests/lifetime/91_map_shared_values_all_keys.aiml
  tests/lifetime/92_map_remove_replace_matrix.aiml
  tests/lifetime/93_list_insert_pop_matrix.aiml
  tests/lifetime/94_iterator_owner_drop.aiml
  tests/lifetime/95_producer_returns_box_string_field.aiml
  tests/lifetime/96_ref_member_string_overwrite.aiml
  tests/lifetime/97_cascade_model_carrier_render.aiml
  tests/lifetime/98_producer_model_in_guarded_loop.aiml
  tests/lifetime/99_borrow_of_view_to_ptr_param.aiml
  tests/lifetime/100_literal_return_drops_locals.aiml
  tests/lifetime/101_conditional_return_in_loop_keeps_drops.aiml
  tests/lifetime/102_model_returns_and_temps.aiml
  tests/lifetime/103_cond_push_model.aiml
  tests/lifetime/104_cond_member_store.aiml
  tests/lifetime/105_cond_map_put.aiml
  tests/lifetime/106_cond_consumed.aiml
  tests/lifetime/107_nested_loop_return.aiml
  tests/lifetime/108_box_reassign_and_list.aiml
  tests/lifetime/109_shared_patterns.aiml
  tests/lifetime/110_idx_replace_and_str_map.aiml
  tests/lifetime/111_throw_paths.aiml
  tests/lifetime/112_box_reassign.aiml
  tests/lifetime/113_box_list_remove.aiml
  tests/lifetime/114_shared_rebind.aiml
  tests/lifetime/115_shared_list_remove.aiml
  tests/lifetime/116_idx_string.aiml
  tests/lifetime/117_idx_model_producer.aiml
  tests/lifetime/118_str_map_replace.aiml
  tests/lifetime/119_nested_member_store.aiml
  tests/lifetime/120_list_of_lists.aiml
  tests/lifetime/121_map_of_model_lists.aiml
  tests/lifetime/122_model_map_field.aiml
  tests/lifetime/123_string_builtins.aiml
  tests/lifetime/124_serialize_roundtrip.aiml
  tests/lifetime/125_nested_interpolation.aiml
  tests/lifetime/126_ptr_field_replace.aiml
  tests/lifetime/127_ptr_list_map_replace.aiml
  tests/lifetime/128_producer_in_condition.aiml
  tests/lifetime/129_copy_clone_nested.aiml
  tests/lifetime/130_class_member_containers.aiml
  tests/lifetime/131_class_instance_in_loop.aiml
  tests/lifetime/132_interface_returns.aiml
  tests/lifetime/133_model_list_slice_copy.aiml
  tests/lifetime/134_list_string_helpers.aiml
  tests/lifetime/135_globals_rebind.aiml
  tests/lifetime/136_recursion_locals.aiml
  tests/lifetime/137_rebind_clone_copy.aiml
  tests/lifetime/138_kv_set_producer_temp.aiml
  tests/lifetime/139_return_inside_try.aiml
  tests/lifetime/140_list_of_maps.aiml
  tests/lifetime/141_buffer_handle_set_and_param.aiml
  tests/lifetime/142_cascade_param_handle.aiml
  tests/lifetime/143_throw_unwind_matrix.aiml
  tests/lifetime/144_container_param_escape.aiml
  tests/lifetime/145_cascade_model_field_drop.aiml
  tests/lifetime/146_cascade_in_containers.aiml
  tests/lifetime/147_cascade_model_copy_kv.aiml
  tests/lifetime/148_getter_fn_iface_throw.aiml
  tests/lifetime/149_throw_with_container_params.aiml
  tests/lifetime/150_map_of_list_param.aiml
  tests/lifetime/151_param_rebind_string_model_shared.aiml
  tests/lifetime/152_model_param_escape.aiml
  tests/lifetime/153_ptr_ref_class_params.aiml
  tests/lifetime/154_condition_temps.aiml
  tests/lifetime/155_strings_maps_deep_paths.aiml
  tests/lifetime/156_unwind_loops_nested_new_init.aiml
  tests/lifetime/157_deserialize_rebind.aiml
  tests/lifetime/158_kv_get_copy_targets.aiml
  tests/lifetime/159_class_member_rebinds.aiml
  tests/lifetime/160_iface_list_ops_buffer_field.aiml
  tests/lifetime/161_iface_map_values.aiml
  tests/lifetime/162_ptr_param_move.aiml
  tests/lifetime/163_yaml_doc_scope_release.aiml
  tests/lifetime/164_forward_signature_ownership.aiml
  tests/lifetime/cascade_params/direct.aiml
  tests/lifetime/cascade_params/dispatch.aiml
  tests/lifetime/cascade_params/indirect.aiml
  tests/lifetime/cascade_params/rebind.aiml
  tests/lifetime/cascade_params/owner_rebind.aiml
  tests/lifetime/cascade_params/param_rebind_matrix.aiml
  tests/lifetime/73_list_model_clear_no_leak.aiml
  tests/lifetime/74_list_model_remove_no_leak.aiml
  tests/_inline_model_red/49_value_survives_remove.aiml
  tests/_inline_model_red/p2_retain_coverage.aiml
  tests/_inline_model_red/p3_copyretain_survives_source_drop.aiml
  tests/_inline_model_red/52_model_strfield_share_no_leak.aiml
  tests/_inline_model_red/53_nested_model_field_copy_no_leak.aiml
  tests/_inline_model_red/54_at_field_string_no_leak.aiml
  tests/_inline_model_red/55_jsonstyle_field_no_leak.aiml
  tests/_inline_model_red/56_list_share_alias_no_leak.aiml
)
# RED: known open findings (documented) — run + report, but do NOT fail the gate yet.
RED=(
  tests/_inline_model_red/50_share_semantics.aiml
  tests/_inline_model_red/p2_array_field_leak_RED.aiml
)

run_probe() {   # $1 = file ; prints "def/ind" ; returns valgrind rc
  local f="$1" d; d="$(mktemp -d /tmp/leakgate-XXXXXX)"
  if ! AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 "$AIML" "$f" -o "$d/main.o" --log-level ERROR >"$d/build.log" 2>&1; then
    echo "BUILD-FAIL"; rm -rf "$d"; return 98; fi
  if ! g++ "$d"/*.o -o "$d/bin" -L"$LIB" -Wl,-rpath,"$RPATH" -laiml -lyaml-cpp -lpthread -lssl -lcrypto -ldl >"$d/link.log" 2>&1; then
    echo "LINK-FAIL"; rm -rf "$d"; return 97; fi
  ( ulimit -v 6000000; AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 timeout 300 $VG --log-file="$d/vg.txt" "$d/bin" >/dev/null 2>&1 ); local rc=$?
  local def ind
  def=$(grep -aE 'definitely lost:' "$d/vg.txt" | tail -1 | grep -aoE '[0-9,]+ bytes' | head -1)
  ind=$(grep -aE 'indirectly lost:' "$d/vg.txt" | tail -1 | grep -aoE '[0-9,]+ bytes' | head -1)
  echo "def=${def:-?} ind=${ind:-?}"; rm -rf "$d"; return $rc
}

echo "── lifetime LEAK gate (hard: 0 definite + 0 indirect) ──"
fail=0
for f in "${GREEN[@]}"; do
  [ -f "$f" ] || { echo "  FAIL [GREEN] $f — missing"; fail=$((fail+1)); continue; }
  out=$(run_probe "$f"); rc=$?
  if [ $rc -eq 0 ]; then printf "  ok   [GREEN] %-55s %s\n" "$(basename "$f")" "$out"
  else printf "  FAIL [GREEN] %-55s rc=%d %s\n" "$(basename "$f")" "$rc" "$out"; fail=$((fail+1)); fi
done
# Newly authored targets are gating, but have not been measured/approved as GREEN.
source tests/lifetime/call_temporaries/cases.sh
echo "── call-temporary regressions (required; previously unverified) ──"
for f in "${CALL_TEMPORARY_PROBES[@]}"; do
  [ -f "$f" ] || { echo "  FAIL [REGRESSION] $f — missing"; fail=$((fail+1)); continue; }
  out=$(run_probe "$f"); rc=$?
  if [ $rc -eq 0 ]; then printf "  ok   [REGRESSION] %-55s %s\n" "$(basename "$f")" "$out"
  else printf "  FAIL [REGRESSION] %-55s rc=%d %s\n" "$(basename "$f")" "$rc" "$out"; fail=$((fail+1)); fi
done
echo "── known RED targets (reported, not gating until fixed) ──"
for f in "${RED[@]}"; do
  [ -f "$f" ] || { echo "  --   [RED]   $f — missing"; continue; }
  out=$(run_probe "$f"); rc=$?
  printf "  red? [RED]   %-55s rc=%d %s\n" "$(basename "$f")" "$rc" "$out"
done
echo "── leak-gate: $fail required failure(s) ──"
[ $fail -eq 0 ]
