#version 450

// Matches Environment_Push_Constants in renderer.cpp: which face of which cube map.
layout(push_constant) uniform Push
{
    uint  face;       // +X, -X, +Y, -Y, +Z, -Z, as Vulkan orders them
    float size;       // Of the face, in pixels
    float roughness;  // Of the reflections the pixel is for
    uint  irradiance; // 1 for the diffuse light, roughness unused
} push;

// Matches Frame_Uniforms in renderer.cpp.
layout(set = 0, binding = 0) uniform Frame
{
    mat4 view_projection;
    mat4 light_view_projection;
    vec4 camera_position;
    vec4 sun_direction;
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
    vec4 shadow;
    vec4 horizon_color;
    mat4 inverse_view_projection;
} frame;

layout(location = 0) out vec4 out_color;

const float PI      = 3.14159265;
const uint  SAMPLES = 256;

// The sky's light from `direction` (unit length): from the horizon's color to the
// sky's straight up, and the ground's below, reached quickly. Without the sun, which
// meshes are lit by directly, with its shadows. As in sky.frag.
vec3 sky_light(vec3 direction)
{
    if (direction.y >= 0.0) return mix(frame.horizon_color.rgb, frame.sky_color.rgb, sqrt(direction.y));
    return mix(frame.horizon_color.rgb, frame.ground_color.rgb, min(-direction.y * 10.0, 1.0));
}

// The direction through the middle of this pixel of the face.
vec3 face_direction()
{
    vec2 p = gl_FragCoord.xy / push.size * 2.0 - 1.0; // +Y down the face
    switch (push.face)
    {
        case 0:  return normalize(vec3( 1.0, -p.y, -p.x));
        case 1:  return normalize(vec3(-1.0, -p.y,  p.x));
        case 2:  return normalize(vec3( p.x,  1.0,  p.y));
        case 3:  return normalize(vec3( p.x, -1.0, -p.y));
        case 4:  return normalize(vec3( p.x, -p.y,  1.0));
        default: return normalize(vec3(-p.x, -p.y, -1.0));
    }
}

// Evenly spread points of the unit square, for sampling.
vec2 hammersley(uint i)
{
    return vec2(float(i) / float(SAMPLES), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// The sky as surfaces facing `normal` reflect it: diffuse, it's the cosine-weighted
// average over the half of it they face; specular, the average over GGX's lobe of
// `roughness` around the mirror direction, taking the view to be along the normal
// (the split-sum approximation, Brian Karis, "Real Shading in Unreal Engine 4").
void main()
{
    vec3 normal = face_direction();
    if (push.irradiance == 0 && push.roughness == 0.0)
    {
        out_color = vec4(sky_light(normal), 1.0);
        return;
    }

    vec3 up        = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    float alpha    = push.roughness * push.roughness;

    vec3  sum    = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0; i < SAMPLES; ++i)
    {
        vec2  u   = hammersley(i);
        float phi = 2.0 * PI * u.x;
        if (push.irradiance != 0)
        {
            float cos_theta = sqrt(1.0 - u.y);
            float sin_theta = sqrt(u.y);
            vec3  direction = tangent * (cos(phi) * sin_theta) + bitangent * (sin(phi) * sin_theta) + normal * cos_theta;
            sum    += sky_light(direction);
            weight += 1.0;
        }
        else
        {
            float cos_theta = sqrt((1.0 - u.y) / (1.0 + (alpha * alpha - 1.0) * u.y));
            float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
            vec3  halfway   = tangent * (cos(phi) * sin_theta) + bitangent * (sin(phi) * sin_theta) + normal * cos_theta;
            vec3  direction = 2.0 * dot(normal, halfway) * halfway - normal;
            float n_dot_l   = dot(normal, direction);
            if (n_dot_l > 0.0)
            {
                sum    += sky_light(direction) * n_dot_l;
                weight += n_dot_l;
            }
        }
    }
    out_color = vec4(sum / max(weight, 1e-4), 1.0);
}
