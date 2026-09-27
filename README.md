# PhotoClone — a Photoshop-like image editor (Qt5 + OpenCV, Linux)

*(Version française : [README.fr.md](README.fr.md))*

A raster image editor in C++17: layers, masks, selections, filters, color adjustments, text, undo history,
a tabbed/dockable workspace, and keyboard shortcuts matching Photoshop's.

> **Note on the UI language**: the application's menus, tool names, and messages are currently in French
> (the person this was originally built for works in French). The code itself — identifiers, comments — is
> in English/French mixed by module (see below); this README describes the English concepts with the French
> UI labels quoted where it helps you find them in the app.

## Building

```bash
sudo apt install build-essential cmake ninja-build qtbase5-dev libqt5svg5-dev libopencv-dev
cmake -S . -B build -G Ninja
cmake --build build
./build/PhotoClone [image ...]          # or drag-and-drop files onto the window
QT_QPA_PLATFORM=offscreen ./build/smoke_test   # automated test (engine + tools + history + UI)
```
Tested with Qt 5.15 / OpenCV 4.6 / GCC 13 (Ubuntu 24.04). OpenMP is used when available.

## Architecture

```
src/
├─ core/        MODEL (no UI dependency, except QImage/QPainter for text rendering)
│  ├─ Document        layer stack, selection, cached composite image (region-based recompositing), QUndoStack
│  ├─ Layer           8-bit BGRA + 8-bit mask + properties (opacity, blend mode, visibility, lock) + editable text
│  ├─ BlendModes      18 blend modes (separable and non-separable, W3C formulas)
│  ├─ Selection       selection masks: shapes, magic wand, combination, feathering, contours
│  ├─ Commands        PixelCommand (diff-based), LayerStateCommand, StructureCommand, SelectionCommand
│  ├─ Operations      all "business" operations (layers, image, selection, clipboard…) = 1 undo entry each
│  ├─ ImageIO         PNG/JPEG/TIFF/WebP/BMP/PPM + native .pcl project format (keeps layers, masks, text)
│  └─ Workspace       shared state: foreground/background color, tool settings, current view
├─ effects/     Effect = a pure cv::Mat→cv::Mat function + a declarative description of its parameters
│  ├─ Filters         27 filters          ├─ Adjustments   16 color adjustments (levels, curves, hue/sat…)
│  └─ EffectRegistry  the UI (dialog + live preview) is generated automatically from the ParamDef list
├─ tools/       Strategy pattern: each tool receives ToolEvents (image coordinates) and draws its own overlay
│  ├─ Tool / ToolManager (tool groups, Shift+key to cycle within a group)
│  ├─ TransformBox    interactive frame (move / scale via 8 handles / rotate), shared by:
│  │    TransformTool (pixels, Ctrl+T) and SelectionTransformTool (selection outline + content)
│  ├─ BrushEngine     shared stamping engine (brush, pencil, eraser, clone, blur, sharpen, smudge, dodge/burn)
│  ├─ FloatingContent "lifted" content shared by the Move tool and Free Transform
│  └─ Selection/Paint/Move/Other Tools
└─ ui/          MainWindow, CanvasView (zoom/pan/checkerboard/marching ants), OptionsBar, ToolBox, panels, dialogs, theme
   └─ MovableDialog   dialogs embedded and draggable inside the main window (see below)
```

**Undo history**: commands keep shared `cv::Mat` headers (no pixel copy); only brush strokes store a rectangular
diff. The invariant (linear undo stack ⇒ a buffer is only ever mutated in place while it is the current state) is
checked by `smoke_test`: undoing everything then redoing everything yields pixel-identical images.

**Adding a filter**: one entry `R.add("filter.x", "Name…", "Category", {P::Int(...)}, lambda)` in `Filters.cpp` —
menu entry, dialog, live preview and undo are all generated automatically. **Adding a tool**: derive from `Tool`,
register it in `ToolManager`, add an icon in `Icons.cpp`.

## Features

