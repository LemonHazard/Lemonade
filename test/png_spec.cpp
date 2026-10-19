#include <catch2/catch_test_macros.hpp>

#include "png.h"

#include <cstring>
#include <vector>

// A 2x2 RGB PNG: red and green on top, blue and white below.
static const u8 png_spec_png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A,
    0x73, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xC0,
    0x00, 0xC2, 0x0C, 0xFF, 0x81, 0x00, 0x00, 0x1F, 0xEE, 0x05, 0xFB, 0x0B, 0xD9, 0x68, 0x8B, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

TEST_CASE("load_png_from_memory decodes to RGBA, rows from the top", "[png]")
{
    u8 storage[64];
    Texture texture;
    u64 used = load_png_from_memory(png_spec_png, sizeof(png_spec_png), &texture, storage, sizeof(storage));

    REQUIRE(used == texture_storage_size(2, 2));
    REQUIRE(texture.pixels == storage);
    REQUIRE(texture.width == 2);
    REQUIRE(texture.height == 2);

    const u8 expected[16] = {
        255, 0,   0,   255,   0,   255, 0,   255,
        0,   0,   255, 255,   255, 255, 255, 255,
    };
    for (u32 i = 0; i < 16; ++i)
        REQUIRE(texture.pixels[i] == expected[i]);
}

TEST_CASE("load_png_from_memory fails when the storage is too small", "[png]")
{
    u8 storage[15];
    Texture texture;
    REQUIRE(load_png_from_memory(png_spec_png, sizeof(png_spec_png), &texture, storage, sizeof(storage)) == 0);
    REQUIRE(texture.pixels == nullptr);
}

TEST_CASE("load_png_from_memory rejects files that aren't PNGs", "[png]")
{
    const u8 text[] = "not a png";

    u8 storage[64];
    Texture texture;
    REQUIRE(load_png_from_memory(text, sizeof(text), &texture, storage, sizeof(storage)) == 0);
    REQUIRE(texture.pixels == nullptr);
}

// Encodes `image` and decodes it back, checking every pixel comes back the same.
static void require_round_trip(const Texture &image)
{
    std::vector<u8> png = encode_png(image);
    REQUIRE(!png.empty());

    std::vector<u8> storage(texture_storage_size(image.width, image.height));
    Texture decoded;
    u64 used = load_png_from_memory(png.data(), png.size(), &decoded, storage.data(), storage.size());

    REQUIRE(used == storage.size());
    REQUIRE(decoded.width == image.width);
    REQUIRE(decoded.height == image.height);
    REQUIRE(memcmp(decoded.pixels, image.pixels, used) == 0);
}

// The color type byte of an encoded PNG's header: 2 for RGB, 6 for RGBA.
static u8 color_type(const std::vector<u8> &png)
{
    return png[25];
}

TEST_CASE("encode_png writes opaque images as RGB", "[png]")
{
    u8 pixels[16] = {
        255, 0,   0,   255,   0,   255, 0,   255,
        0,   0,   255, 255,   255, 255, 255, 255,
    };
    Texture image = { pixels, 2, 2 };

    std::vector<u8> png = encode_png(image);
    REQUIRE(color_type(png) == 2);
    require_round_trip(image);
}

TEST_CASE("encode_png keeps the alpha of images that aren't opaque", "[png]")
{
    u8 pixels[16] = {
        255, 0,   0,   255,   0,   255, 0,   128,
        0,   0,   255, 0,     255, 255, 255, 255,
    };
    Texture image = { pixels, 2, 2 };

    std::vector<u8> png = encode_png(image);
    REQUIRE(color_type(png) == 6);
    require_round_trip(image);
}

TEST_CASE("encode_png round-trips a frame-like image and compresses its flat areas", "[png]")
{
    // A flat background, a gradient and a noisy patch: every filter and long matches.
    const u32 width = 320, height = 200;
    std::vector<u8> pixels(texture_storage_size(width, height));
    u32 noise = 12345;
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            u8 *p = &pixels[((u64)y * width + x) * 4];
            p[0] = 12; p[1] = 12; p[2] = 12; p[3] = 255;

            if (x > 40 && x < 200 && y > 30 && y < 120)
            {
                p[0] = (u8)x; p[1] = (u8)y; p[2] = (u8)(x + y);
            }
            if (x > 220 && y > 100)
            {
                noise = noise * 1103515245u + 12345u;
                p[0] = (u8)(noise >> 16); p[1] = (u8)(noise >> 8); p[2] = (u8)(noise >> 24);
            }
        }
    }
    Texture image = { pixels.data(), width, height };

    require_round_trip(image);
    REQUIRE(encode_png(image).size() < pixels.size() / 4);
}

TEST_CASE("encode_png returns nothing for an image without pixels", "[png]")
{
    REQUIRE(encode_png(Texture{}).empty());
}
