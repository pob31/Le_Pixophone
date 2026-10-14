// Unit tests for Source/Core: the Tx formulas, GEM space, Pd line ramps,
// preset parsing and the parameter store, checked against the numbers in
// Documentation/PD114-ANALYSIS.md. Plain C++, no framework: run the
// executable (or `ctest`), exit code 0 = all passed.

#include "Core/GemSpace.h"
#include "Core/LineRamp.h"
#include "Core/Params.h"
#include "Core/PresetFile.h"
#include "Core/TxMapping.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{

int failures = 0;
int checks = 0;

void check (bool condition, const char* what, int line)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("FAILED (line %d): %s\n", line, what);
    }
}

bool near (double a, double b, double tolerance = 1e-4)
{
    return std::abs (a - b) <= tolerance;
}

#define CHECK(cond)              check ((cond), #cond, __LINE__)
#define CHECK_NEAR(a, b, tol)    check (near ((a), (b), (tol)), #a " ~ " #b, __LINE__)

using namespace pixo;

void testMix()
{
    CHECK_NEAR (txmap::mixCurve (0.0f), 0.0, 1e-6);
    CHECK_NEAR (txmap::mixCurve (1.0f), 1.0, 1e-6);
    CHECK_NEAR (txmap::mixCurve (0.25f), (0.25 + 1.0) / 3.0, 1e-6);

    // Corners of the XY grid select one image each.
    const auto bottomLeft = txmap::mixWeights (0.0f, 0.0f);
    CHECK_NEAR (bottomLeft[1], 1.0, 1e-6);
    CHECK_NEAR (bottomLeft[0] + bottomLeft[2] + bottomLeft[3], 0.0, 1e-6);

    CHECK_NEAR (txmap::mixWeights (0.0f, 127.0f)[0], 1.0, 1e-6);
    CHECK_NEAR (txmap::mixWeights (127.0f, 127.0f)[2], 1.0, 1e-6);
    CHECK_NEAR (txmap::mixWeights (127.0f, 0.0f)[3], 1.0, 1e-6);

    // Centre: every bilinear weight is 1/4, through the curve.
    for (auto w : txmap::mixWeights (63.5f, 63.5f))
        CHECK_NEAR (w, (0.25 + 1.0) / 3.0, 1e-5);

    CHECK_NEAR (txmap::mixLevel (127.0f, 127.0f), 1.0, 1e-6);
    CHECK_NEAR (txmap::mixLevel (127.0f, 0.0f), 0.0, 1e-6);
    CHECK_NEAR (txmap::mixLevel (63.5f, 127.0f), 0.5, 1e-6);
}

void testGeometryFormulas()
{
    CHECK_NEAR (txmap::translateXY (200.0f), 0.0, 1e-6);
    CHECK_NEAR (txmap::translateXY (0.0f), -12.0, 1e-6);
    CHECK_NEAR (txmap::translateXY (400.0f), 12.0, 1e-6);
    CHECK_NEAR (txmap::translateZ (85.0f), -3.875, 1e-6);   // the default (§3.1)
    CHECK_NEAR (txmap::translateZ (0.0f), -0.9, 1e-6);
    CHECK_NEAR (txmap::rotation (180.0f), 0.0, 1e-6);
    CHECK_NEAR (txmap::blurBias (127.0f), 0.1, 1e-6);
}

void testCameraMapping()
{
    CHECK_NEAR (txmap::cameraLevel (127.0f, 127.0f, 1.0f), 1.0, 1e-6);
    CHECK_NEAR (txmap::cameraLevel (127.0f, 31.75f, 1.0f), 0.5, 1e-6);   // √ master
    CHECK_NEAR (txmap::cameraLevel (127.0f, 127.0f, 0.0f), 0.0, 1e-6);   // camera off

    // Windows exposure is in log2 seconds, e.g. -13..-1 step 1.
    CHECK (txmap::toDeviceRange (0.0f, -13, -1, 1) == -13);
    CHECK (txmap::toDeviceRange (127.0f, -13, -1, 1) == -1);
    CHECK (txmap::toDeviceRange (63.5f, -13, -1, 1) == -7);
    CHECK (txmap::toDeviceRange (64.0f, 0, 255, 5) % 5 == 0);
}

void testAspect()
{
    struct Preset { const char* name; float slider; double ratio; };

    // §3.4 presets on a 16:9 projector. Tx's curve only approximates the
    // nominal ratios (3:2 comes out at 1.544, ≈ 3 % off): that is the
    // original's behaviour, so the tolerance is 4 %.
    const Preset presets[] = { { "2:1", 127, 2.0 }, { "16:9", 113, 16.0 / 9.0 }, { "3:2", 100, 1.5 },
                               { "4:3", 89, 4.0 / 3.0 }, { "5:4", 80, 1.25 }, { "1:1", 64, 1.0 },
                               { "4:5", 45, 0.8 }, { "3:4", 40, 0.75 }, { "2:3", 30, 2.0 / 3.0 }, { "1:2", 0, 0.5 } };

    for (const auto& p : presets)
    {
        const auto size = txmap::aspectToSize (p.slider, 0);
        const auto ratio = size.width / size.height;
        check (std::abs (ratio / p.ratio - 1.0) < 0.04, p.name, __LINE__);
        CHECK (size.width <= 4.0f * 16.0f / 9.0f && size.height <= 4.0f + 1e-4f);  // fits the 16:9 view (±7.11 × ±4)
    }

    // The 16:9 width clamp matches GEM's aspect-corrected view: 4 × 16/9 ≈ 7.1.
    CHECK_NEAR (txmap::aspectToSize (127.0f, 0).width, 7.0, 1e-6);

    // 4:3 projector: 4:3 fills the view (±5.33 × ±4).
    const auto fourThree = txmap::aspectToSize (89.0f, 1);
    CHECK_NEAR (fourThree.width, 5.33, 1e-6);
    CHECK_NEAR (fourThree.height, 4.0, 1e-6);
}

