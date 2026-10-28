#version 450
#extension GL_EXT_fragment_shader_barycentric : require

// Matches Wire_Push_Constants in renderer.cpp, like the block in wire.vert.
layout(push_constant) uniform Push
{
    mat4  model;
    vec4  color;       // sRGB-encoded, alpha not premultiplied
    float encode_srgb;  // 1 when the swapchain doesn't encode sRGB itself
    float depth_offset; // The mesh's, as it was drawn
} push;

layout(location = 0) out vec4 out_color;

// The lines' width, in pixels, across the edge between two triangles: each draws its half.
const float LINE_WIDTH = 1.0;

vec3 srgb_to_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(0.04045, color));
}

// The triangles themselves, drawn only where the scene's depth is theirs, i.e. where
// they're seen: what's near their edges is the line, smoothed over a pixel, and the
// rest is left alone. A fragment's distance in pixels to each of its triangle's edges is
// its barycentric coordinate for the corner across, over how fast that changes a pixel.
void main()
{
    vec3  pixels   = gl_BaryCoordEXT / max(fwidth(gl_BaryCoordEXT), vec3(1e-6));
    float edge     = min(min(pixels.x, pixels.y), pixels.z);
    float coverage = clamp(0.5 * LINE_WIDTH + 0.5 - edge, 0.0, 1.0);
    if (coverage <= 0.0) discard;

    // Into the swapchain, over the tonemapped frame, as the UI is (see ui.frag).
    vec3  color = push.encode_srgb > 0.5 ? push.color.rgb : srgb_to_linear(push.color.rgb);
    float alpha = push.color.a * coverage;
    out_color   = vec4(color * alpha, alpha);
}
