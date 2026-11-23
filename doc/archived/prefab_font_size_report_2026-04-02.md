# Prefab Font Size Report

Date: 2026-04-02

## Summary

`font_size` for prefab labels currently does not take effect correctly.

The values are read in from AIML and stored in the runtime, but not used when rendering. As a result, texts on cards effectively all appear in the same font size.

## What works correctly

- `font_size` is defined differently in the prefabs, e.g. in `CardFull` and `CardToken`.
- The AIML builder reads `font_size` correctly from the prefab source.
- The prefab runtime also parses and stores `font_size` correctly in the child.

## Where the error really sits

The error is not primarily in the compiler, but in the prefab runtime:

- When rendering `CHILD_LABEL`, only the globally set prefab font is ever used.
- `child.font_size` is ignored there.
- `push_text_batch(...)` only works with the metrics of the font atlas baked once.

Practical consequence:

- all prefab labels look the same size
- if the globally set prefab font was loaded large, small labels look too large as well

## Why the cards stand out in particular

In `board_screen` the prefab font is set relatively large:

- `ui.load_font_default(24.0)`
- then `engine.prefab_set_font(...)`

Since no per-label size is applied any more, the card labels render with this global font metric instead of their local `font_size` values like `11`, `10`, `9` or `6`.

## The fix, simply explained

There are two possible directions:

### Option A: scale per label

Idea:

- a base font is loaded once, e.g. 24 px
- when rendering a label a factor is computed from it
- example: the label wants 12 px, the base font is 24 px -> render scale `0.5`

Advantages:

- a relatively small intervention
- no re-baking of the font for every size

Disadvantages:

- small/large sizes can look somewhat blurrier than real, separately baked fonts

## Option B: cache fonts per size

Idea:

- for every required `font_size` a separately baked font is loaded or cached
- the label then picks the matching font with its real metrics

Advantages:

- typographically cleaner
- more exact sizes and spacing

Disadvantages:

- more runtime complexity
- more atlas/memory management

## Recommendation

For `9rooms` I would start with option A.

Reason:

- it fixes the visible problem immediately
- it is considerably smaller and less risky
- if needed, it can still be extended to font caching per size later

## Effort

- Option A: about `0.5 to 1 day`
- Option B: about `1 to 2 days`

## Status of earlier reports

I did not delete any old reports.

Reason:

- in `test/aiml` there are currently only the two existing reports
- without a clear instruction which of them should go, deleting is needlessly risky
