#version 450

layout(push_constant) uniform Push
{
    mat4 model;
    mat3 normal_matrix;
    vec4 color;
} push;

// Matches Frame_Uniforms in renderer.cpp: the frame's camera and lighting, in world space.
layout(set = 0, binding = 0) uniform Frame
{
    mat4 view_projection;
    mat4 light_view_projection; // World to the shadow map's clip space
    vec4 camera_position;
    vec4 sun_direction;         // Towards the sun, unit length
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
    vec4 shadow;                // x: 1 when shadows are on; y: normal offset, in world units; z: a texel, in UV
} frame;

// Depths seen from the sun: comparing samples tell how much of them is nearer than the
// reference, bilinearly filtered. Outside it, nothing is shadowed.
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;

// sRGB, so samples come out linear; white for meshes drawn without a texture.
layout(set = 1, binding = 0) uniform sampler2D albedo;

layout(location = 0) in  vec3 in_position;
layout(location = 1) in  vec3 in_normal;
layout(location = 2) in  vec2 in_uv;
layout(location = 0) out vec4 out_color;

// How much of the sun reaches `position`: 0 in shadow, 1 in the open, in between over
// the couple of texels a shadow's edge is blurred across.
float sun_visibility(vec3 position, vec3 normal)
{
    if (frame.shadow.x == 0.0) return 1.0;

    // Looked up a little off the surface, so that it doesn't shadow itself.
    vec4 light_position = frame.light_view_projection * vec4(position + normal * frame.shadow.y, 1.0);
    vec3 coordinates    = vec3(light_position.xy * 0.5 + 0.5, light_position.z); // Orthographic: w is 1
    if (coordinates.z > 1.0) return 1.0; // Past the far side of the shadows' sphere

    // Explicit level: the branches above make implicit derivatives undefined.
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += textureLod(shadow_map, coordinates + vec3(vec2(x, y) * frame.shadow.z, 0.0), 0.0);
    return visibility / 9.0;
}

void main()
{
    vec3 normal  = normalize(in_normal);
    float facing = max(dot(normal, frame.sun_direction.xyz), 0.0);
    vec3 sun     = frame.sun_color.rgb * facing * sun_visibility(in_position, normal);
    vec3 ambient = mix(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);

    vec4 color = push.color * texture(albedo, in_uv);
    out_color = vec4(color.rgb * (sun + ambient), color.a);
}
