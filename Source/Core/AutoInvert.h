#pragma once

#include <cstdint>
#include <vector>

namespace pixo
{

/** Rx's CamAutoInv measurement (§2.7): `pix_movement` → `pix_mean_color`.
    Each pixel's grey level is compared with the previous measured frame; a
    pixel "moved" when the difference exceeds the threshold. The result is
    the share of moved pixels, 0–1 (the mean of pix_movement's alpha).

    Like pix_movement, the previous-frame buffer starts white and survives
    while the analysis is gated off, so the first frame after a pause is
    compared with stale content. */
class MotionMeter
{
public:
    /** `bgra`: 8-bit BGRA rows of `width` pixels, `strideBytes` apart.
        `threshold`: Tx's `cai threshold`, v/127 (0–1, × 255 as in GEM). */
    float measure (const std::uint8_t* bgra, int width, int height, int strideBytes, float threshold);

private:
    std::vector<std::uint8_t> previousGrey;
    int previousWidth = 0;
    int previousHeight = 0;
};

/** Tx's CAMAUTOINV integrator (§3.3). In invert mode 2 (A) every motion
    measurement is added to a sum; once the sum exceeds invsens/8 the
    inversion flips and the sum starts again from zero.

    Entering mode 2 resets the sum and starts from the inversion the last
    manual mode had (0 N → 0, 1 P → 1; P before any manual mode, as in Pd).
    A sensitivity change re-checks the sum at once, so lowering it below the
    sum flips immediately. */
class AutoInvert
{
public:
    /** Every frame: the invert mode (0 N, 1 P, 2 A) and the sensitivity (0–127). */
    void update (int invertMode, float sensitivityRaw);

    /** One measurement from MotionMeter. Only call it in mode 2. */
    void addMotion (float motion);

    /** The auto state, before the mask-bypass XOR (txmap::cameraInverted). */
    bool isInverted() const noexcept { return inverted; }

    /** `invint`: sum / (invsens/8), the GUI's progress towards the next flip. */
    float getProgress() const noexcept;

private:
    void check();

    float sum = 0.0f;
    float threshold = 8.0f;
    bool inverted = false;
    int previousMode = -1;
    int lastManualMode = 1;
};

} // namespace pixo
