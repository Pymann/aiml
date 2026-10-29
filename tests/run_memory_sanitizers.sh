#!/usr/bin/env bash
#
# run_memory_sanitizers.sh — memory-safety acceptance gate for the heap-lifetime
# work (spec/61).  Runs AIML programs under valgrind memcheck, which instruments the
# JIT-generated AIML code AND the C++ runtime at run time (dynamic binary
# instrumentation) — so it catches double-free / use-after-free / invalid-read-write
# AND leaks originating from GENERATED code, without needing an ASan-instrumented
# codegen build (the ASan static build fails to link against libassimp; DBI sidesteps
# that entirely — see doc/analysis).
#
# TWO SEPARATE CONCERNS, judged by TWO runs per probe (do not conflate them):
#   1. CORRECTNESS — run the probe NATIVELY (no valgrind).  Must exit 0.  This is where
#      a probe's own logic assertion belongs, including RSS/heap-delta leak metrics that
#      are ONLY meaningful natively: valgrind never returns freed memory to the OS, so an
#      RSS-delta probe reports a huge false "leak" under valgrind even when nothing leaks.
#      A compile failure (e.g. a probe source that no longer compiles) also lands here as
#      a nonzero exit — which is exactly the class of silently-passing broken probe that
#      the old `|| true` harness used to swallow.
#   2. SAFETY — run the SAME probe UNDER valgrind, and judge ONLY memory-safety:
#      invalid free / UAF / invalid read-write / signal-termination (SIGSEGV/SIGABRT).
#      `--errors-for-leak-kinds=none` makes --error-exitcode fire ONLY for genuine memory
#      errors, NOT for leaks (leaks are still reported and tracked numerically below).
#      The probe's OWN exit code under valgrind is IGNORED here (see the RSS caveat above).
#
# Plus a CONTROL: an intentional double-free from AIML code (_mem_sanitize/ctl_double_free)
# MUST be reported by valgrind.  If not, valgrind is not instrumenting generated code and
# the whole gate is worthless → hard fail.
#
# Usage: bash tests/run_memory_sanitizers.sh
# Env:   AIML_BIN (default ./out/aiml/aiml)
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
# spec/61 heap-lifetime drops are DEFAULT OFF in the compiler while they harden against real-world
# aliasing (see Compiler::compile / context.h).  This harness EXISTS to exercise that feature, so
# turn it ON for every probe here.  (Exported so the `env VAR=val aiml` probe launches inherit it.)
export AIML_LIFETIME_DROPS=1
# --errors-for-leak-kinds=none: leaks are REPORTED (--show-leak-kinds=definite) and tracked,
# but do NOT count toward --error-exitcode, so exit 99 means a genuine memory error only.
# --trace-children=yes: some probes are launched via `env VAR=val aiml …`; without this
# valgrind would instrument `env` only, which execve's aiml UN-instrumented → the probe never
# actually runs under valgrind (and its log has no ERROR SUMMARY). Following the exec makes the
# aiml child the instrumented process (its report overwrites the trivial parent log).
VG="valgrind --trace-children=yes --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=none --error-exitcode=99 --num-callers=20"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if ! command -v valgrind >/dev/null 2>&1; then
    echo "SKIP: valgrind not installed"; exit 0
fi
if [ ! -x "$AIML_BIN" ]; then
    echo "FAIL: aiml binary not found at $AIML_BIN"; exit 1
fi

# Regex for a genuine memory-SAFETY violation (not a mere leak).  Includes signal
# termination so an abort()/assertion or a SIGSEGV that valgrind does not classify as an
# addressing error is still caught (the crash class the old `|| true` used to swallow).
UNSAFE='Invalid free|Invalid read|Invalid write|delete\[\]|has not been freed|contains unaddressable|jump .* uninitialised|Process terminating with default action of signal'

fails=0

# ── CONTROL: intentional double-free MUST be caught by valgrind ───────────────
# (No native correctness run — a double-free natively just corrupts the heap.)
echo "── control: intentional double-free (must be DETECTED) ──"
$VG --log-file="$TMP/ctl.log" "$AIML_BIN" -r tests/_mem_sanitize/ctl_double_free.aiml >/dev/null 2>&1
CTL_RC=$?
if grep -qE 'Invalid free' "$TMP/ctl.log" && [ "$CTL_RC" -eq 99 ]; then
    echo "  OK: valgrind detected the intentional double-free (harness is live, RC=99)"
