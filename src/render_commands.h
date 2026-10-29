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
    Draw_Wireframe,
    Forget_Memory,
    Data,
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
    u32      depth_order; // Where surfaces meet at the same depth, the higher one is seen: entities give their ids
    // Drawn instead of the mesh's vertices when set, as many of them, for this frame only:
    // a skinned model's, posed (see push_posed_model()). The renderer copies them every
    // frame, so they can be anywhere that lasts till the frame's drawn: in the frame's
    // commands, usually (see push_render_data()). The mesh's indices are drawn with them.
    const Mesh_Vertex *posed;
};

// The edges of a mesh's triangles, drawn over the frame once it's lit and tonemapped,
// under the UI, where the mesh is seen, as an editor shows what it picked. The mesh must
// be drawn in the frame too, with the same transform: what's drawn is found by the
// depth it left.
struct Render_Command_Draw_Wireframe
{
    Render_Command_Header header;
    // Read when the frame is drawn, like Draw_Mesh's (see mesh.h).
    Mesh     mesh;
    u32      first_index; // The triangles drawn: index_count of the mesh's indices from first_index
    u32      index_count;
    Matrix4  transform;   // Model to world
    Vector4  color;       // sRGB-encoded like UI colors, alpha not premultiplied
    u32      depth_order; // The mesh's, as it was drawn
    const Mesh_Vertex *posed; // The mesh's, as it was drawn (see Render_Command_Draw_Mesh)
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

// The game's memory from `begin` to `end` is used for other things from this frame on,
// as models loaded there are let go of: the meshes and textures drawn from it are
// forgotten before the frame is drawn, wherever the command is in it, so the frame's
// draws may already have new data there. Their GPU copies are freed, once the frames
// still drawn with them are done.
struct Render_Command_Forget_Memory
{
    Render_Command_Header header;
    const void *begin;
    const void *end;
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

// `size` bytes for the frame's other commands to point to, as posed vertices: they're
// there until the frame's drawn. Returns nullptr when the buffer is full (counted as a
// dropped command).
inline void *push_render_data(Render_Commands *commands, u64 size)
{
    u64 header = (sizeof(Render_Command_Header) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
    if (size > UINT32_MAX - header)
    {
        ++commands->dropped;
        return nullptr;
    }

    auto *data = (u8 *)push_render_command(commands, Render_Command_Type::Data, (u32)(header + size));
    return data ? data + header : nullptr;
}

inline void push_test_triangle(Render_Commands *commands)
{
    push_render_command(commands, Render_Command_Type::Test_Triangle, sizeof(Render_Command_Header));
}

// Draws the mesh's triangles from first_index on, index_count of them (all by default),
// over others at the same depth when its depth_order is higher (see Render_Command_Draw_Mesh).
// With `posed` vertices, they're drawn instead of the mesh's (see Render_Command_Draw_Mesh).
inline void push_mesh(Render_Commands *commands, const Mesh &mesh, const Matrix4 &transform, const Material &material,
                      u32 first_index = 0, u32 index_count = UINT32_MAX, u32 depth_order = 0,
                      const Mesh_Vertex *posed = nullptr)
{
    auto *command = (Render_Command_Draw_Mesh *)
        push_render_command(commands, Render_Command_Type::Draw_Mesh, sizeof(Render_Command_Draw_Mesh));
    if (!command) return;

    command->mesh        = mesh;
    command->first_index = first_index;
    command->index_count = index_count == UINT32_MAX ? mesh.index_count - first_index : index_count;
    command->transform   = transform;
    command->material    = material;
    command->depth_order = depth_order;
    command->posed       = posed;
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

// Draws the edges of the mesh's triangles from first_index on, index_count of them (all
// by default), of the mesh drawn with `depth_order` and `posed`.
inline void push_wireframe(Render_Commands *commands, const Mesh &mesh, const Matrix4 &transform, const Vector4 &color,
                           u32 first_index = 0, u32 index_count = UINT32_MAX, u32 depth_order = 0,
                           const Mesh_Vertex *posed = nullptr)
{
    auto *command = (Render_Command_Draw_Wireframe *)
        push_render_command(commands, Render_Command_Type::Draw_Wireframe, sizeof(Render_Command_Draw_Wireframe));
    if (!command) return;

    command->mesh        = mesh;
    command->first_index = first_index;
    command->index_count = index_count == UINT32_MAX ? mesh.index_count - first_index : index_count;
    command->transform   = transform;
    command->color       = color;
    command->depth_order = depth_order;
    command->posed       = posed;
}

inline void push_texture_update(Render_Commands *commands, const Texture &texture)
{
    auto *command = (Render_Command_Update_Texture *)
        push_render_command(commands, Render_Command_Type::Update_Texture, sizeof(Render_Command_Update_Texture));
    if (!command) return;

    command->texture = texture;
}

inline void push_forget_memory(Render_Commands *commands, const void *begin, const void *end)
{
    auto *command = (Render_Command_Forget_Memory *)
        push_render_command(commands, Render_Command_Type::Forget_Memory, sizeof(Render_Command_Forget_Memory));
    if (!command) return;

    command->begin = begin;
    command->end   = end;
}

// Every part of the model, each with its material, `posed` as push_mesh()'s.
inline void push_model(Render_Commands *commands, const Model &model, const Matrix4 &transform, u32 depth_order = 0,
                       const Mesh_Vertex *posed = nullptr)
{
    for (u32 i = 0; i < model.part_count; ++i)
    {
        const Model_Part &part = model.parts[i];
        push_mesh(commands, model.mesh, transform, part.material, part.first_index, part.index_count, depth_order, posed);
    }
}
