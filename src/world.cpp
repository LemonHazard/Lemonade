#include "world.h"
#include "game.h"

#include <algorithm>

World *game_world(Game_Memory *memory)
{
    auto *world = (World *)memory->permanent;
    if (!world->assets.storage)
    {
        u64 world_size             = (sizeof(World) + 15) & ~(u64)15;
        world->assets.storage      = (u8 *)memory->permanent + world_size;
        world->assets.storage_size = memory->permanent_size - world_size;
    }
    return world;
}

void begin_world_frame(World *world, const Game_Frame *frame)
{
    if (world->editing)
        world->editor_time += frame->dt;
    else
        world->play_time += frame->dt;
    world->second_min     = world->second_frames ? std::min(world->second_min, frame->dt) : frame->dt;
    world->second_max     = std::max(world->second_max, frame->dt);
    world->second_frames += 1;
    world->second_time   += frame->dt;
    if (world->second_time >= 1.0f)
    {
        world->fps           = world->second_frames;
        world->frame_min     = world->second_min;
        world->frame_avg     = world->second_time / (r32)world->second_frames;
        world->frame_max     = world->second_max;
        world->second_frames = 0;
        world->second_max    = 0.0f;
        world->second_time  -= 1.0f;
    }
}
