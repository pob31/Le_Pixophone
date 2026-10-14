#include "Core/ImageOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pixo
{

DecodedImage resizeImage (const DecodedImage& source, int width, int height)
{
    DecodedImage result;
    result.name = source.name;
    result.width = std::max (width, 1);
    result.height = std::max (height, 1);
    result.rgba.resize (static_cast<std::size_t> (result.width) * static_cast<std::size_t> (result.height) * 4);

    if (source.width <= 0 || source.height <= 0)
        return result;

    if (source.width == result.width && source.height == result.height)
    {
        result.rgba = source.rgba;
        return result;
    }

    const auto scaleX = static_cast<float> (source.width) / static_cast<float> (result.width);
    const auto scaleY = static_cast<float> (source.height) / static_cast<float> (result.height);

    const auto texel = [&] (int x, int y, int channel)
    {
        x = std::clamp (x, 0, source.width - 1);
        y = std::clamp (y, 0, source.height - 1);
        return static_cast<float> (source.rgba[(static_cast<std::size_t> (y) * static_cast<std::size_t> (source.width)
                                                + static_cast<std::size_t> (x)) * 4 + static_cast<std::size_t> (channel)]);
    };

    auto* out = result.rgba.data();

    for (int y = 0; y < result.height; ++y)
    {
        const auto sy = (static_cast<float> (y) + 0.5f) * scaleY - 0.5f;
        const auto y0 = static_cast<int> (std::floor (sy));
        const auto fy = sy - static_cast<float> (y0);

        for (int x = 0; x < result.width; ++x)
        {
            const auto sx = (static_cast<float> (x) + 0.5f) * scaleX - 0.5f;
            const auto x0 = static_cast<int> (std::floor (sx));
            const auto fx = sx - static_cast<float> (x0);

            for (int c = 0; c < 4; ++c)
            {
                const auto top = texel (x0, y0, c) * (1.0f - fx) + texel (x0 + 1, y0, c) * fx;
                const auto bottom = texel (x0, y0 + 1, c) * (1.0f - fx) + texel (x0 + 1, y0 + 1, c) * fx;
                *out++ = static_cast<std::uint8_t> (std::lround (top * (1.0f - fy) + bottom * fy));
            }
        }
    }

    return result;
}

DecodedImage joinHorizontally (const std::vector<std::shared_ptr<const DecodedImage>>& parts, int maxWidth)
{
    DecodedImage joined;

    if (parts.empty() || parts.front() == nullptr || parts.front()->height <= 0)
        return joined;

    const int height = parts.front()->height;
    std::vector<DecodedImage> scaled;
    int width = 0;

    for (const auto& part : parts)
    {
        if (part == nullptr || part->width <= 0 || part->height <= 0)
            continue;

        const auto partWidth = static_cast<int> (std::lround (static_cast<double> (part->width) * height / part->height));
        scaled.push_back (part->height == height ? *part : resizeImage (*part, partWidth, height));
        width += scaled.back().width;
    }

    joined.width = width;
    joined.height = height;
    joined.name = parts.front()->name;
    joined.rgba.resize (static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * 4);

    int offset = 0;

    for (const auto& part : scaled)
    {
        const auto rowBytes = static_cast<std::size_t> (part.width) * 4;

        for (int y = 0; y < height; ++y)
            std::memcpy (joined.rgba.data() + (static_cast<std::size_t> (y) * static_cast<std::size_t> (width)
                                               + static_cast<std::size_t> (offset)) * 4,
                         part.rgba.data() + static_cast<std::size_t> (y) * rowBytes, rowBytes);

        offset += part.width;
    }

    if (width > maxWidth)
    {
        const auto fittedHeight = static_cast<int> (std::lround (static_cast<double> (height) * maxWidth / width));
        return resizeImage (joined, maxWidth, std::max (fittedHeight, 1));
    }

    return joined;
}

} // namespace pixo
