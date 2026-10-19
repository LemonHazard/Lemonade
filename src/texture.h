#pragma once

#include "core.h"

// Images to draw meshes with, as the game hands them to the renderer: 8-bit RGBA
// pixels, sRGB-encoded, rows from the top. Like meshes, the pixels stay where the game
// keeps them (its memory, usually). The renderer starts copying a texture to the GPU the
// first time it's drawn and keeps it there for the next frames, recognizing it by its
// pixels' address: pixels changed in place aren't picked up. Big textures take a few
// frames to copy, and meshes drawn with them show up once they're done.

struct Texture
{
    u8  *pixels; // width * height * 4 bytes; none means no texture
    u32  width;
    u32  height;
};

// Bytes for a texture's pixels, laid out as load_png() stores them.
inline u64 texture_storage_size(u32 width, u32 height)
{
    return (u64)width * height * 4;
}