else
    echo "  FAIL: control double-free NOT detected (RC=$CTL_RC) — harness cannot see generated-code errors"
    fails=$((fails+1))
fi

# ── PROBES: native correctness + valgrind safety ─────────────────────────────
probe() {  # name, logprefix, -- cmd... (cmd may start with `env VAR=val`)
    local name="$1" pfx="$2"; shift 2
    echo "── $name ──"

    # 1. CORRECTNESS — native run, must exit 0 (catches compile failures & logic FAILs,
    #    and validates RSS/heap-delta metrics that are only meaningful without valgrind).
    "$@" >"$TMP/$pfx.native.log" 2>&1
    local nrc=$?
    if [ "$nrc" -ne 0 ] || grep -qE 'Compilation failed|error:' "$TMP/$pfx.native.log"; then
        echo "  FAIL: probe failed natively (exit $nrc — compile error or logic assertion):"
        grep -E 'Compilation failed|error:|FAIL' "$TMP/$pfx.native.log" | head -3 | sed 's/^/    /'
        fails=$((fails+1))
        return
    fi

    # 2. SAFETY — same probe under valgrind; judge ONLY memory-safety (ignore its own exit).
    $VG --log-file="$TMP/$pfx.vg.log" "$@" >/dev/null 2>&1
    local vgrc=$?
    # The valgrind run must have COMPLETED: a full report always ends with "ERROR SUMMARY".
    # If it is missing, valgrind crashed / was killed / the probe aborted before teardown —
    # do NOT let that pass as "definitely lost: n/a" (a silently-incomplete run is a failure).
    if ! grep -q "ERROR SUMMARY" "$TMP/$pfx.vg.log"; then
        echo "  FAIL: valgrind run incomplete (no ERROR SUMMARY; VG_RC=$vgrc) — probe aborted or valgrind died:"
        tail -3 "$TMP/$pfx.vg.log" | sed 's/^/    /'
        fails=$((fails+1))
        return
    fi
    if [ "$vgrc" -eq 99 ] || grep -qE "$UNSAFE" "$TMP/$pfx.vg.log"; then
        echo "  FAIL: memory-safety violation (VG_RC=$vgrc):"
        grep -E "$UNSAFE" "$TMP/$pfx.vg.log" | head -3 | sed 's/^/    /'
        fails=$((fails+1))
        return
    fi
    local lost
    lost=$(grep -E 'definitely lost:' "$TMP/$pfx.vg.log" | tail -1 | sed 's/.*definitely lost://')
    echo "  OK: correct (native exit 0) + memory-safe (definitely lost:${lost:- n/a})"
}

# A compile-time rejection check (spec/61 §3.1: consumed on a value model must be refused).
must_reject() {  # name, logfile, needle, -- cmd...
    local name="$1" log="$2" needle="$3"; shift 3
    echo "── $name ──"
    "$@" >"$log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ] && grep -qF "$needle" "$log"; then
        echo "  OK: rejected as expected (exit $rc)"
    else
        echo "  FAIL: expected compile-time rejection (exit=$rc, needle '$needle' present=$(grep -qF "$needle" "$log" && echo yes || echo no))"
        fails=$((fails+1))
    fi
}

must_reject "consumed on value model must be REJECTED (spec/61 §3.1)" "$TMP/reject.log" \
    "cannot be { lifetime: consumed }" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_param_bad_model.aiml
must_reject "consumed on concrete method MISMATCHING its interface must be REJECTED" "$TMP/reji.log" \
    "does NOT declare it consumed" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_param_bad_iface.aiml
must_reject "assign to { const: true } local must be REJECTED (finding #7)" "$TMP/rejc.log" \
    "Cannot assign to const variable" \
    "$AIML_BIN" -r tests/_mem_sanitize/const_local_reject.aiml

probe "consumed param syntax: accepted on ptr/string/list, const on ptr" "cok" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_param_ok.aiml
probe "consumed STRING param: move-out (push) + drop-safety-net, no double-free/UAF" "cstr" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_string.aiml
probe "consumed STRING param on a DIRECT method call: move-out, no double-free/UAF" "cmeth" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_method.aiml
probe "consumed DIRECT interp temp (P1): moved, not double-freed" "cdi" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_direct_interp.aiml
probe "local->local model assign = deep copy + self-assign no-op (§4.2/§4.6)" "alc" \
    "$AIML_BIN" -r tests/_mem_sanitize/assign_local_copy.aiml
