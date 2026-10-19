#include "core.h"
#include "arena.h"
#include "vector.h"
#include "window.h"
#include "input.h"
#include "key_value_file.h"
#include "renderer.h"
#include "game_library.h"
#include "png.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

// Set by the build, see lemonade.lua.
#ifndef GAME_LIBRARY
    #error "GAME_LIBRARY must name the game library, e.g. \"My_Game\" for libMy_Game.so"
#endif
#ifndef GAME_TITLE
    #define GAME_TITLE "Lemonade"
#endif

#define WIDEN_(text) L ## text
#define WIDEN(text)  WIDEN_(text)

constexpr u64 GAME_PERMANENT_MEMORY_SIZE = 1 * GiB;
constexpr u64 GAME_TRANSIENT_MEMORY_SIZE = 256 * MiB;
constexpr u64 RENDER_COMMANDS_SIZE       = 1 * MiB;
// Longer frames (a debugger pause, a slow reload, a dragged window) reach the game as
// this long: it slows down for a frame instead of jumping.
constexpr r32 MAX_FRAME_DT               = 0.1f;

// Where the Screenshot action saves frames, relative to the working directory.
#define SCREENSHOT_DIR "screenshots"

// Command line options.
struct Options
{
    // Saves the `screenshot_frame`-th frame drawn (from 1) there as a PNG and quits:
    // to see what the game draws without anyone looking at the window.
    const char *screenshot_path  = nullptr;
    u32         screenshot_frame = 1;
};

static void print_usage()
{
    std::printf("Usage: %s [--screenshot <path.png> [--screenshot-frame <n>]]\n"
                "  --screenshot <path.png>   save the first frame drawn as a PNG, then quit\n"
                "  --screenshot-frame <n>    save the n-th frame drawn instead (default 1)\n",
                GAME_TITLE);
}

static bool parse_options(int argc, char **argv, Options *options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char *option = argv[i];
        const char *value  = i + 1 < argc ? argv[i + 1] : nullptr;

        if (strcmp(option, "--screenshot") == 0 && value)
        {
            options->screenshot_path = value;
            ++i;
        }
        else if (strcmp(option, "--screenshot-frame") == 0 && value && std::atoi(value) > 0)
        {
            options->screenshot_frame = (u32)std::atoi(value);
            ++i;
        }
        else
        {
            log_error("Unknown or incomplete option \"%s\"", option);
            return false;
        }
    }
    return true;
}

// screenshots/<date>_<time>.png, down to the millisecond.
static str screenshot_file_name()
{
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    int milliseconds = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);

    std::tm local = {};
#ifdef OS_WINDOWS
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%d_%H-%M-%S", &local);

    char name[64];
    std::snprintf(name, sizeof(name), SCREENSHOT_DIR "/%s_%03d.png", date, milliseconds);
    return name;
}

// Ends the frame, saving what it drew to `path`.
static bool end_frame_with_screenshot(Renderer &renderer, const char *path)
{
    Texture frame = renderer.end_frame_and_capture();
    if (!frame.pixels)
    {
        log_error("Could not take a screenshot");
        return false;
    }
    return save_png(path, frame);
}

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
                renderer.draw_mesh(command->mesh, command->transform, command->color, command->texture);
                break;
            }
        }

        if (header->size == 0)
            break;
        at += header->size;
    }
}

int main(int argc, char **argv)
{
    Options options;
    if (!parse_options(argc, argv, &options))
    {
        print_usage();
        return 1;
    }

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

    u64  frames_drawn            = 0;
    int  exit_code               = 0;
    bool dropped_commands_logged = false; // Warned once, like the renderer's limits

    while (!window.closed)
    {
        bool reloaded = game.reload_if_changed();

        window.read_input();
        input.update(window.input_state);

        auto time = std::chrono::steady_clock::now();

        render_commands.clear_color     = clear_color;
        render_commands.view_projection = identity();
        render_commands.camera_position = {};
        render_commands.lighting        = default_lighting();
        render_commands.used            = 0;
        render_commands.dropped         = 0;

        Game_Frame frame = {};
        frame.input    = &input;
        frame.dt       = std::min(std::chrono::duration<r32>(time - previous_time).count(), MAX_FRAME_DT);
        frame.width    = window.framebuffer_width;
        frame.height   = window.framebuffer_height;
        frame.reloaded = reloaded;
        frame.render   = &render_commands;

        previous_time = time;

        game.update_and_render(&memory, &frame);

        if (frame.quit) window.closed = true;

        if (render_commands.dropped && !dropped_commands_logged)
        {
            log_warn("%u render commands didn't fit in the %llu KiB command buffer and weren't drawn",
                     render_commands.dropped, (unsigned long long)(RENDER_COMMANDS_SIZE / KiB));
            dropped_commands_logged = true;
        }

        // The engine's own action, bound in key_bindings.kvl like the game's.
        bool screenshot_pressed = input.was_pressed("Screenshot");

        if (renderer.begin_frame(render_commands))
        {
            draw(renderer, render_commands);
            ++frames_drawn;

            if (options.screenshot_path && frames_drawn == options.screenshot_frame)
            {
                if (!end_frame_with_screenshot(renderer, options.screenshot_path)) exit_code = 1;
                window.closed = true;
            }
            else if (screenshot_pressed)
            {
                std::error_code error;
                std::filesystem::create_directories(SCREENSHOT_DIR, error);
                end_frame_with_screenshot(renderer, screenshot_file_name().c_str());
            }
            else
            {
                renderer.end_frame();
            }
        }
    }

    return exit_code;
}
