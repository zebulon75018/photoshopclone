# PhotoClone — a Photoshop-like image editor (Qt5 + OpenCV, Linux)

![a friends drawing](/logoclone.jpg)

*(Version française : [README.fr.md](README.fr.md))*

![screen shot](/shot1.png)


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
Tested with Qt 5.15 / OpenCV 4.6 / GCC 13 (Ubuntu 24.04); requires a C++20 compiler and CMake ≥ 3.28 (imposed by
vision.cpp, an AI dependency, see below). OpenMP is used when available.

The AI menu (vision.cpp and Stable Diffusion, see below) needs two extra dependencies **built from source** — nothing
vendored, no `.a`/`.so` files in this repo:
```bash
./depend/fetch-sources.sh   # once, after cloning: fetches pinned upstream source trees (~1 GB, needs git+network)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # builds PhotoClone AND both AI dependencies together (the first build takes a while)
```
See `depend/README.md` for how this works, why nothing is vendored, and portability/GPU/memory notes. Skip this step
and PhotoClone still builds and runs fine — the AI commands stay in the menu but report themselves unavailable.

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
│  ├─ Retouch         content-aware fill (Inpaint.h: cv::inpaint, or MI-GAN) — the only effect that is "selection-aware"
│  └─ EffectRegistry  the UI (dialog + live preview) is generated automatically from the ParamDef list
├─ ai/          AI features via vision.cpp (see "AI features" below): AIBackend (models + tasks), AIModels (paths),
│                 VispBridge (cv::Mat ↔ vision.cpp), MaskRefine (pure OpenCV post-processing, testable without models)
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

![screen shot](/shot2.png)

## Features

