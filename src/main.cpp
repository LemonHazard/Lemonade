#include "core.h"
#include "arena.h"
#include "vector.h"
#include "window.h"
#include "input.h"
#include "key_value_file.h"
#include "renderer.h"
#include "game_library.h"
#include "logger.h"

#include <chrono>

// Set by the build, see lemonade.lua.
#ifndef GAME_LIBRARY
    #error "GAME_LIBRARY must name the game library, e.g. \"My_Game\" for libMy_Game.so"
#endif
#ifndef GAME_TITLE
    #define GAME_TITLE "Lemonade"
#endif

#define WIDEN_(text) L ## text
#define WIDEN(text)  WIDEN_(text)

constexpr u64 GAME_PERMANENT_MEMORY_SIZE = 256 * MiB;
constexpr u64 GAME_TRANSIENT_MEMORY_SIZE = 256 * MiB;
constexpr u64 RENDER_COMMANDS_SIZE       = 1 * MiB;

static void draw(Renderer &renderer, const Render_Commands &commands)
{
    for (u64 at = 0; at < commands.used;)
    {
        auto *header = (const Render_Command_Header *)(commands.buffer + at);

        switch (header->type)
        {
            case Render_Command_Type::Test_Triangle:
                renderer.draw_test_triangle();
                break;

            case Render_Command_Type::Draw_Mesh:
            {
                auto *command = (const Render_Command_Draw_Mesh *)header;
                renderer.draw_mesh(command->mesh, command->transform, commands.view_projection, command->color);
                break;
            }
        }

        if (header->size == 0)
            break;
        at += header->size;
    }
}

int main(int, char**)
{
    log_info("Fresh Lemonade on %s!", OS_NAME);

    Window window(WIDEN(GAME_TITLE), 1920, 1080);
    Renderer renderer(window);
    window.show();

    Input input;
    input.use_bindings(Key_Value_File::read("key_bindings.kvl"));

    // Allocated once, so the game's state outlives reloads of its code.
    Arena arena(GAME_PERMANENT_MEMORY_SIZE + GAME_TRANSIENT_MEMORY_SIZE + RENDER_COMMANDS_SIZE);

    Game_Memory memory = {};
    memory.permanent_size = GAME_PERMANENT_MEMORY_SIZE;
    memory.permanent      = arena.push<u8>(GAME_PERMANENT_MEMORY_SIZE);
    memory.transient_size = GAME_TRANSIENT_MEMORY_SIZE;
    memory.transient      = arena.push<u8>(GAME_TRANSIENT_MEMORY_SIZE);

    Render_Commands render_commands = {};
    render_commands.capacity = RENDER_COMMANDS_SIZE;
    render_commands.buffer   = arena.push<u8>(RENDER_COMMANDS_SIZE);

    if (!memory.permanent || !memory.transient || !render_commands.buffer)
    {
        log_error("Can't allocate the game's memory");
        return 1;
    }

    Game_Library game(GAME_LIBRARY);

    // Linear color: on an sRGB swapchain 0.0037 displays as #0C0C0C.
    const Vector4 clear_color = { 0.0037f, 0.0037f, 0.0037f, 1.0f };

    auto previous_time = std::chrono::steady_clock::now();

    while (!window.closed)
    {
        bool reloaded = game.reload_if_changed();

        window.read_input();
        input.update(window.input_state);

        auto time = std::chrono::steady_clock::now();

        render_commands.clear_color     = clear_color;
        render_commands.view_projection = identity();
        render_commands.used        = 0;

        Game_Frame frame = {};
        frame.input    = &input;
        frame.dt       = std::chrono::duration<r32>(time - previous_time).count();
        frame.width    = window.framebuffer_width;
        frame.height   = window.framebuffer_height;
        frame.reloaded = reloaded;
        frame.render   = &render_commands;

        previous_time = time;

        game.update_and_render(&memory, &frame);

        if (frame.quit) window.closed = true;

        if (renderer.begin_frame(render_commands.clear_color))
        {
            draw(renderer, render_commands);
            renderer.end_frame();
        }
    }

    return 0;
}
