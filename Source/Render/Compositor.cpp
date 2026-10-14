#include "Render/Compositor.h"

#include <algorithm>
#include <cmath>

#include "Core/GemSpace.h"
#include "Render/shaders/gem.glsl.h"
#include "Render/shaders/testpattern.glsl.h"

namespace pixo
{
namespace
{

constexpr int mixBufferSize = 1024;             // MIXOUTPUT: gemframebuffer dimen 1024 1024
constexpr int stripBufferWidth = 1024;          // STRIP: gemframebuffer dimen 1024 768 (both)
constexpr int stripBufferHeight = 768;
constexpr float stripBokehHalfWidth = 5.33f;    // STRIP's bokeh: [rectangle 5.33 4]
constexpr float mixSquareHalfSize = 4.04f;      // [square 4.04]
constexpr float layerZ = -4.0f;                 // [translateXYZ 0 0 -4]

// The camera FBOs draw [square 4.04] into the ±4 view: they show the central
// 4 / 4.04 of the camera image.
constexpr float cameraFboUvScale = 4.0f / 4.04f;

// MIXOUTPUT draws its four images bottom to top in this slot order (§2.4).
constexpr int mixDrawOrder[] = { 2, 3, 0, 1 };

sg_pipeline makeOffscreenPipeline (const sg_shader_desc* shaderDesc, bool alphaBlend, const char* label)
{
    sg_pipeline_desc desc {};
    desc.shader = sg_make_shader (shaderDesc);
    desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    desc.sample_count = 1;
    desc.label = label;

    if (alphaBlend)
    {
        // GEM [alpha]: glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
        // which in GL applies to the alpha channel too.
        auto& blend = desc.colors[0].blend;
        blend.enabled = true;
        blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
        blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }

    return sg_make_pipeline (&desc);
}

vs_params_t quadParams (const Mat4& mvp, float halfWidth, float halfHeight)
{
    vs_params_t params {};
    std::copy (mvp.m.begin(), mvp.m.end(), params.mvp);
    params.half_size[0] = halfWidth;
    params.half_size[1] = halfHeight;
    return params;
}

sg_pass offscreenPass (const sg_view& colorView, float r, float g, float b, float a, const char* label)
{
    sg_pass pass {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { r, g, b, a };
    pass.attachments.colors[0] = colorView;
    pass.label = label;
    return pass;
}

} // namespace

//==============================================================================
bool Compositor::initialise (std::string& error)
{
    const auto backend = sg_query_backend();
    zeroToOneDepth = backend != SG_BACKEND_GLCORE && backend != SG_BACKEND_GLES3;

    sg_sampler_desc samplerDesc {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;     // GEM's wrap mode for the bokeh taps: to check in P2
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.label = "linear-clamp";
    linearClamp = sg_make_sampler (&samplerDesc);

    quadPipeline = makeOffscreenPipeline (gem_quad_shader_desc (backend), true, "gem-quad");
    bokehPipeline = makeOffscreenPipeline (gem_bokeh_shader_desc (backend), false, "gem-bokeh");

    {
        sg_pipeline_desc desc {};
        desc.shader = sg_make_shader (testpattern_shader_desc (backend));
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
        desc.label = "testpattern";
        testPatternPipeline = sg_make_pipeline (&desc);
    }

    for (auto* pipeline : { &chainPipeline, &coloralphaPipeline })
    {
        sg_pipeline_desc desc {};
        desc.shader = sg_make_shader (pipeline == &chainPipeline ? camera_chain_shader_desc (backend)
                                                                 : coloralpha_shader_desc (backend));
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
        desc.label = pipeline == &chainPipeline ? "camera-chain" : "coloralpha";
        *pipeline = sg_make_pipeline (&desc);
    }

    maskedPipeline = makeOffscreenPipeline (gem_masked_shader_desc (backend), true, "gem-masked");

    {
        sg_pipeline_desc desc {};      // swapchain: environment defaults (BGRA8, no depth)
        desc.shader = sg_make_shader (blit_shader_desc (backend));
        desc.label = "blit";
        blitPipeline = sg_make_pipeline (&desc);
    }

    for (auto pipeline : { quadPipeline, bokehPipeline, testPatternPipeline, chainPipeline,
                           coloralphaPipeline, maskedPipeline, blitPipeline })
    {
        if (sg_query_pipeline_state (pipeline) != SG_RESOURCESTATE_VALID)
        {
            error = "a compositor pipeline failed to compile (see log)";
            return false;
        }
    }

    transparentBlack = makeSolidTexture (0, 0, 0, 0);
    opaqueBlack = makeSolidTexture (0, 0, 0, 255);

    for (auto& slot : mixSlots)
        slot = Texture {};

    ensureTarget (mixTarget, mixBufferSize, mixBufferSize, "mix-buffer");
    ensureTarget (stripScene, stripBufferWidth, stripBufferHeight, "strip-scene");
    ensureTarget (stripBokeh, stripBufferWidth, stripBufferHeight, "strip-bokeh");
    return true;
}

//==============================================================================
Compositor::Texture Compositor::makeTexture (const DecodedImage& image)
{
    sg_image_desc desc {};
    desc.width = image.width;
    desc.height = image.height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = { image.rgba.data(), image.rgba.size() };
    desc.label = "bank-image";     // a static string: sokol may keep the pointer

    Texture texture;
    texture.image = sg_make_image (&desc);

    sg_view_desc viewDesc {};
    viewDesc.texture.image = texture.image;
    texture.view = sg_make_view (&viewDesc);
    texture.width = image.width;
    texture.height = image.height;
    return texture;
}

Compositor::Texture Compositor::makeSolidTexture (std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    DecodedImage pixel;
    pixel.width = pixel.height = 1;
    pixel.rgba = { r, g, b, a };
    return makeTexture (pixel);
}

void Compositor::destroy (Texture& texture)
{
    if (texture.view.id != SG_INVALID_ID)
        sg_destroy_view (texture.view);

    if (texture.image.id != SG_INVALID_ID)
        sg_destroy_image (texture.image);

    texture = Texture {};
}

void Compositor::ensureTarget (RenderTarget& target, int width, int height, const char* label)
{
    if (target.image.id != SG_INVALID_ID && target.width == width && target.height == height)
        return;

    destroy (target);

    sg_image_desc desc {};
    desc.usage.color_attachment = true;
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.sample_count = 1;
    desc.label = label;
    target.image = sg_make_image (&desc);

    sg_view_desc colorDesc {};
    colorDesc.color_attachment.image = target.image;
    target.colorView = sg_make_view (&colorDesc);

    sg_view_desc textureDesc {};
    textureDesc.texture.image = target.image;
    target.textureView = sg_make_view (&textureDesc);

    target.width = width;
    target.height = height;
}

void Compositor::destroy (RenderTarget& target)
{
    for (auto view : { target.colorView, target.textureView })
        if (view.id != SG_INVALID_ID)
            sg_destroy_view (view);

    if (target.image.id != SG_INVALID_ID)
        sg_destroy_image (target.image);

    target = RenderTarget {};
}

void Compositor::setImage (ImageBank bank, int slot, const DecodedImage* image)
{
    if (bank == ImageBank::strip && slot == 0)
    {
        destroy (stripTexture);

        if (image != nullptr && image->width > 0 && image->height > 0)
            stripTexture = makeTexture (*image);

        return;
    }

    if (bank != ImageBank::mix || slot < 0 || slot >= static_cast<int> (mixSlots.size()))
        return;     // the pad bank arrives with its layer (P3)

    destroy (mixSlots[static_cast<std::size_t> (slot)]);

    if (image != nullptr && image->width > 0 && image->height > 0)
        mixSlots[static_cast<std::size_t> (slot)] = makeTexture (*image);
}

void Compositor::uploadCameraFrame (const CameraFrame& frame)
{
    if (frame.width <= 0 || frame.height <= 0)
        return;

    const auto bytes = static_cast<std::size_t> (frame.width) * static_cast<std::size_t> (frame.height) * 4;

    if (bytes != cameraStagingSize || frame.width != cameraRaw.width || frame.height != cameraRaw.height)
    {
        if (cameraStaging.id != SG_INVALID_ID)
            sg_destroy_buffer (cameraStaging);

        destroy (cameraRaw);

        sg_buffer_desc stagingDesc {};
        stagingDesc.usage.staging_buffer = true;
        stagingDesc.usage.write_transient = true;
        stagingDesc.usage.copy_src = true;
        stagingDesc.size = bytes;
        stagingDesc.label = "camera-staging";
        cameraStaging = sg_make_buffer (&stagingDesc);
        cameraStagingSize = bytes;

        sg_image_desc imageDesc {};
        imageDesc.usage.copy_dst = true;
        imageDesc.width = frame.width;
        imageDesc.height = frame.height;
        imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;     // BGRA bytes: swizzled in camera_fs
        imageDesc.label = "camera-raw";
        cameraRaw.image = sg_make_image (&imageDesc);

        sg_view_desc viewDesc {};
        viewDesc.texture.image = cameraRaw.image;
        cameraRaw.view = sg_make_view (&viewDesc);
        cameraRaw.width = frame.width;
        cameraRaw.height = frame.height;

        for (auto& target : depthChain)
            ensureTarget (target, frame.width, frame.height, "camera-depth");

        for (auto& target : liveChain)
            ensureTarget (target, frame.width, frame.height, "camera-live-chain");

        ensureTarget (cameraLive, frame.width, frame.height, "camera-live");
        lastCameraTickMs = -1.0e9;  // run the chains on the next frame
    }

    sg_write_buffer_desc write {};
    write.src.data = { frame.bgra.data(), bytes };
    write.dst.buffer = cameraStaging;
    sg_write_buffer_transient (&write);

    sg_copy_buffer_to_image_desc copy {};
    copy.src.buffer = cameraStaging;
    copy.src.bytes_per_row = frame.width * 4;
    copy.src.bytes_per_slice = static_cast<int> (bytes);
    copy.dst.image = cameraRaw.image;
    copy.size = { frame.width, frame.height, 1 };
    sg_copy_buffer_to_image (&copy);
}

void Compositor::runChainPass (RenderTarget& target, const sg_view& previous, bool invert, float uvScale,
                               const ParameterStore::Snapshot& params)
{
    auto pass = offscreenPass (target.colorView, 0.0f, 0.0f, 0.0f, 1.0f, "camera-chain");
    sg_begin_pass (&pass);
    sg_apply_pipeline (chainPipeline);

    sg_bindings bindings {};
    bindings.views[VIEW_raw_tex] = cameraRaw.view;
    bindings.views[VIEW_previous_tex] = previous;
    bindings.samplers[SMP_smp] = linearClamp;
    sg_apply_bindings (&bindings);

    chain_params_t chainParams {};
    chainParams.levels[0] = params[ParamId::levelInLow] / 127.0f;
    chainParams.levels[1] = params[ParamId::levelInHigh] / 127.0f;
    chainParams.levels[2] = params[ParamId::levelOutLow] / 127.0f;
    chainParams.levels[3] = params[ParamId::levelOutHigh] / 127.0f;
    chainParams.options[0] = invert ? 1.0f : 0.0f;
    chainParams.options[1] = std::min (txmap::motionBlur (params[ParamId::cameraBlur]), 1.0f);
    chainParams.options[2] = uvScale;
    sg_apply_uniforms (UB_chain_params, SG_RANGE (chainParams));

    sg_draw (0, 3, 1);
    sg_end_pass();
}

void Compositor::runCameraChains (const ParameterStore::Snapshot& params, bool inverted)
{
    // Ping-pong: write the other target, reading the current one as the
    // motion blur's previous frame.
    const auto previous = static_cast<std::size_t> (chainCurrent);
    const auto next = static_cast<std::size_t> (1 - chainCurrent);

    runChainPass (depthChain[next], depthChain[previous].textureView, false, cameraFboUvScale, params);
    runChainPass (liveChain[next], liveChain[previous].textureView, inverted, 1.0f, params);
    chainCurrent = static_cast<int> (next);

    auto pass = offscreenPass (cameraLive.colorView, 0.0f, 0.0f, 0.0f, 0.0f, "coloralpha");
    sg_begin_pass (&pass);
    sg_apply_pipeline (coloralphaPipeline);

    sg_bindings bindings {};
    bindings.views[VIEW_tex] = liveChain[next].textureView;
    bindings.samplers[SMP_smp] = linearClamp;
    sg_apply_bindings (&bindings);

    sg_draw (0, 3, 1);
    sg_end_pass();

    cameraHasPicture = true;
}

//==============================================================================
void Compositor::renderFrame (const ParameterStore::Snapshot& params, const CompatFlags& compat,
                              double nowMs, int width, int height)
{
    ensureTarget (composite, std::max (width, 1), std::max (height, 1), "composite");

    if (params[ParamId::testPattern] > 0.5f)
    {
        renderTestPattern (nowMs);
        return;
    }

    // MIX (§2.4): weights straight from Tx, the level smoothed over 100 ms by Rx.
    const auto weights = txmap::mixWeights (params[ParamId::mixX], params[ParamId::mixY]);
    const auto level = mixLevelRamp.update (txmap::mixLevel (params[ParamId::mixLevel], params[ParamId::masterLevel]), nowMs);

    // MIXHEAD gating: the layer is skipped while its level is 0. The extra
    // 200 ms the original waited before closing is invisible here: at level
    // 0 the opaque MIX quad is black, the same as not drawing it.
    const bool mixVisible = level > 0.0f;

    if (mixVisible)
        renderMixBuffer (weights, level);

    // GEOMETRY (§2.8): Tx's mapping, then Rx's 300 ms ramps.
    const auto target = geometryMapper.map (params, compat);
    const float targets[] = { target.tx, target.ty, target.tz, target.rx, target.ry, target.rz, target.width, target.height };
    float smoothed[8];

    for (std::size_t i = 0; i < 8; ++i)
        smoothed[i] = geometryRamps[i].update (targets[i], nowMs);

    const txmap::Geometry geometry { smoothed[0], smoothed[1], smoothed[2], smoothed[3],
                                     smoothed[4], smoothed[5], smoothed[6], smoothed[7] };

    // blurMode 0 forces bias to 0 (§2.4). Until the camera exists (P2) the
    // depth texture is black, as with the camera off.
    const auto blurMode = static_cast<int> (params[ParamId::mixBlurMode]);

    // CAMERA (§2.7): the chains run on the legacy tick (D14), on the latest
    // camera image, whether or not a new one arrived, as GEM's frame loop did.
    const bool maskBypass = params[ParamId::maskBypass] > 0.5f;
    const bool camInv = txmap::cameraInverted (static_cast<int> (params[ParamId::invertMode]), maskBypass, false);
    const double tickMs = 1000.0 / std::max (static_cast<double> (params[ParamId::cameraTickHz]), 1.0);

    if (cameraRaw.image.id != SG_INVALID_ID && nowMs - lastCameraTickMs >= tickMs)
    {
        runCameraChains (params, camInv);

        // Keep the tick's phase; after a stall, resume from now.
        lastCameraTickMs = nowMs - lastCameraTickMs > 2.0 * tickMs ? nowMs : lastCameraTickMs + tickMs;
    }

    CompositeState state;
    state.geometry = geometry;
    state.mixVisible = mixVisible;
    state.focus = txmap::blurFocus (params[ParamId::mixFocus]);
    state.bias = blurMode == 0 ? 0.0f : txmap::blurBias (params[ParamId::mixBias]);

    // MIX's depth: tex unit 2 (the depth chain) or 3 (its "inverted" copy).
    mixDepthUnit = txmap::depthUnit (blurMode, camInv, mixDepthUnit, false);
    state.mixDepth = cameraHasPicture ? depthChain[static_cast<std::size_t> (chainCurrent)].textureView : opaqueBlack.view;
    state.mixDepthInverted = mixDepthUnit == 3 && compat.realInvertedDepth;

    // STRIP (§2.5): posH ramps over 500 ms; posZ and the level are immediate.
    const auto posH = stripPositionRamp.update (txmap::stripPosH (params[ParamId::stripPosition]), nowMs);
    state.stripAlpha = txmap::stripLevel (params[ParamId::stripLevel], params[ParamId::masterLevel]);
    state.stripVisible = state.stripAlpha > 0.0f;

    const auto stripBlurMode = static_cast<int> (params[ParamId::stripBlurMode]);
    stripDepthUnit = txmap::depthUnit (stripBlurMode, camInv, stripDepthUnit, true);

    if (state.stripVisible)
        renderStrip (posH, txmap::stripPosZ (params[ParamId::stripZoom]),
                     txmap::blurFocus (params[ParamId::stripFocus]),
                     stripBlurMode == 0 ? 0.0f : txmap::blurBias (params[ParamId::stripBias]),
                     state.mixDepth, stripDepthUnit == 3 && compat.realInvertedDepth);

    state.cameraAlpha = cameraLevelRamp.update (txmap::cameraLevel (params[ParamId::cameraLevel],
                                                                     params[ParamId::masterLevel],
                                                                     params[ParamId::cameraOn]), nowMs);
    state.maskColor = txmap::maskColor (params[ParamId::maskColorM], params[ParamId::maskColorR],
                                        params[ParamId::maskColorG], params[ParamId::maskColorB]);
    state.maskOn = ! maskBypass;

    renderComposite (state);
}

void Compositor::renderMixBuffer (const std::array<float, 4>& weights, float level)
{
    auto pass = offscreenPass (mixTarget.colorView, 0.0f, 0.0f, 0.0f, 0.0f, "mix-buffer");
    sg_begin_pass (&pass);
    sg_apply_pipeline (quadPipeline);

    const auto mvp = gemProjection (1.0f, zeroToOneDepth) * translation (0.0f, 0.0f, layerZ);
    const auto vsParams = quadParams (mvp, mixSquareHalfSize, mixSquareHalfSize);

    for (auto slot : mixDrawOrder)
    {
        const auto& texture = mixSlots[static_cast<std::size_t> (slot)];
        const auto alpha = weights[static_cast<std::size_t> (slot)] * level;

        if (texture.image.id == SG_INVALID_ID || alpha <= 0.0f)
            continue;   // an empty slot is transparent black: drawing it changes nothing

        sg_bindings bindings {};
        bindings.views[VIEW_tex] = texture.view;
        bindings.samplers[SMP_smp] = linearClamp;
        sg_apply_bindings (&bindings);

        fs_params_t fsParams {};
        fsParams.color[0] = fsParams.color[1] = fsParams.color[2] = 1.0f;
        fsParams.color[3] = alpha;

        sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
        sg_apply_uniforms (UB_fs_params, SG_RANGE (fsParams));
        sg_draw (0, 4, 1);
    }

    sg_end_pass();
}

void Compositor::renderStrip (float posH, float posZ, float focus, float bias, const sg_view& depth, bool depthInverted)
{
    const auto aspect = static_cast<float> (stripBufferWidth) / static_cast<float> (stripBufferHeight);
    const auto projection = gemProjection (aspect, zeroToOneDepth);

    // First buffer: the panorama, where the original chained four parts.
    {
        auto pass = offscreenPass (stripScene.colorView, 0.0f, 0.0f, 0.0f, 0.0f, "strip-scene");
        sg_begin_pass (&pass);

        if (stripTexture.image.id != SG_INVALID_ID)
        {
            const auto quad = txmap::stripQuad (posH);
            const auto vsParams = quadParams (projection * translation (quad.centreX, 0.0f, posZ),
                                              quad.halfWidth, quad.halfHeight);

            sg_apply_pipeline (quadPipeline);

            sg_bindings bindings {};
            bindings.views[VIEW_tex] = stripTexture.view;
            bindings.samplers[SMP_smp] = linearClamp;
            sg_apply_bindings (&bindings);

            fs_params_t fsParams {};
            fsParams.color[0] = fsParams.color[1] = fsParams.color[2] = fsParams.color[3] = 1.0f;

            sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
            sg_apply_uniforms (UB_fs_params, SG_RANGE (fsParams));
            sg_draw (0, 4, 1);
        }

        sg_end_pass();
    }

    // Second buffer: the bokeh mask over the panorama.
    {
        auto pass = offscreenPass (stripBokeh.colorView, 0.0f, 0.0f, 0.0f, 1.0f, "strip-bokeh");
        sg_begin_pass (&pass);
        sg_apply_pipeline (bokehPipeline);

        sg_bindings bindings {};
        bindings.views[VIEW_rendered_tex] = stripScene.textureView;
        bindings.views[VIEW_depth_tex] = depth;
        bindings.samplers[SMP_smp] = linearClamp;
        sg_apply_bindings (&bindings);

        const auto vsParams = quadParams (projection * translation (0.0f, 0.0f, layerZ), stripBokehHalfWidth, 4.0f);
        bokeh_params_t bokehParams {};
        bokehParams.focus_bias[0] = focus;
        bokehParams.focus_bias[1] = bias;
        bokehParams.focus_bias[2] = depthInverted ? 1.0f : 0.0f;

        sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
        sg_apply_uniforms (UB_bokeh_params, SG_RANGE (bokehParams));
        sg_draw (0, 4, 1);
        sg_end_pass();
    }
}

void Compositor::renderComposite (const CompositeState& state)
{
    const auto& g = state.geometry;
    auto pass = offscreenPass (composite.colorView, 0.0f, 0.0f, 0.0f, 1.0f, "composite");
    sg_begin_pass (&pass);

    if (state.mixVisible)
    {
        const auto aspect = static_cast<float> (composite.width) / static_cast<float> (composite.height);
        const auto mvp = gemProjection (aspect, zeroToOneDepth) * gemModel (g.tx, g.ty, g.tz, g.rx, g.ry, g.rz);

        sg_apply_pipeline (bokehPipeline);

        sg_bindings bindings {};
        bindings.views[VIEW_rendered_tex] = mixTarget.textureView;
        bindings.views[VIEW_depth_tex] = state.mixDepth;
        bindings.samplers[SMP_smp] = linearClamp;
        sg_apply_bindings (&bindings);

        const auto vsParams = quadParams (mvp, g.width, g.height);
        bokeh_params_t bokehParams {};
        bokehParams.focus_bias[0] = state.focus;
        bokehParams.focus_bias[1] = state.bias;
        bokehParams.focus_bias[2] = state.mixDepthInverted ? 1.0f : 0.0f;

        sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
        sg_apply_uniforms (UB_bokeh_params, SG_RANGE (bokehParams));
        sg_draw (0, 4, 1);
    }

    // STRIP: its mask buffer on the output quad, over MIX (§2.2).
    if (state.stripVisible)
        drawGemQuad (stripBokeh.textureView, g, state.stripAlpha);

    // CAMERAOUTPUT: the live camera on the output quad, not HEAD-gated (§2.2).
    if (cameraHasPicture && state.cameraAlpha > 0.0f)
        drawMaskedQuad (cameraLive.textureView, g, state.cameraAlpha, state.maskColor, state.maskOn);

    sg_end_pass();
}

void Compositor::drawMaskedQuad (const sg_view& texture, const txmap::Geometry& g, float alpha,
                                 const std::array<float, 3>& maskColor, bool maskOn)
{
    const auto aspect = static_cast<float> (composite.width) / static_cast<float> (composite.height);
    const auto mvp = gemProjection (aspect, zeroToOneDepth) * gemModel (g.tx, g.ty, g.tz, g.rx, g.ry, g.rz);

    sg_apply_pipeline (maskedPipeline);

    sg_bindings bindings {};
    bindings.views[VIEW_tex] = texture;
    bindings.samplers[SMP_smp] = linearClamp;
    sg_apply_bindings (&bindings);

    const auto vsParams = quadParams (mvp, g.width, g.height);
    mask_params_t maskParams {};
    maskParams.mask[0] = maskColor[0];
    maskParams.mask[1] = maskColor[1];
    maskParams.mask[2] = maskColor[2];
    maskParams.mask[3] = alpha;
    maskParams.enable[0] = maskOn ? 1.0f : 0.0f;

    sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
    sg_apply_uniforms (UB_mask_params, SG_RANGE (maskParams));
    sg_draw (0, 4, 1);
}

void Compositor::drawGemQuad (const sg_view& texture, const txmap::Geometry& g, float alpha)
{
    const auto aspect = static_cast<float> (composite.width) / static_cast<float> (composite.height);
    const auto mvp = gemProjection (aspect, zeroToOneDepth) * gemModel (g.tx, g.ty, g.tz, g.rx, g.ry, g.rz);

    sg_apply_pipeline (quadPipeline);

    sg_bindings bindings {};
    bindings.views[VIEW_tex] = texture;
    bindings.samplers[SMP_smp] = linearClamp;
    sg_apply_bindings (&bindings);

    const auto vsParams = quadParams (mvp, g.width, g.height);
    fs_params_t fsParams {};
    fsParams.color[0] = fsParams.color[1] = fsParams.color[2] = 1.0f;
    fsParams.color[3] = alpha;

    sg_apply_uniforms (UB_vs_params, SG_RANGE (vsParams));
    sg_apply_uniforms (UB_fs_params, SG_RANGE (fsParams));
    sg_draw (0, 4, 1);
}

void Compositor::renderTestPattern (double nowMs)
{
    auto pass = offscreenPass (composite.colorView, 0.0f, 0.0f, 0.0f, 1.0f, "test-pattern");
    sg_begin_pass (&pass);
    sg_apply_pipeline (testPatternPipeline);

    pattern_params_t params {};
    params.params[0] = static_cast<float> (nowMs * 0.001);
    params.params[1] = 15.0f;   // ≈ the reference frames' band count (PD114-ANALYSIS §1)
    params.tint[0] = 0.82f;     // the light table's red
    params.tint[1] = 0.27f;
    params.tint[2] = 0.16f;
    sg_apply_uniforms (UB_pattern_params, SG_RANGE (params));

    sg_draw (0, 3, 1);
    sg_end_pass();
}

//==============================================================================
void Compositor::drawToWindow (int windowWidth, int windowHeight)
{
    if (composite.image.id == SG_INVALID_ID || windowWidth <= 0 || windowHeight <= 0)
        return;

    // Letterbox: keep the composite's (the projector's) aspect.
    const auto compositeAspect = static_cast<float> (composite.width) / static_cast<float> (composite.height);
    auto width = windowWidth;
    auto height = static_cast<int> (static_cast<float> (windowWidth) / compositeAspect + 0.5f);

    if (height > windowHeight)
    {
        height = windowHeight;
        width = static_cast<int> (static_cast<float> (windowHeight) * compositeAspect + 0.5f);
    }

    sg_apply_viewport ((windowWidth - width) / 2, (windowHeight - height) / 2, width, height, true);
    sg_apply_pipeline (blitPipeline);

    sg_bindings bindings {};
    bindings.views[VIEW_tex] = composite.textureView;
    bindings.samplers[SMP_smp] = linearClamp;
    sg_apply_bindings (&bindings);

    sg_draw (0, 3, 1);
}

} // namespace pixo
