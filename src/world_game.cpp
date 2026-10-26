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
    update_editor(world, frame, game);

    if (frame->input->was_pressed("Quit"))
        frame->quit = true;

    // The game waits while the editor's open: its scripts aren't run, and the editor
    // sees its level through its own camera, or the palette's level through the
    // palette's. They share models, each level with its own emission for them.
    if (palette_shown(*world)) view_palette(world, frame); // May go back to the editor's view
    if (palette_shown(*world))
    {
        Level &palette = world->palette.level;
        scale_level_emission(palette, &world->assets);
        frame->render->sky_background = true;
        push_level(frame->render, palette, world->assets);
        return;
    }
    scale_level_emission(level, &world->assets);

    if (world->editing)
        view_editor(world, frame);
    else
    {
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

        // For the editor to start where the game's camera is.
        world->game_view_projection = frame->render->view_projection;
        world->game_camera_position = frame->render->camera_position;
    }

    edit_in_view(world, frame);

    frame->render->sky_background = true;
    push_level(frame->render, level, world->assets);
}