probe "§4 ownership gate: inner-list view in a VOID fn is not double-freed" "oiv" \
    "$AIML_BIN" -r tests/_mem_sanitize/ownership_inner_view_void.aiml
probe "§7.0 (C): shared value-model member-store deep-copies → no double-free" "csm" \
    "$AIML_BIN" -r tests/_mem_sanitize/ownership_shared_member_void.aiml
probe "P2a return-cleanup (dataflow-gated): scalar-return frees owned locals" "rc" \
    "$AIML_BIN" -r tests/_mem_sanitize/return_cleanup.aiml
probe "map<string:string> put-move: build is UAF-free (drop deferred, §8 audit)" "msd" \
    "$AIML_BIN" -r tests/_mem_sanitize/map_string_drop.aiml
probe "list<string> scope-drop (E-leak): buffer + element decref reclaimed, no double-free" "lsd" \
    "$AIML_BIN" -r tests/_mem_sanitize/list_string_drop.aiml
probe "list<Model> scope-drop (elemKind 2): per-element heap + buffer freed, no double-free" "lmd" \
    "$AIML_BIN" -r tests/_mem_sanitize/list_model_drop.aiml
probe "consumed on interface method (match): compiles + runs, no double-free" "cifc" \
    "$AIML_BIN" -r tests/_mem_sanitize/consumed_iface_ok.aiml
probe "borrow-escape via ref<T> weak back-edge: compiles + memory-safe (owning-member escape is now an error)" "besc" \
    "$AIML_BIN" -r tests/_mem_sanitize/borrow_escape_warn.aiml
probe "get_copy_at: correctness + corrupt→-1 + refill + final-drop" "gca" \
    "$AIML_BIN" -r tests/test_kv_get_copy_at.aiml
probe "store-then-drop: get_copy/fresh → member store → move (A3, no double-free/UAF)" "std" \
    "$AIML_BIN" -r tests/_mem_sanitize/store_then_drop.aiml
probe "double-move member (finding #4): 2nd move of same local deep-copies, no double-free" "dmm" \
    "$AIML_BIN" -r tests/_mem_sanitize/double_move_member.aiml
probe "member container-field deep-copy (9rooms persist_from_req): map/list field from a borrow → independent heap, no double-free/leak" "mcd" \
    "$AIML_BIN" -r tests/_mem_sanitize/member_container_deepcopy.aiml
probe "model into two list<Model> containers / at->push: deep-copy per insert, no shared-heap double-free" "m2c" \
    "$AIML_BIN" -r tests/_mem_sanitize/model_into_two_containers.aiml
probe "string field from a borrow incref'd (9rooms deck-editor UAF): no-init source dropped, library string survives" "sfs" \
    "$AIML_BIN" -r tests/_mem_sanitize/string_field_share_uaf.aiml
probe "map.put moves a DECLARED string key (9rooms mana_choice): put consumes the key's drop, map keeps its key" "mpk" \
    "$AIML_BIN" -r tests/_mem_sanitize/map_put_declared_key.aiml
probe "leak-probe A (model deep-copy, N=200)" "pa" \
    env AIML_MEM_PROBE=A AIML_MEM_ITERS=200 "$AIML_BIN" -r tests/test_mem_leak_probe.aiml
probe "owned new-box scope-drop (spec/61): { new: T } local deep-freed at scope exit, bounded RSS + no double-free/UAF" "nbx" \
    "$AIML_BIN" -r tests/_mem_sanitize/newbox_scope_drop.aiml
probe "string field from an interpolation TEMP: single ref transferred (not incref'd) → no per-store ref leak, bounded RSS" "sfi" \
    "$AIML_BIN" -r tests/_mem_sanitize/string_field_interp_drop.aiml
probe "conditional push of a value-model (9rooms DeckService.list): source freed on skip paths, no per-iter leak, bounded RSS" "cpd" \
    "$AIML_BIN" -r tests/_mem_sanitize/conditional_push_drop.aiml
probe "producer-returned list<string> (9rooms active_match_ids): caller's list freed at scope exit + no orphaned initList buffer, bounded RSS" "pld" \
    "$AIML_BIN" -r tests/_mem_sanitize/producer_list_drop.aiml
