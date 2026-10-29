#pragma once

#include "core.h"
#include "model.h"

struct Animation;

// Loads binary glTF 2.0 (.glb) models, as Blender exports them.
//
// Every triangle primitive of the default scene goes into the model's one Mesh, with
// its node's transform applied, so the mesh sits as it did in the scene. Its indices
// are grouped by material, into one part per material. Read are positions, normals
// (computed when missing) and the first texture coordinates (0, 0 when missing), and
// materials' factors (with KHR_materials_emissive_strength) and base color, normal and
// emissive textures: PNG images, by file name relative to the .glb's folder, loaded
// through `textures` (with none, materials get their factors only), and alpha modes.
// Ignored are other textures, samplers, texture transforms and animations. A
// primitive without a material gets plain_material() in white, not glTF's metallic
// default. Data must be in the file's binary chunk: external buffers, sparse accessors
// and quantized positions aren't supported.
//
// The first skin a mesh of the scene has becomes the model's (see animation.h): the
// mesh's vertices stay as they are, in the skin's bind pose, its node's transform left
// out as glTF has it, with their first 4 joints and weights. Meshes with another skin, or
// none, are placed by their nodes' transforms and don't move with the skin. A skin with
// more than SKIN_MAX_JOINTS joints, with the nodes above them, is left out (logged).
//
// The vertices and indices, then the skin, then the textures new to `textures`, are
// written to `storage` (4-byte aligned); the returned Model points into it. Returns the bytes
// used, or 0 if the file can't be loaded or the mesh doesn't fit (logged), leaving
// `model` empty. A texture that can't be loaded leaves its material without it.

u64 load_glb(const char *path, Model *model, Texture_Library *textures, void *storage, u64 storage_size);
// `directory` is where the images are, "" for the working directory.
u64 load_glb_from_memory(const u8 *data, u64 size, const char *directory, Model *model, Texture_Library *textures,
                         void *storage, u64 storage_size);

// Loads the first animation of a .glb, as tools/import_animation.py writes them: its
// channels' joints are their nodes' names. Translations, rotations and scales are read
// as floats, linear and step interpolation as they are and cubic splines as linear
// between their keys; morph target weights are skipped. Its channels, then their keys,
// are written to `storage`; the returned Animation points into it. Returns the bytes
// used, or 0 if the file can't be loaded or doesn't fit (logged), leaving `animation`
// empty.
u64 load_animation_glb(const char *path, Animation *animation, void *storage, u64 storage_size);
u64 load_animation_glb_from_memory(const u8 *data, u64 size, Animation *animation, void *storage, u64 storage_size);
