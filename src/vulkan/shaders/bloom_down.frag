#version 450

// Matches Bloom_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    vec2  source_texel; // Size of a texel of the source, in UV
    vec2  target_texel; // And of the level drawn
    float first;        // 1 for the first step down, from the frame
} push;

layout(set = 0, binding = 0) uniform sampler2D source; // Clamped to its edges

layout(location = 0) out vec4 out_color;

vec3 at(vec2 uv, vec2 offset)
{
    return texture(source, uv + offset * push.source_texel).rgb;
}

// Blocks of four samples are weighted down by their brightness on the first step, so
// that a lone very bright pixel (a sun glint) doesn't flicker as a whole blob of glow
// as it moves (Brian Karis's average).
float weight(vec3 a, vec3 b, vec3 c, vec3 d)
{
    if (push.first == 0.0) return 1.0;
    vec3 average = (a + b + c + d) * 0.25;
    return 1.0 / (1.0 + dot(average, vec3(0.2126, 0.7152, 0.0722)));
}

// Half the size, with 13 bilinear samples: five overlapping blocks of 2x2 texels, the
// middle one weighted as much as the four others (Jorge Jimenez, "Next Generation Post
// Processing in Call of Duty: Advanced Warfare").
void main()
{
    vec2 uv = gl_FragCoord.xy * push.target_texel;

    vec3 a = at(uv, vec2(-2.0, -2.0)), b = at(uv, vec2(0.0, -2.0)), c = at(uv, vec2(2.0, -2.0));
    vec3 d = at(uv, vec2(-1.0, -1.0)), e = at(uv, vec2(1.0, -1.0));
    vec3 f = at(uv, vec2(-2.0,  0.0)), g = at(uv, vec2(0.0,  0.0)), h = at(uv, vec2(2.0,  0.0));
    vec3 i = at(uv, vec2(-1.0,  1.0)), j = at(uv, vec2(1.0,  1.0));
    vec3 k = at(uv, vec2(-2.0,  2.0)), l = at(uv, vec2(0.0,  2.0)), m = at(uv, vec2(2.0,  2.0));

    float w_middle = 0.5   * weight(d, e, i, j);
    float w_1      = 0.125 * weight(a, b, f, g);
    float w_2      = 0.125 * weight(b, c, g, h);
    float w_3      = 0.125 * weight(f, g, k, l);
    float w_4      = 0.125 * weight(g, h, l, m);

    vec3 color = (d + e + i + j) * 0.25 * w_middle
               + (a + b + f + g) * 0.25 * w_1 + (b + c + g + h) * 0.25 * w_2
               + (f + g + k + l) * 0.25 * w_3 + (g + h + l + m) * 0.25 * w_4;
    out_color = vec4(color / (w_middle + w_1 + w_2 + w_3 + w_4), 1.0);
}
