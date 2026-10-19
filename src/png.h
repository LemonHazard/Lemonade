#pragma once

#include "core.h"
#include "texture.h"

#include <vector>

// Loads PNG images, converting them to 8-bit RGBA.
//
// The pixels are written to `storage`; the returned Texture points into it. Returns
// the bytes used, or 0 if the file can't be loaded or the pixels don't fit (logged),
// leaving `texture` empty.

u64 load_png(const char *path, Texture *texture, void *storage, u64 storage_size);
u64 load_png_from_memory(const u8 *data, u64 size, Texture *texture, void *storage, u64 storage_size);

// Saves 8-bit RGBA pixels, rows from the top (a Texture's layout), as PNG images: RGB
// when every pixel is opaque, RGBA otherwise.
//
// encode_png() returns the file's bytes, none when the image has no pixels or is too
// large for stb_image_write (modules/common/stb), which encodes it. save_png()
// returns false if the file can't be written (logged).

std::vector<u8> encode_png(const Texture &image);
bool save_png(const char *path, const Texture &image);
