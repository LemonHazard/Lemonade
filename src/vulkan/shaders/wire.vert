#version 450

// Matches Wire_Push_Constants in renderer.cpp, like the block in wire.frag.
layout(push_constant) uniform Push
{
    mat4  model;       // Model to world
    vec4  color;       // sRGB-encoded, alpha not premultiplied
    float encode_srgb; // 1 when the swapchain doesn't encode sRGB itself
} push;

// Matches Frame_Uniforms in renderer.cpp, like the block in mesh.vert.
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

// Where mesh.vert puts it, to the bit, so that the depth the mesh left is equal to its own
// where it's seen, and only there.
invariant gl_Position;

void main()
{
    gl_Position = frame.view_projection * (push.model * vec4(in_position, 1.0));
}
