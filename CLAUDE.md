# Le Pixophone — Claude Code notes

New implementation of **Le Pixophone**, Pierre-Olivier Boulant's live
audiovisual instrument. A camera on an enlarger films a light table, and the
live image masks a mix of pinhole photographs. This repo ports the original Pure
Data + GEM system (patch114, ca. 2010) to one native application.

Status: **planning, no code yet.** Start with the docs below.

## Where things are decided

- `Documentation/PIXOPHONE-PLAN.md` — decision records (D1–D18; **D19 is the
  next free number**), proposed architecture, phases P0–P5, research topics
  R1–R4, next steps. It wins over anything else here.
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
  (Linux); shaders cross-compiled with sokol-shdc. Targets Windows, macOS,
  Linux.
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
- Pico (third renderer) is out of scope. Controllers are ported as is behind swappable device profiles; the
  hardware is not on hand, so develop against the GUI and virtual MIDI.
- Camera: manual exposure/gain/fps abstraction plus an optional PS3 Eye-style
  "sensor character" stage. The PS3 Eye's scan lines and noise were part of
  the look.

## Repo shape

- `pd_original/` — **read-only reference**, an exact copy of
  `E:\Make\Le_Pixophone\patch_PureData\1_IMAGE` (v114): `patch114Rx_*.pd`
  (GEM render), `patch114Tx_*.pd` (control), `preset1.pd`, `pix_shader.pd`,
  `shader/` (GLSL; only `bokeh` is used). Never edit it.
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
  the default ±1 frustum means z = −4 spans ±4 units, not aspect-corrected.

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
