#pragma once

#include <array>

namespace pixo
{

/** GEM's coordinate system, made explicit (PD114-ANALYSIS §2.1).

    - Eye at the origin looking down −Z (`gemwin view 0 0 0`).
    - Frustum `perspec -1 1 -1 1 1 20`, with x widened by the target's
      aspect ratio. So at z = −4 the view spans ±4 vertically and
      ±4·aspect horizontally.
    - [square s] and [rectangle w h] take half sizes.
    - [translateXYZ] then [rotateXYZ] (X, then Y, then Z) build the model.

    Column-major 4×4 matrices, OpenGL conventions, as sokol-shdc expects. */
struct Mat4
{
    std::array<float, 16> m {};     // m[column * 4 + row]

    static Mat4 identity();
    Mat4 operator* (const Mat4& other) const;
};

struct Vec4
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

Vec4 operator* (const Mat4& matrix, const Vec4& v);

Mat4 frustum (float left, float right, float bottom, float top, float nearZ, float farZ, bool zeroToOneDepth);
Mat4 translation (float x, float y, float z);
Mat4 rotationX (float degrees);
Mat4 rotationY (float degrees);
Mat4 rotationZ (float degrees);

/** GEM's projection for a render target of the given aspect (width/height).
    D3D11 and Metal clip depth to [0, 1], OpenGL to [−1, 1]. */
Mat4 gemProjection (float aspect, bool zeroToOneDepth);

/** [translateXYZ tx ty tz] → [rotateXYZ rx ry rz], angles in degrees. */
Mat4 gemModel (float tx, float ty, float tz, float rx, float ry, float rz);

} // namespace pixo
