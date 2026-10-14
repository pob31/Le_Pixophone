// GEM-equivalent shaders (PD114-ANALYSIS §2). Texture coordinates use v = 0
// at the top of an image, matching D3D11/Metal texture memory order.

// ---------------------------------------------------------------------------
// A GEM [rectangle]/[square]: four vertices from the vertex index (triangle
// strip), half-sizes in `half_size`, full transform in `mvp`.
@vs gem_vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    vec4 half_size;     // xy: rectangle half width/height (GEM sizes)
};

out vec2 uv;

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1)) * 2.0 - 1.0;
    uv = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    gl_Position = mvp * vec4(corner * half_size.xy, 0.0, 1.0);
}
@end

// [pix_texture] modulated by [colorRGB r g b a] (GL_MODULATE).
@fs textured_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform fs_params {
    vec4 color;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
}
@end

// shader/bokeh/bokeh.frag, ported tap for tap. The camera image is the
// "depth": pixels whose depth is near `focus` stay sharp, others blur by
// |depth - focus| * bias, clamped to 3. The tap set is symmetric in y, so
// the v-axis convention doesn't change the result. Alpha is forced to 1.
@fs bokeh_fs
layout(binding=0) uniform texture2D rendered_tex;
layout(binding=1) uniform texture2D depth_tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform bokeh_params {
    vec4 focus_bias;    // x: focus, y: bias, z: 1 = read the depth inverted (compat: a real tex unit 3)
};

in vec2 uv;
out vec4 frag_color;

vec4 tap(vec2 offset, vec2 dofblur, float scale) {
    return texture(sampler2D(rendered_tex, smp), uv + offset * dofblur * scale);
}

void main() {
    const float blurclamp = 3.0;
    // aspectcorrect = vec2(1.0, 500.0/500.0) in the original: a no-op.

    vec4 depth1 = texture(sampler2D(depth_tex, smp), uv);
    if (focus_bias.z > 0.5) {
        depth1 = 1.0 - depth1;
    }
    float factor = depth1.x - focus_bias.x;
    vec2 dofblur = vec2(clamp(factor * focus_bias.y, -blurclamp, blurclamp));

    vec4 col = texture(sampler2D(rendered_tex, smp), uv);

    col += tap(vec2( 0.0,   0.4 ), dofblur, 1.0);
    col += tap(vec2( 0.15,  0.37), dofblur, 1.0);
    col += tap(vec2( 0.29,  0.29), dofblur, 1.0);
    col += tap(vec2(-0.37,  0.15), dofblur, 1.0);
    col += tap(vec2( 0.4,   0.0 ), dofblur, 1.0);
    col += tap(vec2( 0.37, -0.15), dofblur, 1.0);
    col += tap(vec2( 0.29, -0.29), dofblur, 1.0);
    col += tap(vec2(-0.15, -0.37), dofblur, 1.0);
    col += tap(vec2( 0.0,  -0.4 ), dofblur, 1.0);
    col += tap(vec2(-0.15,  0.37), dofblur, 1.0);
    col += tap(vec2(-0.29,  0.29), dofblur, 1.0);
    col += tap(vec2( 0.37,  0.15), dofblur, 1.0);
    col += tap(vec2(-0.4,   0.0 ), dofblur, 1.0);
    col += tap(vec2(-0.37, -0.15), dofblur, 1.0);
    col += tap(vec2(-0.29, -0.29), dofblur, 1.0);
    col += tap(vec2( 0.15, -0.37), dofblur, 1.0);

    col += tap(vec2( 0.15,  0.37), dofblur, 0.9);
    col += tap(vec2(-0.37,  0.15), dofblur, 0.9);
    col += tap(vec2( 0.37, -0.15), dofblur, 0.9);
    col += tap(vec2(-0.15, -0.37), dofblur, 0.9);
    col += tap(vec2(-0.15,  0.37), dofblur, 0.9);
    col += tap(vec2( 0.37,  0.15), dofblur, 0.9);
    col += tap(vec2(-0.37, -0.15), dofblur, 0.9);
    col += tap(vec2( 0.15, -0.37), dofblur, 0.9);

    col += tap(vec2( 0.29,  0.29), dofblur, 0.7);
    col += tap(vec2( 0.4,   0.0 ), dofblur, 0.7);
    col += tap(vec2( 0.29, -0.29), dofblur, 0.7);
    col += tap(vec2( 0.0,  -0.4 ), dofblur, 0.7);
    col += tap(vec2(-0.29,  0.29), dofblur, 0.7);
    col += tap(vec2(-0.4,   0.0 ), dofblur, 0.7);
    col += tap(vec2(-0.29, -0.29), dofblur, 0.7);
    col += tap(vec2( 0.0,   0.4 ), dofblur, 0.7);

    col += tap(vec2( 0.29,  0.29), dofblur, 0.4);
    col += tap(vec2( 0.4,   0.0 ), dofblur, 0.4);
    col += tap(vec2( 0.29, -0.29), dofblur, 0.4);
    col += tap(vec2( 0.0,  -0.4 ), dofblur, 0.4);
    col += tap(vec2(-0.29,  0.29), dofblur, 0.4);
    col += tap(vec2(-0.4,   0.0 ), dofblur, 0.4);
    col += tap(vec2(-0.29, -0.29), dofblur, 0.4);
    col += tap(vec2( 0.0,   0.4 ), dofblur, 0.4);

    frag_color = vec4((col / 41.0).rgb, 1.0);
}
@end

