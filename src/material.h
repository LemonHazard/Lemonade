#pragma once

#include "core.h"
#include "vector.h"
#include "texture.h"

// How a surface looks, in glTF's metallic-roughness terms. Each texture multiplies its
// factor; one without pixels counts as white (as flat, for the normal texture). Colors
// are linear: the renderer reads color textures as sRGB and the normal texture as is.

struct Material
{
    Vector4 base_color;         // Its alpha isn't used yet: everything is drawn opaque
    Texture base_color_texture;
    Texture normal_texture;     // Tangent-space normals: +X right and +Y up in the image
    r32     normal_scale;       // Scales the normals' X and Y, flattening them below 1
    r32     metallic;
    r32     roughness;
    Vector3 emissive;           // Light the surface gives off: can go past 1
    Texture emissive_texture;
};

// A rough, non-metallic surface of `color` times `texture`.
inline Material plain_material(const Vector4 &color, const Texture &texture = {})
{
    Material material = {};
    material.base_color         = color;
    material.base_color_texture = texture;
    material.normal_scale       = 1.0f;
    material.roughness          = 1.0f;
    return material;
}
