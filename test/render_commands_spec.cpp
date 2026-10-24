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

TEST_CASE("push_ui records the triangles to draw, all of them by default", "[render_commands]")
{
    alignas(std::max_align_t) u8 buffer[512];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    Ui_Vertex vertices[4] = {};
    u32       indices[6]  = { 0, 1, 2, 0, 2, 3 };
    u8        pixels[4]   = {};
    Ui_Mesh   mesh        = { vertices, indices, 4, 6 };
    Texture   texture     = { pixels, 1, 1 };

    push_ui(&commands, mesh, texture, { 1.0f, 2.0f, 3.0f, 4.0f });
    push_ui(&commands, mesh, {}, { 0.0f, 0.0f, 8.0f, 8.0f }, 1.5f, 3, 3);

    auto *all = (Render_Command_Draw_Ui *)buffer;
    REQUIRE(all->header.type == Render_Command_Type::Draw_Ui);
    REQUIRE(all->mesh.vertices == vertices);
    REQUIRE(all->first_index == 0);
    REQUIRE(all->index_count == 6);
    REQUIRE(all->texture.pixels == pixels);
    REQUIRE(all->clip.z == 3.0f);
    REQUIRE(all->scale == 1.0f);

    auto *second = (Render_Command_Draw_Ui *)(buffer + all->header.size);
    REQUIRE(second->first_index == 3);
    REQUIRE(second->index_count == 3);
    REQUIRE(second->texture.pixels == nullptr);
    REQUIRE(second->scale == 1.5f);
}

TEST_CASE("push_texture_update records the texture", "[render_commands]")
{
    alignas(std::max_align_t) u8 buffer[128];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    u8 pixels[16] = {};
    push_texture_update(&commands, { pixels, 2, 2 });

    auto *command = (Render_Command_Update_Texture *)buffer;
    REQUIRE(command->header.type == Render_Command_Type::Update_Texture);
    REQUIRE(command->texture.pixels == pixels);
    REQUIRE(command->texture.width == 2);
    REQUIRE(commands.used == command->header.size);
}

TEST_CASE("push_wireframe records the triangles to draw, all of them by default", "[render_commands]")
{
    alignas(std::max_align_t) u8 buffer[512];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    Mesh_Vertex vertices[4] = {};
    u32         indices[6]  = { 0, 1, 2, 0, 2, 3 };
    Mesh        mesh        = { vertices, indices, 4, 6 };

    push_wireframe(&commands, mesh, translation({ 1.0f, 2.0f, 3.0f }), { 1.0f, 0.5f, 0.0f, 1.0f });
    push_wireframe(&commands, mesh, identity(), {}, 3, 3);

    auto *all = (Render_Command_Draw_Wireframe *)buffer;
    REQUIRE(all->header.type == Render_Command_Type::Draw_Wireframe);
    REQUIRE(all->mesh.vertices == vertices);
    REQUIRE(all->first_index == 0);
    REQUIRE(all->index_count == 6);
    REQUIRE(all->transform.at(1, 3) == 2.0f);
    REQUIRE(all->color.g == 0.5f);

    auto *second = (Render_Command_Draw_Wireframe *)(buffer + all->header.size);
    REQUIRE(second->first_index == 3);
    REQUIRE(second->index_count == 3);
    REQUIRE(commands.used == all->header.size + second->header.size);
}
