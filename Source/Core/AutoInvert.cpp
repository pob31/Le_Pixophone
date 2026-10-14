#include "Core/AutoInvert.h"

#include <algorithm>
#include <cstdlib>

namespace pixo
{

float MotionMeter::measure (const std::uint8_t* bgra, int width, int height, int strideBytes, float threshold)
{
    if (bgra == nullptr || width <= 0 || height <= 0)
        return 0.0f;

    const auto pixels = static_cast<std::size_t> (width) * static_cast<std::size_t> (height);

    if (width != previousWidth || height != previousHeight)
    {
        previousGrey.assign (pixels, 255);     // pix_movement: setWhite() on a size change
        previousWidth = width;
        previousHeight = height;
    }

    const auto limit = static_cast<int> (std::clamp (threshold, 0.0f, 1.0f) * 255.0f);
    auto* previous = previousGrey.data();
    std::size_t moved = 0;

    for (int y = 0; y < height; ++y)
    {
        const auto* row = bgra + static_cast<std::ptrdiff_t> (y) * strideBytes;

        for (int x = 0; x < width; ++x, row += 4, ++previous)
        {
            // pix_movement's integer luma: (R·79 + G·156 + B·21) >> 8.
            const int grey = (row[2] * 79 + row[1] * 156 + row[0] * 21) >> 8;

            if (std::abs (grey - *previous) > limit)
                ++moved;

            *previous = static_cast<std::uint8_t> (grey);
        }
    }

    return static_cast<float> (static_cast<double> (moved) / static_cast<double> (pixels));
}

void AutoInvert::update (int invertMode, float sensitivityRaw)
{
    if (invertMode == 2 && previousMode != 2)
    {
        // Pd: [sel 2] → reset the sum, and recall the inversion of the last
        // manual mode (its [f] starts at 0, which reads as P).
        inverted = lastManualMode != 0;
        sum = 0.0f;
    }
    else if (invertMode == 0 || invertMode == 1)
    {
        lastManualMode = invertMode;
    }

    previousMode = invertMode;

    const auto newThreshold = sensitivityRaw / 8.0f;

    if (newThreshold != threshold)
    {
        threshold = newThreshold;
        check();
    }
}

void AutoInvert::addMotion (float motion)
{
    sum += motion;
    check();
}

float AutoInvert::getProgress() const noexcept
{
    return threshold > 0.0f ? sum / threshold : 0.0f;   // Pd's [/] gives 0 for a zero divisor
}

void AutoInvert::check()
{
    if (sum > threshold)
    {
        inverted = ! inverted;
        sum = 0.0f;
    }
}

} // namespace pixo
