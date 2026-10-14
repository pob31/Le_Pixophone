# Le Pixophone — patch114 reverse-engineered spec

Reference spec of the original Pure Data + GEM system (`pd_original/`,
version 114, ca. 2010–2011), written for the port. It describes **what the
patches do**, with enough numeric detail to reproduce the behaviour. Where the
patch is ambiguous it says so and the question is listed in §6.

- `pd_original/patch114Rx_pob2-4_c.pd` — **Rx**, the GEM render engine (§2).
- `pd_original/patch114Tx_pob2-4_b.pd` — **Tx**, MIDI + GUI control surface (§3).
- `pd_original/preset1.pd` — preset thumbnail/save helper used by Tx (§4).
- `pd_original/pix_shader.pd` + `shader/*` — GLSL loader abstraction (§2.9).

Tooling: `tools/pd2txt.py <patch.pd> <out.txt>` dumps every canvas as a
numbered object list plus `src:outlet -> dst:inlet` connections. Pd orders a
subpatch's inlets/outlets by **x position**, not creation index — check the
raw `.pd` coordinates when it matters.

---

## 1. The instrument

An enlarger stands on a light table; a camera (PS3 Eye) on the enlarger head
films the table. The performer's hands and objects on the table make a live
image that **masks** a mix of pinhole photographs (sténopés): photos dissolve
into each other, flash in, slide past as long panoramic strips; short camera
loops are recorded and replayed as material. Everything is improvised from
MIDI controllers, a foot controller and an Arduino touchpad. (Source:
`E:\Make\Le_Pixophone\dossier2\Le_Pixophone_pob.pdf`.)

The two-computer split (Tx on host `pob2`, Rx on host `pob4`) existed only
because 2010 hardware could not run GUI and GEM together. The port is one
application; the Tx→Rx messages below become its internal command API.

### Reference look and the light rig

Frames from the original output, supplied by the author:
`Documentation/reference/ref-hand-soft-bands.jpg`,
`ref-orange-crisp-bands.jpg`, `ref-teal-soft-bands.jpg`. The picture is a
4:3 quad inside a 16:9 frame (geometry §3.4).

**The light rig** (author):
- **Bottom: the light table.** Red SMD LED strips under opal plexiglass,
  PWM-dimmed by an Arduino dial (stock `analogWrite` on pin 3, ≈ 490 Hz,
  §3.2). Through the camera's rolling shutter this flickering light becomes
  horizontal **scan lines**. A **switch on the back of the rig** lit 1, 2 or
  3 of the dies in each SMD LED package. That changed brightness in large
  steps, independently of the PWM.
- **Top: the enlarger's own lamp**, dimmed with an ordinary household slider
  dimmer. It gives constant top-down light, so hands and objects read
  against the striped table: in `ref-hand-soft-bands.jpg` the hand is
  nearly unstriped while the background is banded.
- **Played live:** the scan lines changed with both camera **exposure** and
  the **PWM dial** (duty cycle); **gain** added grain. The 1/2/3-die switch
  set the level coarsely, so a band shape could be kept at a different
  brightness.

**Measured** with `tools/bandmeter.py`: all three frames show ≈ 15 band
cycles per picture height (period ≈ 40 px of 608). With 480 sensor rows,
that is ≈ 32 rows per PWM cycle (2.04 ms), so a line time of ≈ 65 µs ≈ 33.3 ms
/ 516 lines. That is the PS3 Eye in **VGA at 30 fps**, the M_Reset default,
which confirms the 490 Hz explanation.

**Band model** (each row integrates the PWM square wave, period T = 2.04 ms
and duty d, over the exposure time tₑ):
- every band edge becomes a linear ramp tₑ wide;
- the bright plateau is `dT − tₑ` and the dark plateau `(1 − d)T − tₑ`;
- peak brightness grows with tₑ until tₑ ≥ dT;
- at tₑ = T the bands vanish.

The controls act on the model like this:
- **duty d (dial)** sets the bright-to-dark ratio within each band cycle;
- **exposure tₑ** sets edge softness and eats into both plateaus;
- **the die switch** scales the amplitude (×1/×2/×3, roughly) without
  changing the band shape;
- **top light** adds a constant under hands and objects, which lowers band
  contrast there;
- **gain** scales everything and adds noise.

So short exposure gives crisp, thin-edged lines (`ref-orange-crisp-bands`).
Exposure near T gives soft, sinusoidal ones (`ref-hand…`, `ref-teal…`).
Bands per picture height = PWM frequency × readout time of the visible rows.
Band drift per frame = the fractional part of PWM frequency ÷ frame rate.

### Old network link (for reference only)

| Rx object | Role |
|---|---|
| `netreceive 3000` (TCP), `netreceive 3010 1` (UDP) | commands from Tx |
| `netsend` → `pob2:3001` (TCP), `netsend 1` → `pob2:3011` (UDP) | feedback to Tx |
| `net 1` / `net 0` | connect / disconnect; on connect sends `test TCP Rx-Tx`, `test UDP Rx-Tx` |

Every incoming message is `route`d by its first word and re-sent locally:
`geo loadmix mix sampler samput strip loadstrip cam cai camcap camput loadpad
pad net gem` (anything else is printed). Rx sends back only `cai 0 <motion>`
(UDP, every 50 ms), `camcap frame <n>` (UDP) and `camcap recend` (TCP).

---

## 2. Rx — the render engine

### 2.1 Window and camera space

`[gemwin 15]` (creation arg — GEM frame rate, see Q1). `gem window <n>` picks a preset:

| n | Name | offset | dimen | other |
|---|---|---|---|---|
| 0 | OFF | — | — | `0, destroy` |
| 1 | Ext.FullHD | 1366 0 | 1920×1080 | FSAA 4, border 0, topmost 1 |
| 2 | 1024:960 | 1366 0 | 1024×960 | FSAA 4, border 0, topmost 1 |
| 3 | Window16:9 | 500 0 | 800×450 | border 1 |
| 4 | Window4:3 | 500 0 | 800×600 | border 1 |
| 5 | Laptop | 0 0 | 1366×768 | border 0 |

