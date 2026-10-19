#version 450

// Matches Ui_Push_Constants in renderer.cpp, like the block in ui.frag.
layout(push_constant) uniform Push
{
    vec2  scale;       // From the vertices' units to clip space: 2 * pixels per unit / the frame's size
    float encode_srgb; // 1 when the swapchain doesn't encode sRGB itself
} push;

layout(location = 0) in vec2 in_position; // From the frame's top-left corner, +Y down
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;    // sRGB-encoded

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

void main()
{
    // Vulkan's clip space has -Y at the top, as the vertices' (0, 0) is.
    gl_Position = vec4(in_position * push.scale - 1.0, 0.0, 1.0);
    out_uv      = in_uv;
    // Still encoded, so that gradients between vertices go as they would on an sRGB
    // display: decoded in ui.frag.
    out_color   = in_color;
}