probe "push of a borrowed field-string into a producer list (9rooms take_gone_players): list increfs the borrow, source model's string survives the drop (no UAF/heap corruption)" "pfp" \
    "$AIML_BIN" -r tests/_mem_sanitize/producer_field_push_incref.aiml
probe "member field reassign releases the old value (9rooms take_gone_players + poll_board_state): repeated set:[<.field,x] frees prior list<Model>/model heap, bounded RSS" "mrf" \
    "$AIML_BIN" -r tests/_mem_sanitize/member_reassign_free.aiml
probe "managed string local (spec/61 §7 class 3): self-accum frees old + borrow-copy owns independent ref, bounded RSS + no UAF" "srd" \
    "$AIML_BIN" -r tests/_mem_sanitize/string_reassign_drop.aiml
probe "producer-returned STRING (9rooms deckset-editor populate_card_full): fresh string freed when bound to a local (direct + bare-call-result return) AND when used as a transient call arg, bounded RSS" "psd" \
    "$AIML_BIN" -r tests/_mem_sanitize/producer_string_drop.aiml
probe "producer-returned VALUE MODEL (spec/61 §3.2; 9rooms mkp_*_cascade render taker): a { lifetime: producer } fn returns a fresh model with list+string fields → caller deep-frees them at scope exit, 200 iters leak-free" "pvm" \
    "$AIML_BIN" -r tests/_mem_sanitize/producer_value_model_drop.aiml
probe "return-cleanup owner-exemption (spec/61 §6.2 T1): a SCALAR return (return s.len) drops the owned string local; a heap (string) return stays exempt (moved out), bounded RSS + no double-free" "rcs" \
    "$AIML_BIN" -r tests/_mem_sanitize/return_cleanup_scalar_drop.aiml
probe "producer early-return of a STRING FIELD of an owned value-model local (spec/61 §6.2 T1; 9rooms AuthService.pid_for — leaked User siblings on EVERY authenticated request): field increfed + owner dropped FULLY, siblings freed, 500 iters leak-free" "erf" \
    "$AIML_BIN" -r tests/_mem_sanitize/early_return_field_scope_drop.aiml
probe "discarded list.pop + list.clear release element ownership mid-lifetime (spec/61 §5.4; 9rooms card-editor pop-loops leaked ptr<Model> boxes + nested list<i32> handles per drain): elemKind 4/5 freed on pop/clear, value-model-inline (kind 2) deferred, 300 iters leak-free + no double-free" "pcr" \
    "$AIML_BIN" -r tests/_mem_sanitize/pop_clear_elem_release.aiml
probe "direct interpolation return (spec/61 T1b): `return \"\${…}\"` frees its intermediate conversion/concat temps (block terminator skips the per-statement temp loop), bounded RSS" "rit" \
    "$AIML_BIN" -r tests/_mem_sanitize/return_interp_temp_drop.aiml
probe "if/while condition interpolation temp (spec/61 T1c): a concat built in an if/while condition is freed on every path incl. a terminating branch (CardArtCache has_texture hit path), bounded RSS" "cit" \
    "$AIML_BIN" -r tests/_mem_sanitize/cond_interp_temp_drop.aiml
probe "read-after-move managed string (spec/61 §7): a string local pushed into a container stays VALID for a later read (9rooms event-log/mana-key read-after-push) — managed move DUPS the source; both copies freed, bounded RSS + correct content" "ram" \
    "$AIML_BIN" -r tests/_mem_sanitize/read_after_move_drop.aiml
probe "map<string:*> owns its keys (spec/61 §8): interpolated map keys passed through a HELPER (ManaPool.add_color→colors.put) must not collapse — put DUPS keys so the caller's key local frees independently; accumulates correctly (no 10× slot-collapse) + bounded RSS + memory-safe" "mpk" \
    "$AIML_BIN" -r tests/_mem_sanitize/mana_pool_key_drop.aiml
probe "list<string> owns its elements (spec/61 §8): a string pushed through a HELPER param (event_log log_event(msg)→lines.push(msg), caller passes an interpolation temp) stays readable after the pushing frame returns — push DUPS the element; correct content + bounded RSS + memory-safe" "elp" \
    "$AIML_BIN" -r tests/_mem_sanitize/event_log_push_drop.aiml