All presets use `cursor 0` and `view 0 0 0`, which puts the eye at the origin
looking down −Z. GEM's default projection is the frustum
`perspec -1 1 -1 1 1 20`, **with x widened by the window's aspect ratio**.
So a plane at z = −4 spans ±4 units vertically and ±4·aspect horizontally:
±7.11 on 16:9, ±5.33 on 4:3. (Corrected 2026-10-14; the first version of
this spec said "not aspect-corrected". Tx's §3.4 constants prove it: the
16:9 width clamp `Wc = 7` ≈ 7.11, the 4:3 one `Wc = 5.33`, and the 16:9
preset gives 7.08 / 4 = 1.77. The STRIP's `rectangle 5.33 4` exactly
filling its 1024×768 buffer shows `gemframebuffer` does the same.) The
picture's shape comes from the global `width`/`height` geometry (§2.8),
chosen per projector ratio. `gem render 0|1` toggles rendering;
`gem reset` resets gemwin. Port: `Source/Core/GemSpace.*`, unit-tested.

GEM transformation objects (`translateXYZ`, `rotateXYZ`) act on the GL matrix
stack, which is only saved and restored by `[separator]`. Without a separator,
transformations **leak into later branches** of the same trigger. STRIP relies
on this (§2.5). GEM `[rectangle w h]` and `[square s]` take **half** sizes:
`rectangle 13.303 4` is 26.6 × 8 units.

### 2.2 Frame graph and layer order

One `[gemhead 10]` feeds `[t a a a a a a a]`. Pd triggers fire right-to-left,
so a frame runs in this order (later = drawn on top):

| # | Trigger outlet | Gate (HEAD) | Subpatch | What it does |
|---|---|---|---|---|
| 0 | 6 | none | CAMERA inlet 1 | Off-screen: camera → FBO tex unit 2 ("depth") and unit 3 (inverted); motion analysis for auto-invert |
| 1 | 5 | MIXHEAD (`mixlvl`) | MIXOUTPUT | **MIX** — 4 photos blended, bokeh-masked by the camera; opaque background |
| 2 | 4 | PADHEAD (`padlvl`) | PAD inlet 0 | **PAD bank A** — PAD16, PAD17, PADv16, PADv17 |
| 3 | 3 | STRIPHEAD (`striplvl`) | STRIP | **STRIP** — 4-image panorama, bokeh-masked |
| 4 | 2 | none | CAMERA inlet 0 → CAMERAOUTPUT | **CAMERA live** layer; also records camera loops |
| 5 | 1 | PADHEAD (`padlvl`) | PAD inlet 1 | **PAD bank B** — PAD25, PAD26, PADv25, PADv26 |
| 6 | 0 | SAMPLEHEAD (`samplerlvl`) | SAMPLER → SAMPLEROUTPUT | **SAMPLER** — two camera-loop players |

**HEAD gating** (performance optimisation): each HEAD opens its spigot as soon
as the layer's level is ≠ 0, and closes it 200 ms after the level reaches 0,
so fade-outs complete. In the port: skip a layer whose effective alpha is 0.

Blending: GEM `[alpha]` → `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`. Layers are
painter-ordered quads. The port should draw 2D layers in the order above with
standard alpha blending and no depth test.

### 2.3 Image buffers (`pix_buffer`)

| Buffer | Slots | Allocated as | Filled by | Source images |
|---|---|---|---|---|
| `mixbuffer` | 5 | 1024×1024 RGBA | `loadmix …` → pix_buffer | `images1024/` |
| `padbuffer` | 12 | 1024×1024 RGBA | `loadpad …` | `images800x600/` |
| `stripbuffer` | 4 | 2048×600 RGBA | `loadstrip …` | `images_strip/` |
| `camcap1`, `camcap2` | 128 each | 640×480 RGBA | live camera (§2.7) | — |

The `load*` message bodies go straight to `pix_buffer` (`open <file> <slot>`).
Tx expands presets into these messages (§3).

The photos are not square: `images1024/` files are 1024 wide and 676–1024
tall. A slot holds the file at its own size, and `pix_texture` maps the
**whole image onto the quad**, so each photo is stretched to fill the
square buffer and its own aspect is lost. The final shape comes from the
output geometry (§3.4). The port reproduces this (D14). A missing or
unreadable file leaves the slot's previous image in place, as `pix_buffer`
did.

### 2.4 MIX (MIXOUTPUT)

Messages: `mix mix0|mix1|mix2|mix3 <a>`, `mix mixLevel <l>`,
`mix focus <f>`, `mix bias <b>`, `mix blurMode <m>`, `mix r0..r3` (re-pin slot
index).

1. Off-screen FBO (1024×1024, tex unit 1). Four full-frame squares
   (`square 4.04` at z = −4), each showing `mixbuffer` slot k with
   `alpha = mix_k × mixLevel`. `mixLevel` is smoothed over 100 ms. The mix_k
   values are **not** smoothed in Rx. Each image has its own `[separator]`.
   Draw order bottom→top: slot 2, 3, 0, 1.
2. Bokeh pass (`pix_shader bokeh 1`): `bgl_RenderedTexture` = unit 1,
   `bgl_DepthTexture` = camera unit 2 or 3 (see blurMode). The result goes on
   the output quad with global geometry. The shader writes `a = 1`, so **MIX
   is opaque** and forms the background.

`mixLevel` is also sent as `mixlvl` (gates MIXHEAD).

**blurMode** (same logic in STRIP):

| m | Depth texture | bias |
|---|---|---|
| 0 | unchanged | forced to 0 (no blur); later `bias` messages blocked |
| 1 | unit 2 — camera | stored bias re-sent, bias passes |
| 2 | unit 3 — inverted camera | idem |
| 3 | `2 + CamInv` — follows auto-invert live | idem |
| 4 | `2 + (CamInv == 0)` — opposite of auto-invert (MIX only; in STRIP the gate is never opened) | idem |

**Bokeh shader** (`shader/bokeh/bokeh.frag`) — the camera-as-mask effect:

```
factor  = depth(uv).r - focus
dofblur = clamp(factor * bias, -3, 3)
color   = mean of 41 taps of RenderedTexture at uv + offset_i * dofblur
          (offsets on rings of radius 0.4 · {1, 0.9, 0.7, 0.4}; aspect 1)
alpha   = 1
```

Pixels whose camera brightness is close to `focus` stay sharp. Elsewhere the
photo blurs in proportion to the brightness difference × `bias`. Offsets reach
1.2 in normalised UV at full blur, a very wide kernel, and that is part of the
look. `bgl_DepthTexture` is sampled with the same UV as the photo.

