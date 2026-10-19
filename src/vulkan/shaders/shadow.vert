#version 450

// Matches Shadow_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    mat4 model_light_view_projection; // Model to the shadow map's clip space
} push;

layout(location = 0) in vec3 in_position;

// Depth only: there's no fragment shader.
void main()
{
    gl_Position = push.model_light_view_projection * vec4(in_position, 1.0);
}
