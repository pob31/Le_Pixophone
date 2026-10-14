#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace pixo
{

/** Parameters in the controller domain of the original Tx patch (D8): the
    raw values its widgets held — mostly 0–127, the 14-bit NRPN ranges for
    geometry. Renderer-domain values are derived from these by TxMapping,
    exactly as Tx derived the messages it sent to Rx (PD114-ANALYSIS §3.1). */
enum class ParamId : int
{
    // MIX (§2.4, §3.1)
    mixX,
    mixY,
    mixLevel,       // Tx `mxlvl`
    masterLevel,    // Tx `mstrlvl`
    mixFocus,
    mixBias,
    mixBlurMode,

    // STRIP (§2.5, §3.1)
    stripPosition,      // 0–800 (14-bit NRPN 223), posH = (h − 75.59) / 8.15748
    stripZoom,          // 0–127, posZ = −4 − z/127·8
    stripLevel,
    stripFocus,
    stripBias,
    stripBlurMode,

    // GEOMETRY (§2.8, §3.4)
    translateX,
    translateY,
    translateZ,
    rotateX,
    rotateY,
    rotateZ,
    aspect,
    projectorRatio, // 0 = 16:9 projector, 1 = 4:3 projector

    // CAMERA (§2.7, §3.1). Exposure and gain are played live (D17), 0–127
    // across the device's range.
    cameraOn,
    cameraLevel,        // Tx `wclvl`
    exposure,
    gain,
    exposureAuto,       // the automatics are not in the original (the PS3 Eye
    gainAuto,           // ran manual); webcams start in auto
    whiteBalanceAuto,

    // Camera chain (§2.7): mask colour, levels, blur, inversion.
    maskColorM,         // cam col = M·R/127², M·G/127², M·B/127²
    maskColorR,
    maskColorG,
    maskColorB,
    maskBypass,         // 1: no mask colour (pix_color off); also flips the inversion (Q12)
    levelInLow,         // cam mlvl inLo inHi outLo outHi, each /127
    levelInHigh,
    levelOutLow,
    levelOutHigh,
    cameraBlur,         // cam blur = v/133 (pix_motionblur)
    invertMode,         // 0 N, 1 P (inverted), 2 A (auto: flips with motion, §3.3)
    invertSensitivity,  // invsens: auto flips once the motion sum exceeds v/8
    motionThreshold,    // cai threshold = v/127: grey change that counts as motion
    cameraTickHz,       // D14 legacy tick for per-frame camera effects

    // Output utilities (not in the original)
    testPattern,    // 0/1: show the alignment test pattern instead of the picture

    count
};

constexpr std::size_t numParams = static_cast<std::size_t> (ParamId::count);

struct ParamDef
{
    ParamId id;
    const char* key;        // stable identifier, for settings and future presets/OSC
    const char* label;
    const char* group;
    float minValue;
    float maxValue;
    float defaultValue;
    bool integer;
};

const ParamDef& paramDef (ParamId id);

/** Quirks reproduced from patch114 (D14). Each flag defaults to false = the
    original behaviour; true opts into the fix. */
struct CompatFlags
{
    bool projectorRatioRecompute = false;   // Q10: Tx only recomputed width/height when the aspect slider moved
    bool realInvertedDepth = false;         // Q4: tex unit 3 was an uninverted copy of unit 2
};

/** Last-value parameter store shared by the control and render threads
    (D15): any thread sets, the render thread reads a snapshot once per frame. */
class ParameterStore
{
public:
    ParameterStore();

    float get (ParamId id) const noexcept;

    /** Clamps to the parameter's range (and rounds integer parameters). */
    void set (ParamId id, float value) noexcept;

    void resetToDefaults() noexcept;

    struct Snapshot
    {
        std::array<float, numParams> values {};

        float operator[] (ParamId id) const noexcept { return values[static_cast<std::size_t> (id)]; }
    };

    Snapshot snapshot() const noexcept;

    CompatFlags compat;     // set before the render thread starts

private:
    std::array<std::atomic<float>, numParams> values;
};

} // namespace pixo
