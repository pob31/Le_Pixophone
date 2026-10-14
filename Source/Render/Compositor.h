#pragma once

#include <array>
#include <string>

#include "sokol_gfx.h"

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

    Render thread only, between sg_setup() and sg_shutdown(). P1 draws the
    MIX layer; later phases add their layers in the §2.2 order. */
class Compositor
{
public:
    bool initialise (std::string& error);

    /** Replaces an image-bank slot (nullptr clears it). */
    void setImage (ImageBank bank, int slot, const DecodedImage* image);

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

    void renderMixBuffer (const std::array<float, 4>& weights, float level);
    void renderComposite (const txmap::Geometry& geometry, bool mixVisible, float focus, float bias);
    void renderTestPattern (double nowMs);

    bool zeroToOneDepth = true;

    sg_sampler linearClamp {};
    sg_pipeline quadPipeline {};        // offscreen, alpha-blended (GEM [alpha])
    sg_pipeline bokehPipeline {};       // offscreen, opaque
    sg_pipeline testPatternPipeline {}; // offscreen
    sg_pipeline blitPipeline {};        // swapchain

    Texture transparentBlack;           // an empty pix_buffer slot
    Texture opaqueBlack;                // stands in for the camera "depth" until P2
    std::array<Texture, 5> mixSlots;    // mixbuffer: 4 slots + the swap scratch slot

    RenderTarget mixTarget;             // MIXOUTPUT's gemframebuffer, 1024×1024
    RenderTarget composite;             // the finished picture, output-sized

    txmap::GeometryMapper geometryMapper;
    std::array<LineRamp, 8> geometryRamps { LineRamp (300), LineRamp (300), LineRamp (300), LineRamp (300),
                                            LineRamp (300), LineRamp (300), LineRamp (300), LineRamp (300) };
    LineRamp mixLevelRamp { 100 };
};

} // namespace pixo
