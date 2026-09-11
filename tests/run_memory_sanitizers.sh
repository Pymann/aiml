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
probe "borrow-escape warning: warns but compiles (non-breaking), memory-safe" "besc" \
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

echo "────────────────────────────────────────"
if [ "$fails" -eq 0 ]; then
    echo "PASS: control detected + all probes correct + memory-safe"
    exit 0
else
    echo "FAIL: $fails memory-sanitizer check(s) failed"
    exit 1
fi
