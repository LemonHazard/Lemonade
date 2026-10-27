#include "world_game.h"
#include "editor.h"
#include "game.h"
#include "logger.h"

#include <imgui.h>

static const Script_Info *script_info(std::span<const Script_Info> scripts, Script script)
{
    return (u32)script < scripts.size() ? &scripts[(u32)script] : nullptr;
}

// In the top right corner, over the game and the editor, without taking the mouse.
static void show_stats(const World &world)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 corner = { viewport->WorkPos.x + viewport->WorkSize.x - 16.0f, viewport->WorkPos.y + 16.0f };
    ImGui::SetNextWindowPos(corner, ImGuiCond_Always, { 1.0f, 0.0f });
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Stats", nullptr, flags))
    {
        ImGui::Text("%.1f min, %.1f avg, %.1f max ms a frame", world.frame_min * 1000.0f,
                    world.frame_avg * 1000.0f, world.frame_max * 1000.0f);
        ImGui::Text("%u fps, %.0f s of play", world.fps, world.play_time);
    }
    ImGui::End();
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
    update_editor(world, frame, game); // Makes ImGui's context current

    if (frame->input->was_pressed("Stats")) world->stats_shown = !world->stats_shown;
    if (world->stats_shown) show_stats(*world);

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
