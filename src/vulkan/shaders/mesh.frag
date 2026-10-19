#version 450

// Matches Mesh_Push_Constants in renderer.cpp: the material's factors.
layout(push_constant) uniform Push
{
    mat4 model;
    vec4 base_color;
    vec4 emissive_metallic;      // Emissive color, metallic
    vec4 roughness_normal_alpha; // Roughness, normal scale, alpha cutoff (0 for none), 1 when blended
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
    vec4 horizon_color;
    mat4 inverse_view_projection;
} frame;

// Depths seen from the sun: comparing samples tell how much of them is nearer than the
// reference, bilinearly filtered. Outside it, nothing is shadowed.
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;

// The sky as surfaces reflect it (see environment.frag): specularly, a mip level for
// each step of roughness from 0 to 1, and diffusely, by the direction they face.
layout(set = 0, binding = 2) uniform samplerCube environment;
layout(set = 0, binding = 3) uniform samplerCube irradiance;

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
    bent.xy *= push.roughness_normal_alpha.y;

    // The image's +Y is up, and glTF's v grows down it.
    return normalize(mat3(tangent * scale, -bitangent * scale, normal) * bent);
}

const float PI = 3.14159265;

// The sun's light, as glTF's metallic-roughness materials reflect it: Lambert diffuse
// and Cook-Torrance specular, with GGX's microfacets, Smith's height-correlated masking
// and Schlick's Fresnel. Scaled by pi, so that a white, rough, non-metallic surface
// facing the light reflects the light's color.
void reflected_light(vec3 normal, vec3 to_light, vec3 to_eye, vec3 diffuse_color, vec3 f0, float roughness,
                     out vec3 diffuse_light, out vec3 specular_light)
{
    vec3  halfway   = normalize(to_light + to_eye);
    float n_dot_l   = max(dot(normal, to_light), 0.0);
    float n_dot_v   = max(dot(normal, to_eye), 1e-4);
    float n_dot_h   = max(dot(normal, halfway), 0.0);
    float v_dot_h   = max(dot(to_eye, halfway), 0.0);
    float alpha     = roughness * roughness; // Roughness is perceptual: squared, it's linear
    float alpha2    = alpha * alpha;

    float d = alpha2 / (PI * pow(n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0, 2.0));
    float v = 0.5 / max(n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha2) + alpha2) +
                        n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha2) + alpha2), 1e-6);
    vec3  f = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);

    // What isn't reflected off the surface goes into it, and comes out diffuse.
    diffuse_light  = (1.0 - f) * diffuse_color * n_dot_l; // Lambert's 1 / pi, times pi
    specular_light = PI * d * v * f * n_dot_l;
}

// How much of light from all around a surface reflects specularly: the split-sum
// approximation's scale and bias, fitted analytically (Brian Karis, "Physically Based
// Shading on Mobile"), as there's no table of them to look up.
vec3 environment_reflectance(vec3 f0, float roughness, float n_dot_v)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    vec2  ab   = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

void main()
{
    vec3 vertex_normal = normalize(in_normal);
    vec3 normal        = surface_normal(vertex_normal, in_position, in_uv);
    vec3 to_eye        = normalize(frame.camera_position.xyz - in_position);

    vec4  color     = push.base_color * texture(base_color_texture, in_uv);
    float metallic  = clamp(push.emissive_metallic.w, 0.0, 1.0);
    float roughness = clamp(push.roughness_normal_alpha.x, 0.045, 1.0); // Smoother turns highlights into specks

    // Metals reflect their color and don't diffuse; everything else reflects 4%.
    vec3 diffuse_color = color.rgb * (1.0 - metallic);
    vec3 f0            = mix(vec3(0.04), color.rgb, metallic);

    // Shadows are cast by the geometry, so they're looked up off its own surface.
    vec3 sun_diffuse, sun_specular;
    reflected_light(normal, frame.sun_direction.xyz, to_eye, diffuse_color, f0, roughness, sun_diffuse, sun_specular);
    vec3 sun = frame.sun_color.rgb * sun_visibility(in_position, vertex_normal);

    // The sky and the ground light it diffusely from where it faces, and are reflected
    // from where the view bounces to, blurred by roughness.
    float n_dot_v    = max(dot(normal, to_eye), 1e-4);
    vec3  specular   = environment_reflectance(f0, roughness, n_dot_v);
    vec3  reflection = reflect(-to_eye, normal);
    float levels     = float(textureQueryLevels(environment) - 1);
    vec3  sky        = textureLod(environment, reflection, roughness * levels).rgb;

    vec3 diffuse_light  = sun * sun_diffuse + (1.0 - specular) * diffuse_color * texture(irradiance, normal).rgb;
    vec3 specular_light = sun * sun_specular + specular * sky;
    vec3 emitted        = push.emissive_metallic.rgb * texture(emissive_texture, in_uv).rgb;

    // Cut-outs, after the derivatives above, which discarding would leave undefined.
    float alpha = color.a;
    if (alpha < push.roughness_normal_alpha.z) discard;

    // Blended, premultiplied: what's behind shows through as much as the surface lets
    // light into it, and what it reflects or gives off is added whole.
    if (push.roughness_normal_alpha.w > 0.0)
        out_color = vec4(diffuse_light * alpha + specular_light + emitted, alpha);
    else
        out_color = vec4(diffuse_light + specular_light + emitted, 1.0);
}