probe "value-model set from a VIEW deep-copies (spec/61 §4.2): a producer that fills an owned model local via set:[out,{at:[view,i]}] and returns it (9rooms load_entry_at→att_entry→aent in drive_ai_combat) must give out an INDEPENDENT heap — a shallow store shares the container element's list buffer → double-free when both the returned copy and the container drop; memory-safe" "svr" \
    "$AIML_BIN" -r tests/_mem_sanitize/producer_set_view_return.aiml
probe "nested model-list push+index-writeback (9rooms add_buff→buffs.push + idx:[zone,i,ent]): a model with list<Buff> (Buff = string + list<i32> + list<Model>) built via extract→build→push→writeback→free must keep the inner string intact and be memory-safe (bounded RSS)" "nml" \
    "$AIML_BIN" -r tests/_mem_sanitize/nested_model_list_deepcopy.aiml
probe "memV3: req.params / url.get_param / cookies are managed map handles, url parts managed strings" "hrm" \
    "$AIML_BIN" -r tests/_mem_sanitize/http_request_maps.aiml
probe "classgroup free with a sees edge: the borrowed sibling is freed once (spec/58 §9)" "gfs" \
    "$AIML_BIN" -r tests/test_classgroup_free_sees.aiml
probe "interface-typed member from {new:}: upcast on set, released with its owner (spec/61)" "ims" \
    "$AIML_BIN" -r tests/test_iface_member_set_new.aiml
probe "classgroup interfaces: ptr<G>/ref<G>, export borrows, member contract, drop frees the group (spec/58 §12)" "gif" \
    "$AIML_BIN" -r tests/test_classgroup_giface.aiml
probe "classgroup interfaces: export satisfied by an instance-level implements (spec/58 §12.3)" "gii" \
    "$AIML_BIN" -r tests/test_classgroup_giface_instance_impl.aiml
probe "namespaced class: implements + vtable + nested <.method buffer results (spec/61)" "nsi" \
    "$AIML_BIN" -r tests/test_namespace_class_iface.aiml
probe "bytes file streaming: append_file / file_slice / copy_range / gzip_file / gunzip_file" "bst" \
    "$AIML_BIN" -r tests/test_bytes_stream.aiml
probe "sys.deserialize: defaults, null, number conversion, errors with paths, bare containers, per-thread error (spec/29 §5.2d)" "sde" \
    "$AIML_BIN" -r tests/test_ser_defaults_errors.aiml
probe "processes: sys.wait_any pool with captured output, signal, caught signals, stdin, list<process> release (spec/23 §5)" "ppl" \
    "$AIML_BIN" -r tests/test_process_pool.aiml
probe "memV3: chained req.params.get / url.get_param.get / resp.headers.put give managed strings" "hcm" \
    "$AIML_BIN" -r tests/_mem_sanitize/http_chained_maps.aiml
probe "identity: base64 strings + owned buffers bound, rebound and dropped (spec/67 §2)" "b64" \
    "$AIML_BIN" -r tests/_mem_sanitize/identity_base64.aiml
probe "identity: ldap escaping + offline handle (error string, empty lists, double close) (spec/67 §3)" "ldp" \
    "$AIML_BIN" -r tests/_mem_sanitize/identity_ldap_offline.aiml
probe "identity: saml.verify_response maps (ok + error) and AuthnRequest URLs (spec/67 §4)" "sml" \
    "$AIML_BIN" -r tests/_mem_sanitize/identity_saml.aiml
probe "http server: chunked request bodies, spooling to temp files, HEAD without body, connection: close, TLS sniff" "hcs" \
    "$AIML_BIN" -r tests/test_http_chunked_spool.aiml
probe "io.append_from: streams one file onto another, refuses self-append" "iaf" \
    "$AIML_BIN" -r tests/test_io_append_from.aiml

# spec/61 call-temporary matrix: same fixtures as the semantic and AOT leak gates.
source tests/lifetime/call_temporaries/cases.sh
for case_file in "${CALL_TEMPORARY_PROBES[@]}"; do
    case_name=$(basename "$case_file" .aiml)
    probe "call temporaries: $case_name (unverified acceptance target)" "calltemp_$case_name" \
        env AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 "$AIML_BIN" -r "$case_file"
done

echo "────────────────────────────────────────"
if [ "$fails" -eq 0 ]; then
    echo "PASS: control detected + all probes correct + memory-safe"
    exit 0
else
    echo "FAIL: $fails memory-sanitizer check(s) failed"
    exit 1
fi
