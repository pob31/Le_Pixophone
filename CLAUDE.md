# Le Pixophone — Claude Code notes

New implementation of **Le Pixophone**, Pierre-Olivier Boulant's live
audiovisual instrument. A camera on an enlarger films a light table, and the
live image masks a mix of pinhole photographs. This repo ports the original Pure
Data + GEM system (patch114, ca. 2010) to one native application.

Status: **P1 done** (MIX layer, geometry, presets, parameter test bench;
Windows only). Next: P2 (camera).

## Build

```
./tools/setup.sh                          # after a fresh clone: submodules
cmake -S . -B build
cmake --build build --config Release      # or Debug (sokol validation + D3D11 debug layer)
ctest --test-dir build -C Release         # pixo_core_tests: Tx formulas, GEM space, presets
```

Binary: `build/LePixophone_artefacts/<config>/Le Pixophone.exe`. On this
machine CMake is the one bundled with Visual Studio 2026, not on PATH:
`C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`
(generator "Visual Studio 18 2026"). Configuring on macOS/Linux stops with a
clear error until P5. Runtime log: `%APPDATA%\Le Pixophone\pixophone.log`
(engine, sokol and surface events, adapter name, device release check).

Running: the media root (the old `Le_Pixophone` folder with `presets/`,
`images1024/` ...) is chosen in the control window and saved in
`%APPDATA%\Le Pixophone\Le Pixophone.settings`; `PIXO_MEDIA_ROOT` is the
fallback. `--set key=value` (repeatable) sets any parameter at start-up, by
its `ParamDef` key (e.g. `--set mixX=64 --set aspect=113`). Use it for
reproducible visual checks.

## Where things are decided

- `Documentation/PIXOPHONE-PLAN.md` — decision records (D1–D24; **D25 is the
  next free number**), architecture and source layout, phases P0–P5, research
  topics R1–R5, known issues, next steps. It wins over anything else here.
- `Documentation/PD114-ANALYSIS.md` — reverse-engineered spec of the original:
  render engine (§2), control surface, MIDI map and command API (§3), presets
  and assets (§4), the author's answers and the defaults chosen where he
  couldn't recall (§6 — all closed; new questions start at Q15). When porting a
  behaviour, implement it from this spec. If the spec and the patch disagree,
  re-read the patch and fix the spec.

## Key decisions (summary — see the plan)

- One application (the old Tx/Rx two-computer split is gone). Video only;
  audio comes later as a new design, not a port of `3_AUDIO`.
- C++ / JUCE 9.0.4 (D18), CMake-native (same shape as `d:/dev/XOA` and
  `d:/dev/Tight-WFS`: `ThirdParty/JUCE` submodule, `Source/`,
  `Documentation/`).
- GPU: sokol_gfx under JUCE — D3D11 (Windows), Metal (macOS), GL core
  (Linux); shaders cross-compiled with sokol-shdc (D19). Targets Windows,
  macOS, Linux. 8-bit BGRA UNORM, no sRGB, gamma-space blending like GEM (D21).
- **Threading rule (D20): the message thread never blocks on the render
  thread.** DXGI messages the window thread when swapchains/devices are
  released, so waiting deadlocks. Surface detach is async, and quit is
  two-step (`RenderEngine::stopAsync`, then `quit()`). Keep it that way when
  adding features.
- Faithful port of patch114 first, then evolve — **quirks and all** (D14).
  Reproduce the original's bugs and odd behaviours; each gets a named compat
  flag defaulting to the original. Don't "fix" behaviour while porting.
  Exception: artefacts of the old network split dissolve (D15).
- The Tx/Rx split becomes threading (D15): continuous values → last-value
  snapshot; discrete commands → ordered lock-free FIFO; Pd clocks → an
  ms-resolution scheduler; per-frame camera effects on a 15 Hz legacy tick.
- Camera exposure and gain are live performance controls (D17): grain and
  rolling-shutter scan lines from the flickering light table are the look.
  Changes must apply while streaming, with low latency.
