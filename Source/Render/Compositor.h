#pragma once

#include <array>
#include <string>

#include "sokol_gfx.h"

#include "Camera/CameraSource.h"
#include "Core/DecodedImage.h"
#include "Core/LineRamp.h"
#include "Core/Params.h"
#include "Core/PresetFile.h"
#include "Core/TxMapping.h"

namespace pixo
{

/** Builds each frame of the picture (Rx's GEM chain, PD114-ANALYSIS §2) into
    an offscreen "composite" target, which every window then shows. So the
    preview always matches the projector exactly.

    Render thread only, between sg_setup() and sg_shutdown(). Layers so far,
    in the §2.2 order: MIX, CAMERA live. */
class Compositor
{
public:
    bool initialise (std::string& error);

    /** Replaces an image-bank slot (nullptr clears it). */
    void setImage (ImageBank bank, int slot, const DecodedImage* image);

    /** Uploads a new camera frame (through a staging buffer into a persistent
        texture). Call outside any pass, before renderFrame(). */
    void uploadCameraFrame (const CameraFrame& frame);

    /** Offscreen passes: the MIX buffer, then the composite at the output's
        size (its aspect drives GEM's projection). Call outside any pass. */
    void renderFrame (const ParameterStore::Snapshot& params, const CompatFlags& compat,
                      double nowMs, int width, int height);

    /** Draws the composite into the current swapchain pass, letterboxed to
        keep its aspect. */
    void drawToWindow (int windowWidth, int windowHeight);

private:
    struct Texture
    {
        sg_image image {};
        sg_view view {};
        int width = 0;
        int height = 0;
    };

    struct RenderTarget
    {
        sg_image image {};
        sg_view colorView {};
        sg_view textureView {};
        int width = 0;
        int height = 0;
    };

    static Texture makeTexture (const DecodedImage& image);
    static Texture makeSolidTexture (std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);
    static void destroy (Texture&);
    static void ensureTarget (RenderTarget&, int width, int height, const char* label);
    static void destroy (RenderTarget&);

    struct CompositeState
    {
        txmap::Geometry geometry;
        bool mixVisible = false;
        float focus = 0.0f;
        float bias = 0.0f;
        sg_view mixDepth {};            // the camera "depth" the MIX bokeh reads (§2.4)
        bool mixDepthInverted = false;  // compat realInvertedDepth on tex unit 3
        bool stripVisible = false;      // STRIPHEAD
        float stripAlpha = 0.0f;
        float cameraAlpha = 0.0f;       // CAMERAOUTPUT's colourRGB alpha (camput lvl)
        std::array<float, 3> maskColor {};
        bool maskOn = true;             // [pix_color] on, i.e. not bypassed
    };

    void renderMixBuffer (const std::array<float, 4>& weights, float level);
    void runCameraChains (const ParameterStore::Snapshot& params, bool inverted);
    void runChainPass (RenderTarget& target, const sg_view& previous, bool invert, float uvScale,
                       const ParameterStore::Snapshot& params);
    void renderStrip (float posH, float posZ, float focus, float bias, const sg_view& depth, bool depthInverted);
    void renderComposite (const CompositeState& state);
    void drawGemQuad (const sg_view& texture, const txmap::Geometry& g, float alpha);
    void drawMaskedQuad (const sg_view& texture, const txmap::Geometry& g, float alpha,
                         const std::array<float, 3>& maskColor, bool maskOn);
    void renderTestPattern (double nowMs);

    bool zeroToOneDepth = true;

    sg_sampler linearClamp {};
    sg_pipeline quadPipeline {};        // offscreen, alpha-blended (GEM [alpha])
    sg_pipeline bokehPipeline {};       // offscreen, opaque
    sg_pipeline testPatternPipeline {}; // offscreen
    sg_pipeline chainPipeline {};       // offscreen, camera-sized: invert, levels, motion blur
    sg_pipeline coloralphaPipeline {};  // offscreen, camera-sized
    sg_pipeline maskedPipeline {};      // offscreen, alpha-blended: [pix_color] quads
    sg_pipeline blitPipeline {};        // swapchain

    Texture transparentBlack;           // an empty pix_buffer slot
    Texture opaqueBlack;                // stands in for the camera "depth" until P2
    std::array<Texture, 5> mixSlots;    // mixbuffer: 4 slots + the swap scratch slot

    RenderTarget mixTarget;             // MIXOUTPUT's gemframebuffer, 1024×1024
    RenderTarget composite;             // the finished picture, output-sized

    // STRIP (§2.5, D25): one panorama texture; `stripScene` is its first
    // gemframebuffer (the panorama), `stripBokeh` the second (the mask).
    Texture stripTexture;
    RenderTarget stripScene;
    RenderTarget stripBokeh;
    int stripDepthUnit = 2;

    // Camera: frames arrive through a staging buffer into `cameraRaw`. On
    // each legacy tick (D14) the two chains run (§2.7): the depth chain into
    // `depthChain` (tex unit 2), the live chain into `liveChain` and then,
    // with its colour-alpha, into `cameraLive`. Each chain ping-pongs between
    // two targets for its motion-blur feedback.
    sg_buffer cameraStaging {};
    std::size_t cameraStagingSize = 0;
    Texture cameraRaw;
    std::array<RenderTarget, 2> depthChain;
    std::array<RenderTarget, 2> liveChain;
    int chainCurrent = 0;
    RenderTarget cameraLive;
    bool cameraHasPicture = false;
    double lastCameraTickMs = -1.0e9;
    int mixDepthUnit = 2;               // §2.4: the tex unit MIX's bokeh last selected

    txmap::GeometryMapper geometryMapper;
    std::array<LineRamp, 8> geometryRamps { LineRamp (300), LineRamp (300), LineRamp (300), LineRamp (300),
                                            LineRamp (300), LineRamp (300), LineRamp (300), LineRamp (300) };
    LineRamp mixLevelRamp { 100 };
    LineRamp cameraLevelRamp { 100 };   // CAMERAOUTPUT: lvl → $1 100 → line
    LineRamp stripPositionRamp { 500 }; // STRIP: posH → $1 500 → line
};

} // namespace pixo
