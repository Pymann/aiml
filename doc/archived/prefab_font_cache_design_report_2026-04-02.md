# Prefab Font Cache Design Report

Date: 2026-04-02

## Summary

Yes, reusing already baked font sizes makes sense and should be implemented for prefab labels in the runtime.

The right place for it is not the AIML compiler but the prefab/UI runtime. The compiler does know the `font_size` values from the prefabs, but the actual baking of the font atlases happens at runtime, after the font file has been loaded.

## Why not in the compiler

The compiler only sees:

- prefab definitions
- child types
- `font_size` numbers

But the compiler does not automatically have:

- the loaded font file as TTF bytes
- the prefab font set at runtime
- the concrete DPI/scale context
- the moment at which `engine.prefab_set_font(...)` is called

That is why a compiler cache is the wrong level here.

## The right level: runtime

The runtime has all the information needed:

- the currently set prefab font
- the TTF data of this font
- the required `font_size` values per label
- the real screen/scale situation

So the solution should look like this:

1. A base font is loaded normally.
2. The runtime remembers the TTF source of this font.
3. For every required prefab label size a matching baked font is created on demand.
4. It is stored in a cache map.
5. Subsequent labels with the same size reuse the same baked font.

## Concrete design

### Data model

`AimlUIFont` should additionally keep:

- `ttf_data`
- `ttf_size`
- `baked_size_px`

The prefab scene should hold a cache, e.g.:

```cpp
std::unordered_map<int, AimlUIFont*> font_cache;
```

### Cache key

A quantised integer key is practical, e.g.:

- `round(font_size * 10)`

That way half or tenth sizes can also be reused stably.

### Lookup path

When rendering a label child:

1. Determine the target size
2. Compute the cache key from it
3. Check the cache
4. if not present: bake a font with exactly this size
5. store the result in the cache
6. render with this font

## Why that is fine for modern machines

The number of font sizes actually needed is normally small.

In the current `9rooms` prefab set, for example, only a few values are relevant:

- `6`
- `9`
- `10`
- `11`
- `12`
- `16`

That means:

- not thousands of atlases
- but only a few baked variants
- after that almost only cache hits

The memory and CPU effort is perfectly acceptable for today's machines.

## Advantages over a simple scale fix

Option B has these advantages:

- real typographic metrics per size
- cleaner glyph spacing
- sharper small texts
- fewer visual artefacts than merely scaling down a large atlas font

## Open points

Two things still need to be made clear in the design:

### 1. Invalidation on a font change

When `engine.prefab_set_font(...)` is called again, the cache must be cleared.

Otherwise baked sizes of an old font would keep being used.

### 2. Cleanup

The cached `AimlUIFont*` must be freed cleanly on a font change or at scene teardown.

## Recommendation

Recommendation: implement exactly this runtime cache variant.

In short:

- not in the compiler
- but in the prefab runtime
- bake lazily per size
- reuse via a map
- clear the cache on a font change

## Effort

Clean implementation including cleanup and rebuild:

- about `0.5 to 1 day`
