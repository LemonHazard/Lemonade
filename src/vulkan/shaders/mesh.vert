#version 450

// Matches Mesh_Push_Constants in renderer.cpp: 128 bytes, the most every device takes.
layout(push_constant) uniform Push
{
    mat4 model_view_projection;
    mat3 normal_matrix; // Model to world, for normals
    vec4 color;
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_normal; // World space
layout(location = 1) out vec2 out_uv;

void main()
{
    gl_Position = push.model_view_projection * vec4(in_position, 1.0);
    out_normal  = push.normal_matrix * in_normal;
    out_uv      = in_uv;
}
