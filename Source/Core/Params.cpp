#include "Core/Params.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace pixo
{
namespace
{

// Defaults are the values the original sets at load / M_Reset (§3.3), except
// masterLevel: M_Reset blacks out (0), but a fresh start should show
// something while the port is being built.
constexpr ParamDef definitions[] = {
    { ParamId::mixX,           "mixX",           "Mix X",            "Mix",      0.0f, 127.0f,   0.0f, true },
    { ParamId::mixY,           "mixY",           "Mix Y",            "Mix",      0.0f, 127.0f,   0.0f, true },
    { ParamId::mixLevel,       "mixLevel",       "Mix level",        "Mix",      0.0f, 127.0f, 127.0f, true },
    { ParamId::masterLevel,    "masterLevel",    "Master level",     "Mix",      0.0f, 127.0f, 127.0f, true },
    { ParamId::mixFocus,       "mixFocus",       "Blur focus",       "Mix",      0.0f, 127.0f,  32.0f, true },
    { ParamId::mixBias,        "mixBias",        "Blur bias",        "Mix",      0.0f, 127.0f,  89.0f, true },
    { ParamId::mixBlurMode,    "mixBlurMode",    "Blur mode",        "Mix",      0.0f,   4.0f,   0.0f, true },
    { ParamId::translateX,     "translateX",     "Translate X",      "Geometry", 0.0f, 400.0f, 200.0f, true },
    { ParamId::translateY,     "translateY",     "Translate Y",      "Geometry", 0.0f, 400.0f, 200.0f, true },
    { ParamId::translateZ,     "translateZ",     "Translate Z",      "Geometry", 0.0f, 400.0f,  85.0f, true },
    { ParamId::rotateX,        "rotateX",        "Rotate X",         "Geometry", 0.0f, 360.0f, 180.0f, true },
    { ParamId::rotateY,        "rotateY",        "Rotate Y",         "Geometry", 0.0f, 360.0f, 180.0f, true },
    { ParamId::rotateZ,        "rotateZ",        "Rotate Z",         "Geometry", 0.0f, 360.0f, 180.0f, true },
    { ParamId::aspect,         "aspect",         "Aspect ratio",     "Geometry", 0.0f, 127.0f,  89.0f, true },
    { ParamId::projectorRatio, "projectorRatio", "Projector 4:3",    "Geometry", 0.0f,   1.0f,   0.0f, true },
    { ParamId::cameraOn,       "cameraOn",       "Camera on",        "Camera",   0.0f,   1.0f,   1.0f, true },
    { ParamId::cameraLevel,    "cameraLevel",    "Camera level",     "Camera",   0.0f, 127.0f, 127.0f, true },
    { ParamId::exposure,       "exposure",       "Exposure",         "Camera",   0.0f, 127.0f,  64.0f, true },
    { ParamId::gain,           "gain",           "Gain",             "Camera",   0.0f, 127.0f,  64.0f, true },
    { ParamId::exposureAuto,   "exposureAuto",   "Exposure auto",    "Camera",   0.0f,   1.0f,   1.0f, true },
    { ParamId::gainAuto,       "gainAuto",       "Gain auto",        "Camera",   0.0f,   1.0f,   1.0f, true },
    { ParamId::whiteBalanceAuto, "whiteBalanceAuto", "White bal. auto", "Camera", 0.0f,  1.0f,   1.0f, true },
    { ParamId::testPattern,    "testPattern",    "Test pattern",     "Output",   0.0f,   1.0f,   0.0f, true },
};

static_assert (std::size (definitions) == numParams, "one definition per ParamId, in order");

} // namespace

const ParamDef& paramDef (ParamId id)
{
    return definitions[static_cast<std::size_t> (id)];
}

ParameterStore::ParameterStore()
{
    resetToDefaults();
}

float ParameterStore::get (ParamId id) const noexcept
{
    return values[static_cast<std::size_t> (id)].load (std::memory_order_relaxed);
}

void ParameterStore::set (ParamId id, float value) noexcept
{
    const auto& def = paramDef (id);
    value = std::clamp (value, def.minValue, def.maxValue);

    if (def.integer)
        value = std::round (value);

    values[static_cast<std::size_t> (id)].store (value, std::memory_order_relaxed);
}

void ParameterStore::resetToDefaults() noexcept
{
    for (const auto& def : definitions)
        values[static_cast<std::size_t> (def.id)].store (def.defaultValue, std::memory_order_relaxed);
}

ParameterStore::Snapshot ParameterStore::snapshot() const noexcept
{
    Snapshot result;

    for (std::size_t i = 0; i < numParams; ++i)
        result.values[i] = values[i].load (std::memory_order_relaxed);

    return result;
}

} // namespace pixo
