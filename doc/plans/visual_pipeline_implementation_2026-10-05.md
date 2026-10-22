# Visual pipeline implementation and acceptance

Status: specified, not implemented; 2026-10-05. Implementation completion requires
the stage evidence below. This plan does not mark existing P10 work as finished.

## 1. Contracts and decisions

| Contract | Authority |
|---|---|
| [ShapeML 2](../../../shaper/doc/spec/visual_pipeline_v2.md) | source schema, geometry, recipes, baking, export |
| [glbview studio](../../../glbview/doc/spec/studio_renderer_v2.md) | shading, pass ABI, profiles, scheduling |
| [AIML 43.5](../../spec/43_5_stylized_rendering.md) | runtime methods, capabilities, failure/ownership rules |
| [VFX 1 follow-on](../../../shaper/doc/spec/vfx_v1.md) | proposed effects schema, runtime and budgets; independent F1-F5 packages, not an additional V1-V7 acceptance gate |
| [Existing shaper](../../../shaper/doc/spec/shaper.md) | v1 compatibility and P10 skin binding |
| [Memory model](../../spec/61_memory_management_DRAFT.md) | ownership and cleanup requirements |

These are normative implementation targets. Fix disagreements in the owning
contract and its consumers. Preserve unrelated local changes and earlier user
decisions about separately reported compiler defects.

Decisions: explicit ShapeML 2; v1 generation retained; author RGB defaults to
sRGB, baking/shading linear; portable PBR plus optional custom ABI; three lights
and offline environment; CPU-baked AO/detail; real bevels and sweep/loft geometry;
opaque stylized crystals; balanced 60/economy 30 FPS and idle event waiting.
Deferred: shadow maps, SSAO, SSR, OIT, transmission, generic CSG bevel, automatic
LOD and circuit routing. These are not hidden tasks required for baseline completion.

## 2. Baseline

Inspected shaders, loading, baking, engine/graphics runtime, Golem reference and
saved screenshots. Structural counts: Scout 2,024 triangles/38 primitives; spider
13,216 triangles/104 primitives. These are not GPU timings. Capture fresh images
before implementation; saved screenshots may predate the current source.
Existing material getters, alpha fields, samplers, FPS cap and VSync are integration
work. Verify P10 implementation/tests against its locally modified specification.
Historical notes describing missing textures may refer to earlier milestones.

## 3. Work packages

| Stage | Work | Entry | Exit evidence |
|---|---|---|---|
| V1 Foundation | material/texture-info access, sRGB, alpha/culling, framebuffer pixels, cap/idle; final triangulation and MikkTSpace basis for existing shapes | baseline | color/material/idle fixtures, tangent-basis fixture, legacy comparison |
| V2 Studio | HDR, GGX, environment assets, tone mapping, FXAA, debug views | V1 | independent renderer GLB fixture, explicit capability fallback |
| V3 Surfaces | v2 schema, recipes/maps, AO/BVH, ORM/emission export | V1 | deterministic maps, valid portable GLB |
| V4 Geometry | bevel/chamfer, explicit normal policies, sweep/loft; extend V1 tangent pipeline to new geometry | V3; verified P10 for skin export cases | manifold/seam fixtures and exported skin data checks |
| V5 Shader ABI | versioned helpers/uniforms, skin variants, pass states/cache | V2 + verified P10 | built-in/custom parity, failure fallback, alpha order |
| V6 Resources | profiles, bloom, telemetry, resize/fallback, batching/report | V2-V5 | resource checks, report on named hardware |
| V7 Art | Golem probe, shoulder/torso/face study, full character | V2-V6 | reviewed beauty/debug captures at intended sizes |

V2 and V3 are independent after V1. V1 finalizes the shared triangulation/UV/normal/
tangent contract before V3 normal baking; V4 extends it without changing the basis
used by existing shapes. V3 edge masks first cover existing geometric edges;
bevel-strip provenance is added and tested with V4. V4 skin acceptance checks
exported data; animated renderer parity is gated by V5. This does not instruct
spawning agents.

V2 uses a checked-in ordinary glTF fixture with existing box/sphere geometry,
known PBR factors and small authored base/normal/ORM/emissive textures. Its fixture
builder uses the independent test GLB writer and requires neither ShapeML 2 nor
bevels, procedural baking, P10 or visual_report.json. The complete SHAML Golem
probe is an integration gate after V3/V4/V6, not an entry or exit gate for V2.
Keep implementation commits scoped by stage/subsystem. Extend existing modules
where responsibilities still fit; separate AO, geometry builders and render graph
only as their own contracts require. Integrate MikkTSpace and FXAA with upstream
licenses. Viewer bundles environment/helper assets; no runtime web download.

