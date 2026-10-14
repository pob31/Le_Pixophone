#pragma once

#include <memory>
#include <vector>

#include "Core/DecodedImage.h"

namespace pixo
{

/** Bilinear resize of an RGBA image. */
DecodedImage resizeImage (const DecodedImage& source, int width, int height);

/** Joins images left to right into one (D25: STRIP parts into a panorama).
    Parts are scaled to the first part's height; if the result is wider than
    `maxWidth` (the GPU's texture limit), the whole is scaled down to fit. */
DecodedImage joinHorizontally (const std::vector<std::shared_ptr<const DecodedImage>>& parts, int maxWidth);

/** The texture width every target backend supports (D3D11 feature level 11,
    Metal on Apple silicon, desktop GL 4.x): 16384 px. */
constexpr int portableMaxTextureSize = 16384;

} // namespace pixo
