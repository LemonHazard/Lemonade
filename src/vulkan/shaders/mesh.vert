#version 450

// Matches Mesh_Push_Constants in renderer.cpp: 128 bytes, the most every device takes.
layout(push_constant) uniform Push
{
    mat4 model;         // Model to world
    mat3 normal_matrix; // Model to world, for normals
    vec4 color;
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
    vec4 position = push.model * vec4(in_position, 1.0);
    gl_Position   = frame.view_projection * position;
    out_position  = position.xyz;
    out_normal    = push.normal_matrix * in_normal;
    out_uv        = in_uv;
}
