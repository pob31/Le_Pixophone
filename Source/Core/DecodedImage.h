#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pixo
{

/** A decoded image ready for upload: 8-bit RGBA, rows top to bottom,
    straight (not premultiplied) alpha. Produced by MediaLibrary on a worker
    thread, consumed by the render thread. */
struct DecodedImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string name;
};

} // namespace pixo
