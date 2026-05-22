# AIML Scientific Visualization Specification

## 1. Goal

AIML should make it easy to draw scientific plots and tensor visualizations
without forcing the user into low-level graphics code.

This spec defines a small `viz` namespace for:

- line plots
- scatter plots
- bar charts
- heatmaps
- tensor slices
- tensor grids
- export to PNG and SVG

The design goal is simplicity first:

- data-oriented API
- few required objects
- deterministic output
- useful defaults

This is not a full plotting framework like matplotlib.
It is a compact scientific visualization layer for AIML programs,
tests, experiments, and model inspection.

## 2. Design Principles

### 2.1 Simple by Default

The shortest useful case should look like this:

```yaml
- decl: [fig, VizFigure, { call: [viz.figure, "Loss Curve"] }]
- call: [viz.line, fig, steps, losses]
- call: [viz.save_png, fig, "loss.png"]
```

### 2.2 Data First

The user should usually pass plain lists or tensors.
The runtime is responsible for scaling, axes, ticks, and layout defaults.

### 2.3 Reproducible Output

The same input data and style options should produce the same image.

### 2.4 Tensor-Native

Tensor inspection is a first-class use case, not an afterthought.

## 3. Core Types

### 3.1 `VizFigure`

Top-level figure object.

Fields:

- `title: string`
- `width: i32`
- `height: i32`
- `background: string`
- `subplots: list<VizPlot>`

### 3.2 `VizPlot`

One plot region inside a figure.

Fields:

- `title: string`
- `x_label: string`
- `y_label: string`
- `x_min: f32`
- `x_max: f32`
- `y_min: f32`
- `y_max: f32`
- `series: list<VizSeries>`

### 3.3 `VizSeries`

One drawable dataset.

Fields:

- `kind: string`
- `name: string`
- `x: list<f32>`
- `y: list<f32>`
- `z: list<f32>`
- `tensor_data: tensor`
- `style: VizStyle`

Notes:

- `kind` may be `"line"`, `"scatter"`, `"bar"`, `"heatmap"`, `"tensor2d"`, or `"tensor_grid"`.
- not every field is used by every series kind

### 3.4 `VizStyle`

Optional style hints.

Fields:

- `color: string`
- `line_width: f32`
- `point_size: f32`
- `alpha: f32`
- `colormap: string`
- `vmin: f32`
- `vmax: f32`
- `legend: i32`

## 4. Namespace `viz`

## 4.1 Figure Creation

### `viz.figure(title, width?, height?) -> VizFigure`

Creates a new figure.

Defaults:

- width: `1200`
- height: `800`
- background: `"white"`

Example:

```yaml
- decl: [fig, VizFigure, { call: [viz.figure, "Training Metrics", 1400, 900] }]
```

### `viz.subplot(fig, title?) -> VizPlot`

Creates a new plot area inside a figure and returns it.

If omitted, plotting functions may create a default single plot automatically.

## 4.2 Standard Plots

### `viz.line(target, x, y, style?) -> void`

Draws a line plot.

Accepted `x` and `y` forms:

- `list<f32>`
- `list<i32>`
- `tensor` with rank 1

If `x` is omitted in a future shorthand form, implicit indices `0..N-1` may be used.

### `viz.scatter(target, x, y, style?) -> void`

Draws a scatter plot.

### `viz.bar(target, labels, values, style?) -> void`

Draws a bar chart.

Accepted forms:

- `labels: list<string>`, `values: list<f32>`
- `labels: list<string>`, `values: list<i32>`

### `viz.heatmap(target, data, style?) -> void`

Draws a 2D heatmap.

Accepted forms:

- `tensor` with rank 2
- flat numeric data plus explicit shape in a future extension

Default colormap:

- `"viridis"`

## 4.3 Tensor Visualizations

### `viz.tensor2d(target, t, style?) -> void`

Visualizes a rank-2 tensor directly as a heatmap.

Equivalent to `viz.heatmap`, but semantically explicit for model inspection.

### `viz.tensor_slice(target, t, axis0, index0, axis1?, index1?, style?) -> void`

Extracts a rank-2 slice from a rank-3 or rank-4 tensor and visualizes it.

Examples:

- attention head from `[layers, heads, seq, seq]`
- channel slice from `[channels, height, width]`

The runtime must reject slices that do not resolve to rank 2.

### `viz.tensor_grid(target, t, layout?, style?) -> void`

Visualizes multiple rank-2 slices as a grid.

