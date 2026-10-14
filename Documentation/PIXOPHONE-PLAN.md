# Le Pixophone (new) — plan and decisions

Status: **planning** (2026-10-14). No code yet. The reverse-engineered spec of
the original is `PD114-ANALYSIS.md`; this file records what we decided, the
proposed architecture, and the order of work.

## Decision records

Numbered D1…; D19 is the next free number. A decision is changed by adding a
new record that supersedes it, not by editing the old one.

| # | Decision | Why |
|---|---|---|
| D1 | **One application.** No Tx/Rx split; the old network messages become the internal command/parameter API (`PD114-ANALYSIS.md` §3.1). | The split only existed because 2010 hardware could not run GUI and GEM on one machine. |
| D2 | **Video/image side only for now.** The old `3_AUDIO` patches are not ported. Audio will be designed from scratch later, so the architecture keeps a seam for an audio engine (JUCE makes that cheap). Audio is **not scheduled yet** (author, 2026-10-14). That leaves time for Tracktion Engine, which still requires JUCE 8, to catch up with JUCE 9 (see D18). | User decision: "Things have changed for the better! We'll make something new!" |
| D3 | **C++ / JUCE, CMake-native**, same shape as XOA and Tight-WFS: JUCE as a pinned submodule in `ThirdParty/JUCE`, `juce_add_gui_app`, `Source/`, `Documentation/`. JUCE version: see D18 (9.0.4). | One framework for GUI, MIDI, serial/OSC, file handling and later audio. Matches the author's other projects. |
| D4 | **GPU layer: sokol_gfx under JUCE.** D3D11 on Windows, Metal on macOS, GL core on Linux. sokol_gfx does not own windows: the app creates swapchains on JUCE native windows (output + preview). Shaders are written once and cross-compiled with `sokol-shdc` at build time. Camera frames go in as native textures where possible. | OpenGL is not required. A small vendored header, native modern backend per OS, no window-system conflict with JUCE. Chosen over WebGPU, hand-written backends and the JUCE GL module. |
| D5 | **Targets: Windows, macOS, Linux.** Windows is the performance machine and the first platform brought up. | User decision. |
| D6 | **Faithful port of patch114 first, then evolve.** Reproduce the layer stack, pad modes, camera mask (bokeh), loops, sampler, presets and controller behaviour, so the instrument plays as before. Each Tx bug listed in `PD114-ANALYSIS.md` §6 Q10 is decided individually (decided by D14: all kept). | Keep the instrument's vocabulary intact before changing it. |
| D7 | **Camera is an abstraction with manual controls** (exposure, gain, frame rate, format). It is not tied to one device, and an optional "sensor character" stage (scan lines, noise, low-light grain) can recreate the PS3 Eye look on any camera. Hardware choice is research topic R1. | The PS3 Eye's defects and manual sensitivity/fps were part of the look; it has no working driver and isn't available. |
| D8 | **One parameter model replaces "widget = state"**. Every parameter has an id, range, default, scaling curve, smoothing time, MIDI binding(s) and a feedback policy (with echo suppression). GUI, MIDI, footswitches, pedal, touchpads and presets all bind to it. Persisted in a JUCE `ValueTree` (as XOA's parameter store). | Tx scattered state across IEM widgets, and smoothing lived on both sides of the network. |
| D9 | **Threads**: GPU work on a dedicated render thread that owns the sokol context. GUI on the JUCE message thread. MIDI, serial and camera callbacks push into lock-free queues read once per frame. | Projector output must not stall on GUI or device I/O. |
| D10 | **Pico is out of scope.** The third renderer (pico projector, `picolevel`/`picoload`… messages, `patch112Rx_pico_d.pd`) is not ported. | The pico projector no longer works. |
| D11 | **Controllers: port the old mapping as is, behind swappable device profiles.** BCR2000 (preset 27, ch 1 + NRPN ch 11), nanoPAD (ch 2), FBV (ch 3), BCR footswitch jacks and the Arduino Firmata touchpads are reproduced per `PD114-ANALYSIS.md` §3.2. Each device is a profile (bindings + feedback) on top of the D8 parameter model, so the BCR2000 can later be replaced without touching the engine. Hardware is not on hand: FBV and nanoPAD will come back, and the touchpads are built into the rig. Develop against the GUI plus a virtual-MIDI or test harness until then. | The author still owns them all, but the BCR may be swapped and the interface will evolve. |
| D12 | **Render at display rate; loops keep 15 fps capture.** Output runs at the projector's refresh (vsync). Every time-based behaviour stays defined in milliseconds as in the patch (envelopes, fades, `140 − speed` ms frame periods, smoothing). Camera loops keep the 128-frame model with a capture-rate parameter, default 15 fps. Raising it (with a matching frame count) is a later option. | 15 fps was the hardware limit, not a choice (Q1). Capture rate is what sets loop length (≈ 8.5 s) and makes default speed 73 (67 ms/frame) play back in real time. |
| D13 | ~~**Master level = final gain on the composite**~~ — **superseded by D14** (master stays per-layer; final gain is a compat flag). Original text: (uniform fade to black). Per-layer levels keep their own curves from §3.1, without master folded in. A "legacy master" switch reproduces the old per-layer behaviour for A/B comparison. | The old per-layer multiplication made upper layers translucent as master dropped, giving the odd transparency mixes the author remembers (Q11). |
| D14 | **Quirks and all.** Reproduce patch114 exactly, known bugs and odd behaviours included (`PD114-ANALYSIS.md` §6 table). Each one gets a named **compat flag** that defaults to the original behaviour, so the later "evolve" phase can fix them one by one. Includes frame-rate-dependent behaviour: per-frame camera processing (motion-blur feedback, CamAutoInv motion sums, loop capture) runs on a **legacy camera tick**, default 15 Hz, so trails, auto-invert speed and loop timing match 2011. Output still renders at display rate (D12). Supersedes D13 and the "fix it" defaults briefly recorded for Q3/Q4/Q5/Q10. | Author: "Let's build it as it is with the quirks and all." |
| D15 | **The Tx/Rx split becomes threading.** Old link → new mechanism: **UDP messages** (continuous values) → a last-value parameter snapshot, read once per frame; **TCP messages** (loads, modes, notes, record start, swaps) → an ordered lock-free command FIFO into the engine; **Rx→Tx back-channel** (`cai` motion, `camcap frame/recend`) → an engine→control FIFO. Pd clocks (`metro`, `delay`, `line`, `pipe`) → one ms-resolution scheduler independent of render rate. Image decoding runs on a worker thread and is uploaded on the render thread. **Split artefacts dissolve**: behaviour that only failed because a message never crossed the network is not reproduced. Example: Pad_Kill `fswpk` now reaches the crossfade logic. Throttles that existed for the network (`speedlim`, ≤ 20 Hz playhead streaming) are dropped. | Author: "The network split is no longer on the table. I guess this is threading now." It refines D1/D9. The TCP/UDP split already separated ordered commands from latest-value streams, so it maps directly. |
| D16 | **Loopers hold more frames.** Capacity **N** frames and capture rate **R** are parameters per looper (camcap1/2); loop length = N / R. All 128-frame logic becomes **loop units** u ∈ 0–127 (1/128 of the loop), scaled to N:<br>• in point → first frame of its unit, `floor(u·N/128)`; out point → last frame of its unit, `floor((u+1)·N/128) − 1`; scrub → first frame of its unit;<br>• playheads step ±1 frame every `(140 − speed) · 15/R` ms, so default speed 73 still plays in real time at any R;<br>• camera-loop pad start = `((p−1)·14 + padVidInc·7) mod 128` units → frames; counters run `mod N`;<br>• recording captures N frames at R fps; progress is shown in units.<br>Frames live in **system RAM** (one ring per looper, at a configurable capture resolution). Only the frames on screen are uploaded each render frame (≤ 6: two players + four PADv quads), so VRAM doesn't bound N. A **legacy preset (N = 128, R = 15)** reproduces patch114 exactly and is used to verify against the spec. Default N/R: set by the author in play. This extends D12; capture rate is no longer tied to D14's 15 Hz legacy tick, which still drives motion blur and auto-invert. | Author: "The loopers can probably hold more frames than at the time." 2 × 128 frames was a 2010 memory limit, not a musical choice. Unit scaling keeps every controller mapping and pad layout meaningful at any capacity. |
| D17 | **Camera exposure and gain are performance controls.** They are parameters like any other (D8): MIDI, pedal soft takeover, footswitch resets, smoothing as in the patch. The camera backend applies them **live while streaming**: no stream restart, no dropped frames, latest value wins (coalesced on the camera thread), with low and measured control-to-image latency. Frame rate and format are also live-switchable, accepting a short re-sync as the original's 500 ms gain/exposure re-push implies. The sensor-character stage (D7) is an add-on for cameras that can't produce enough grain or banding, not a replacement for the real sensor. | Author: gain and exposure "were part of the performance, giving grainy pictures and scan lines of various thickness". The physical camera + flickering light *is* the instrument here. |
| D18 | **JUCE 9.0.4**, the latest upstream tag on 2026-10-14, pinned as the `ThirdParty/JUCE` submodule (completes D3). Bumps are deliberate and noted here. The author is updating his local `C:\JUCE` to 9.x too. The juce-docs MCP reads XOA's 9.0.2 doxygen docs, close enough for API lookups. | No reason to pin older: same AGPLv3/commercial licence as 8.x, no legacy code to protect, and JUCE 9's breaking changes (SVG/`Drawable` rework, EGL on Linux) don't touch a sokol-rendered app written fresh. House direction is 9.x (author, 2026-10-14): WFS-DIY is getting the 9 pin now and XOA is half migrated. Only go.dot stays mixed, because its Tracktion Engine dependency still requires JUCE 8. **Risk for D2:** a future audio engine built on Tracktion Engine would face the same JUCE 8 constraint; check Tracktion's JUCE support before choosing it. |

## Architecture (proposed)

```
            ┌──────────────── control (message thread) ─────────────────┐
 MIDI ──►   │ ControllerRouter (BCR2000, nanoPAD, FBV, footswitches)    │
 Serial ──► │ TouchpadInput (Firmata)          ParameterStore (D8) ◄──► GUI panels
            │ PresetManager / MediaLibrary     Automation (sampler     │  + output preview
            │                                   transport, mix loop,   │
            │                                   auto-invert, refresh)  │
            └───────────────┬──────────────────────────────────────────┘
                            │ parameter snapshot + commands (lock-free)
            ┌───────────────▼──────────── render thread ───────────────┐
 Camera ──► │ CameraSource → CameraChain (invert, levels, motion blur, │
 (thread)   │   colour-alpha, tint, sensor character) → depth textures │
            │ LoopRecorder (camcap1/2)       ImageBanks (mix/pad/strip)│
            │ Compositor: MIX → PAD A → STRIP → CAMERA → PAD B → SAMPLER│
            │   + Geometry (GEM camera space, §2.1) + Bokeh pass       │
            │ sokol_gfx → swapchains: Output window, Preview panel     │
            └──────────────────────────────────────────────────────────┘
```

Proposed source layout (decided at scaffold time):

- `Source/App/` — application, windows, display selection.
- `Source/Params/` — parameter store, definitions table, scaling and smoothing.
- `Source/Control/` — MIDI profiles and router, feedback, footswitch matrix,
  FBV soft takeover, Firmata touchpads, automation.
- `Source/Media/` — media root, image banks, preset loading (both formats, §4).
- `Source/Camera/` — `CameraSource` interface and per-OS backends, motion analysis.
- `Source/Render/` — sokol setup, swapchains, render graph, layers, shaders
  (`Source/Render/shaders/*.glsl` → sokol-shdc).
- `Source/UI/` — control window panels mirroring the old layout (§3.5).
- `tools/` — Pd analysis scripts (`pd2txt.py`, `pdxref.py`).

Each layer of `PD114-ANALYSIS.md` §2 is a class with a clear parameter list,
and its numeric behaviour (curves, envelope times, frame maths) is unit-tested
against the spec's formulas.

## Phases

| Phase | Content | Done when |
|---|---|---|
| P0 Scaffold | CMake + JUCE submodule + vendored sokol + sokol-shdc build step; control window and borderless output window on a chosen display, both cleared by sokol (D3D11); CLAUDE.md build section | `cmake --build` makes an exe that opens both windows at vsync |
| P1 Stills | Parameter store + auto-generated debug panel; media root + preset loader; GEM camera space and geometry (§2.1, §3.4); MIX layer with XY weights; output quad | Loading `mixpreset0` and moving X/Y reproduces the old MIX blend |
| P2 Camera | `CameraSource` (first backend: Windows) with manual exposure/gain/fps; camera chain; depth textures; bokeh shader port with blurModes; STRIP layer; CAMERA live layer | The hand-over-light-table mask works on MIX and STRIP |
| P3 Loops and pads | Loop recorder (camcap1/2) with N/R capacity in system RAM (D16); SAMPLER players with transport in the engine; PAD layer with modes 0–3 incl. camera-loop pads | With the legacy preset (128 @ 15 fps), all §2.6 pad modes and §3.3 sampler transport behave as specified; larger N/R keep the same feel |
| P4 Control | Real control UI; BCR2000/nanoPAD/FBV profiles with feedback and echo suppression; footswitch matrix; soft takeover; Firmata touchpads; mix loop; auto-invert; refresh modes; M_Reset | A show can be played from the controllers without touching the mouse |
| P5 Platforms | macOS (Metal, AVFoundation) and Linux (GL, V4L2) backends; packaging | Same feature set on all three |
| Then | Evolve: sensor-character stage, new layers or effects, projection mapping beyond one quad, replacement for the BCR2000, new audio engine | — |

## Research topics

- **R1 — Camera hardware** (user: "a real topic"). Wanted: a PS3 Eye-like
  character (visible scan lines and noise at high gain), manual
  exposure/gain, high frame rate at VGA, and good adjustable optics. Leads to
  evaluate:
  - UVC board cameras with M12 or CS-mount lenses (small-sensor CMOS, high fps
    at low resolution);
  - driving a second-hand PS3 Eye directly over libusb (the open-source
    PS3EYEDriver approach; it needs a WinUSB driver on Windows), since the
    camera is cheap and the old CL-Eye driver is dead;
  - emulating the defects in the sensor-character stage on a clean camera.
  
  Reference point, the old PS3 Eye with the CL-Eye driver (2008 release
  notes): 640×480 at 15/30/40/50/60 fps, 320×240 at 15/30/60/75/100/125 fps.
  The patch only ever used **15 or 30 fps** (VGA or QVGA; M_Reset = VGA @ 30),
  and loops captured one frame per GEM frame (≈ 15 fps). Bar for the
  replacement: ≥ 30 fps at 640×480 (match), 60 fps (headroom), a high-fps
  low-res mode as a bonus.

  **Rolling shutter preferred** (author). The PS3 Eye's "scan lines" were most
  likely rolling-shutter banding: the line-by-line readout catches the light
  table's mains flicker (100 Hz on 50 Hz mains, or LED PWM). So the camera's
  manual exposure time directly controls the effect: exposures shorter than
  the flicker period show bands, multiples of 10 ms suppress them. A global
  shutter would only pulse frame to frame.

  Shortlist (2026-10-14; verify current specs and mounts before buying):

  | Tier | Camera | Sensor / output | Shutter | Mount | Interface |
  |---|---|---|---|---|---|
  | **Pick** | The Imaging Source **DFK 37AUX178** | Sony IMX178, 1/1.8", 3072×2048 @ 60 fps | rolling | C/CS | USB3 Vision **and UVC** — works through the generic camera path on all three OSes |
  | Low-light variant | The Imaging Source DFK 37BUX462 | Sony IMX462 (STARVIS, NIR-sensitive), 1920×1080 @ 143 fps | rolling | confirm C/CS (the ML board variant is M12) | as above |
  | Premium | Basler ace 2 R **a2A3840-45ucBAS** | Sony IMX334, 1/1.8", 3840×2160 @ ≈ 45 fps | rolling | C | USB3 Vision only → needs a pylon backend (pylon C++ runs on Windows, macOS, Linux) |
  | Budget | ELP OV2710 USB board camera | 1080p @ 60 fps, low-light | rolling | CS (C lenses need a 5 mm ring) | UVC; quality closest to PS3 Eye-level cheapness |

  Lenses: C-mount for 1/1.8" or 2/3" sensors. Focal length
  ≈ camera height × sensor width / table width. Example: 70 cm above the table,
  50 cm field, IMX178 (7.4 mm wide) → ≈ 10 mm; so an 8–16 mm lens or a
  C-mount zoom covering that range. Options:
  - modern machine-vision primes (Fujinon HF-XA, Kowa LM-JC, Computar families),
    sharp with a manual iris;
  - vintage 16 mm cine lenses (Switar, Angénieux, Kern, Schneider), which cover
    up to 2/3" and bring optical character.

  Avoid M12 board cameras: no proper mount.

  Bench test before committing to a camera (D17), with the real light source:
  - sweep exposure and gain from MIDI at 30–60 updates/s: smooth, no stalls or
    stream restarts; measure latency;
  - minimum exposure and step size: crisp bands need ≪ 2 ms against a 490 Hz
    PWM;
  - analog gain range, and **no forced ISP denoise or sharpening**, which would
    erase the grain. Industrial cameras usually pass raw-ish data; cheap
    UVC webcams often smear it;
  - readout time per frame at each frame rate and format (sets bands per
    frame). **Target ≈ 15 band cycles per picture height** under the 490 Hz
    table, as in `Documentation/reference/` (measure with
    `tools/bandmeter.py`). Fast modern sensors read a frame out in ≈ 16 ms,
    which gives only ≈ 8 bands. Remedies:
    - a longer line time or slower readout mode, if the camera exposes one;
    - a row crop (ROI);
    - simplest: move the LED wire to Arduino **D5 or D6**, whose stock PWM is
      ≈ 977 Hz (≈ 15.6 bands at 16 ms), and change `LEDpwm` in the sketch
      (D5/D6 are free; A5 is the dial input, a different pin);
  - VGA/QVGA-like binning or ROI modes, to recover the coarser old look.
- **R5 — Light source** (it is half of the scan-line instrument).
  **Confirmed** (author, 2026-10-14): red LED strips under opal plexiglass,
  dimmed by a dial on the Arduino. The sketch PWMs pin 3 at ≈ 490 Hz with the
  duty cycle taken from the dial. Top light: the enlarger's own lamp on a
  household slider dimmer, a constant top-down light that keeps hands and
  objects unstriped against the striped table. A switch on the back of the
  rig lights 1, 2 or 3 dies per SMD LED, giving coarse brightness steps
  independent of the PWM. The bands were shaped by camera exposure **and**
  the PWM dial, with the die switch and top light setting levels (band model
  in `PD114-ANALYSIS.md` §1). The rig, with the Arduino, is not on hand
  for now.
  - Faithful: reuse the rig as is (or rebuild the same dimmer and die
    switch). The port only reads the dial value (Firmata A5) for display, as
    the patch did.
  - Evolve — **deferred until the rig hardware is back** (author: fine-tuning
    that needs the hardware). Until then, the only design constraint is to
    keep the Arduino link two-way-capable (Firmata in both directions), even
    though the port only reads the dial.
    - let the app set PWM frequency and duty as performable parameters, via
      the same microcontroller link, with the dial reading sent to the app
      as an input. Frequency sets band density and drift
      speed (the fractional part of PWM frequency ÷ frame rate); duty sets
      band thickness;
    - an Uno's Timer2 only offers prescaler steps (≈ 31 kHz, 3.9 kHz, 980,
      490, 245, 122, 31 Hz), so continuous frequency needs a different timer
      setup or a dedicated LED driver.
  - A DC-driven or high-frequency "flicker-free" LED panel would show **no
    bands at all**; keep a PWM-driven source.
  - Colour: under red light a colour (Bayer) sensor mostly uses its red
    photosites. That cost resolution and added noise, and the bokeh mask
    reads the red channel. Run the R1 bench test **under the red table**, and
    compare colour against mono versions of the shortlisted sensors only if
    the colour image proves too weak.
- **R2 — sokol_gfx integration**:
  - creating swapchains on JUCE native handles on each OS (HWND, an NSView
    with a CAMetalLayer, an X11 window with a GL context);
  - two swapchains (output + preview) on one device;
  - sokol-shdc in CMake;
  - status of the upstream Vulkan backend for Linux (in development; check
    when vendoring).
- **R3 — Camera capture library**: a library such as openpnp-capture (Media
  Foundation / AVFoundation / V4L2, with exposure and gain properties) vs
  native per-OS backends. JUCE's `CameraDevice` has no manual
  exposure/gain, so it is ruled out for the main path.
- **R4 — Frame pacing**: output vsync on the projector display,
  camera-to-photon latency target, behaviour when the camera, capture (D12)
  and projector rates differ.

## Next steps

1. All questions in `PD114-ANALYSIS.md` §6 are closed: answered by the author
   (Q1–Q3, Q6–Q9, Q11) or settled by D14 (keep the original quirk, behind a
   compat flag).
2. P0 scaffold with JUCE 9.0.4 (D18).
3. R1 camera shortlist can run in parallel with P0–P1, since P1 needs no camera.
4. Preset media: the author checks his archives for missing strip images
   (and others). The loader must tolerate missing files either way.