void testGeometryMapperQuirk()
{
    ParameterStore store;
    store.set (ParamId::aspect, 113.0f);    // 16:9 on a 16:9 projector
    store.set (ParamId::projectorRatio, 0.0f);

    txmap::GeometryMapper mapper;
    CompatFlags original;
    const auto before = mapper.map (store.snapshot(), original);

    // Q10 quirk: switching the projector ratio does not recompute the size...
    store.set (ParamId::projectorRatio, 1.0f);
    CHECK_NEAR (mapper.map (store.snapshot(), original).width, before.width, 1e-6);

    // ...until the aspect slider moves.
    store.set (ParamId::aspect, 112.0f);
    CHECK (std::abs (mapper.map (store.snapshot(), original).width - txmap::aspectToSize (112.0f, 1).width) < 1e-6f);

    // The compat flag recomputes on the ratio change itself.
    txmap::GeometryMapper fixedMapper;
    CompatFlags fixed;
    fixed.projectorRatioRecompute = true;
    store.set (ParamId::projectorRatio, 0.0f);
    fixedMapper.map (store.snapshot(), fixed);
    store.set (ParamId::projectorRatio, 1.0f);
    CHECK_NEAR (fixedMapper.map (store.snapshot(), fixed).width, txmap::aspectToSize (112.0f, 1).width, 1e-6);
}

void testGemSpace()
{
    // The 4:3 rectangle at z = −4 fills a 4:3 target exactly (§3.4).
    for (bool zeroToOne : { true, false })
    {
        const auto projection = gemProjection (4.0f / 3.0f, zeroToOne);
        const auto corner = projection * Vec4 { 4.0f * 4.0f / 3.0f, 4.0f, -4.0f, 1.0f };
        CHECK_NEAR (corner.x / corner.w, 1.0, 1e-5);
        CHECK_NEAR (corner.y / corner.w, 1.0, 1e-5);

        const auto nearPoint = projection * Vec4 { 0.0f, 0.0f, -1.0f, 1.0f };
        const auto farPoint = projection * Vec4 { 0.0f, 0.0f, -20.0f, 1.0f };
        CHECK_NEAR (nearPoint.z / nearPoint.w, zeroToOne ? 0.0 : -1.0, 1e-5);
        CHECK_NEAR (farPoint.z / farPoint.w, 1.0, 1e-5);
    }

    // rotateXYZ follows translateXYZ: rotate about the object's own origin.
    const auto p = gemModel (0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 90.0f) * Vec4 { 1.0f, 0.0f, 0.0f, 1.0f };
    CHECK_NEAR (p.x, 0.0, 1e-5);
    CHECK_NEAR (p.y, 1.0, 1e-5);
    CHECK_NEAR (p.z, -4.0, 1e-5);

    // GEM order is X, then Y, then Z: M = Rx · Ry · Rz.
    const auto q = gemModel (0, 0, 0, 90.0f, 90.0f, 0) * Vec4 { 0.0f, 0.0f, 1.0f, 1.0f };
    CHECK_NEAR (q.x, 1.0, 1e-5);    // Ry(90) takes +Z to +X, Rx(90) leaves X alone
}

void testLineRamp()
{
    LineRamp ramp (300.0);
    CHECK_NEAR (ramp.update (5.0f, 0.0), 5.0, 1e-6);     // first target: immediate
    ramp.setTarget (15.0f, 1000.0);
    CHECK_NEAR (ramp.valueAt (1150.0), 10.0, 1e-4);      // halfway
    CHECK_NEAR (ramp.valueAt (1300.0), 15.0, 1e-6);
    ramp.setTarget (15.0f, 2000.0);                       // same target: no restart
    CHECK_NEAR (ramp.valueAt (2000.0), 15.0, 1e-6);
    ramp.setTarget (5.0f, 2000.0);                        // retarget starts from the current value
    CHECK_NEAR (ramp.valueAt (2150.0), 10.0, 1e-4);
}

void testPresetFiles()
{
    CHECK (presetEntryFileName ("F:/Le_Pixophone/images1024/04112005_m2.jpg\r") == "04112005_m2.jpg");
    CHECK (presetEntryFileName ("F:\\Tchernobyl\\images_strip\\sdt_17032008_2_0.jpg") == "sdt_17032008_2_0.jpg");
    CHECK (presetEntryFileName ("04112005_m2") == "04112005_m2.jpg");    // v114 bare name
    CHECK (presetEntryFileName ("   \r") == "");

    const auto names = parsePresetFile ("F:/a/one.jpg\r\n\r\ntwo\nF:/b/three.jpg");
    CHECK (names.size() == 3);
    CHECK (names.size() == 3 && names[1] == "two.jpg");
    CHECK (bankSlotCount (ImageBank::pad) == 12);
}

void testParameterStore()
{
    ParameterStore store;
    CHECK_NEAR (store.get (ParamId::translateZ), 85.0, 1e-6);
    store.set (ParamId::mixX, 300.0f);
    CHECK_NEAR (store.get (ParamId::mixX), 127.0, 1e-6);   // clamped
    store.set (ParamId::mixY, 10.4f);
    CHECK_NEAR (store.get (ParamId::mixY), 10.0, 1e-6);    // integer parameter
    CHECK_NEAR (store.snapshot()[ParamId::mixY], 10.0, 1e-6);
}

} // namespace

int main()
{
    testMix();
    testGeometryFormulas();
    testCameraMapping();
    testAspect();
    testGeometryMapperQuirk();
    testGemSpace();
    testLineRamp();
    testPresetFiles();
    testParameterStore();

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
