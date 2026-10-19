#version 450

// Matches Mesh_Push_Constants in renderer.cpp: the material's factors.
layout(push_constant) uniform Push
{
    mat4 model;
    vec4 base_color;
    vec4 emissive_metallic;      // Emissive color, metallic (not used yet)
    vec4 roughness_normal_scale; // Roughness (not used yet), normal scale
} push;

// Matches Frame_Uniforms in renderer.cpp: the frame's camera and lighting, in world space.
layout(set = 0, binding = 0) uniform Frame
{
    mat4 view_projection;
    mat4 light_view_projection; // World to the shadow map's clip space
    vec4 camera_position;
    vec4 sun_direction;         // Towards the sun, unit length
    vec4 sun_color;
    vec4 sky_color;
    vec4 ground_color;
    vec4 shadow;                // x: 1 when shadows are on; y: normal offset, in world units; z: a texel, in UV
} frame;

// Depths seen from the sun: comparing samples tell how much of them is nearer than the
// reference, bilinearly filtered. Outside it, nothing is shadowed.
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;

// The material's textures: colors sRGB, so samples come out linear; white, or flat
// normals, for those it doesn't have.
layout(set = 1, binding = 0) uniform sampler2D base_color_texture;
layout(set = 1, binding = 1) uniform sampler2D normal_texture;
layout(set = 1, binding = 2) uniform sampler2D emissive_texture;

layout(location = 0) in  vec3 in_position;
layout(location = 1) in  vec3 in_normal;
layout(location = 2) in  vec2 in_uv;
layout(location = 0) out vec4 out_color;

// How much of the sun reaches `position`: 0 in shadow, 1 in the open, in between over
// the couple of texels a shadow's edge is blurred across.
float sun_visibility(vec3 position, vec3 normal)
{
    if (frame.shadow.x == 0.0) return 1.0;

    // Looked up a little off the surface, so that it doesn't shadow itself.
    vec4 light_position = frame.light_view_projection * vec4(position + normal * frame.shadow.y, 1.0);
    vec3 coordinates    = vec3(light_position.xy * 0.5 + 0.5, light_position.z); // Orthographic: w is 1
    if (coordinates.z > 1.0) return 1.0; // Past the far side of the shadows' sphere

    // Explicit level: the branches above make implicit derivatives undefined.
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += textureLod(shadow_map, coordinates + vec3(vec2(x, y) * frame.shadow.z, 0.0), 0.0);
    return visibility / 9.0;
}

// The surface's normal, bent by the normal texture. The texture's tangent frame comes
// from how the position and texture coordinates change from pixel to pixel, so meshes
// need no tangents (Christian Schüler, "Normal Mapping Without Precomputed Tangents").
// Where the coordinates don't change, as on the palette's flat swatches, the normal
// stays as it is.
vec3 surface_normal(vec3 normal, vec3 position, vec2 uv)
{
    vec3 dp_dx  = dFdx(position);
    vec3 dp_dy  = dFdy(position);
    vec2 duv_dx = dFdx(uv);
    vec2 duv_dy = dFdy(uv);

    // These come out scaled by how the screen's axes turn around the normal, whose
    // sign is Vulkan's (+Y down) for front faces: it's taken out.
    vec3  dp_dy_across = cross(dp_dy, normal);
    vec3  dp_dx_across = cross(normal, dp_dx);
    float handedness   = sign(dot(normal, cross(dp_dx, dp_dy)));
    vec3  tangent      = handedness * (dp_dy_across * duv_dx.x + dp_dx_across * duv_dy.x); // Towards +u
    vec3  bitangent    = handedness * (dp_dy_across * duv_dx.y + dp_dx_across * duv_dy.y); // Towards +v
    float scale        = inversesqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-20));

    vec3 bent = texture(normal_texture, uv).xyz * 2.0 - 1.0;
    bent.xy *= push.roughness_normal_scale.y;

    // The image's +Y is up, and glTF's v grows down it.
    return normalize(mat3(tangent * scale, -bitangent * scale, normal) * bent);
}

void main()
{
    vec3 vertex_normal = normalize(in_normal);
    vec3 normal        = surface_normal(vertex_normal, in_position, in_uv);

    // Shadows are cast by the geometry, so they're looked up off its own surface.
    float facing  = max(dot(normal, frame.sun_direction.xyz), 0.0);
    vec3  sun     = frame.sun_color.rgb * facing * sun_visibility(in_position, vertex_normal);
    vec3  ambient = mix(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);

    vec4 color   = push.base_color * texture(base_color_texture, in_uv);
    vec3 emitted = push.emissive_metallic.rgb * texture(emissive_texture, in_uv).rgb;
    out_color = vec4(color.rgb * (sun + ambient) + emitted, color.a);
}
