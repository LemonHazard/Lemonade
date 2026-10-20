#pragma once

#include "core.h"
#include "vector.h"

// Triangle meshes, as the game hands them to the renderer: the data stays where the
// game keeps it (its memory, usually). Like textures, the renderer copies a mesh to the
// GPU the first time it's drawn and keeps it there for the next frames, recognizing it
// by its vertices' and indices' addresses and counts: data changed in place isn't
// picked up.

struct Mesh_Vertex
{
    Vector3 position;
    Vector3 normal;   // Unit length
    Vector2 uv;       // Texture coordinates: (0, 0) is the texture's top-left corner
};

struct Mesh
{
    Mesh_Vertex *vertices;
    u32         *indices;      // Three per triangle, counter-clockwise from the front
    u32          vertex_count;
    u32          index_count;
};

// Bytes for a mesh's vertices and indices, laid out as load_glb() stores them.
inline u64 mesh_storage_size(u32 vertex_count, u32 index_count)
{
    return (u64)vertex_count * sizeof(Mesh_Vertex) + (u64)index_count * sizeof(u32);
}

// A square on the XZ plane around the origin, facing up (+Y), with a texture repeating
// every `tile_size` across it. Its vertices and indices are written to `storage`; the
// mesh points into it. Returns the bytes used, or 0 when they don't fit, leaving `mesh`
// empty.
inline u64 build_plane(Mesh *mesh, float half_size, float tile_size, void *storage, u64 storage_size)
{
    *mesh = {};
    u64 needed = mesh_storage_size(4, 6);
    if (needed > storage_size) return 0;

    mesh->vertex_count = 4;
    mesh->index_count  = 6;
    mesh->vertices     = (Mesh_Vertex *)storage;
    mesh->indices      = (u32 *)(mesh->vertices + mesh->vertex_count);

    const Vector3 up      = { 0.0f, 1.0f, 0.0f };
    const float   repeats = 2.0f * half_size / tile_size;
    mesh->vertices[0] = { { -half_size, 0.0f, -half_size }, up, { 0.0f,    0.0f    } };
    mesh->vertices[1] = { { -half_size, 0.0f,  half_size }, up, { 0.0f,    repeats } };
    mesh->vertices[2] = { {  half_size, 0.0f,  half_size }, up, { repeats, repeats } };
    mesh->vertices[3] = { {  half_size, 0.0f, -half_size }, up, { repeats, 0.0f    } };

    const u32 indices[6] = { 0, 1, 2, 0, 2, 3 };
    for (u32 i = 0; i < 6; ++i) mesh->indices[i] = indices[i];

    return needed;
}

// Flat triangles drawn over the frame, for UI: (0, 0) is its top-left corner and +Y goes
// down (see Render_Command_Draw_Ui for the units). Unlike meshes, the renderer copies
// them every frame they're drawn, since UI changes from one frame to the next.

struct Ui_Vertex
{
    Vector2 position;
    Vector2 uv;    // Texture coordinates: (0, 0) is the texture's top-left corner
    u32     color; // 8-bit RGBA, red in the lowest byte: sRGB-encoded like textures, alpha not premultiplied
};

struct Ui_Mesh
{
    Ui_Vertex *vertices;
    u32       *indices;  // Three per triangle, either way around: they aren't culled
    u32        vertex_count;
    u32        index_count;
};
