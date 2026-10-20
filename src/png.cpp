#include "png.h"
#include "file.h"
#include "logger.h"

#include <climits>
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

    str data = read_file(path);
    if (data.empty())
    {
        log_error("Could not read \"%s\"", path);
        return 0;
    }

    u64 used = load_png_from_memory((const u8 *)data.data(), data.size(), texture, storage, storage_size);
    if (used)
        log_info("Loaded \"%s\": %ux%u", path, texture->width, texture->height);
    else
        log_error("Could not load \"%s\"", path);

    return used;
}

// stb_image_write encodes them. Its functions are kept private to this file, and the
// file is written here, to log failures like load_png().

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

static void append_to_vector(void *context, void *data, int size)
{
    auto *out = (std::vector<u8> *)context;
    out->insert(out->end(), (const u8 *)data, (const u8 *)data + size);
}

std::vector<u8> encode_png(const Texture &image)
{
    std::vector<u8> png;
    if (!image.pixels || image.width == 0 || image.height == 0) return png;
    if (image.width > INT_MAX / 4 || image.height > INT_MAX) return png;

    u64 pixel_count = (u64)image.width * image.height;
    bool opaque = true;
    for (u64 i = 0; i < pixel_count && opaque; ++i) opaque = image.pixels[i * 4 + 3] == 255;

    // Without alpha, the pixels are packed as RGB first.
    const u8 *pixels = image.pixels;
    std::vector<u8> rgb;
    if (opaque)
    {
        rgb.resize(pixel_count * 3);
        for (u64 i = 0; i < pixel_count; ++i)
        {
            rgb[i * 3 + 0] = image.pixels[i * 4 + 0];
            rgb[i * 3 + 1] = image.pixels[i * 4 + 1];
            rgb[i * 3 + 2] = image.pixels[i * 4 + 2];
        }
        pixels = rgb.data();
    }

    int channels = opaque ? 3 : 4;
    if (!stbi_write_png_to_func(append_to_vector, &png, (int)image.width, (int)image.height, channels, pixels,
                                (int)image.width * channels))
        png.clear();

    return png;
}

bool save_png(const char *path, const Texture &image)
{
    std::vector<u8> png = encode_png(image);
    if (png.empty())
    {
        log_error("Could not save \"%s\": the image can't be encoded", path);
        return false;
    }

    bool ok = write_file(path, std::string_view((const char *)png.data(), png.size()));

    if (ok)
        log_info("Saved \"%s\": %ux%u", path, image.width, image.height);
    else
        log_error("Could not write \"%s\"", path);

    return ok;
}
