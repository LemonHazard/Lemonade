#pragma once

#include "core.h"
#include "vector.h"
#include "matrix.h"
#include "mesh.h"

#include <cstddef>

// What the game wants drawn in a frame. The game pushes commands into a buffer the
// engine owns, and the engine renders them after the game's update, so the game
// doesn't depend on the renderer.

enum class Render_Command_Type : u32
{
    Test_Triangle,
    Draw_Mesh,
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
    // Read when the frame is drawn, after the game's update returns: the vertices and
    // indices must still be there then (in the game's memory, not on its stack).
    Mesh    mesh;
    Matrix4 transform; // Model to world
    Vector4 color;     // Linear color
};

struct Render_Commands
{
    Vector4 clear_color;     // Linear color
    Matrix4 view_projection; // World to clip space, for every mesh in the frame (identity by default)

    u8  *buffer;
    u64  capacity;
    u64  used;
};

// Reserves a command of `size` bytes, header included, and fills in the header.
// Returns nullptr when the buffer is full: the command is dropped.
inline Render_Command_Header *push_render_command(Render_Commands *commands, Render_Command_Type type, u32 size)
{
    constexpr u64 alignment = alignof(std::max_align_t);
    u64 aligned_size = ((u64)size + alignment - 1) & ~(alignment - 1);

    if (commands->capacity - commands->used < aligned_size)
        return nullptr;

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

inline void push_mesh(Render_Commands *commands, const Mesh &mesh, const Matrix4 &transform, const Vector4 &color)
{
    auto *command = (Render_Command_Draw_Mesh *)
        push_render_command(commands, Render_Command_Type::Draw_Mesh, sizeof(Render_Command_Draw_Mesh));
    if (!command) return;

    command->mesh      = mesh;
    command->transform = transform;
    command->color     = color;
}
