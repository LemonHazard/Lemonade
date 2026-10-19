#include <catch2/catch_test_macros.hpp>

#include "render_commands.h"

TEST_CASE("push_render_command appends aligned commands", "[render_commands]")
{
    alignas(std::max_align_t) u8 buffer[256];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    Render_Command_Header *first  = push_render_command(&commands, Render_Command_Type::Test_Triangle, 12);
    Render_Command_Header *second = push_render_command(&commands, Render_Command_Type::Test_Triangle, 12);

    REQUIRE(first == (Render_Command_Header *)buffer);
    REQUIRE(first->type == Render_Command_Type::Test_Triangle);
    REQUIRE(first->size == alignof(std::max_align_t));
    REQUIRE((u8 *)second == buffer + first->size);
    REQUIRE(commands.used == first->size + second->size);
}

TEST_CASE("push_render_command drops and counts commands that don't fit", "[render_commands]")
{
    alignas(std::max_align_t) u8 buffer[2 * alignof(std::max_align_t)];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    push_test_triangle(&commands);
    push_test_triangle(&commands);

    REQUIRE(commands.dropped == 0);
    REQUIRE(push_render_command(&commands, Render_Command_Type::Test_Triangle, 1) == nullptr);
    push_test_triangle(&commands);
    REQUIRE(commands.used == sizeof(buffer));
    REQUIRE(commands.dropped == 2);
}
