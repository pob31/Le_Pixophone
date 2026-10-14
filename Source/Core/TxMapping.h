#pragma once

#include <array>

#include "Core/Params.h"

namespace pixo
{

/** The Tx patch's formulas: controller-domain values → the values Tx sent
    to the renderer (PD114-ANALYSIS §3.1, §3.4). Pure functions, unit-tested
    against the spec. */
namespace txmap
{

/** Tx's mix curve: (w + 2√w) / 3. */
float mixCurve (float weight);

/** Bilinear XY-grid weights through mixCurve, indexed by mix slot 0–3.
    With x = mixX/127 and y = mixY/127:
    slot 0 = (1−x)·y, slot 1 = (1−x)(1−y), slot 2 = x·y, slot 3 = x·(1−y). */
std::array<float, 4> mixWeights (float mixX, float mixY);

/** mxlvl/127 × mstrlvl/127 (master applied per layer, linearly — D14). */
float mixLevel (float mixLevelRaw, float masterRaw);

float translateXY (float raw);  // v·24/400 − 12
float translateZ (float raw);   // v·(−14/400) − 0.9
float rotation (float raw);     // 180 − v, in degrees

float blurFocus (float raw);    // /127
float blurBias (float raw);     // /1270

/** `strip posH`: (h − 75.59) / 8.15748 for h ∈ 0–800 (§3.1). */
float stripPosH (float raw);

/** `strip posZ`: −4 − z/127 × 8 (§3.1). */
float stripPosZ (float raw);

/** `strip stripLevel`: lvl/127 × mstrlvl/127 (linear master, D14). */
float stripLevel (float stripLevelRaw, float masterRaw);

/** The STRIP panorama (D25): the original's four `rectangle 13.303 4`, chained
    26.6 apart from posH leftwards, as one quad. */
struct StripQuad
{
    float centreX = 0.0f;
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
};

StripQuad stripQuad (float posH);

/** `camput lvl`: wclvl/127 × √(master/127 × camOn) (§3.1; √ master — D14). */
float cameraLevel (float cameraLevelRaw, float masterRaw, float cameraOn);

/** `cam col r g b`: M·C / 127² per channel (§3.1). */
std::array<float, 3> maskColor (float m, float r, float g, float b);

/** `cam blur`: v / 133 (pix_motionblur's feedback amount, 0–0.955). */
float motionBlur (float raw);

/** The inversion Tx sends as `cai inv` (and Rx publishes as CamInv): the
    manual mode (1 = P) XOR mask bypass (Q12 quirk, kept). Auto mode (2)
    keeps the state the motion analysis last decided (`autoState`). */
bool cameraInverted (int invertMode, bool maskBypass, bool autoState);

/** Which camera FBO a bokeh pass reads as depth, 2 or 3 (§2.4). blurMode
    3 follows CamInv, 4 its opposite; 0–2 fix it. STRIP never opened its
    mode-4 gate, so there mode 4 keeps the previous unit (Q5 quirk). */
int depthUnit (int blurMode, bool camInv, int previousUnit, bool isStrip);

/** A 0–127 controller value across a device control's range, snapped to its step. */
long toDeviceRange (float raw, long minValue, long maxValue, long step);

/** The output rectangle's half-size from the aspect slider (0–127) and the
    projector ratio (0 = 16:9, 1 = 4:3), §3.4. */
struct RectSize
{
    float width = 0.0f;
    float height = 0.0f;
};

RectSize aspectToSize (float aspectRaw, int projectorRatio);

/** Renderer-domain geometry (Rx GEOMETRY, before its 300 ms smoothing). */
struct Geometry
{
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    float width = 0.0f, height = 0.0f;
};

/** Maps the geometry parameters, keeping Tx's quirk: width/height are only
    recomputed when the aspect slider moves, not when the projector ratio
    changes (Q10; CompatFlags::projectorRatioRecompute opts into the fix). */
class GeometryMapper
{
public:
    Geometry map (const ParameterStore::Snapshot& params, const CompatFlags& compat);

private:
    float lastAspect = -1.0f;
    int lastProjector = -1;
    RectSize size;
};

} // namespace txmap
} // namespace pixo