## 4. Change map

| Area | Work |
|---|---|
| shaper spec_reader/yaml_check/types | version dispatch, validation, resolved linear materials |
| evaluator/mesh_builder/hull_op/curve_ops | bevels, profiles, stable frames, feature-edge provenance |
| texture_baker/uv_packer | channels, differentials, AO groups, dilation |
| glb_writer/joint_writer | maps/factors/extensions, metadata, consistent skin export |
| glbview scene_loader/DrawItem | material identity table, full bindings, lifetime/cache |
| shading/pass_library | BRDF, helpers, ABI variants, complete render state |
| viewer/orbit_camera | passes, content aspect, quality/idle/debug controls |
| AIML gltf/gl/engine runtimes | 43.5 methods, capabilities, rollback and cleanup |
| AIML registrations/linking/ownership | matching public/native signatures, resource destructors |

## 5. Functional acceptance matrix

Extend existing fixture infrastructure; each row is a behavior, not a new framework.

| ID | Case | Assertion |
|---|---|---|
| C01 | v1 models, new v2 keys under v1 | compatibility; new keys fail with version/path diagnostic |
| C02 | preset overrides and YAML key order | identical resolved result; duplicate/unknown keys fail |
| C03 | factor/baked/imported solid gray | linear values agree within one 8-bit encoding step |
| C04 | black-white color filter, flat normal | linear color filtering; data unaffected by gamma |
| C05 | metallic 0/1, roughness 0.15/0.8 | distinct expected highlights, unchanged geometry |
| C06 | emission 0/1/2 and AO 0/1 | emission applied once; AO affects indirect light only |
| C07 | opaque/mask/blend, mirrored/back faces | correct coverage, culling, depth and blending |
| C08 | shared image with different encoding/sampler | distinct correct uploads/bindings |
| C09 | object mask across UV charts | continuity before quantization |
| C10 | static vs moving-attachment AO | no cross-attachment baked stain |
| C11 | imported normal plus height; overlapping UVs | precise input rejection, no silent overwrite |
| G01 | bevel box segments 1/2/4; hull chamfer | closed geometry and preserved requested box bounds |
| G02 | excessive chamfer or unsupported segments | geometry/schema diagnostic respectively |
| G03 | flat/smooth/weighted, UV seam | intended creases, no triangulation-edge wear |
| G04 | bent/tapered/twisted sweep and loft | stable frames, profiles, seam, caps |
| G05 | reversal, self-intersection, bad section count | deterministic geometry/schema errors with paths |
| G06 | mirrored/skinned normal maps | correct tangent handedness, normals and palette |
| R01 | built-in vs equivalent ABI2 shader | material parity within image tolerance |
| R02 | failed second pass, mixed ABI1/ABI2 | whole-material fallback, documented compatibility path |
| R03 | alpha overlays and duplicate material names | per-item order, distinct state, no state leak |
| R04 | idle/paused/animated, input/resize/restore | correct redraw, wakeup and no time jump |
| R05 | HiDPI, wide/portrait, allocation failure | correct aspect/caps, atomic fallback without leaks |
| R06 | unavailable/pending/disjoint timer | null timing, never blocking or fake GPU values |
| R07 | ABI1 uTime-only shader, no clip; custom disabled; hidden/restore | visible capped updates; wall-clock uTime retained; disabling custom allows idle |
| R08 | 120 warm-up + 600 measured frames, delayed/invalid GPU results | frame-ID attribution, aggregate nearest-rank percentiles and explicit completeness |
| R09 | fixed output image across all profiles | actual internal dimensions recorded; separate per-profile baselines |
| E01 | GLB in independent conforming viewer | portable material appearance without custom extras |
| E02 | report vs independent GLB parser | actual counts/bytes, no factors applied twice |
| E03 | missing files and output failure | correct diagnostics, old successful outputs intact |

Closed-solid checks: positive volume, two oriented incident faces per topological
edge, no nonadjacent intersections/degenerate triangles. Compare positions with
max(1e-6 metres,1e-6*bbox diagonal). Normal/tangent unit length and orthogonality
within 1e-4 after float conversion; skin weight sum within 1e-5. Attribute seams
must not be mistaken for geometric holes. Bake determinism is for a fixed tool
version and platform; report timing fields are excluded from byte comparisons.

## 6. Visual acceptance

