#include "Core/GemSpace.h"

#include <cmath>

namespace pixo
{
namespace
{

constexpr float degreesToRadians = 3.14159265358979f / 180.0f;

} // namespace

Mat4 Mat4::identity()
{
    Mat4 result;
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::operator* (const Mat4& other) const
{
    Mat4 result;

    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;

            for (int k = 0; k < 4; ++k)
                sum += m[k * 4 + row] * other.m[column * 4 + k];

            result.m[column * 4 + row] = sum;
        }

    return result;
}

Vec4 operator* (const Mat4& a, const Vec4& v)
{
    const auto& m = a.m;
    return { m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
             m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
             m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
             m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w };
}

Mat4 frustum (float l, float r, float b, float t, float n, float f, bool zeroToOneDepth)
{
    Mat4 result;
    result.m[0] = 2.0f * n / (r - l);
    result.m[5] = 2.0f * n / (t - b);
    result.m[8] = (r + l) / (r - l);
    result.m[9] = (t + b) / (t - b);
    result.m[11] = -1.0f;

    if (zeroToOneDepth)
    {
        result.m[10] = f / (n - f);
        result.m[14] = n * f / (n - f);
    }
    else
    {
        result.m[10] = (f + n) / (n - f);
        result.m[14] = 2.0f * f * n / (n - f);
    }

    return result;
}

Mat4 translation (float x, float y, float z)
{
    auto result = Mat4::identity();
    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;
    return result;
}

Mat4 rotationX (float degrees)
{
    const auto c = std::cos (degrees * degreesToRadians);
    const auto s = std::sin (degrees * degreesToRadians);
    auto result = Mat4::identity();
    result.m[5] = c;   result.m[9] = -s;
    result.m[6] = s;   result.m[10] = c;
    return result;
}

Mat4 rotationY (float degrees)
{
    const auto c = std::cos (degrees * degreesToRadians);
    const auto s = std::sin (degrees * degreesToRadians);
    auto result = Mat4::identity();
    result.m[0] = c;   result.m[8] = s;
    result.m[2] = -s;  result.m[10] = c;
    return result;
}

Mat4 rotationZ (float degrees)
{
    const auto c = std::cos (degrees * degreesToRadians);
    const auto s = std::sin (degrees * degreesToRadians);
    auto result = Mat4::identity();
    result.m[0] = c;   result.m[4] = -s;
    result.m[1] = s;   result.m[5] = c;
    return result;
}

Mat4 gemProjection (float aspect, bool zeroToOneDepth)
{
    return frustum (-aspect, aspect, -1.0f, 1.0f, 1.0f, 20.0f, zeroToOneDepth);
}

Mat4 gemModel (float tx, float ty, float tz, float rx, float ry, float rz)
{
    return translation (tx, ty, tz) * rotationX (rx) * rotationY (ry) * rotationZ (rz);
}

} // namespace pixo
