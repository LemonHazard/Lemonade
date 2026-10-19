#include <catch2/catch_test_macros.hpp>

#include "png.h"

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
