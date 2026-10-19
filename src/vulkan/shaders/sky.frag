#version 450

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

layout(location = 0) in  vec4 in_far_point;
layout(location = 0) out vec4 out_color;

// The sky's light from `direction` (unit length), as in environment.frag.
vec3 sky_light(vec3 direction)
{
    if (direction.y >= 0.0) return mix(frame.horizon_color.rgb, frame.sky_color.rgb, sqrt(direction.y));
    return mix(frame.horizon_color.rgb, frame.ground_color.rgb, min(-direction.y * 10.0, 1.0));
}

// The background: the sky, and the sun in it, a disc twice its real size with a glow
// around it, bright enough to come out white.
void main()
{
    vec3 direction = normalize(in_far_point.xyz / in_far_point.w - frame.camera_position.xyz);

    float to_sun = dot(direction, frame.sun_direction.xyz);
    float disc   = smoothstep(cos(radians(0.55)), cos(radians(0.5)), to_sun);
    float glow   = pow(max(to_sun, 0.0), 1000.0);
    float above  = smoothstep(-0.01, 0.01, direction.y); // The ground hides it
    vec3  sun    = frame.sun_color.rgb * (disc * 40.0 + glow * 1.5) * above;

    out_color = vec4(sky_light(direction) + sun, 1.0);
}
