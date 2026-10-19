#version 450

// Matches Bloom_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    vec2  source_texel; // Size of a texel of the smaller level read, in UV
    vec2  target_texel; // And of the level added to
    float first;        // Unused
} push;

layout(set = 0, binding = 0) uniform sampler2D source; // Clamped to its edges

layout(location = 0) out vec4 out_color;

// The smaller level, twice the size, through a 3x3 tent filter; it's added to what the
// way down left in this level, so each level ends up with the glow of all the smaller
// ones (Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare").
void main()
{
    vec2 uv = gl_FragCoord.xy * push.target_texel;
    vec2 d  = push.source_texel;

    vec3 color = texture(source, uv).rgb * 4.0
               + (texture(source, uv + vec2(-d.x, 0.0)).rgb + texture(source, uv + vec2(d.x, 0.0)).rgb +
                  texture(source, uv + vec2(0.0, -d.y)).rgb + texture(source, uv + vec2(0.0, d.y)).rgb) * 2.0
               + (texture(source, uv + vec2(-d.x, -d.y)).rgb + texture(source, uv + vec2(d.x, -d.y)).rgb +
                  texture(source, uv + vec2(-d.x,  d.y)).rgb + texture(source, uv + vec2(d.x,  d.y)).rgb);
    out_color = vec4(color / 16.0, 1.0);
}
