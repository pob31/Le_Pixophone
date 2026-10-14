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
