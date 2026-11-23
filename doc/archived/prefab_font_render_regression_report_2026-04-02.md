# Prefab Font Render Regression Report

Date: 2026-04-02

## Short conclusion

The broken game render very probably does not come from `board_screen.aiml` setting too large a font, but from a struct layout mismatch between `ui_runtime.cpp` and `prefab_runtime.cpp`.

## Root cause

`prefab_runtime.cpp` defines its own local `AimlUIFont` struct and itself comments that it must match `ui_runtime.cpp` exactly.

In [prefab_runtime.cpp](../../src/sys/prefab_runtime.cpp#L41) it currently is:
- `info`
- `ttf_data`
- `atlas_tex`
- `cdata`
- `line_height`
- `ascent_px`
- `scale`
- `atlas_w`, `atlas_h`

In [ui_runtime.cpp](../../src/sys/ui_runtime.cpp#L64), however, it was extended by:
- `ttf_size`
- `baked_size_px`

So the layout is no longer identical.

## Why that wrecks rendering

Prefab rendering reads fields directly from `AimlUIFont*` in several places, e.g. in [prefab_runtime.cpp](../../src/sys/prefab_runtime.cpp#L587):
- `lf->atlas_tex`
- after that indirectly also glyph data and metrics in `push_text_batch(...)`

When `prefab_runtime.cpp` interprets the font object with the wrong layout, it reads shifted offsets:
- `atlas_tex` is potentially no longer the real texture handle
- `cdata`, `line_height`, `ascent_px`, `scale` can be read from wrong memory positions
- so broken glyphs, wrong positions, implausible sizes or complete visual garbage are plausible

That is a hard ABI/layout error, not a mere tuning problem.

## Placement relative to the new font logic

The new font cache idea in `prefab_runtime.cpp` makes sense in itself.

The actual regression point is very probably the combination of:
- extending `AimlUIFont` in `ui_runtime.cpp`
- but not updating the mirror struct in `prefab_runtime.cpp`

## Not the main culprit

The spot in `test/9rooms/client/board_screen.aiml#L929` with `ui.load_font_default, 24.0` is probably not the actual cause.

It can make texts large, but it does not explain why the game as a whole "no longer renders properly". That symptom fits the layout mismatch much better.

## Recommended fix

### Fix A: enforce a shared font definition

Cleanest:
- do not define `AimlUIFont` twice
- move the shared struct definition into a shared header and use it in both runtimes

That removes the error class permanently.

### Fix B: short-term minimal fix

If it has to be quick:
- bring the local `AimlUIFont` definition in `prefab_runtime.cpp` exactly up to the new state of `ui_runtime.cpp`

But that is only the less robust variant.

## Effort

- Minimal fix: `0.25 to 0.5 days`
- robust shared header fix: `0.5 to 1 day`

## Assessment

Probability that this is the current render bug: high.
