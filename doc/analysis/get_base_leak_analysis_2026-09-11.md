# get_base / list leak audit, 2026-09-11

Read-only project analysis; no Git, no compiler or game source changes. All test data and outputs are isolated in this directory. The shared mem_repro executable was copied before testing. Its shared runtime library was not snapshotted.

## Main finding

The successful get_base path drops dat. The remaining per-card leak in the tested executable is preallocated destination container storage overwritten by copy_header.

Sequence:
1. new CreatureCard calls initClassContainerMembers and allocates initial empty container capacity.
2. copy_header assigns mana_cost, active_abilities and passive_abilities.
3. emitContainerFieldDeepCopy explicitly memsets the destination field to zero WITHOUT freeing its existing storage, then calls aiml_ser_deep_copy.
4. dat and the returned card box are dropped, but the original container addresses were already erased and cannot be freed by these drops.

Compiler: src/core/ast_compiler.cpp, emitContainerFieldDeepCopy (starts line 3113), zero-before-copy around line 3150. new initialization: src/core/ast_compiler_expr.cpp:143. Assignments: sources/cards/card_header_util.aiml:copy_header.

## Runtime evidence

mirror.aiml and nested.aiml: AIML_LIFETIME_DROPS=1 AIML_TRACE_SYMBOL=dat,obj,cardbox AIML_CACHE=0 aiml -r <file>. Both exit 0; each has 4 DECL dat and 4 DROP dat, followed by 4 returned-interface-box drops. nested includes the enclosing if and the early-null return shape.

Actual copied mem_repro under Valgrind with independent AIONDB_PATH directories and AIML_REPRO_ITERS 0 versus 10:
- 0 iterations: dat DECL/DROP = 24/24; cbase DECL/DROP = 12/12.
- 10 iterations: dat DECL/DROP = 144/144; cbase DECL/DROP = 132/132.
Thus all 120 additional successful card lookups drop both dat and cbase.

Lost allocations directly under CardService_get_base -> DeckService_compute_mkp -> DeckService_list:
- active_abilities initial buffer: 224 bytes x 120.
- mana_cost initial map: 4 + 32 + 16 bytes x 120.
- passive_abilities initial buffer: 16 bytes x 120.
Total: 5 blocks / 292 bytes per card, 60 blocks / 3504 bytes per 12-card list call.

Allocation return addresses reported by Valgrind: 0x1F4072, 0x1F40B0, 0x1F40BE, 0x1F40CC, 0x1F40F4. Disassembly maps these to initial allocations before copy_header; executable offsets are 0xec072, 0xec0b0, 0xec0be, 0xec0cc, 0xec0f4.

Additional list-call leaks:
- Result decks buffer: 288 direct bytes per list + nested deck/string storage. The repro's resp (DeckListResp from a general function call) has no registered model drop: freshOwned / declaration registration currently cover no-init and get_copy, not general owned model-return calls.
- KV iterator object: 56 direct bytes per call (aiml_iter_create_kv); iterator cleanup is missing here.

Valgrind totals:
0: definitely 60707 bytes / 309 blocks; indirectly 17684 / 494.
10: definitely 99187 bytes / 929 blocks; indirectly 28658 / 664.
Difference: 49454 bytes / 790 blocks over 10 calls, including direct+indirect. Tiny incidental variation is possible. The quoted 110 blocks per call is not reproduced by this current executable; exact attribution to an older build would require that executable and its log.

## Separate return-null issues

null.aiml forces the early-null path after get_copy. Trace has DECL dat but no DROP dat. safeToCleanup in visitRetStmt only admits void/scalar/owned-base returns, so pointer/interface null return fails that gate.
The same probe additionally crashes on dropping the null interface value: kind-5 emitScopeDrop loads the free slot from the null vtable without checking null. Exit signal 11, address 0x8. This is independent of the successful-card leak.

## Fix direction (not implemented)

Implement owned destination replacement semantics for container field copies: preserve/copy source before releasing an owned destination when aliases are possible. Do not simply free every destination: borrowed fields and self-assignment require ownership/alias handling. A carefully scoped fix for fresh default-owned container storage is a possible first step.
Handle owned model-return values at the caller, iterator destruction, null-return cleanup and null-interface destruction separately. Verify each by Valgrind allocation stacks and slope comparisons, plus self-copy/borrowed-field and early-return regression cases.