| Area | Contents |
|---|---|
| Tools | Move, Rectangular/Elliptical selection, Lasso, Polygonal lasso, Magic wand, Crop, Eyedropper, Brush, Pencil, Eraser, Clone stamp, Gradient (5 types), Paint bucket, Blur, Sharpen, Smudge, Dodge/Burn, Text, Shape, Hand, Zoom, Free Transform |
| Selection | New / add (Shift) / subtract (Alt) / intersect, feather, grow, shrink, smooth, invert, reselect, marching ants, **Selection Transform** (scale / rotate / move the selection outline, optionally dragging the active layer's pixels along with it, plus numeric width/height/angle entry) |
| Layers | New, duplicate, delete, reorder (drag & drop), merge (down / visible / flatten), 18 blend modes, opacity, lock, visibility, layer masks (paint directly on the mask), editable text layers, layer via copy/cut |
| Image | Image size, canvas size (with anchor), crop, rotate, flip |
| Color | Brightness/Contrast, Levels, Curves (RGB + per-channel), Exposure, Hue/Saturation, Vibrance, Color Balance, Black & White, Photo Filter, Shadows/Highlights, Invert, Desaturate, Threshold, Posterize, Auto Tone/Contrast/Color; Histogram, Color, and Swatches panels |
| Filters | Blur (Gaussian, box, motion, surface, median), sharpen (unsharp mask, detail enhance), noise, emboss, find edges, cartoon, pencil sketch, watercolor, mosaic, twirl, wave, spherize, vignette, high pass, min/max, clouds |
| Edit | Copy / cut / paste (system clipboard, paste from other apps), copy merged, paste in place, fill foreground/background, clear, **content-aware fill (Shift+F5, OpenCV inpainting)**, repeat last filter (Ctrl+F) |
| AI (vision.cpp) | **Remove Background** (BiRefNet, tunable, 4 output modes), **AI selection tool** (MobileSAM: click or drag a box), **depth map** (Depth-Anything V2), **MI-GAN inpainting**, **AI upscaling** (Real-ESRGAN) — all in the new **IA** menu; requires downloading model files (not bundled) |
| Workspace | **Movable in-window dialogs** (position remembered), multi-document tabs, dockable panels, dark theme, zoom (Alt+wheel, Ctrl+±, Ctrl+0/1, Zoom tool), pan (Space, middle-click, wheel), grid, drag-and-drop files, saved layout |

## Keyboard shortcuts (matching Photoshop)
`V` Move · `M` Selection (Shift+M: ellipse, then Selection Transform) · `L` Lasso (Shift+L: polygonal) · `W` Magic wand · `C` Crop · `I` Eyedropper ·
`B` Brush (Shift+B: pencil) · `S` Clone stamp · `E` Eraser · `G` Gradient (Shift+G: bucket) · `R` Blur/Sharpen/Smudge · `O` Dodge/Burn · `T` Text ·
`U` Shape · `H` Hand · `Z` Zoom · `X` swap colors · `D` default colors · `[` `]` brush size · `{` `}` hardness ·
`Space` temporary pan · `Alt` eyedropper (brush) / source point (clone) · `Shift+click` straight line ·
`Ctrl+N/O/S/Shift+S/W` · `Ctrl+Z` / `Ctrl+Shift+Z` · `Ctrl+X/C/V` · `Ctrl+A/D/Shift+D/Shift+I` · `Ctrl+J` · `Ctrl+Shift+N` · `Ctrl+E` ·
`Ctrl+T` · `Ctrl+L/M/U/B/I` · `Ctrl+Shift+U/L` · `Ctrl+F` · `Shift+F5` content-aware fill · `Ctrl+Alt+K` remove background (AI) · `Shift+W` AI selection · `Ctrl+0/1/±` · `Alt+Backspace` / `Ctrl+Backspace` · `Tab` · `F1` (full list, in-app).

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

## Content-Aware Fill (Edit menu, or `Shift+F5`)
Reconstructs the selected area from its surroundings using OpenCV's `cv::inpaint` — this is **not** Photoshop's
proprietary algorithm (which uses advanced patch-matching, and in recent versions a generative model), but the two
classic OpenCV inpainting algorithms. They work well for removing small defects (dust, scratches, a small object,
a cable…) on a fairly uniform or textured background. A selection is required; the command refuses (with a message)
otherwise.

Parameters, all adjustable in the dialog with a live preview:
- **Algorithm**: *Telea* (fast, fast marching method — recommended in most cases) or *Navier-Stokes* (fluid-dynamics-based
  propagation, sometimes better over large smooth areas).
- **Reconstruction radius (px)**: size of the neighborhood used to reconstruct each pixel (default 3 px). A larger value
  smooths more but costs more computation.
- **Expand selection (px)**: grows (dilates) the area to reconstruct before running the algorithm, useful to remove a
  thin leftover fringe of the erased object if the selection hugged it too closely (default 0).
- **Sample all layers**: by default, reconstruction only looks at pixels of the **active layer** — if that layer is
  transparent around the selected area (e.g. an isolated retouching layer), the result will be based on that
  transparency. Checking this box reconstructs from the merged (all visible layers) image instead, while still
  painting the result only onto the active layer, like a targeted "copy merged".

The result is painted only inside the selection (soft-edged if the selection is feathered), as a single undo step.
If the active layer was transparent in the reconstructed area, it becomes opaque there (new color was just painted in).

## AI features (vision.cpp)
PhotoClone integrates [vision.cpp](https://github.com/Acly/vision.cpp) (ggml-based inference, CPU) for five neural models. Everything runs
locally; nothing is sent over the network. All commands are in the new **IA** menu; the models are configured once in
**IA ▸ Réglages des modèles…** (a `.gguf` file per model, with download links and live validation of each file).

> **The model weights are not bundled** (hundreds of MB, separate licenses — see `depend/README.md` for how the vision.cpp library itself is built from source). Without
> them the AI commands offer to open the settings instead of failing. In the environment where this integration was developed the
> model hosting site was unreachable, so **the neural inference itself has never been run end-to-end**: what has been tested (see
> `tests/smoke_test.cpp`) is everything around it — the vision.cpp library builds, links and runs, image conversion, mask/depth
> post-processing, all document operations, validation and clean refusal of missing/wrong/corrupt model files, the dialogs (driven with a
> stand-in mask provider), and the no-model error paths. Please report any problem you meet with real models.

| Command | Model | What it does |
|---|---|---|
| **IA ▸ Supprimer l'arrière-plan…** (`Ctrl+Alt+K`) | BiRefNet | Automatic subject cut-out, see below |
| **Sélection par IA** tool (group `W`, `Shift+W`) | MobileSAM | *Click* an object, or *drag a box* around it → selection. `Shift` adds, `Alt` subtracts, feather and "all layers" options. The image is analysed once (a few seconds on CPU), then each click is near-instant; it is re-analysed automatically if the document changed |
| **IA ▸ Remplissage IA (MI-GAN)…** | MI-GAN | Same dialog as Content-Aware Fill (`Shift+F5`) with the algorithm set to MI-GAN (also selectable there). The model works at a fixed 256/512 px resolution, so PhotoClone gives it a square crop around the selection (about 2× its size for context) and pastes the result back |
| **IA ▸ Carte de profondeur…** | Depth-Anything V2 | New grayscale "Profondeur" layer and/or a selection by depth threshold (near or far zones). Options: invert (the model's polarity depends on the file), auto contrast, smoothing. Tip: depth-threshold selection is a quick way to separate foreground from background when BiRefNet is not enough |
| **IA ▸ Agrandissement IA…** | Real-ESRGAN | Upscales the **whole document** (every layer and mask; one model pass per layer; text layers are rasterized) by the model's native factor (usually ×4) or a custom final factor. Transparent areas are colour-bled first to avoid dark fringes; alpha is resized conventionally. Refuses results above 64 Mpx (memory safety) |

### Remove Background
`IA ▸ Supprimer l'arrière-plan…` runs BiRefNet **once** when the dialog opens (seconds on CPU — the *lite* model is far faster than the full one; see vision.cpp's own README for benchmarks) and then lets you tune the result with an
**instant live preview** (only cheap OpenCV post-processing is replayed, not the network):
- **Threshold** — 0 keeps the soft matte (hair, fur, glass); >0 gives a hard-edged cut-out;
- **Shrink/expand** (px) and **feather** (px) — fix halos or jagged edges;
- **Keep only the largest subject** — removes stray islands; **Invert** — keep the background instead;
- **Output** — *layer mask* on the active layer (non-destructive, **default**), *new layer with the subject only* (transparent background),
  *replace the active layer* (transparent background applied to its pixels), or *selection only*;
- **Fix edge colours** (radius) — removes the old background's colour fringe using vision.cpp's foreground estimation (new-layer / replace modes);
- **Analyse the merged image** — analyse all visible layers instead of the active one; **Recompute** re-runs the network.
The whole command is a single undo step. The network call is synchronous: the interface is frozen while it runs (no worker thread yet).

**Portability**: the static library is built for x86_64 Linux, CPU only, AVX2 baseline (Haswell 2013+, deliberately
not `-march=native`). For another platform, GPU (Vulkan) or a different CPU baseline, see `depend/README.md`.
The project requires C++20 and CMake ≥ 3.28. Without `depend/visioncpp-src` (run `./depend/fetch-sources.sh` to get
it) PhotoClone still builds; the IA commands then say the library is missing.

## Stable Diffusion (stable-diffusion.cpp)
PhotoClone also integrates [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) for text-to-image generation and
inpainting on a selection. Unlike the vision.cpp models above, **it runs in its own worker thread and can be cancelled mid-generation**:
the dialog stays responsive (progress bar, per-step ETA, live log) and the rest of the application (menus, panels) is locked the same
way other dialogs lock it, but the canvas can still be panned/zoomed.

| Command | What it does |
|---|---|
| **IA ▸ Générer une image (Stable Diffusion)…** (`Ctrl+Alt+G`) | Text-to-image. Choose a size (multiple of 8), prompt/negative prompt, steps, CFG, sampler/scheduler, seed, and up to 8 variants in one batch. Result is added as a new layer (placement: centered/fit/cover/into the current selection) or, with no document open, creates a new document |
| **IA ▸ Inpainting sur la sélection (Stable Diffusion)…** (`Ctrl+Alt+P`) | Regenerates the current selection with a prompt. PhotoClone crops a context-padded square around the selection, scales it to the model's working resolution (512 for SD 1.x, 1024 for SDXL, configurable), sends it with a mask, and blends the result back into the document with a feathered edge — everything outside the (dilated, feathered) selection stays bit-identical |
| **IA ▸ Réglages de Stable Diffusion…** | Three tabs: "Checkpoint complet" (common case: one file), "Modèle de diffusion + encodeurs" (advanced: Flux, SD3…), "Performances" (threads, flash attention, mmap, unload after use). A consolidated status line at the bottom shows which method will be used. Every field has a diagnostic button (ⓘ icon) that inspects the file — see below |
| **IA ▸ Libérer la mémoire des modèles IA** | Frees both the Stable Diffusion and the vision.cpp model caches |

**Model file diagnostics** (ⓘ button next to each field): before ever trying to load a file into stable-diffusion.cpp, PhotoClone
inspects it directly — no dependency on the library — and reports what it found: detected container format (GGUF, safetensors,
PyTorch zip archive `.ckpt`/`.pt`), structural consistency (expected vs actual size, truncated archive, corrupt header…), tensor
count, a name preview, metadata, and a heuristic architecture guess (SD 1.x/2.x/SDXL-style UNet, Flux/SD3 blocks, VAE, CLIP/T5 text
encoder — matched from tensor names for GGUF/safetensors, or by pattern-scanning the un-deserialized pickle bytes for `.ckpt`) that
says whether the file should be enough on its own or needs completing in the other tab. A separate button computes the SHA-256 on
demand (reads the whole file: several seconds on a large checkpoint, so not done by default). The zip reader handles Zip64, required
past 4 GB (the normal case for a full checkpoint); tested against a real 4.3 GB archive in that format.

**Cancellation**: the native C API (`sd_cancel_generation`) is genuinely cancellable — checked at multiple points inside the sampling
loop — so "Annuler le calcul" typically stops within about one sampling step, not the full remaining generation. Two library quirks
were worked around: the cancel flag is reset internally at the very start of each call, so PhotoClone re-arms it on every progress
callback rather than relying on a single `cancel()` call; and cancelling is treated as *abandon*, not *keep partial result* — if the
model still returns images after a cancellation was requested, they are discarded. Model **loading** cannot be interrupted (cancelling
during load takes effect as soon as loading finishes). Closing a generation dialog while it is running cancels it and defers the actual
close until the worker thread has truly stopped.

**Isolation from vision.cpp**: stable-diffusion.cpp vendors its own copy of ggml (a different version from the one vision.cpp statically
links). To avoid symbol collisions, it is built as a separate shared library, `depend/stablediffusioncpp/lib/libpcsd.so`, with a linker
version script exposing only the ~20 C API functions PhotoClone needs and **zero** ggml symbols (checked with `nm -D`); PhotoClone loads
it at runtime with `dlopen`/`QLibrary`, so it never touches vision.cpp's statically-linked ggml. Without
`depend/stablediffusioncpp-src` (run `./depend/fetch-sources.sh` to get it), this file is simply never built, and the app still builds
and runs — the Stable Diffusion commands report the library as unavailable.

> **No model weights are bundled** (several GB, separate licenses). Configure yours in **IA ▸ Réglages de Stable Diffusion…**; the file
> is sniffed for a plausible header (GGUF magic / safetensors JSON header / zip or pickle for `.ckpt`) before use, and commands offer to
> open the settings if nothing is configured. In the environment where this integration was developed, the model hosting site was
> unreachable, so **actual image generation has never been run end-to-end** — no real image has ever come out of this code. What has
> been tested (see `tests/smoke_test.cpp`): the library builds, links as an isolated shared object and loads at runtime; crafted-but-invalid
> model files (fake GGUF/safetensors/zip headers) are rejected cleanly by the real library with no crash; the crop/scale/mask/composite
> geometry around inpainting (pure OpenCV, no model needed); and, most importantly, the threading and cancellation machinery itself —
> using a real `QThread` with an injected stand-in generation function — verified to run off the GUI thread, keep the UI responsive,
> deliver progress in order, cancel within about one step, never lose an early cancel request, discard results from a "completed but
> cancelled" run, refuse a second concurrent generation, survive an exception thrown mid-generation, and refuse invalid requests (bad
> size, empty mask, …) before ever starting a thread. A mutation test (temporarily breaking `cancel()`) confirmed 5 tests fail as
> expected, then pass again once reverted. Please report any problem you meet with real models.

**Portability**: `libpcsd.so` is x86_64 Linux, CPU only, AVX2 baseline (Haswell 2013+, not `-march=native`), same rationale as
vision.cpp. For another platform, GPU backend, or CPU baseline, see `depend/README.md`.

## Movable dialogs
The application's dialogs (filters and adjustments, sizes, new document, text, color picker, numeric prompts) are
floating frames *embedded as children of the main window*: you drag them by their title bar (they always stay
inside the window, including under Wayland), their position is remembered per dialog type, and by default they
open in the top-right corner so the image stays visible. While a dialog is open, menus, panels and shortcuts are
disabled, but you can still pan/zoom the image (hand tool, Space, wheel, Alt+wheel) to judge the live preview.
Only file open/save dialogs and alert boxes remain native system windows. To create a new one: derive from
`MovableDialog` (the `exec()` call works the same way as a normal `QDialog`).

## Known limitations / possible future work
Not implemented: magnetic lasso (an AI "click to select" tool exists, see above), pen tool / vector paths / shape layers (shapes are rasterized),
layer styles (fx), smart objects, layer groups, non-destructive adjustment layers (adjustments are applied
directly to pixels), spot healing / healing brush, Liquify, quick mask, rulers and guides, navigator, channels panel,
16-bit / CMYK / ICC color profiles, PSD import/export, tablet pressure sensitivity. Content-Aware Fill uses classic
OpenCV inpainting (Telea / Navier-Stokes), not Photoshop's proprietary engine (advanced patch-matching, or generative).
Layers are always the size of the document (simple and robust; a "layer + offset" model would use less memory).
Text is edited through a dialog box rather than directly on the canvas.
