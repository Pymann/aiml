# Findings from the ongoing ownership monitoring

As of: 2026-09-10, review after watcher reports up to 06:31. Direct source review, no git, no executed repros. Line numbers refer to the working tree as read; changes running in parallel can shift them.

## W1 — P1: missing analysis still allows unchecked drops

`src/core/ast_compiler_stmt.cpp:1575–1578`: mayDrop is only evaluated when a plan, an exit fact and a non-null ID are present. If one precondition is missing, the value is freed anyway. In particular, `ownership_analyzer.cpp:58–72,106–171` lacks handling of TryStmt/catch blocks. Their declarations get no IDs/exit facts and therefore fall back to the old unsafe release. The global suppressFunctionDrops can also be bypassed for these candidates.

Proposal: a missing proof of ownership must not allow a new automatic drop. Analyse try/catch or conservatively suppress the unproven candidates. Consumed parameters need an explicit contract instead of an ID-0 bypass.

## W2 — P1: foreign heap in model fields wrongly stays Owned

`ownership_analyzer.cpp:85–89,123–143`: field/index targets are skipped. A freshly declared model owner stays Owned even when a field takes over a foreign inner container via shallow assignment. The recursive model drop can still free this buffer; the other ownership chain is not captured. Merely setting a reassignment of the whole local slot to Unknown does not fix this case.

Proposal: link field/container resources to the real owner; for an unresolved field binding at least suppress the recursive drop of the target owner. Take both owners involved and the lifetime of existing views into account.

## W3 — P1: calls and conditions are treated as effect-free

`ownership_analyzer.cpp:145–170`: call expressions, conditions and unknown statements do not update any ownership state. A function can redirect the freshly declared list to a foreign buffer through a by-ref list parameter. The analyzer leaves it Owned and later allows the wrong release. Retained/shared model parameters across function boundaries also go unrecognised.

Proposal: apply resolved call effects; without a contract, conservatively account for possible mutation/retention. The existing consumeLocalDrop in lowering does not replace alias/call data flow.

## Assessment

The new hookup protects part of the direct local rebind cases. It is not yet a general Owned/Borrowed/SharedView analyzer. The label "fail-safe, only leaks" is not substantiated for the three paths above. Findings are re-checked during further monitoring; they do not end the review.