Use the datacreatures Golem PNG as art direction, not a pixel target for a different
model/pose. V2 starts with the independent renderer fixture defined in section 3.
After V3/V4/V6, generate the complete material-probe example in the ShapeML contract;
then author a torso/shoulder/face study before building the whole character in V7.
Review at 256,512,1024 output-image pixel character height, including turntable
and animation; these sizes do not override a profile's internal resolution cap.

Fixed captures: clip/uTime frozen, yaw 35, pitch 15 degrees, identical fitted camera
across look comparisons, exposure 0, environment bundle fixed, 1024x1024 output
content rectangle excluding UI, bloom off unless tested. Internal scene targets
obey profiles: economy 720x720, balanced/showcase 1024x1024 for this square output.
Scale the final FXAA image bilinearly to the output rectangle; never stretch aspect
or bypass the profile for captures. Save beauty plus base/normal/roughness/metallic/
AO/emissive. Record output and internal dimensions, source hash, commits, GPU/driver,
quality and fallbacks. Keep a separate baseline for each look/profile/debug view;
do not apply pixel-equality thresholds between different profiles.

Human checks: silhouette readable at 256 pixels; face/chest crystal dominate;
stone/gold/crystal differ; bevel highlights have no floating corner lines; no
black AO stains exposed by animation; no crawling grid, UV seam or inverted normal;
bloom optional and restrained. Geometry proportions still require art decisions.

Pinned-stack image regression: reviewed baseline MAE <=2/255, <=1% pixels exceeding
12/255. Exclude UI; cross-driver comparisons allow a 1-pixel silhouette band and
separate reviewed baselines. Check nonblank foreground and finite shader output.
These are regression thresholds, not an automatic aesthetic score. Inspect image
diffs before accepting a new baseline. Maps/GLB also get numeric/structural checks.

## 7. Performance and memory

Target hardware is not specified yet. Record CPU/GPU/driver/OS/power mode and
resolution. Software GL verifies correctness, not hardware speed. Warm 120 frames,
measure 600 uncapped benchmark frames for a hero turntable, an animated creature
and a repeated-instance stress scene, bloom off/on separately. Report p50/p95 CPU
submission and GPU scene+post; exclude load/compile/readback/present/wait time.
Use the renderer's benchmark_summary record, not averages of its 60-frame rolling
percentiles. It aggregates the original 600 samples using nearest-rank quantiles
(sorted index ceil(p*N)-1). Warm-up samples are excluded, delayed GPU results remain
attached to their originating frame IDs, and unavailable/invalid results are null.
Check measured_frames=600, cpu_valid=600, complete=true; GPU performance acceptance
additionally requires gpu_valid=600 and gpu_complete=true. An incomplete/unsupported
GPU run is reported as unmeasured, not a passed performance gate. The renderer
emits counts for missing/invalid GPU samples and drains pending queries for at most
1000 ms without drawing additional benchmark frames or blocking on a result call.
Then verify capped profiles separately. Capped FPS is not a GPU-cost measurement.

| Metric | Initial goal or hard behavior |
|---|---|
| hero geometry/draws | advisory 10k..25k triangles, 5..15 model draws where articulation permits |
| atlas cost | initially <=4 1K RGBA8 maps with mips, about 21.3 MiB |
| balanced 1080p GPU p95 | provisional <=8 ms on a recorded target GPU |
| CPU render submission p95 | provisional <=4 ms on recorded target CPU |
| active cap | 10-second average <=cap+1 FPS; slower rendering allowed |
| static idle/minimized | no model/post draws or swaps until needed; no busy loop |
| resource stability | no monotonic live-object growth after return to initial state |

Count model/ground/post/custom separately; batch only compatible material/attachment/
skin groups. 2K atlases cost four times 1K; 4K rendering shades four times 1080p
pixels. PNG bytes do not measure GPU memory. Hardware goals are provisional until
a target device is named and measured, never a promise for every GPU.

Memory acceptance follows spec 61: compare 1 versus 8 repeated loads/bakes in one
process and 100 resize/quality-toggle cycles. Track app owners, GL objects and
byte estimates separately from driver caches. Exercise decode/upload, partial
environment, incomplete FBO, shader and output-write failures. Preserve old valid
targets/assets; release new partial owners; GPU owners die before context; no
animation reads after scene release. No disabling lifetime checks or unrelated
defect workarounds to obtain a green report.

## 8. Completion evidence

Stage reports in each owning project's acceptance directory contain commands,
exit codes, captures, statistics, unresolved failures and exact commits. Update
READMEs/implemented-version specs when stages land; retain v1/ABI1 compatibility.
This specification phase changes documentation only. No new parser support,
visual improvement, GPU performance or leak freedom is claimed without execution.