### 2.5 STRIP

Messages: `strip posH <x>` (smoothed 500 ms), `strip posZ <z>`,
`strip stripLevel <l>`, `strip focus|bias|blurMode` (as MIX), anything else →
re-pins slots 0–3 200 ms later (refresh after loading).

1. FBO A (1024×768, tex unit 4): four `rectangle 13.303 4` (26.6×8 units),
   one per `stripbuffer` slot, as a chain with **no separator**:
   slot 3 at `translate(posH, 0, −4 + posZ)`, then slot 2 at a further −26.6 in x,
   slot 1 at −26.6, slot 0 at −26.6. The result is a 106 × 8 unit horizontal
   panorama scrolled by `posH`, slot 3 rightmost.
2. FBO B (1024×768): `pix_shader bokeh 2` on a `rectangle 5.33 4` at z = −4,
   rendered texture = unit 4, depth = unit 2/3.
3. FBO B's texture on the output quad with global geometry,
   `alpha = stripLevel` (unsmoothed in Rx). Also sent as `striplvl`.

### 2.6 PAD (two banks × four quads)

Messages (`pad …`): `notevel <pad 1–24> <velocity 0–127>`, `padmode <0–4>`,
`padlevel <l>` → `padlvl` (master pad level, gates PADHEAD),
`pddcy <d>` (decay factor), `pdfade <f>` (fade factor),
`padVidInc <0|1>` (camera-pad start-frame behaviour).

Each pad quad (PAD16/17/25/26 read `padbuffer`, PADv16/25 read `camcap1`,
PADv17/26 read `camcap2`) draws its texture with `alpha = level × padlvl` on
the output quad with global geometry. The PADv quads are also tinted by
`sampler col1` (camcap1) or `sampler col2` (camcap2) RGB.
Bank A draw order: PADv17, PADv16, PAD17, PAD16 (top). Bank B: PADv26, PADv25,
PAD26, PAD25.

`padmode` selects how notes are interpreted (PADMODE subpatch; mode 4's outlet
is unconnected):

**Mode 0 — flash** (padmode0a/0b). Pads 1–12 → PAD16 shows image `pad−1`;
pads 13–24 → PAD25 shows image `pad−13`. Level jumps to `v = vel/127`, then
ramps linearly to 0 over `v × pddcy × 400` ms. Each bank has one quad, so a new
hit replaces the image and restarts the envelope.

**Mode 1 / mode 2 — crossfade** (padmode1 → PAD16/PAD17 bank A;
padmode2 → PAD25/PAD26 bank B). Image = `(pad−1) mod 12`. Two quads (L1, L2)
act as an A/B pair. Fade time `T = (128 − vel) × pdfade / 2.5` ms, so harder
hits fade faster. On a hit:
- both levels 0 → load image on L1, fade L1 to 1 over T;
- different pad, L1 showing (L1>0, L2=0) → load on L2, fade L2 in and L1 out over T;
- different pad, L2 showing → load on L1, fade L1 in and L2 out;
- same pad again while exactly one is showing → fade it out. Two quirks
  (Q3):
  - the fade-out uses the T stored at the **previous** re-press (trigger-order
    bug: `[t f b]` bangs before it stores), so the very first re-press snaps
    out in 0 ms;
  - when L2 is the visible one it fades over `T_prev × 50`; the "L1 → 0" in
    that branch is a no-op, since L1 is already 0.

  The port reproduces both quirks (PIXOPHONE-PLAN D14; compat flag to fix later).
- `padmode` change, or footswitch `fswpk`, fades both out over T.

**Mode 3 — camera-loop pads** (padmode3a/3b/4a/4b). Pads 1–6 → PADv16
(camcap1, speed1), 7–12 → PADv17 (camcap2, speed2), 13–18 → PADv25 (camcap1),
19–24 → PADv26 (camcap2). Pads 13–24 are re-based by −12 first. On a hit:
- level envelope as mode 0 (jump to vel/127, linear decay over `v·pddcy·400` ms);
- frame playback: a metro with period `140 − speedN` ms (`sampler speed1|speed2`)
  advances a counter mod 128. Displayed frame =
  `(counter + (p−1)·14 + padVidInc·7) mod 128`, with p the (re-based) pad number,
  so pads 7–12 start at frame 84, 98, … mod 128. On retrigger
  `counter = counter × padVidInc mod 128`: 0 restarts, 1 continues;
- the metro stops when the decay ends.

### 2.7 CAMERA, CAMERAOUTPUT, SAMPLERCAPTURE, CamAutoInv

The patch has three `[pix_ps3eye]` objects: **one camera opened three times**
(author, Q2). The port has one `CameraSource` feeding three chains.

**Off-screen chain** (frame step 0):
- FBO 1024×1024, **tex unit 2**: camera → `pix_levels` (`cam mlvl`) →
  `pix_motionblur` (`cam blur`) → full-frame square. This is the bokeh "depth".
