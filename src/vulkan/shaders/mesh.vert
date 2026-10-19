#version 450

// Matches Mesh_Push_Constants in renderer.cpp, like the block in mesh.frag.
layout(push_constant) uniform Push
{
    mat4 model; // Model to world
    vec4 base_color;
    vec4 emissive_metallic;
    vec4 roughness_normal_alpha;
} push;

// Matches Frame_Uniforms in renderer.cpp, like the block in mesh.frag.
layout(set = 0, binding = 0) uniform Frame
{
    mat4 view_projection;       // World to clip space
    mat4 light_view_projection; // World to the shadow map's clip space
    vec4 camera_position;
    vec4 sun_direction;
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
    vec4 shadow;
} frame;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_position; // World space
layout(location = 1) out vec3 out_normal;   // World space
layout(location = 2) out vec2 out_uv;

void main()
{
    // Normals go through the cofactor matrix of the model's 3x3 part: its inverse
    // transpose times its determinant, whose sign keeps mirrored normals facing out.
    mat3  m        = mat3(push.model);
    mat3  cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    float sign     = dot(m[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;

    vec4 position = push.model * vec4(in_position, 1.0);
    gl_Position   = frame.view_projection * position;
    out_position  = position.xyz;
    out_normal    = sign * (cofactor * in_normal);
    out_uv        = in_uv;
}
