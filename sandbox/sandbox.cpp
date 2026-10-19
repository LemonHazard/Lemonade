#include "game.h"
#include "logger.h"

#include <imgui.h>

// The game the engine runs on its own: draws the test triangle, logs jumps, shows
// ImGui's demo window on F1 and quits on Escape. Edit it while it runs and rebuild
// Sandbox to see the change live.

struct Sandbox_State
{
    u32  jump_count;
    bool imgui_demo;
};

GAME_EXPORT GAME_UPDATE_AND_RENDER(game_update_and_render)
{
    auto *state = (Sandbox_State *)memory->permanent;
    const Input *input = frame->input;
    ImGui::SetCurrentContext(frame->imgui);

    if (frame->reloaded)
        log_info("Sandbox code loaded, %u jumps so far", state->jump_count);

    if (input->was_pressed("Quit"))
        frame->quit = true;

    if (input->was_pressed("Jump"))
    {
        state->jump_count += 1;
        log_info("Jump #%u at (%.0f, %.0f)", state->jump_count, input->mouse_x, input->mouse_y);
    }

    if (input->was_pressed("ImGuiDemo"))
        state->imgui_demo = !state->imgui_demo;
    if (state->imgui_demo)
        ImGui::ShowDemoWindow(&state->imgui_demo);

    push_test_triangle(frame->render);
}
