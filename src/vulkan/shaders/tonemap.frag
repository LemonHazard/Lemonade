#version 450

// Matches Tonemap_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    float exposure;
    float bloom; // How much of the bloom to blend in
} push;

// The frame as rendered: linear colors, which can go past 1. Same size as the screen.
layout(set = 0, binding = 0) uniform sampler2D scene;
// Its glow: the frame blurred wide, at half its size (see bloom_up.frag).
layout(set = 0, binding = 1) uniform sampler2D bloom;

layout(location = 0) out vec4 out_color;

// Fits colors into 0..1 for the display: Khronos' PBR Neutral tonemapper. Base colors
// of textures, lit by white light of 1, come out as they are: below 0.8 colors only
// lose the 4% every surface reflects (its toe darkens the darkest more gently), and
// brighter ones are compressed towards 1 and desaturated towards white, as bright light
// looks.
vec3 tonemap(vec3 color)
{
    const float start_compression = 0.8 - 0.04;
    const float desaturation      = 0.15;

    float darkest = min(color.r, min(color.g, color.b));
    float offset  = darkest < 0.08 ? darkest - 6.25 * darkest * darkest : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) return color;

    const float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

void main()
{
    // A blend, so the light is spread around rather than added: what's only a little
    // brighter than around it hardly changes.
    vec2 uv    = gl_FragCoord.xy / vec2(textureSize(scene, 0));
    vec3 color = mix(texelFetch(scene, ivec2(gl_FragCoord.xy), 0).rgb, texture(bloom, uv).rgb, push.bloom) * push.exposure;
    out_color = vec4(tonemap(max(color, vec3(0.0))), 1.0);
}
