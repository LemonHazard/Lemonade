#include "game.h"
#include "logger.h"

// The game the engine runs on its own: draws the test triangle, logs jumps and quits
// on Escape. Edit it while it runs and rebuild Sandbox to see the change live.

struct Sandbox_State
{
    u32 jump_count;
};

GAME_EXPORT GAME_UPDATE_AND_RENDER(game_update_and_render)
{
    auto *state = (Sandbox_State *)memory->permanent;
    const Input *input = frame->input;

    if (frame->reloaded)
        log_info("Sandbox code loaded, %u jumps so far", state->jump_count);

    if (input->was_pressed("Quit"))
        frame->quit = true;

    if (input->was_pressed("Jump"))
    {
        state->jump_count += 1;
        log_info("Jump #%u at (%.0f, %.0f)", state->jump_count, input->mouse_x, input->mouse_y);
    }

    push_test_triangle(frame->render);
}
