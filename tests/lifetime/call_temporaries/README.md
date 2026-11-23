# Spec 61 call temporaries: regressions

Status (2026-10-02): **all 44 programs compiled and run — exit 0, 0 definitely/indirectly
lost bytes, no Valgrind errors.** Before the first run the programs were brought into valid AIML
(missing string quotes, the abolished hyphen notation, seven `main`s
over 300 lines split into `main_part_1/2`, the `buffer.len` expectation per spec/11 with `zero`).

All programs expect exit code 0. The AOT leak gate additionally requires
0 definitely and 0 indirectly lost bytes as well as no Valgrind errors.
Throw probes check the catch count and the exception message; an unexpected normal
return from a call expected to throw leads to exit code 90.

| Files | Coverage |
|---|---|
| `forward*`, `chain*`, `fnptr*`, `throw*`, `iface_consumer*` | Six original patterns, each with a direct temporary variant and a `_local` control at 10/1000 iterations; additionally two forward ordering controls (26 programs). |
| `ptr_dispatch_{free,method,interface,fn}` | Named and freshly created `ptr<Card>` on normal return and consumer throw. A later throwing argument before the move; the named caller must still be readable afterwards (4 programs). |
| `unwind_{string,list,map,buffer,model}` | All four dispatch routes with the normal path, producer throw, consumer throw and a later throwing argument. Nested handlers and a surviving outer owner; string additionally interpolated directly (5 programs). |
| `consumed_rc_{string,list,map,buffer,shared,cascade}` | The named caller stays valid after the callee reference is released/replaced; a stored value survives the caller scope. Temporary passing and throw after retain, each over four dispatch routes (6 programs). |
| `condition_resets_once` | Repeated while condition, early return, exact producer call count via a nested receiver. |
| `void_sret_alias_returns` | Interface void/sret, indirect void call, a container return shares the argument reference and stays valid after caller temp cleanup. |
| `imported_inherited_signature` | Imported/inherited method via a field chain and a method of the same name with a scalar return value. |

`support/imported_factory.aiml` is only an import fixture and not an executable test target.
The five further original patterns come from
`9rooms_v2/leak_probes/review_20261002_more`; the forward probe is reduced independently of the game.
The tests need no 9rooms imports or production databases.

## Integration

The explicit manifest `cases.sh` is used by all three existing runners:

- `tests/lifetime/run_lifetime.sh`: functional acceptance in `spec61` mode; `ONLY=...` still applies.
- `tests/run_lifetime_leak_gates.sh`: mandatory section `REGRESSION`, separate from cases already listed as `GREEN`. A failure makes the gate fail; untested does not mean optional.
- `tests/run_memory_sanitizers.sh`: normal execution plus Valgrind memory safety with memV3 and lifetime drops active.

Targeted run:

```sh
ONLY=ptr_dispatch bash tests/lifetime/run_lifetime.sh
```

Open: complete gate acceptance in the full run, 9rooms end-to-end check and the planned
compile-time/binary-size measurements.
