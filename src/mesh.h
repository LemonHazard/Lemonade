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