Typical use cases:

- all attention heads of one layer
- multiple convolution channels
- embedding projection tiles

Supported input:

- rank-3 tensor interpreted as `[n, h, w]`
- rank-4 tensor with one fixed axis chosen by implementation defaults or future options

### `viz.tensor_hist(target, t, bins?, style?) -> void`

Draws a histogram over all values in a tensor.

Useful for:

- activation distributions
- gradient distributions
- weight drift inspection

## 4.4 Labels and Axes

### `viz.set_title(plot, title) -> void`
### `viz.set_xlabel(plot, label) -> void`
### `viz.set_ylabel(plot, label) -> void`
### `viz.set_xlim(plot, min, max) -> void`
### `viz.set_ylim(plot, min, max) -> void`

These are optional overrides.

Without them, the runtime auto-computes ranges from the data.

## 4.5 Output

### `viz.show(fig) -> void`

Shows the figure in an interactive window when the backend supports it.

If interactive display is unavailable, the runtime should fail clearly.

### `viz.save_png(fig, path) -> void`
### `viz.save_svg(fig, path) -> void`

Exports the figure.

`PNG` is the default raster export.
`SVG` is preferred for papers and vector graphics.

## 5. Accepted Data Conversions

The runtime should accept these conversions automatically:

- `list<i32>` -> `list<f32>` for plotting
- rank-1 tensor -> vector input
- rank-2 tensor -> heatmap input

The runtime should reject ambiguous cases instead of guessing too much.

Examples of rejection:

- `viz.line(fig, rank2_tensor, rank1_tensor)`
- `viz.tensor2d(fig, rank3_tensor)`

## 6. Colormap Requirements

At minimum, these colormaps should exist:

- `"viridis"`
- `"magma"`
- `"gray"`
- `"blue_red"`

For signed tensor values, `"blue_red"` is the preferred default.

For strictly positive scalar fields, `"viridis"` is the preferred default.

## 7. Tensor-Specific Behavior

### 7.1 Value Range

For heatmaps and tensor plots:

- if `style.vmin` and `style.vmax` are set, they must be used
- otherwise, the runtime derives the range from the data

### 7.2 NaN and Inf

If tensor data contains `NaN` or `Inf`:

- the renderer must not crash
- invalid values should be drawn in a dedicated fallback color
- export should remain deterministic

### 7.3 Shape Annotation

Tensor-oriented plots should be able to expose shape metadata in titles or legends.

Example:

- `"attn_head_3 shape=[128,128]"`

## 8. Minimal Examples

### 8.1 Loss Curve

```yaml
- decl: [steps, "list<f32>"]
- decl: [losses, "list<f32>"]
- call: [steps.push, 1.0]
- call: [steps.push, 2.0]
- call: [losses.push, 0.9]
- call: [losses.push, 0.7]
- decl: [fig, VizFigure, { call: [viz.figure, "Loss"] }]
- call: [viz.line, fig, steps, losses]
- call: [viz.save_png, fig, "loss.png"]
```

### 8.2 Tensor Heatmap

```yaml
- decl: [fig, VizFigure, { call: [viz.figure, "Weights"] }]
- call: [viz.tensor2d, fig, my_tensor]
- call: [viz.save_svg, fig, "weights.svg"]
```

### 8.3 Attention Heads Grid

```yaml
- decl: [fig, VizFigure, { call: [viz.figure, "Attention Heads"] }]
- call: [viz.tensor_grid, fig, attn_heads]
- call: [viz.save_png, fig, "attn_heads.png"]
```

## 9. Non-Goals for Version 1

Not required in the first version:

- 3D interactive plotting
- animation timelines
- arbitrary chart composition DSLs
- full publication layout systems
- browser-first reactive dashboards

These may be added later, but the first version should stay compact.

## 10. Recommended Implementation Order

1. `VizFigure`, `VizPlot`, `VizStyle`
2. `viz.figure`
3. `viz.line`, `viz.scatter`, `viz.bar`
4. `viz.heatmap`, `viz.tensor2d`
5. `viz.save_png`
6. `viz.save_svg`
7. `viz.tensor_slice`, `viz.tensor_grid`, `viz.tensor_hist`

## 11. Summary

This spec defines a small scientific visualization layer for AIML.

The core promise is:

- easy plotting from lists and tensors
- first-class tensor inspection
- stable export for experiments and papers
- much simpler usage than the low-level graphics APIs
