#include "Core/TxMapping.h"

#include <cmath>

namespace pixo::txmap
{

float mixCurve (float weight)
{
    return (weight + 2.0f * std::sqrt (weight)) / 3.0f;
}

std::array<float, 4> mixWeights (float mixX, float mixY)
{
    const float x = mixX / 127.0f;
    const float y = mixY / 127.0f;

    return { mixCurve ((1.0f - x) * y),
             mixCurve ((1.0f - x) * (1.0f - y)),
             mixCurve (x * y),
             mixCurve (x * (1.0f - y)) };
}

float mixLevel (float mixLevelRaw, float masterRaw)
{
    return (mixLevelRaw / 127.0f) * (masterRaw / 127.0f);
}

float translateXY (float raw)  { return raw * 24.0f / 400.0f - 12.0f; }
float translateZ (float raw)   { return raw * (-14.0f / 400.0f) - 0.9f; }
float rotation (float raw)     { return 180.0f - raw; }
float blurFocus (float raw)    { return raw / 127.0f; }
float blurBias (float raw)     { return raw / 1270.0f; }

float cameraLevel (float cameraLevelRaw, float masterRaw, float cameraOn)
{
    return cameraLevelRaw / 127.0f * std::sqrt (masterRaw / 127.0f * cameraOn);
}

std::array<float, 3> maskColor (float m, float r, float g, float b)
{
    constexpr float scale = 1.0f / (127.0f * 127.0f);
    return { m * r * scale, m * g * scale, m * b * scale };
}

float motionBlur (float raw)
{
    return raw / 133.0f;
}

bool cameraInverted (int invertMode, bool maskBypass, bool autoState)
{
    const bool inverted = invertMode == 2 ? autoState : invertMode == 1;
    return inverted != maskBypass;
}

int depthUnit (int blurMode, bool camInv, int previousUnit, bool isStrip)
{
    switch (blurMode)
    {
        case 1:  return 2;
        case 2:  return 3;
        case 3:  return camInv ? 3 : 2;
        case 4:  return isStrip ? previousUnit : (camInv ? 2 : 3);
        default: return previousUnit;     // mode 0: bias is 0, the depth doesn't matter
    }
}

long toDeviceRange (float raw, long minValue, long maxValue, long step)
{
    const auto span = static_cast<double> (maxValue - minValue);
    const auto steps = std::round (span * static_cast<double> (raw) / 127.0 / static_cast<double> (step));
    const auto value = minValue + static_cast<long> (steps) * step;
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

RectSize aspectToSize (float aspectRaw, int projectorRatio)
{
    // Tx fed integers into `expr $i1 ...`.
    const auto a = std::round (aspectRaw);
    const auto curve = [] (float i) { return i * i * 6.35e-5f + i * 3.75e-3f + 0.5f; };

    const bool wide = projectorRatio == 0;
    const float widthThreshold  = wide ? 0.562f : 0.74f;
    const float widthClamp      = wide ? 7.0f   : 5.33f;
    const float heightThreshold = wide ? 1.778f : 1.4f;
    const float heightClamp     = wide ? 7.0f   : 5.33f;

    const float fw = curve (127.0f - a);
    const float fh = curve (a);

    return { fw < widthThreshold ? widthClamp : 4.0f / fw,
             fh < heightThreshold ? 4.0f : heightClamp / fh };
}

Geometry GeometryMapper::map (const ParameterStore::Snapshot& params, const CompatFlags& compat)
{
    const auto aspect = params[ParamId::aspect];
    const auto projector = static_cast<int> (params[ParamId::projectorRatio]);

    const bool aspectMoved = aspect != lastAspect;
    const bool projectorMoved = projector != lastProjector;

    if (lastProjector < 0 || aspectMoved || (projectorMoved && compat.projectorRatioRecompute))
        size = aspectToSize (aspect, projector);

    lastAspect = aspect;
    lastProjector = projector;

    Geometry g;
    g.tx = translateXY (params[ParamId::translateX]);
    g.ty = translateXY (params[ParamId::translateY]);
    g.tz = translateZ (params[ParamId::translateZ]);
    g.rx = rotation (params[ParamId::rotateX]);
    g.ry = rotation (params[ParamId::rotateY]);
    g.rz = rotation (params[ParamId::rotateZ]);
    g.width = size.width;
    g.height = size.height;
    return g;
}

} // namespace pixo::txmap
