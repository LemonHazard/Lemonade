#pragma once

#include "core.h"
#include "game.h"

#include <filesystem>

class Native_Game_Library;

// The game's shared library, next to the engine's executable. The engine runs a copy
// of it, so the build can replace the library while the game runs, and loads the new
// one as soon as it's there: live code editing.
//
// While the build links the library it keeps a <library>.lock file next to it (see
// lemonade.lua), so a half-written library is never loaded.
class Game_Library
{
public:
    // name: the library's file name without prefix and extension, e.g. "My_Game" for
    // libMy_Game.so or My_Game.dll. Nothing is loaded until reload_if_changed().
    Game_Library(const str &name);
    ~Game_Library();

    // Loads the library if it changed since it was last loaded, or tried; returns true
    // when new code was loaded. If the new library can't be loaded, the previous one
    // keeps running.
    bool reload_if_changed();

    // Never null: does nothing until the library is loaded.
    Game_Update_And_Render *update_and_render;

    Native_Game_Library *native;
};

// The platform-specific part, implemented per OS in <os>/game_library.cpp.

// Where the build puts the library: next to the executable.
std::filesystem::path game_library_path(const str &name);

// Return nullptr on failure; load_game_library() logs why.
void *load_game_library(const std::filesystem::path &path);
void *find_game_library_symbol(void *library, const char *name);
void  unload_game_library(void *library);