- Camera loopers hold N frames at R fps (D16); the old 128-frame logic runs
  in "loop units" (1/128 of the loop) scaled to N. Legacy preset 128 @ 15 fps
  = exact patch114, used for verification.
- Pico (third renderer) is out of scope. Controllers are ported as is
  behind swappable device profiles; the hardware is not on hand, so develop
  against the GUI and virtual MIDI.
- Camera: manual exposure/gain/fps abstraction plus an optional PS3 Eye-style
  "sensor character" stage. The PS3 Eye's scan lines and noise were part of
  the look.

## Repo shape

- `Source/` — the application (layout in PIXOPHONE-PLAN "Source layout").
  `Source/Core/` is pure C++ with no JUCE (unit-tested, D24); put formulas
  and other pure logic there. `Source/Render/Win/` holds Win32/D3D11 code
  and must stay free of JUCE headers (`<windows.h>` clashes).
- `tests/` — `CoreTests.cpp`, a framework-free test executable run by CTest.
- `ThirdParty/` — submodules: `JUCE` (9.0.4), `sokol`, `sokol-tools-bin`
  (shallow). Bump sokol and sokol-tools-bin together. sokol's API moves fast:
  read `ThirdParty/sokol/CHANGELOG.md` before using a new area.
- `pd_original/` — **read-only reference**, an exact copy of
  `E:\Make\Le_Pixophone\patch_PureData\1_IMAGE` (v114): `patch114Rx_*.pd`
  (GEM render), `patch114Tx_*.pd` (control), `preset1.pd`, `pix_shader.pd`,
  `shader/` (GLSL; only `bokeh` is used). Never edit it.
- `tools/setup.sh` — initialise submodules.
- `tools/pd2txt.py` — dump a patch as numbered objects + connections, with
  x,y positions. `tools/pdxref.py` — send/receive cross-reference. Run with
  `python -I tools/pd2txt.py <patch.pd> <out.txt>`. Raw `.pd` files are
  unreadable by index; always dump first.
- `Documentation/` — plan and spec (above). `Documentation/reference/` —
  original output frames (the target look: ≈ 15 scan-line cycles per picture
  height). `tools/bandmeter.py` measures band spacing in any frame.

## Reading the Pd patches — traps

- `[t …]` (trigger) fires **right-to-left**, which sets the layer draw order.
- A subpatch's inlets/outlets are ordered by **x position**, not by creation
  index.
- GEM transforms leak between branches unless a `[separator]` restores the
  matrix. `[rectangle]`/`[square]` take **half** sizes. `view 0 0 0` with
  the default frustum (x widened by the window aspect) means z = −4 spans
  ±4 vertically and ±4·aspect horizontally (spec §2.1, `Core/GemSpace`).

## Outside the repo

- `E:\Make\Le_Pixophone` — old project root:
  - `presets/` — the preset source; older full-path format, resolve by file
    name (spec §4). Many strip images are missing, so the loader must
    tolerate gaps;
  - image banks: `images1024`, `images800x600`, `images640x480`,
    `images_gif`, `images_strip`;
  - `_extra_sets/` — show-specific sets;
  - `patch_PureData/controllers/` — BCR2000, nanoPAD, FBV and LPD8 files, and
    the Arduino sketch;
  - `patch_PureData/1_IMAGE/_versions/` — history 101–114;
    `108/Le_Pixophone_prenet.pdf` is a printout of the pre-network patches;
  - `dossier2/Le_Pixophone_pob.pdf` — artist statement + GUI screenshot.
- `C:\JUCE` — the author's local JUCE install (being updated to 9.x). The
  project builds against its own pinned submodule, not this copy.
- The juce-docs MCP reads XOA's JUCE 9.0.2 doxygen docs
  (`D:\dev\XOA\ThirdParty\JUCE\docs\doxygen\doc`); fine for 9.0.4 lookups.
