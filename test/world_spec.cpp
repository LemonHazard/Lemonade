#include <catch2/catch_test_macros.hpp>

#include "game.h"
#include "world.h"

#include <cstdlib>

TEST_CASE("game_world is at the start of the game's memory, with the rest for models", "[world]")
{
    Game_Memory memory = {};
    memory.permanent_size = sizeof(World) + 1024;
    memory.permanent      = std::calloc(1, memory.permanent_size);

    World *world = game_world(&memory);
    REQUIRE((void *)world == memory.permanent);
    REQUIRE(world->assets.storage >= (u8 *)memory.permanent + sizeof(World));
    REQUIRE((uintptr_t)world->assets.storage % 16 == 0);
    REQUIRE(world->assets.storage + world->assets.storage_size == (u8 *)memory.permanent + memory.permanent_size);

    // Kept as it was set, as after a reload.
    world->assets.storage_used = 64;
    REQUIRE(game_world(&memory)->assets.storage_used == 64);

    std::free(memory.permanent);
}

TEST_CASE("begin_world_frame counts the play time and the frames in each second", "[world]")
{
    Game_Memory memory = {};
    memory.permanent_size = sizeof(World);
    memory.permanent      = std::calloc(1, memory.permanent_size);
    World *world = game_world(&memory);

    Game_Frame frame = {};
    frame.dt = 0.25f;
    for (u32 i = 0; i < 3; ++i) begin_world_frame(world, &frame);
    REQUIRE(world->fps == 0);

    begin_world_frame(world, &frame);
    REQUIRE(world->fps == 4);
    REQUIRE(world->second_frames == 0);
    REQUIRE(world->play_time == 1.0);

    std::free(memory.permanent);
}

TEST_CASE("begin_world_frame keeps the shortest, average and longest frames of the last second", "[world]")
{
    Game_Memory memory = {};
    memory.permanent_size = sizeof(World);
    memory.permanent      = std::calloc(1, memory.permanent_size);
    World *world = game_world(&memory);

    Game_Frame frame = {};
    const r32 seconds[] = { 0.25f, 0.125f, 0.5f, 0.125f };
    for (r32 dt : seconds)
    {
        frame.dt = dt;
        begin_world_frame(world, &frame);
    }
    REQUIRE(world->fps == 4);
    REQUIRE(world->frame_min == 0.125f);
    REQUIRE(world->frame_avg == 0.25f);
    REQUIRE(world->frame_max == 0.5f);

    // The next second's are its own.
    frame.dt = 0.5f;
    begin_world_frame(world, &frame);
    begin_world_frame(world, &frame);
    REQUIRE(world->fps == 2);
    REQUIRE(world->frame_min == 0.5f);
    REQUIRE(world->frame_avg == 0.5f);
    REQUIRE(world->frame_max == 0.5f);

    std::free(memory.permanent);
}

TEST_CASE("begin_world_frame stops the play time while the editor's open, counting the editor's", "[world]")
{
    Game_Memory memory = {};
    memory.permanent_size = sizeof(World);
    memory.permanent      = std::calloc(1, memory.permanent_size);
    World *world = game_world(&memory);

    Game_Frame frame = {};
    frame.dt = 0.25f;
    begin_world_frame(world, &frame);
    world->editing = true;
    begin_world_frame(world, &frame);
    begin_world_frame(world, &frame);
    REQUIRE(world->play_time == 0.25);
    REQUIRE(world->editor_time == 0.5);

    // Frames are counted all the same.
    begin_world_frame(world, &frame);
    REQUIRE(world->fps == 4);

    std::free(memory.permanent);
}
