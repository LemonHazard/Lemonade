#pragma once

#include "core.h"
#include "mesh.h"
#include "material.h"

// Models, as load_glb() loads them: one mesh, drawn in parts that each have their own
// material. Like meshes, they stay where the game keeps them, and point into its memory.

constexpr u32 MODEL_MAX_PARTS = 16;

struct Model_Part
{
    u32      first_index; // Into the mesh's indices
    u32      index_count;
    Material material;
};

struct Model
{
    Mesh       mesh;
    u32        part_count;
    Model_Part parts[MODEL_MAX_PARTS];
};

// The textures models' materials use, loaded once each however many models use them.
// It keeps their paths in itself, so it can live in the game's memory.

constexpr u32 TEXTURE_LIBRARY_SIZE = 64;
constexpr u32 TEXTURE_PATH_SIZE    = 256;

struct Texture_Library
{
    u32     count;
    char    paths[TEXTURE_LIBRARY_SIZE][TEXTURE_PATH_SIZE];
    Texture textures[TEXTURE_LIBRARY_SIZE];
};

// The PNG at `path`, loaded into `storage` the first time it's asked for. Returns the
// bytes of storage used (0 when it was already loaded); `texture` is left without
// pixels when the file can't be loaded or the library is full (logged), and stays so
// for the next asks.
u64 library_texture(Texture_Library *library, const char *path, Texture *texture, void *storage, u64 storage_size);

// Models loaded from .glb files, once each however many times they're asked for. Like
// the texture library, it keeps their paths in itself, and they keep their indices.

constexpr u32 MODEL_LIBRARY_SIZE = 256;
constexpr u32 MODEL_PATH_SIZE    = 256;
constexpr u32 NO_MODEL           = 0xFFFFFFFF;

struct Model_Library
{
    u32   count;
    char  paths[MODEL_LIBRARY_SIZE][MODEL_PATH_SIZE];
    Model models[MODEL_LIBRARY_SIZE];
};

// The .glb at `path`, loaded with load_glb() into `storage`, its textures through
// `textures`, the first time it's asked for. Returns the bytes of storage used (0 when
// it was already loaded) and sets `index` to the model's, into the library's models.
// When the file can't be loaded or the library is full (logged), `index` is NO_MODEL
// and nothing is kept: it's tried again on the next ask.
u64 library_model(Model_Library *library, const char *path, Texture_Library *textures, u32 *index, void *storage,
                  u64 storage_size);
