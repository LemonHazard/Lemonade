#version 450

layout(push_constant) uniform Push
{
    mat4 model_view_projection;
    mat3 normal_matrix;
    vec4 color;
} push;

// sRGB, so samples come out linear; white for meshes drawn without a texture.
layout(set = 0, binding = 0) uniform sampler2D albedo;

layout(location = 0) in  vec3 in_normal;
layout(location = 1) in  vec2 in_uv;
layout(location = 0) out vec4 out_color;

// Towards the light, in world space: a sun up and to the right, behind the camera.
const vec3 light_direction = normalize(vec3(0.5, 1.0, 0.7));

void main()
{
    vec3  normal  = normalize(in_normal);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float ambient = 0.10 + 0.08 * normal.y; // Brighter from the sky than from the ground

    vec4 color = push.color * texture(albedo, in_uv);
    out_color = vec4(color.rgb * (diffuse * 0.85 + ambient), color.a);
}
