#pragma once

#include "core.h"
#include "input.h"
#include "render_commands.h"

struct ImGuiContext;

// The interface between the engine and the game.
//
// The game is a shared library with one entry point, game_update_and_render(), which
// the engine calls every frame. The engine reloads the library whenever it is
// rebuilt, so code changes show up in the running game without restarting it. For
// that to work:
//   - the game keeps its state in Game_Memory, which the engine owns and keeps
//     across reloads: the library's own globals and statics start over on reload;
//   - nothing kept in Game_Memory may point into the library, since all of it moves
//     on reload: function pointers, vtables (classes with virtual functions,
//     std::function), string literals and other constants;
//   - types kept in Game_Memory must keep their layout: adding a field to the game
//     state needs a restart (or code that migrates the old state).
//
// The game doesn't call into the engine: it gets the input and returns render
// commands. The engine code it uses (input queries, logging, math, Dear ImGui) is
// compiled into the library; ImGui's state is the engine's (see imgui_host.h).

struct Game_Memory
{
    // Zeroed at startup and kept across reloads: the game's state lives here.
    void *permanent;
    u64   permanent_size;
    // Zeroed at startup and kept too, for what the game can rebuild: scratch space,
    // caches.
    void *transient;
    u64   transient_size;
};

struct Game_Frame
{
    // Set by the engine
    Input           *input;    // The game may disable it for the frame (see Input::enabled)
    r32              dt;       // Seconds since the previous frame, at most 0.1
    u32              width;    // Framebuffer size in pixels
    u32              height;
    bool             reloaded; // The game's code was loaded or reloaded since the previous frame
    Render_Commands *render;   // Empty, with defaults for the frame's camera and lighting
    ImGuiContext    *imgui;    // Dear ImGui's, in its frame, for tools: make it current first (see imgui_host.h)

    // Set by the game
    bool             quit;     // Closes the game after this frame
};

#define GAME_UPDATE_AND_RENDER(name) void name(Game_Memory *memory, Game_Frame *frame)
typedef GAME_UPDATE_AND_RENDER(Game_Update_And_Render);

#define GAME_UPDATE_AND_RENDER_SYMBOL "game_update_and_render"

// The game defines its entry point as
//   GAME_EXPORT GAME_UPDATE_AND_RENDER(game_update_and_render) { ... }
#ifdef OS_WINDOWS
    #define GAME_EXPORT extern "C" __declspec(dllexport)
#else
    #define GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif
