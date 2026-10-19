#version 450
#extension GL_EXT_multiview : require

// Matches Shadow_Push_Constants in renderer.cpp.
layout(push_constant) uniform Push
{
    mat4 model; // Model to world
} push;

// Matches Frame_Uniforms in renderer.cpp.
layout(set = 0, binding = 0) uniform Frame
{
    mat4 view_projection;              // World to clip space
    mat4 light_view_projections[4];    // World to each shadow cascade's clip space
    vec4 camera_position;
    vec4 sun_direction;                // Towards the sun, unit length
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
    vec4 shadow;                       // x: 1 when shadows are on; z: a shadow map texel, in UV
    vec4 shadow_offsets;               // How far off surfaces each cascade is looked up, in world units
    vec4 horizon_color;
    mat4 inverse_view_projection;      // Clip space to world
} frame;

layout(location = 0) in vec3 in_position;

// Depth only: there's no fragment shader. Drawn once into every cascade, a layer of the
// shadow map each, as views.
void main()
{
    gl_Position = frame.light_view_projections[gl_ViewIndex] * (push.model * vec4(in_position, 1.0));
}
