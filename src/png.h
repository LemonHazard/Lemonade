#pragma once

#include "core.h"
#include "texture.h"

// Loads PNG images, converting them to 8-bit RGBA.
//
// The pixels are written to `storage`; the returned Texture points into it. Returns
// the bytes used, or 0 if the file can't be loaded or the pixels don't fit (logged),
// leaving `texture` empty.

u64 load_png(const char *path, Texture *texture, void *storage, u64 storage_size);
u64 load_png_from_memory(const u8 *data, u64 size, Texture *texture, void *storage, u64 storage_size);
