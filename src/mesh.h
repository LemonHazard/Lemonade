#pragma once

#include "core.h"
#include "vector.h"

// Triangle meshes, as the game hands them to the renderer: the data stays where the
// game keeps it (its memory, usually) and is read when the frame is drawn.

struct Mesh_Vertex
{
    Vector3 position;
    Vector3 normal;   // Unit length
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
