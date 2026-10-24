#version 450

// Matches Wire_Push_Constants in renderer.cpp, like the block in wire.vert.
layout(push_constant) uniform Push
{
    mat4  model;
    vec4  color;       // sRGB-encoded, alpha not premultiplied
    float encode_srgb; // 1 when the swapchain doesn't encode sRGB itself
} push;

layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(0.04045, color));
}

// Into the swapchain, over the tonemapped frame, as the UI is (see ui.frag).
void main()
{
    vec3 color = push.encode_srgb > 0.5 ? push.color.rgb : srgb_to_linear(push.color.rgb);
    out_color  = vec4(color * push.color.a, push.color.a);
}