@program gem_quad gem_vs textured_fs
@program gem_bokeh gem_vs bokeh_fs

// ---------------------------------------------------------------------------
// The finished picture into a window: one full-viewport triangle.
@vs blit_vs
out vec2 uv;

void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = vec2(pos.x, 1.0 - pos.y);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
@end

@fs blit_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = vec4(texture(sampler2D(tex, smp), uv).rgb, 1.0);
}
@end

@program blit blit_vs blit_fs

// ---------------------------------------------------------------------------
// The CAMERA chains (§2.7), run on the legacy camera tick (D14). The capture's
// BGRA bytes were uploaded as RGBA8, so red and blue are swapped back here;
// its alpha byte is undefined.
//
//   depth chain: camera → [pix_levels] → [pix_motionblur] → tex unit 2
//   live chain:  camera → [pix_invert] → [pix_levels] → [pix_motionblur]
//                → [pix_coloralpha] → (CAMERAOUTPUT: [pix_color], level)
//
// [pix_motionblur] is a feedback: out = in·(1 − b) + previous·b, kept in an
// 8-bit target like GEM's.
@fs camera_chain_fs
layout(binding=0) uniform texture2D raw_tex;
layout(binding=1) uniform texture2D previous_tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform chain_params {
    vec4 levels;    // in low, in high, out low, out high
    vec4 options;   // x: invert, y: motion blur, z: uv scale about the centre
};

in vec2 uv;
out vec4 frag_color;

void main() {
    vec3 c = texture(sampler2D(raw_tex, smp), 0.5 + (uv - 0.5) * options.z).bgr;

    if (options.x > 0.5) {
        c = 1.0 - c;
    }

    float span = max(levels.y - levels.x, 1.0 / 255.0);
    c = levels.z + clamp((c - levels.x) / span, 0.0, 1.0) * (levels.w - levels.z);

    vec3 previous = texture(sampler2D(previous_tex, smp), uv).rgb;
    frag_color = vec4(mix(c, previous, options.y), 1.0);
}
@end

@program camera_chain blit_vs camera_chain_fs

// [pix_coloralpha]: alpha from the colour (the average of r, g and b).
@fs coloralpha_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    vec3 c = texture(sampler2D(tex, smp), uv).rgb;
    frag_color = vec4(c, (c.r + c.g + c.b) / 3.0);
}
@end

@program coloralpha blit_vs coloralpha_fs

// A GEM quad showing a camera image: [pix_color] (unless bypassed) replaces
// the colour and keeps the alpha, then colourRGB's alpha is the level.
@fs masked_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform mask_params {
    vec4 mask;      // rgb: [pix_color] colour, a: level
    vec4 enable;    // x: 1 = [pix_color] on
};

in vec2 uv;
out vec4 frag_color;

void main() {
    vec4 t = texture(sampler2D(tex, smp), uv);
    vec3 rgb = enable.x > 0.5 ? mask.rgb : t.rgb;
    frag_color = vec4(rgb, t.a * mask.a);
}
@end

@program gem_masked gem_vs masked_fs
