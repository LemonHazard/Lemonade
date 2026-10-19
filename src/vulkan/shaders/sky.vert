#version 450

// Matches Frame_Uniforms in renderer.cpp, like the block in sky.frag.
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

// One triangle covering the screen: the pixels outside it are clipped.
const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

// Where the pixel's on the far plane, in world space, before the divide by w: it
// interpolates linearly across the screen, the divided point wouldn't.
layout(location = 0) out vec4 out_far_point;

void main()
{
    gl_Position   = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    out_far_point = frame.inverse_view_projection * vec4(positions[gl_VertexIndex], 1.0, 1.0);
}
