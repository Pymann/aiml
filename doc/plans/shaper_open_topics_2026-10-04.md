# Shaper / glbview — open AIML topics, 2026-10-04

## Origin and basis of review

Comparison of the user's list with `../../../shaper/acceptance/P0/findings.md`, the
project READMEs and selectively read AIML code and specs. No renewed
defect reproduction or Valgrind measurement. W/D IDs come from the shaper P0
register; IDs of the same name in other AIML plans are not meant.

Existing decisions stand: W1, W3, W4 and defects that are explicitly only
to be reported are not released for repair here. The W10 migration
still requires a decision. This comparison changes documentation only.

## Current project state

According to the README, shaper now supports primitives, open surfaces, union,
hull, mirroring, UV atlas and GLB/SVG/PNG output. Textures are explicitly
not supported yet: textured leaf surfaces remain functional remaining work.
Existing UVs and transparency material values do not replace leaf images.

`buffer.write_to` and `buffer.encode_png` are now present in spec 11 and in the
builtin register. Do not implement them again as missing. That does not close
W4 about the separate contract of `buffer.write`.

## Decisions and restrictions

| ID | Finding | Handling |
|---|---|---|
| W10 | Borrow as `ptr<T>` rejected in a declaring initialisation, a later `set` still possible according to the register. | Spec 61 gap. Reported: 43 tests, 7 cosmos sites, 0 in 9rooms; not recounted here. Before implementation, renew the inventory and plan the migration together with diagnostics. |
| W1 | Literals are produced as f32; mixed f64 expressions can yield invalid IR. | Keep only reporting. f64 intermediate values avoid mixed types, but do not restore previously lost decimal precision. |
| W3 | Spec 22: typed multi-part catch clauses; according to the register only one catch-all. | Keep only reporting; shaper diagnostics as data/status. Spec 61 unwind must still be observed separately. |
| W4 | The spec 11 write contract deviates from native stores: endianness, strings and bounds/exceptions; the register also reports no growth. | Keep only reporting; check capacities and pack in a controlled way. Do not derive automatic growth as a spec obligation from the summary alone. |

W10 is not an open design decision about ownership: spec 61 already defines the
borrow. What is open is the release of the migration scope. Do not blanket-replace
`ptr` by `ref`: check the owner lifetime and invalidating mutations;
acquire real ownership only through a defined move-out operation or a clone.

## Reported defects

| ID | Finding | Classification |
|---|---|---|
| D29 | About 430 B definitely lost per `db.open`, not per query according to the report. | Class 6: check runtime/registry ownership and shutdown, no manual release of borrowed store handles as a workaround. Not re-measured here. |
| D23 | zip_entries/tar_entries do not free their own elements. | Check container, elements and their names separately; according to the register not used by shaper. |
| D19 | u8 output as bool and wrong comparisons above 127. | Relevant for binary data. Reproduce output and comparison separately; test 127/128/255. An i32 workaround is not a fix. |
| D11 | Multi-file JIT of shaper fails on external relocations. | shaper/glbview stay AOT. Do not declare every multi-file program affected across the board. |
| D1 | Numeric operator promotion. | The user's list names f64 with i32, the P0 register f32 with f64. Check both cases separately; document the connection with W1. |
| D6 | The member name input leads to a crash according to the register. | Name resolution/diagnostics; renaming stays the workaround. |
| D10 | sys.get_arg in an imported method fails under JIT. | Read arguments in the entry module; investigate the relation to D11, do not merge them without evidence. |

Additionally open in the original register, not named in the summary:

- **D5:** i32 to i64 for call arguments with zero instead of sign extension.
- **D8:** unknown `lifetime: persistent` in a new initialiser silently ignored.
- **D9:** dyn_array probe 21 reported 16 B definitely lost; re-measure on the current
  compiler. Spec 61 relevant, even if shaper avoids the type according to the register.
- **W6:** globber missing; shaper has its own FileGlob, only report.
- Intermittent test_http_auth_helpers failure in the register; a separate test/
  network finding, not a demonstrated shaper defect.

Historical entries are thus neither reproduced again nor declared done.

## glbview documentation findings missing so far

Local IDs DOC-G1 to DOC-G5 avoid collisions with existing W IDs.

| ID | Observation | Next clarification |
|---|---|---|
| DOC-G1 | ui.init is missing in spec 44.3; an explicit call was reported as practically necessary. `aiml_ui_begin`, however, initialises by itself when g_ui is missing (`src/sys/ui_runtime.cpp`). | Reproduce the concrete call order and earlier style/widget accesses. Do not derive a general obligation from the report alone. |
| DOC-G2 | ui.button is bool according to the table, but a test and an example in spec 44.3 use i32; the runtime returns int32_t. | Distinguish the C ABI from the AIML type; merge registration, conversion, tests and documentation into one contract. |
| DOC-G3 | input.init: ptr in 44.0, window_handle in 44.1. | Align the public signature and migration examples; the same representation does not replace a type contract. |
| DOC-G4 | 43.2 names concretely gladLoadGLLoader(SDL_GL_GetProcAddress), but elsewhere allows SDL/GLFW and GLAD/GLEW. The runtime uses GLEW, the engine GLFW according to 44.1. | Document loader and window backend separately; do not declare SDL contexts invalid across the board. |
| DOC-G5 | aUV in 43.2 §4.1 at location 2; gl.create_mesh binds position=0, UV=1, normal=2 (`src/sys/gl_runtime.cpp`). | Adapt the example to the mesh contract and check a textured reference case. Freely configured VAOs are a different case. |

## Recommended priority — no repair release yet

1. Delimit and reproduce the spec 61 findings W10, D29, D23, D9; record the actual
   owners and affected applications.
2. Plan numeric correctness D19, D5, W1/D1 and W4 together; respect previous
   report-only decisions.
3. Clean up DOC-G5 and the UI/input/backend contracts after a signature comparison.
4. Investigate D10/D11 as a JIT package; AOT stays the usable route until acceptance.
5. Plan the remaining language/diagnostic topics and the missing texture support separately.

## Spec 61 as the acceptance criterion

Every later repair needs positive/negative regressions, a native functional
check and a separate memory check. No switching off of lifetime drops and
no persistent to hide an ownership error. Explicitly cover returns, temporary values,
error paths, aliasing and container element releases.

A green safety exit is not proof of no leaks. Compare repetitions in the same process,
the compiler/runtime baseline and actual leak counts. Evaluate class 6
lifetime, losses per open and losses per processing step separately.
No statement about current leak freedom from this note alone.