| Area | Contents |
|---|---|
| Tools | Move, Rectangular/Elliptical selection, Lasso, Polygonal lasso, Magic wand, Crop, Eyedropper, Brush, Pencil, Eraser, Clone stamp, Gradient (5 types), Paint bucket, Blur, Sharpen, Smudge, Dodge/Burn, Text, Shape, Hand, Zoom, Free Transform |
| Selection | New / add (Shift) / subtract (Alt) / intersect, feather, grow, shrink, smooth, invert, reselect, marching ants, **Selection Transform** (scale / rotate / move the selection outline, optionally dragging the active layer's pixels along with it, plus numeric width/height/angle entry) |
| Layers | New, duplicate, delete, reorder (drag & drop), merge (down / visible / flatten), 18 blend modes, opacity, lock, visibility, layer masks (paint directly on the mask), editable text layers, layer via copy/cut |
| Image | Image size, canvas size (with anchor), crop, rotate, flip |
| Color | Brightness/Contrast, Levels, Curves (RGB + per-channel), Exposure, Hue/Saturation, Vibrance, Color Balance, Black & White, Photo Filter, Shadows/Highlights, Invert, Desaturate, Threshold, Posterize, Auto Tone/Contrast/Color; Histogram, Color, and Swatches panels |
| Filters | Blur (Gaussian, box, motion, surface, median), sharpen (unsharp mask, detail enhance), noise, emboss, find edges, cartoon, pencil sketch, watercolor, mosaic, twirl, wave, spherize, vignette, high pass, min/max, clouds |
| Edit | Copy / cut / paste (system clipboard, paste from other apps), copy merged, paste in place, fill foreground/background, clear, repeat last filter (Ctrl+F) |
| Workspace | **Movable in-window dialogs** (position remembered), multi-document tabs, dockable panels, dark theme, zoom (Alt+wheel, Ctrl+±, Ctrl+0/1, Zoom tool), pan (Space, middle-click, wheel), grid, drag-and-drop files, saved layout |

## Keyboard shortcuts (matching Photoshop)
`V` Move · `M` Selection (Shift+M: ellipse, then Selection Transform) · `L` Lasso (Shift+L: polygonal) · `W` Magic wand · `C` Crop · `I` Eyedropper ·
`B` Brush (Shift+B: pencil) · `S` Clone stamp · `E` Eraser · `G` Gradient (Shift+G: bucket) · `R` Blur/Sharpen/Smudge · `O` Dodge/Burn · `T` Text ·
`U` Shape · `H` Hand · `Z` Zoom · `X` swap colors · `D` default colors · `[` `]` brush size · `{` `}` hardness ·
`Space` temporary pan · `Alt` eyedropper (brush) / source point (clone) · `Shift+click` straight line ·
`Ctrl+N/O/S/Shift+S/W` · `Ctrl+Z` / `Ctrl+Shift+Z` · `Ctrl+X/C/V` · `Ctrl+A/D/Shift+D/Shift+I` · `Ctrl+J` · `Ctrl+Shift+N` · `Ctrl+E` ·
`Ctrl+T` · `Ctrl+L/M/U/B/I` · `Ctrl+Shift+U/L` · `Ctrl+F` · `Ctrl+0/1/±` · `Alt+Backspace` / `Ctrl+Backspace` · `Tab` · `F1` (full list, in-app).

## Selection Transform (tool in the `M` group, or the Selection menu)
Handles surround the selection: dragging a handle scales it (Shift: keep proportions, Alt: from the center), dragging
outside the frame rotates around the center (Shift: 15° steps), dragging inside moves it. The Width/Height/Rotation
fields in the options bar allow numeric entry. Enter or double-click confirms, Escape cancels; switching tools or
triggering a menu action also confirms.

**Content**: by default, pixels of the **active layer** that fall inside the selection travel with the transform,
with a live preview (the old spot is cleared, like a "cut, then paste transformed"). Pixels and outline are
committed — and undone — as **a single history entry**. Uncheck "Transform layer content" in the options bar to
transform only the outline. If the active layer is hidden or locked, only the outline is transformed (a message
is shown). Text layers are rasterized on commit. `Ctrl+T` (Free Transform) does the same thing on the selection,
or on the whole layer if there is no selection.

## Movable dialogs
The application's dialogs (filters and adjustments, sizes, new document, text, color picker, numeric prompts) are
floating frames *embedded as children of the main window*: you drag them by their title bar (they always stay
inside the window, including under Wayland), their position is remembered per dialog type, and by default they
open in the top-right corner so the image stays visible. While a dialog is open, menus, panels and shortcuts are
disabled, but you can still pan/zoom the image (hand tool, Space, wheel, Alt+wheel) to judge the live preview.
Only file open/save dialogs and alert boxes remain native system windows. To create a new one: derive from
`MovableDialog` (the `exec()` call works the same way as a normal `QDialog`).

## Known limitations / possible future work
Not implemented: quick selection, magnetic lasso, pen tool / vector paths / shape layers (shapes are rasterized),
layer styles (fx), smart objects, layer groups, non-destructive adjustment layers (adjustments are applied
directly to pixels), spot healing / healing brush, Liquify, content-aware fill, quick mask, rulers and guides,
navigator, channels panel, 16-bit / CMYK / ICC color profiles, PSD import/export, tablet pressure sensitivity.
Layers are always the size of the document (simple and robust; a "layer + offset" model would use less memory).
Text is edited through a dialog box rather than directly on the canvas.