- FBO 1024×1024, **tex unit 3**: unit 2's texture through `pix_invert` →
  square. Intended: the inverted camera. But this gemlist carries no pixel
  data (no `pix_` source upstream; the texture comes in through
  `pix_texture`'s right inlet), and GEM `pix_` objects do nothing without
  pixel data. So unit 3 is most likely an **uninverted copy** of unit 2. That
  makes blurMode 1 ≡ 2 and 3 ≡ 4 in practice. The port reproduces this (D14),
  with a compat flag for a real inversion. Confirm on a real Pd + GEM install
  if one is ever available.
- Motion: camera → CamAutoInv (below).

**Live chain** (frame step 4): camera → `pix_invert` (driven by CamAutoInv /
`cai inv`) → `pix_levels` (`mlvl`) → `pix_motionblur` (`blur`) →
`pix_coloralpha` (alpha from colour, so bright table = opaque, shadows =
transparent) →
- SAMPLERCAPTURE (record), and
- `pix_color` (`cam col <r g b>` tint; `cam bypass <0|1>` enables or disables
  the tint) → CAMERAOUTPUT: output quad with global geometry,
  `alpha = camput lvl` (smoothed 100 ms). Not HEAD-gated.

`cam ps3eye 0|1`: camera off/on, also starts/stops CamAutoInv's 50 ms
reporting metro. `cam ps3eye <anything else>` is forwarded to the camera object
(device settings — see §3 for what Tx sends).

**SAMPLERCAPTURE**: `camcap 1|2` selects the target buffer (`camcap1`/`camcap2`).
Any other `camcap` value starts recording: one frame per rendered frame into
slots 0…127, reporting `camcap frame <n>` and finally `camcap recend`. At 15 fps
that is ≈ 8.5 s of material. The port records at a configurable capture rate,
default 15 fps, so the 128-frame logic keeps its timing (D12).

**CamAutoInv**: `cai threshold <t>` → `pix_movement` threshold;
`cai aMode <0|1>` enables analysis; `cai inv <0|1>` sets the inversion (also
published as `CamInv`, which drives blurMode 3/4). Analysis: `pix_movement` →
`pix_mean_color` alpha channel summed over each 50 ms window, sent to Tx as
`cai 0 <sum>`. **The inversion decision is made in Tx** (§3) and sent back as
`cai inv`, so in the port this is a local loop: motion → decide → invert.

### 2.8 SAMPLER / SAMPLEROUTPUT and global GEOMETRY

**SAMPLER**: two players over `camcap1`/`camcap2`.
`sampler pos1|pos2 <frame>` sets the displayed frame. Playhead logic (play,
speed, loop in/out, scrub) lives in **Tx** and streams positions (§3).
`sampler col1|col2 <r g b>` tints players and PADv quads. `samput 3 <0|1>`
swaps which player draws first.

**SAMPLEROUTPUT**: each player on the output quad;
`samput 1 <l>` / `samput 2 <l>` → alpha (smoothed 100 ms);
`samplerlvl = l1 + l2` gates SAMPLEHEAD.
`samput 4 <1|2> v|h <x>` → flip: player rotation X (v) or Y (h) =
`x × 180 + rx|ry`.

**GEOMETRY** — one shared transform for every output quad (projection fitting):
`geo tx|ty|tz|width|height|rx|ry|rz <value>`, each smoothed over 300 ms.
Applied as `translate(tx,ty,tz) · rotateXYZ(rx,ry,rz) · rectangle(width,height)`.
Nominal: tz ≈ −4, width 5.33, height 4 (a 4:3 plane filling the ±4 view).
Tx computes these from CCs and aspect-ratio presets (§3).

### 2.9 `pix_shader` and unused shaders

`[pix_shader <name> <id>]` loads `shader/<name>/<name>.vert|.frag`, links them,
and accepts uniforms on its right inlet or via `<name>[<id>].args`. It bangs
its right outlet when loaded (used to set the sampler units). Only `bokeh` is
used in v114. `vBlur` (3-tap separable blur on `sampler2DRect`) and `zBuffer`
(eye-space depth to grey) are leftovers of earlier versions.

---

## 3. Tx — the control surface

Tx holds all parameter state, scales it, and sends the §2 messages. It uses no
GEM objects. **The GUI widgets are the parameter store.** Each IEM widget has
a receive name (e.g. `padlevel`) and a send name (e.g. `pdlvl`). MIDI,
footswitches, the FBV pedal, the Arduino and the resets all write to the
receive name. The widget re-emits on its send name, and the logic listens
there. The port should replace this with one parameter model (value, range,
default, smoothing, MIDI binding, feedback) that GUI and controllers both bind
to.

| Subpatch | Role |
|---|---|
| PAD (+PADMODE) | nanoPAD notes; pad level, decay, fade, mode, Vid.Inc |
| LOADPAD (+PADLOCK, PADPRESETCREATOR) | 12 pad slots: manual load (file dialog), preset load, locks, thumbnails |
| LOADMIX (+MIXTHUMBNAILS, MIXLOCK, ldmx) | 4 mix slots: load, preset load, locks, swaps, thumbnails |
| MIXOUTPUT (+REFRESHMIX) | XY grid → 4 layer weights; mix level, master level, blur, refresh modes |
| MIXLOOP | Records and loops the XY-grid gesture |
| STRIP / LOADSTRIP | Strip position, zoom, level, blur; 4-image strip presets |
| CAMERA → ps3eyeControl, MASKOLOR, MASKLVL, MOTIONBLUR, CAMAUTOINV (+CmodeIndex), SAMPLERCAPTURE | Camera settings, colour mask, levels, blur, auto-inversion, loop recording |
| CAMERAOUTPUT, SAMPLER (+MASKOLOR_SAMPLER1/2), SAMPLEROUTPUT | Levels; **sampler playheads (computed on Tx)**; crossfade, order, flips |
| GEOMETRY | Translate, rotate, aspect ratio of the output quad |
| FTSW1 / FTSW2, FBV (+FBV_PARAM, FBV_LEVEL), ARDUINO | Footswitch function matrix, FBV pedal with soft takeover, Firmata touchpads |
| GUI (+GRIDMIX, sidepanel, graph) | The 1400×1008 control window |
| NET ×2, PICO | Links to Rx (`pob4`) and to a third renderer, Pico (localhost) |

### 3.1 Commands sent to the renderer (the internal API)

`master` = `mstrlvl / 127`. Values marked † went over TCP (discrete
messages); everything else went over UDP.

| Message | Args, range, formula |
|---|---|
| `pad notevel n v`† | nanoPAD note-on only (`stripnote`); raw note and velocity |
| `pad padmode m`† | 0 Flash, 1 Fade 1 (bank A), 2 Fade 2 (bank B), 3 Vidéo |
| `pad padlevel` | `pdlvl / 127` |
| `pad pddcy` | `ln(cc + 6)`, range 1.79–4.89; reset = ln 70 |
| `pad pdfade` | raw 0–127; reset 36 |
| `pad padVidInc 0/1`† | |
| `geo tx`, `geo ty` | `v·24/400 − 12`, v ∈ 0–400 (14-bit NRPN), default 200 → 0 |
| `geo tz` | `v·(−14/400) − 0.9`, default 85 → −3.875 |
| `geo rx`, `ry`, `rz` | `180 − v`, v ∈ 0–360, default 180 → 0 |
| `geo width`, `geo height` | from aspect slider a ∈ 0–127 and projector ratio, see §3.4 |
| `mix mix0..mix3` | x = `mixx/127`, y = `mixy/127`. Bilinear: w0 = (1−x)·y (top-left), w1 = (1−x)(1−y) (bottom-left), w2 = x·y (top-right), w3 = x(1−y) (bottom-right). Sent value `g = (w + 2√w) / 3` |
| `mix mixLevel` | `mxlvl/127 × master` |
| `mix focus`, `bias`, `blurMode`† | `/127`; `/1270`; 0–4 (labels OFF, Neg, Pos, Auto, A.inv). Defaults 32, 89, 0 |
| `mix r0..r3`† | refresh layer N (§3.3 refresh modes) |
| `strip posH` | `(h − 75.59) / 8.15748`, h ∈ 0–800 (14-bit), default 400 |
| `strip posZ` | `−4 − z/127 × 8` |
| `strip stripLevel` | `lvl/127 × master` |
| `strip focus`, `bias`, `blurMode` | as mix |
| `strip loadbang`† | re-pin slots after a strip load |
| `camput lvl` | `wclvl/127 × √(master × camOn)` |
| `cam col r g b` | `m · c / 16129` (mask colour M × R/G/B, 0–1) |
| `cam bypass`† | **inverted**: GUI toggle on → sends 0 |
| `cam mlvl inLo inHi outLo outHi` | each 0–1, throttled to 100 ms |
| `cam blur` | `v / 133` |
| `cam ps3eye gain g`, `exposure e` | `v·79/127`; `v·3.2` |
| `cam ps3eye format VGA\|QVGA`, `fps 15\|30`, `ps3eye 0\|1`† | gain/exposure re-pushed 500 ms after a format or fps change (driver workaround) |
| `cai threshold` | `v / 127`, default 8 |
| `cai inv 0/1`†, `cai aMode 0/1`† | |
| `camcap 1\|2`†, `camcap recstrt`† | record target; start recording |
| `sampler pos1`, `pos2 f` | frame 0–127; sent on change by a 50 ms metro, only while that sampler's level ≠ 0 |
| `sampler speed1`, `speed2` | raw 0–127 (frame period = 140 − speed ms) |
| `sampler col1`, `col2 r g b` | as `cam col` |
| `samput 1`, `samput 2 x` | `lvl/127 × xf × √master × playing`. Crossfade on: `xf = ((127−c)/127)²` for sampler 1, `(c/127)²` for sampler 2; otherwise xf = 1 |
| `samput 3 0/1`†, `samput 4 <1\|2> <h\|v> 0/1`† | which sampler is on top; flips |
| `loadmix open <path> <0–3>`† | |
| `loadmix copy src dst`† | swap a↔b = `copy a 4`, `copy b a`, `copy 4 b` (slot 4 is scratch) |
| `loadpad open <path> <0–11>`†, `loadstrip open <path> <0–3>`† | |
| `gem window 0–5`†, `gem render 0/1`†, `gem reset`† | §2.1 |
| Pico (**out of scope**, D10) | `picolevel` (raw 0–127, ignores master), `picothreshold`, `picoinvert`, `picofliph`, `picoflipv`, `picogemwin`, `picorender`, `picoreset`, `picoload open ../../images640x480/<name>.jpg <idx>` |

Back-channel from the renderer: `cai 0 <motion>`, `camcap frame N`,
`camcap recend`.

**Logic that existed only because of the two-machine split** (drop or move
into the engine): the NET block (connect/disconnect with 200 ms delays,
`net 0/1`, test pings, TCP/UDP status); `speedlim 20` on the mix XY and
`speedlim 100` on the mask levels; the sampler playhead streamed at ≤ 20 Hz
(move it into the engine and keep the `140 − speed` ms semantics); the
auto-invert round trip; the record countdown and progress round trip; Tx's
mirror of what Rx has loaded (thumbnails, locks); relative image paths that
need the same folder layout on both machines; GIF thumbnail copies for the Tk
GUI; staggered 20/60/80/100 ms delays and `[pipe 5]` after a master reset.
**Keep**: the camera settings re-push after format changes (if the new camera
needs it) and the MIDI echo suppression (§3.2).

### 3.2 MIDI and other inputs

One Pd MIDI input (MIDI Yoke 2, merged by MIDI-OX — `controllers/MIDI_merge.ini`)
and one output (MIDI Yoke 1). Devices are told apart by channel:
**BCR2000 preset 27 on ch 1** (plus NRPN on ch 11), **nanoPAD on ch 2**,
**FBV on ch 3**. A comment mentions an LPD8 on ch 4, but nothing uses it.
NRPN lives on ch 11 so that NRPN CCs 98/99/6/38 don't collide with
CC 98/99 on ch 1, which are levels. The layout matches a stock BCR2000: each
top push-encoder's push sends its CC + 32.

| BCR2000 control | Function |
|---|---|
| Group 1, enc 1–6 | NRPN 201/202/203 = translate X/Y/Z (0–400); NRPN 204/205/206 = rotate Y/Z/X (0–360). Pushes CC 33–38 reset each one |
| Enc 7 | push CC 39 = reset geometry |
| Enc 8 | CC 8 aspect ratio; push CC 40 = 4:3 |
| Group 2, enc 9–16 | CC 9 exposure (push 41 → 127); CC 10 gain (42 → 70); CC 11 invert-mode preview (push 43 applies); CC 12 invert sensitivity (44 → 64); CC 13 threshold (45 → 8); CC 14 camera blur (46 → 0); CC 15 refresh-mode preview (47 applies); CC 16 mix preset number (48 loads) |
| Group 3, enc 17–24 | CC 17 pad decay (49 reset); CC 18 pad fade (50 reset); CC 19 / push 51 unused; CC 20 pad-mode preview (52 applies); CC 21 pad preset (53 loads); CC 22 strip preset (54 loads); NRPN 223 strip position 0–800 (55 → 400); CC 24 strip zoom (56 → 0) |
| Group 4, enc 25–32 | Sampler 1: CC 25 in, 26 out, 27 speed, 28 scrub. Sampler 2: CC 29–32. Pushes 57/58/59/60 = play/stop/pause/loop for sampler 1; 61–64 for sampler 2 |
| Buttons CC 65–72 / 73–80 | mask colour M/R/G/B and mask levels inLo/inHi/outLo/outHi → 127 / → 0 |
| Encoder rows CC 81–104 | 81–84 mask colour M/R/G/B; 85–88 mask levels; 89–92 sampler 1 colour; 93–96 sampler 2 colour; 97 mix, 98 camera, 99 sampler 1, 100 sampler 2, 101 pad, 102 strip, 103 Pico, 104 master |
| Bottom-right buttons | CC 105 mask bypass; 106 master → 127; 107 invert auto/manual; 108 master → 0 (blackout) |
| CC 109 / 110 (ch 1) | footswitch 1 / 2 (probably the BCR's footswitch jacks) |

"Preview" encoders (CC 11, 15, 20) send values with an offset, and the patch
takes `mod N`. Turning lights the GUI indicator; pushing applies the last
**encoder** value. **Feedback**: `ctlout`/`nrpn_out` echo every continuous
value, plus toggles 59/60/63/64/105/107, back to the BCR's LED rings. Each
goes through an echo filter (`[t f f] → [!=] → [spigot]`): the patch
remembers the controller's last value and only sends when the parameter
differs from it.

- **nanoPAD** (ch 2): note-on → `pad notevel`. Its 12 pads match the 12 pad
  slots; scene changes give 24 notes (banks A/B). The XY pad is unused.
- **Line 6 FBV** (ch 3; likely an FBV Express with A/B/C/D and a pedal):
  CC 110/112 step the parameter target ±1 (11 targets); CC 111/113 step the
  level target ±1 (8 targets). The first press only switches bank.
  CC 115/116 = pedal. **Soft takeover**: the pedal only takes over within ±2
  of the current value (±7 for strip position, where it is scaled ×800/127)
  and lets go when the value changes elsewhere. The GUI colours the pedal
  slider by state (below / caught / above).
  - Parameter targets: pad decay (log domain), pad fade, exposure, gain,
    invert sensitivity, blur, speed 1, speed 2, sampler crossfade, strip
    position, strip zoom.
  - Level targets: mix, camera, sampler 1, sampler 2, pad, strip, master, Pico.
- **Arduino** (`comport`, port 8, 57600 baud, Firmata analog):
  - Touchpad 1 (A0/A1) → mix XY grid. Scaling `(raw−90)/850·127` and
    `(raw−100)/820·127`, clipped 0–127, sampled 22 ms after each reading.
    "Touched" means not both raw values < 10. The touch signal `tp1` drives
    refresh mode T.
  - Touchpad 2 (A2/A3, `(raw−120)/770·127`) scrubs sampler 1 (X) and
    sampler 2 (Y).
  - A5 is only displayed. In the sketch, a potentiometer on A5 directly sets
    `analogWrite` on **pin 3**: stock PWM at ≈ 490 Hz (Timer2 on an Uno or
    Duemilanove). **Confirmed by the author:** this dial dimmed the light
    table, made of **red LED strips under opal plexiglass**. The value is
    only reported to Pd. Through the camera's rolling shutter, that PWM
    became the horizontal scan lines.
    - The author shaped them with **camera exposure and this dial (PWM
      duty)**, plus a 1/2/3-die switch on the rig for coarse brightness
      (band model in §1).
    - Frame rate (via readout time) sets how many bands fit in a frame:
      ≈ 15 at VGA @ 30 fps, measured.
    - **Band drift**: 490 Hz is not a whole multiple of the frame rate
      (490/30 ≈ 16.33), so the pattern shifts by the fractional part, ≈ 1/3
      cycle per frame, and the bands crawl vertically.
    Red light on the PS3 Eye's colour sensor mostly reached the red-filtered
    photosites, so effective resolution was lower and the image noisier than
    nominal. The bokeh shader reads the camera's **red channel** as its
    depth (`depth1.x`, §2.4), so the mask was driven by exactly that signal.
  - Sketch: `controllers/Arduino code/Touchpad_LEDpwm` (Firmata, 57600 baud;
    the touchpads are 4-wire resistive, driven by pins 2/4 and read on
    A0–A3).
- **Camera exposure and gain were played live** (author): gain for grain,
  exposure (with the flickering light) for scan lines of varying thickness.
  BCR CC 9 / CC 10, FBV pedal targets, footswitch resets (§3.2, §3.3).
  Exposure and gain are performance parameters, not setup.

### 3.3 Control logic

- **Footswitches** (FTSW1/FTSW2): each has a matrix of about 33 assignable
  functions (sidepanel page 1); one press fires every assigned function.
  - Functions: load pad/mix/strip preset; clear pad/mix locks; reset camera
    exposure/gain/sensitivity/threshold/levels/colour; toggle invert
    auto/manual; blur toggle (0 ↔ `fswcbv`, default 64); reset pad decay/fade;
    cycle pad mode; cycle refresh mode; level toggles 0 ↔ 127 (camera, mix,
    master, sampler); reset geometry; recall an aspect preset; toggle GEM
    render.
  - Sampler transport: each footswitch drives its own sampler.
  - Record: "Sampler_Record" on FTSW1 starts recording; on FTSW2 it toggles
    the target 1/2.
  - Mix loop: FTSW1 tap = play/stop, hold > 250 ms = pause while held;
    FTSW2 = record while held.
  - Order: load fires before unlock when both are assigned.
- **Sampler transport** (per sampler, one expr per tick):
  - Not playing: hold the position.
  - At the out point: jump to in if looping, else stop. Otherwise step ±1
    toward out, so in > out plays backwards.
  - Loop is forced off when in == out.
  - Play: position = in, unpause. Stop or end of play: `playing = 0`, which
    also zeroes `samput` (a stopped sampler is hidden).
  - Reset: in 0, out 127, speed 73, loop and pause off.
- **Recording**: arm → 5 s countdown (bar 127 → 0 over 5000 ms) →
  `camcap recstrt` → `frame N` progress → `recend` re-arms.
- **Auto-invert**: invert modes 0 N / 1 P are manual; 2 A is auto. In auto,
  the `cai` motion values are summed, and when the sum exceeds `invsens / 8`
  the inversion flips and the sum resets (`invint` shows progress). The final
  `inv` is **XORed with mask bypass**.
- **Refresh modes** (re-assign MIX slot textures after a load or swap, so
  changes appear at a good moment):
  - L: refresh all immediately.
  - T: refresh all when touchpad 1 is touched.
  - I: refresh each layer once its weight drops below 0.1, i.e. while it is
    invisible.
  - A GUI bang refreshes all.
- **Mix loop**: 1 s pre-roll, then records `mixx/mixy` every 67 ms (up to
  15000 samples; the last 15 are trimmed on stop). Plays back with period
  `140 − speed` ms and `line` interpolation, looping. Any manual grid move
  stops playback.
- **M_Reset** (`mastereset`): every parameter to its default, **master → 0**,
  mix/pad/strip preset 0 reloaded, camera on/VGA/30 fps, aspect 4:3,
  projector 16:9.

### 3.4 Geometry and aspect ratio

With `f(i) = i²·6.35e-5 + i·3.75e-3 + 0.5` and aspect slider a ∈ 0–127:

```
width  = f(127 − a) < Wt ? Wc : 4 / f(127 − a)
height = f(a)       < Ht ? 4  : Hc / f(a)
```

| Projector | Wt | Wc | Ht | Hc |
|---|---|---|---|---|
| 16:9 | 0.562 | 7 | 1.778 | 7 |
| 4:3 | 0.74 | 5.33 | 1.4 | 5.33 |

Aspect presets (value of a): 2:1 = 127, 16:9 = 113, 3:2 = 100, 4:3 = 89,
5:4 = 80, 1:1 = 64, 4:5 = 45, 3:4 = 40, 2:3 = 30, 1:2 = 0. Changing the
projector ratio does not recompute width/height until the slider moves (bug).

### 3.5 GUI (1400×1008, plus a small root window)

The root window has: connection, M_Reset, GEM window radio, render/reset,
projector ratio, Pico controls, Arduino port, TCP/UDP status.

Main window panels (a screenshot is on page 3 of `dossier2/Le_Pixophone_pob.pdf`):
- **PAD**: 6×2 thumbnails with lock bars; preview of the selected preset;
  12 load buttons; lock rows; mode radio; Vid.Inc; Decay and Fade; level;
  preset number + Load.
- **MIX**: 2×2 thumbnails, locks, 4 swap buttons, refresh L/T/I, preset +
  Load, the 127×127 XY grid, MIX_LEVEL, loop transport.
- **BLUR**: mode radio, Focus and Bias for mix and strip.
- **WEBCAM**: on/off, QVGA/VGA, 15/30 fps, exposure, gain, threshold, invert
  N/P/A with sensitivity and progress, four level sliders with a live curve,
  blur, mask colour with Reset and Bypass, level.
- **SAMPLER**: two transport blocks (in/out/scrub/speed, flips, level), top
  selector, crossfade, Rec with target and frame bar.
- **STRIP**: preview with section indicator, position/zoom, level, preset +
  Load.
- **VIDEO_OUTPUT**: 10 aspect buttons, ratio rectangle, MASTER_LEVEL, current
  FBV target.
- **SIDEPANEL**, 3 pages: footswitch matrix; colours (mask, sampler 1/2,
  copy buttons); FBV targets, pedal, geometry.

External Pd dependencies (none needed in the port): cyclone `comment`, zexy
`l2s`, ggee `image`, iemlib `splitfilename`/`speedlim`, unauthorized `grid`,
`knob`, mapping `tolist`, `comport`. `nrpn_in`/`nrpn_out` are the author's
own abstractions and are not in the archive.

---

## 4. Presets and assets

Old project root: `E:\Make\Le_Pixophone`.

**Preset source = `E:\Make\Le_Pixophone\presets`** (author, Q6). These files
date from 2009–2010 and hold full absolute paths
(`F:/Le_Pixophone/images1024/04112005_m2.jpg`). v114 (Oct 2011) instead reads
each line as a bare image name and builds `../../images1024/<name>.jpg` (mix
and pad) or `../../images_strip/<name>.jpg` (strip). The port's loader accepts
both forms: keep the file name, and resolve it against a configurable media
root and the layer's bank folder.

Checked on 2026-10-14 by file name:
- mix presets: 68/68 lines found in the archive;
- pad presets: 60/60;
- strip presets: 8/248 — `images_strip/` holds only 8 files.

The author has more in other archives. Missing images are not a blocker:
what matters is that the preset **logic** is ported, and the loader must
tolerate missing files (empty slot plus a warning).

| File | Content | Used for |
|---|---|---|
| `presets/mixpresetN.txt` (0–16) | 4 lines (`images1024/`) | MIX slots 0–3 |
| `presets/padpresetN.txt` (0–4) | 12 lines (`images800x600/` in these files; v114 reads `images1024/`) | PAD images 0–11 |
| `presets/stripresetN.txt` (0–61) | 4 lines (`images_strip/` or set-specific) | STRIP slots 0–3 |
| `presets/*presetN.gif/.jpg` | preset thumbnails shown in the Tx GUI | GUI |
| `presets/samplongN.txt`, `sampshrtN.txt` | `<idx> <path.wav>` lines | **audio** — out of scope |
| `presets2/{mix,pad}preset%d.txt` | written by `preset1.pd` (`textfile write`) | preset save target |

Preset numbers run 0–127. A number is "armed" 60 ms after it changes, its
`.gif` preview is shown, and Load applies it. **Locked slots are skipped.**
Presets hold only image names: no locks, levels or geometry. There is no strip
preset writer. `preset1.pd` saves the names in `presetpad0–11`/`presetmix0–3`,
which are only updated by manual (file-dialog) loads, so a save after a preset
load or swap can be stale.

Media: about 298 pinhole photos, each in `images format original/`,
`images1024/`, `images800x600/`, `images640x480/` and `images_gif/`
(GUI thumbnails), plus `images_strip/`. Set-specific banks live in
`_extra_sets/Tchernobyl` and `_extra_sets/W-In-Memoria`.

Controllers (`patch_PureData/controllers/`): Akai LPD8 programs (`1–4.lpd8`),
Korg nanoPAD set (`pixophone1.nanopad_set`), Behringer BCF2000/BCR2000
manuals and reference sheets, Line 6 FBV (`FBV_1.fbv` — the `fsw*` footswitch
receives), `MIDI_merge.ini`, and the Arduino `Touchpad_LEDpwm` sketch.

---

## 5. Port notes (render side)

- Everything GEM did implicitly must become explicit: matrix leakage (STRIP),
  the non-aspect-corrected ±1 frustum, half-size primitives, painter's order.
  Reproduce the geometry exactly first, then modernise (e.g. a proper
  projection-mapping quad).
- Smoothing (`line`) is in two places: Rx smooths geometry (300 ms), posH
  (500 ms), and mixLevel, camput and samput levels (100 ms). Tx smooths
  others. In one app, make smoothing a property of each parameter.
- Image `pix_buffer` slots (mix/pad/strip) become GPU textures. Camera loops
  (2 × 128 frames at 640×480 RGBA ≈ 315 MB in 2010) become RAM rings of N
  frames at R fps with logic in 1/128 "loop units" (PIXOPHONE-PLAN D16); the
  legacy preset 128 @ 15 fps is the exact original.
- The PS3 Eye defects (scan lines, noise) were part of the look. The real
  source is the camera plus the PWM light table (§1). The optional
  "sensor character" stage can emulate it with the §1 band model, using
  PWM frequency, duty, LED die level, top-light fill, exposure, line time
  and drift, plus gain-driven
  noise, and should be checked against `Documentation/reference/`. The 15 fps output rate was **not**
  part of the look — it was the hardware limit (Q1). Render at display rate;
  only loop capture keeps 15 fps by default (D12).
- GEM camera objects as ported (assumed semantics; check against a real GEM
  if one is ever available). All are 8-bit, per frame of the legacy tick:
  - `pix_invert` = 1 − rgb.
  - `pix_levels` with `inLo inHi outLo outHi` = a linear map of
    [inLo, inHi] onto [outLo, outHi], clamped, the same on r, g and b.
  - `pix_motionblur` = feedback, out = in·(1 − b) + previous·b.
  - `pix_coloralpha` = alpha from the average of r, g and b.
  - `pix_color r g b` = replaces the colour and keeps the alpha.
- Master level: per layer, with the original curves (√ or linear), as in
  patch114 (Q11, D14). A final-gain mode exists only as a compat flag.
- Quirks are reproduced, each behind a named compat flag that defaults to the
  original behaviour (D14).

---

## 6. Questions for the author (all resolved)

**Answered (2026-10-14):**
- **Q6** — presets live in `E:\Make\Le_Pixophone\presets` (older full-path
  format; see §4). Not every preset's images survive; port the logic and
  tolerate missing files.
- **Q7** — Pico is **out of scope**; that pico projector no longer works
  (PIXOPHONE-PLAN D10).
- **Q9** — the author still owns everything, though it is not on hand: the
  FBV and nanoPAD will come back; the Arduino touchpads are built into the
  performance rig (in storage for now); the BCR2000 may be replaced by
  another controller. For now, **port the old system as is**, behind
  swappable device profiles (PIXOPHONE-PLAN D11).
- **Q1** — 15 fps was simply the most Pd/GEM could sustain then; it is not
  an aesthetic choice. The port renders at the display rate. Camera loops keep
  their 15 fps capture rate by default, so loop length and the
  default-speed feel are unchanged (PIXOPHONE-PLAN D12). Note: the sampler's
  default speed 73 gives 140 − 73 = 67 ms per frame ≈ 15 fps, i.e. real-time
  playback of a 15 fps capture.
- **Q2** — one camera opened three times (§2.7).
- **Q3** — the author doesn't remember the intent; "old-style sample trigger
  with a restart" did exist. Retrigger-and-restart lives in flash mode (new
  hit restarts the envelope) and the camera-loop pads (`padVidInc = 0`
  restarts the loop). Crossfade mode keeps both quirks in §2.6 (D14).
- **Q8** — the missing objects came from Pd-extended or from add-ons kept in
  Pd's own `extra` folder, not the project. The patch uses
  `[nrpn_in <param> 11]`/`[nrpn_out <param> 11]` with params 201–206 and 223.
  Their 0–400/0–800 ranges imply 14-bit data, so the port implements standard
  NRPN: CC 99/98 = parameter MSB/LSB, CC 6/38 = data MSB/LSB, value =
  MSB·128 + LSB. Verify against the hardware once a controller is back
  (`controllers/BCR2000 ref1.pdf` is a blank template, no mapping in it).
- **Q11** — the author remembers master sometimes producing odd transparency
  mixes, so the √/linear differences were probably tweaks. Cause: master is
  multiplied into **each layer's alpha**. Lowering it makes the upper layers
  translucent and reveals the ones below, instead of dimming the whole
  picture. The port keeps the per-layer master, odd mixes included (D14,
  superseding D13). A final-gain master is a compat flag for later.

**Resolved: keep the original behaviour** (author, 2026-10-14: "build it as
it is with the quirks and all" — PIXOPHONE-PLAN D14). Each quirk below is
reproduced and gets a named compat flag (default = original), so it can be
fixed later without hunting for it.

| Q | Original behaviour kept | Compat flag (default off) |
|---|---|---|
| Q3 | Crossfade re-press fades with the previous T; L2 fades over T × 50 (§2.6) | `fixCrossfadeRepress` |
| Q4 | Tex unit 3 is an uninverted copy of unit 2, so blurMode 1 ≡ 2 and 3 ≡ 4 (§2.7) | `realInvertedDepth` |
| Q5 | STRIP blurMode 4 never switches the depth texture | `stripBlurMode4` |
| Q10 | FTSW2 cycles pad modes `mod 3`; Clear misses `ftsw1vs`; aspect labels mismatch routing; Level_Pad sets 127 instead of toggling; projector-ratio change doesn't recompute width/height | one flag per bug: `ftsw2PadModeMod4`, `ftsw1ClearVs`, `aspectLabels`, `levelPadToggle`, `projectorRatioRecompute` |
| Q11 | Master multiplied into each layer's alpha (√ for camera and samplers, linear for mix and strip) | `masterAsOutputGain` |
| Q12 | Mask bypass disables the tint **and** flips the inversion (XOR) | `bypassNoInvert` |
| Q13 | One semantics: frame period = `140 − speed` ms (sampler transport and camera-loop pads) | — (this is the original) |
| Q14 | Mix loop: 1 s pre-roll, last 15 samples trimmed on stop | — (constants) |

**Exception: split artefacts dissolve.** Behaviour that only failed because
a message never crossed the network is not a quirk of the instrument, so it
is not reproduced. Example: Pad_Kill (`fswpk`) — the Tx footswitch sends it
and the Rx crossfade logic listens for it, so in one app it connects and
fades both crossfade layers out (PIXOPHONE-PLAN D15).

No questions are open. New ones go here, numbered from Q15.
