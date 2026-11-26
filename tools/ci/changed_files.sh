#!/usr/bin/env bash
# changed_files.sh — the files a CI run changes, against a base that was verified
# (TEST_SUITES.md §4.1).  Prints `#base <sha>` and then one path per line, or a
# single `#full <reason>` line when there is no base to diff against — then
# select_tests.py runs everything.
#
#   pull_request   its merge commit against the base branch's tip (HEAD^1)
#   push (branch)  the newest commit of this branch whose CI run succeeded and
#                  that is an ancestor of HEAD — not github.event.before: a
#                  cancelled or failed run verified nothing, so its changes stay
#                  in the diff until a run passes with them
#   anything else  (a v* tag, workflow_dispatch, …) everything
#
# Needs the full history (actions/checkout fetch-depth: 0) and, for a push, `gh`
# with a token that may read the repository's workflow runs (actions: read).
# Any failure on the way is `#full`: not knowing the diff means testing it all.
set -uo pipefail

full() { echo "#full $*"; exit 0; }

event="${GITHUB_EVENT_NAME:-}"
ref="${GITHUB_REF:-}"
base=""
case "$event" in
    pull_request)
        base=$(git rev-parse --verify -q 'HEAD^1') || full "no parent of the merge commit"
        ;;
    push)
        case "$ref" in refs/heads/*) ;; *) full "a push of $ref (a tag is always tested fully)" ;; esac
        branch="${GITHUB_REF_NAME:-${ref#refs/heads/}}"
        runs=$(gh api "repos/${GITHUB_REPOSITORY:-}/actions/workflows/ci.yml/runs?branch=${branch}&event=push&status=success&per_page=50" \
                 --jq '.workflow_runs[].head_sha' 2>/dev/null) || full "the run history of $branch cannot be read"
        for sha in $runs; do
            if git merge-base --is-ancestor "$sha" HEAD 2>/dev/null; then base="$sha"; break; fi
        done
        [ -n "$base" ] || full "no successful run of $branch among the ancestors of HEAD"
        ;;
    *)
        full "the event ${event:-?} is always tested fully"
        ;;
esac

files=$(git diff --no-renames --name-only "$base" HEAD) || full "git diff $base HEAD failed"
echo "#base $base"
[ -n "$files" ] && printf '%s\n' "$files"
exit 0
