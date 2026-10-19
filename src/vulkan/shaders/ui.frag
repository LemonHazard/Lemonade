#version 450

// Matches Ui_Push_Constants in renderer.cpp, like the block in ui.vert.
layout(push_constant) uniform Push
{
    vec2  scale;
    float encode_srgb; // 1 when the swapchain doesn't encode sRGB itself
} push;

// Read as sRGB, so it's linear here.
layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color; // sRGB-encoded

layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(0.04045, color));
}

vec3 linear_to_srgb(vec3 color)
{
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, color));
}

void main()
{
    vec4 color = vec4(srgb_to_linear(in_color.rgb), in_color.a) * texture(image, in_uv);
    if (push.encode_srgb > 0.5) color.rgb = linear_to_srgb(color.rgb);

    // Blended over the frame as premultiplied.
    out_color = vec4(color.rgb * color.a, color.a);
}
