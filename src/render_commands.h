#pragma once

#include "core.h"
#include "vector.h"
#include "matrix.h"
#include "mesh.h"
#include "texture.h"
#include "material.h"
#include "model.h"
#include "lighting.h"

#include <cstddef>

// What the game wants drawn in a frame. The game pushes commands into a buffer the
// engine owns, and the engine renders them after the game's update, so the game
// doesn't depend on the renderer.

enum class Render_Command_Type : u32
{
    Test_Triangle,
    Draw_Mesh,
    Draw_Ui,
    Update_Texture,
};

// Every command starts with this header; `size` covers the header and the command's
// data after it, so the next command starts `size` bytes later.
struct Render_Command_Header
{
    Render_Command_Type type;
    u32                 size;
};

struct Render_Command_Draw_Mesh
{
    Render_Command_Header header;
    // Read when the frame that first draws it is drawn, after the game's update
    // returns: the vertices and indices must still be there then (in the game's
    // memory, not on its stack). Later frames draw the renderer's copy (see mesh.h).
    Mesh     mesh;
    u32      first_index; // The triangles drawn: index_count of the mesh's indices from first_index
    u32      index_count;
    Matrix4  transform;   // Model to world
    Material material;    // Its textures are read then too
};

// Drawn over the frame once it's lit and tonemapped, in the order they're pushed, with
// no depth or light: the texture's colors times the vertices', blended by their alpha.
struct Render_Command_Draw_Ui
{
    Render_Command_Header header;
    // Read when the frame is drawn, after the game's update returns: the vertices and
    // indices must still be there then.
    Ui_Mesh  mesh;
    u32      first_index; // The triangles drawn: index_count of the mesh's indices from first_index
    u32      index_count;
    Texture  texture;     // None draws the vertices' colors alone
    Vector4  clip;        // Nothing is drawn outside of it: left, top, right and bottom (x, y, z, w)
    r32      scale;       // Pixels per unit of the positions and the clip rectangle: 1 for pixels
};

// The texture's pixels changed in place: the renderer copies them again before its next
// draw, in this frame or later, so the frame's draws all see the new pixels. Until the
// copy is done (big textures take a few frames, see texture.h) what's drawn with it waits.
struct Render_Command_Update_Texture
{
    Render_Command_Header header;
    Texture  texture;
};

struct Render_Commands
{
    // For every mesh in the frame, reset by the engine each frame.
    Vector4  clear_color;     // Linear color
    bool     sky_background;  // Draws the lighting's sky and sun behind everything, over the clear color (off by default)
    Matrix4  view_projection; // World to clip space (identity by default)
    Vector3  camera_position; // World space, for light reflected towards it; the origin by default
    Lighting lighting;        // default_lighting() by default

    u8      *buffer;
    u64      capacity;
    u64      used;
    u32      dropped; // Commands that didn't fit this frame: the engine warns about them
};

// Reserves a command of `size` bytes, header included, and fills in the header.
// Returns nullptr when the buffer is full: the command is dropped and counted.
inline Render_Command_Header *push_render_command(Render_Commands *commands, Render_Command_Type type, u32 size)
{
    constexpr u64 alignment = alignof(std::max_align_t);
    u64 aligned_size = ((u64)size + alignment - 1) & ~(alignment - 1);

    if (commands->capacity - commands->used < aligned_size)
    {
        ++commands->dropped;
        return nullptr;
    }

    auto *header = (Render_Command_Header *)(commands->buffer + commands->used);
    header->type = type;
    header->size = (u32)aligned_size;
    commands->used += aligned_size;

    return header;
}

inline void push_test_triangle(Render_Commands *commands)
{
    push_render_command(commands, Render_Command_Type::Test_Triangle, sizeof(Render_Command_Header));
}

// Draws the mesh's triangles from first_index on, index_count of them (all by default).
inline void push_mesh(Render_Commands *commands, const Mesh &mesh, const Matrix4 &transform, const Material &material,
                      u32 first_index = 0, u32 index_count = UINT32_MAX)
{
    auto *command = (Render_Command_Draw_Mesh *)
        push_render_command(commands, Render_Command_Type::Draw_Mesh, sizeof(Render_Command_Draw_Mesh));
    if (!command) return;

    command->mesh        = mesh;
    command->first_index = first_index;
    command->index_count = index_count == UINT32_MAX ? mesh.index_count - first_index : index_count;
    command->transform   = transform;
    command->material    = material;
}

inline void push_mesh(Render_Commands *commands, const Mesh &mesh, const Matrix4 &transform, const Vector4 &color,
                      const Texture &texture = {})
{
    push_mesh(commands, mesh, transform, plain_material(color, texture));
}

// Draws the UI mesh's triangles from first_index on, index_count of them (all by default),
// over the frame and inside `clip`, with positions and `clip` in units of `scale` pixels.
inline void push_ui(Render_Commands *commands, const Ui_Mesh &mesh, const Texture &texture, const Vector4 &clip,
                    r32 scale = 1.0f, u32 first_index = 0, u32 index_count = UINT32_MAX)
{
    auto *command = (Render_Command_Draw_Ui *)
        push_render_command(commands, Render_Command_Type::Draw_Ui, sizeof(Render_Command_Draw_Ui));
    if (!command) return;

    command->mesh        = mesh;
    command->first_index = first_index;
    command->index_count = index_count == UINT32_MAX ? mesh.index_count - first_index : index_count;
    command->texture     = texture;
    command->clip        = clip;
    command->scale       = scale;
}

inline void push_texture_update(Render_Commands *commands, const Texture &texture)
{
    auto *command = (Render_Command_Update_Texture *)
        push_render_command(commands, Render_Command_Type::Update_Texture, sizeof(Render_Command_Update_Texture));
    if (!command) return;

    command->texture = texture;
}

// Every part of the model, each with its material.
inline void push_model(Render_Commands *commands, const Model &model, const Matrix4 &transform)
{
    for (u32 i = 0; i < model.part_count; ++i)
    {
        const Model_Part &part = model.parts[i];
        push_mesh(commands, model.mesh, transform, part.material, part.first_index, part.index_count);
    }
}
