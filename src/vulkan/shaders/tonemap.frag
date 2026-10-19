#version 450

// Matches Tonemap_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    float exposure;
} push;

// The frame as rendered: linear colors, which can go past 1. Same size as the screen.
layout(set = 0, binding = 0) uniform sampler2D scene;

layout(location = 0) out vec4 out_color;

// Fits colors into 0..1 for the display. Below 0.8 they stay as they are, so the base
// colors of textures show unchanged; brighter ones are compressed towards 1 and
// desaturated towards white, as bright light looks. This is the shoulder of Khronos'
// PBR Neutral tonemapper, without its toe: that darkens the darkest colors to cancel
// the reflection PBR adds to every surface, and there's no such reflection yet.
vec3 tonemap(vec3 color)
{
    const float start_compression = 0.8;
    const float desaturation      = 0.15;

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
    vec3 color = texelFetch(scene, ivec2(gl_FragCoord.xy), 0).rgb * push.exposure;
    out_color = vec4(tonemap(max(color, vec3(0.0))), 1.0);
}
