#pragma once

#include "core.h"
#include "vector.h"

#include <cstddef>

// What the game wants drawn in a frame. The game pushes commands into a buffer the
// engine owns, and the engine renders them after the game's update, so the game
// doesn't depend on the renderer.

enum class Render_Command_Type : u32
{
    Test_Triangle,
};

// Every command starts with this header; `size` covers the header and the command's
// data after it, so the next command starts `size` bytes later.
struct Render_Command_Header
{
    Render_Command_Type type;
    u32                 size;
};

struct Render_Commands
{
    Vector4 clear_color; // Linear color

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
