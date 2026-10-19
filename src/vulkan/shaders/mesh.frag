#version 450

layout(push_constant) uniform Push
{
    mat4 model_view_projection;
    mat3 normal_matrix;
    vec4 color;
} push;

// Matches Frame_Uniforms in renderer.cpp: the frame's camera and lighting, in world space.
layout(set = 0, binding = 0) uniform Frame
{
    vec4 camera_position;
    vec4 sun_direction; // Towards the sun, unit length
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
} frame;

// sRGB, so samples come out linear; white for meshes drawn without a texture.
layout(set = 1, binding = 0) uniform sampler2D albedo;

layout(location = 0) in  vec3 in_normal;
layout(location = 1) in  vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 normal  = normalize(in_normal);
    vec3 sun     = frame.sun_color.rgb * max(dot(normal, frame.sun_direction.xyz), 0.0);
    vec3 ambient = mix(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);

    vec4 color = push.color * texture(albedo, in_uv);
    out_color = vec4(color.rgb * (sun + ambient), color.a);
}
