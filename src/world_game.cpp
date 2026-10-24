#include "world_game.h"
#include "editor.h"
#include "game.h"
#include "logger.h"

static const Script_Info *script_info(std::span<const Script_Info> scripts, Script script)
{
    return (u32)script < scripts.size() ? &scripts[(u32)script] : nullptr;
}

void run_world(Game_Memory *memory, Game_Frame *frame, const Game_Definition &game)
{
    World *world = game_world(memory);
    Level &level = world->level;

    if (!world->initialized)
    {
        world->initialized = true;
        load_level(&level, &world->assets, game.first_level, game.scripts);
    }

    if (frame->reloaded)
        log_info("Game code loaded after %.1f s of play", world->play_time);

    begin_world_frame(world, frame);
    update_editor(world, frame, game.scripts);

    if (frame->input->was_pressed("Quit"))
        frame->quit = true;

    // Scripts may add or remove entities as they run: each pass takes them as they are.
    for (u32 i = 0; i < level.entity_count; ++i)
    {
        const Script_Info *script = script_info(game.scripts, level.entities[i].script);
        if (script && script->update) script->update(world, &level.entities[i], frame);
    }
    for (u32 i = 0; i < level.entity_count; ++i)
    {
        const Script_Info *script = script_info(game.scripts, level.entities[i].script);
        if (script && script->late_update) script->late_update(world, &level.entities[i], frame);
    }

    edit_in_view(world, frame);

    frame->render->sky_background = true;
    push_level(frame->render, level, world->assets);
}
