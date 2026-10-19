#pragma once

#include "core.h"
#include "vector.h"
#include "texture.h"

// How a surface looks, in glTF's metallic-roughness terms. Each texture multiplies its
// factor; one without pixels counts as white (as flat, for the normal texture). Colors
// are linear: the renderer reads color textures as sRGB and the normal texture as is.

// What the base color's alpha does, as glTF's alphaMode.
enum class Alpha_Mode : u32
{
    Opaque, // Nothing: the surface is solid
    Mask,   // Cuts out where it's below alpha_cutoff, solid elsewhere
    Blend,  // Lets what's behind show through: diffuse light fades with alpha, reflections
            // and emitted light don't (as glass). Drawn after solid surfaces, casting no shadow.
};

struct Material
{
    Vector4 base_color;
    Texture base_color_texture;
    Texture normal_texture;     // Tangent-space normals: +X right and +Y up in the image
    r32     normal_scale;       // Scales the normals' X and Y, flattening them below 1
    r32     metallic;
    r32     roughness;
    Vector3 emissive;           // Light the surface gives off: can go past 1
    Texture emissive_texture;
    Alpha_Mode alpha_mode;
    r32        alpha_cutoff;    // For Alpha_Mode::Mask
};

// A rough, non-metallic surface of `color` times `texture`.
inline Material plain_material(const Vector4 &color, const Texture &texture = {})
{
    Material material = {};
    material.base_color         = color;
    material.base_color_texture = texture;
    material.normal_scale       = 1.0f;
    material.roughness          = 1.0f;
    material.alpha_mode         = Alpha_Mode::Opaque;
    material.alpha_cutoff       = 0.5f;
    return material;
}
