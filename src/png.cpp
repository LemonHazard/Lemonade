#include "png.h"
#include "logger.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

// stb_image (modules/common/stb) decodes the PNGs. Its failure reason would be a
// thread_local, which can keep a game library from unloading on reload.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include <stb_image.h>

u64 load_png_from_memory(const u8 *data, u64 size, Texture *texture, void *storage, u64 storage_size)
{
    *texture = {};

    if (size > INT_MAX)
    {
        log_error("PNG: file too large");
        return 0;
    }

    int width, height, channels;
    u8 *pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!pixels)
    {
        log_error("PNG: %s", stbi_failure_reason());
        return 0;
    }

    u64 needed = texture_storage_size((u32)width, (u32)height);
    if (needed > storage_size)
    {
        log_error("PNG: the %dx%d image needs %llu bytes of storage, %llu given",
                  width, height, (unsigned long long)needed, (unsigned long long)storage_size);
        stbi_image_free(pixels);
        return 0;
    }

    texture->pixels = (u8 *)storage;
    texture->width  = (u32)width;
    texture->height = (u32)height;
    memcpy(texture->pixels, pixels, needed);
    stbi_image_free(pixels);

    return needed;
}

u64 load_png(const char *path, Texture *texture, void *storage, u64 storage_size)
{
    *texture = {};

    FILE *file = fopen(path, "rb");
    if (!file)
    {
        log_error("Could not open \"%s\"", path);
        return 0;
    }

    std::vector<u8> data;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        long size = ftell(file);
        if (size > 0 && fseek(file, 0, SEEK_SET) == 0)
        {
            data.resize((u64)size);
            if (fread(data.data(), 1, data.size(), file) != data.size()) data.clear();
        }
    }
    fclose(file);

    u64 used = data.empty() ? 0 : load_png_from_memory(data.data(), data.size(), texture, storage, storage_size);
    if (used)
        log_info("Loaded \"%s\": %ux%u", path, texture->width, texture->height);
    else
        log_error("Could not load \"%s\"", path);

    return used;
}
