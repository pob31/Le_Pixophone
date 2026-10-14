#include "Camera/CameraSource.h"

#include <cstdio>

namespace pixo
{

std::string CameraFormat::describe() const
{
    char text[96];
    std::snprintf (text, sizeof (text), "%d x %d @ %.4g fps %s", width, height, fps, encoding.c_str());
    return text;
}

} // namespace pixo
