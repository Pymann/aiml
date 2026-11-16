# AIML VFX foundations — implementation and acceptance

Date: 2026-10-05. Implements the AIML work in
[ShapeML VFX 1 sections 10 and 13.1](../../../shaper/doc/spec/vfx_v1.md), under
[graphics spec 43.2](../../spec/43_2_graphics.md) and
[memory spec 61](../../spec/61_memory_management.md).
This is the runtime/compiler layer, not a claim that shaper/glbview VFX exists.

## Implemented

- `gl_vao.attrib_divisor(location, divisor) -> bool`.
- `gl.draw_elements_instanced(vao, index_count, instance_count) -> bool`.
- `gl_buffer.reserve_bytes(capacity) -> bool` and
  `gl_buffer.update_range(offset, data: buffer<u8>) -> bool`.
- C runtime declarations, typed compiler lowering, JIT symbol registration and
  AOT runtime linkage. Wrong arity/argument types produce diagnostics.
- Checked signed ranges/overflow, attribute limits, index values, instance
  divisors and backing storage. Zero-count draws are no-ops.
- Transactional buffer growth: preserve contents and VAO bindings on failure;
  successful growth rebinds managed VAOs while retaining logical resource identity.
  Bounded subrange uploads allocate no new GPU storage. Index buffers retain a
  private CPU shadow for validating referenced vertex ranges.
- Spec-61 Class-4 buffer/VAO owners: scope/return/unwind drops, moves, destination
  replacement, model-field/container-of-model teardown, early delete nulling,
  discarded/temporary producer cleanup and rejection of borrowed delete/move.
  Recursive cloning of these GL owners is rejected, including through containers.
- Named upload buffers remain borrowed. Temporary producer uploads are freed
  after the synchronous call. VAO binding metadata borrows wrappers without
  retaining ownership and is invalidated on early buffer deletion.

## Validation

`exodus build --all` completed, including native compiler, native shared runtime,
test target and WASM runtime archive. The WASM archive compiles; browser/WebGL
execution was not performed. Existing warnings in input/mirror/prefab runtime
are unrelated to the changed files.

`python3 tests/gl_vfx/run.py --valgrind` passes:

- JIT and separately linked AOT pixel checks: two colored instances, correct
  positions, preserved geometry after reserve and a targeted color-byte update.
- Native GL test with deterministic allocation-failure injection: old buffer
  remains usable and bound; growth preserves data; checked draws reject invalid
  index values, insufficient backing storage and deleted buffer borrows.
- Nine negative compiler cases: arity, numeric type, upload element type, VAO
  type, raw/model/container clone and borrowed delete/move.
- Independent GLB fixture reads distinct TEXCOORD_1 values, root shaper_vfx extras
  and embedded PNG pixels through existing glTF APIs. Existing animation fixtures
  exercise sampled node/joint transforms. No extra loader accessor was necessary.
- Existing graphics/material, texture-owner, timer-query, glTF metadata, animation
  and image-owner regression tests.
- 32 ownership rounds covering owner return, local/field moves, replacement,
  container-of-model destruction, early delete, temporary uploads, discarded
  owners, a temporary VAO argument and exception unwind.
- Valgrind on the AOT executable: **0 errors; 0 definitely, indirectly or possibly
  lost bytes; no suppressions**. 383,792 bytes remain reachable at process exit;
  this is not a claim of zero process/driver retained memory.

The spec-61 regression command is `bash tests/lifetime/run_lifetime.sh`.
Final results: **203 positive cases passed, 27 negative cases rejected as expected,
2 warning cases matched; zero failures** on the final compiler build.

## Reproduction and limits

Run from the AIML project root with a desktop GL context available:

```sh
source ../../exvenv/bin/activate
exodus build --all
python3 tests/gl_vfx/run.py --valgrind
bash tests/lifetime/run_lifetime.sh
```

The focused runner writes binaries, generated transport fixtures and logs to
`/tmp/aiml-vfx-tests` (override with `--output`). It needs no network or image tools.
GPU owners must finish before their GL context is destroyed. GL operations must
use the owning current context; external raw-GL mutation of tracked buffer/VAO
storage is outside these checked APIs. Particle simulation, shader families,
transparent VFX queues and authored effects remain work in shaper/glbview.
