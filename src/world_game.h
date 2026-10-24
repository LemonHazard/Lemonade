#pragma once

#include "world.h"

// A game made of a world: levels, and the game's scripts for their entities. All it
// defines is what's in Game_Definition, and its entry point runs the world:
//
//   static const Game_Definition GAME = { "levels/first.json", SCRIPTS };
//
//   GAME_EXPORT GAME_UPDATE_AND_RENDER(game_update_and_render) { run_world(memory, frame, GAME); }

struct Game_Definition
{
    const char                  *first_level; // Loaded when the game starts
    std::span<const Script_Info> scripts;     // By Script: see level.h
};

// One frame of the world, in the game's memory: loads the first level when the game
// starts, opens the editor on the "Editor" key (see editor.h) and quits on "Quit",
// runs the entities' scripts' updates, then their late updates, lets the editor pick
// entities in the level as the camera they set sees it, and draws the level with the sky
// behind it.
void run_world(Game_Memory *memory, Game_Frame *frame, const Game_Definition &game);
