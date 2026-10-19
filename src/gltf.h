#pragma once

#include "core.h"
#include "mesh.h"

// Loads binary glTF 2.0 (.glb) models, as Blender exports them.
//
// Every triangle primitive of the default scene goes into one Mesh, with its node's
// transform applied, so the mesh sits as it did in the scene. Only positions, normals
// (computed when missing) and the first texture coordinates (0, 0 when missing) are
// read; materials, textures, animations and skins are ignored. Data must be in the file's binary chunk: external
// buffers, sparse accessors and quantized positions aren't supported.
//
// The vertices and indices are written to `storage` (4-byte aligned); the returned
// Mesh points into it. Returns the bytes used, or 0 if the file can't be loaded or
// the mesh doesn't fit (logged), leaving `mesh` empty.

u64 load_glb(const char *path, Mesh *mesh, void *storage, u64 storage_size);
u64 load_glb_from_memory(const u8 *data, u64 size, Mesh *mesh, void *storage, u64 storage_size);
