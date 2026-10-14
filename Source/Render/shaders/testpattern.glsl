// P0 test pattern: proves the sokol-shdc toolchain, pipelines and uniforms on
// every GPU surface. A full-screen triangle generated from the vertex index
// (no vertex buffer), a tinted gradient and drifting horizontal bands — a nod
// to the light table's rolling-shutter scan lines (PD114-ANALYSIS §1).

@vs vs
out vec2 uv;

void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
@end

@fs fs
layout(binding=0) uniform pattern_params {
    vec4 params;    // x: time in seconds, y: band cycles per picture height, zw: unused
    vec4 tint;      // rgb: surface tint, a: unused
};

in vec2 uv;
out vec4 frag_color;

void main() {
    float time = params.x;
    float cycles = params.y;
    float bands = 0.5 + 0.5 * sin((uv.y * cycles - time * 0.33) * 6.2831853);
    vec3 gradient = mix(vec3(0.04), tint.rgb, uv.x);
    frag_color = vec4(gradient * (0.55 + 0.45 * bands), 1.0);
}
@end

@program testpattern vs fs
