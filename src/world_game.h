#pragma once

#include "world.h"

// A game made of a world: levels, and the game's scripts for their entities. All it
// defines is what's in Game_Definition, and its entry point runs the world:
//
//   static const Game_Definition GAME = { "levels/first.json", SCRIPTS, "models", "models/ground.glb" };
//
//   GAME_EXPORT GAME_UPDATE_AND_RENDER(game_update_and_render) { run_world(memory, frame, GAME); }

struct Game_Definition
{
    const char                  *first_level;    // Loaded when the game starts
    std::span<const Script_Info> scripts;        // By Script: see level.h
    const char                  *model_folder;   // The editor's palette has the models in it and the folders under it: none when null
    const char                  *palette_ground; // A .glb the palette's models stand on, at the origin: none when null
};

// One frame of the world, in the game's memory: loads the first level when the game
// starts, opens the editor on the "Editor" key (see editor.h) and quits on "Quit",
// runs the entities' scripts' updates, then their late updates, and draws the level, as
// the camera they set sees it, with the sky behind it. While the editor's open, the game
// waits, its scripts not run and its play time stopped: the level is drawn through the
// editor's camera, which picks entities in it, or, while the editor shows its palette,
// the palette's level is drawn instead, through the palette's camera.
void run_world(Game_Memory *memory, Game_Frame *frame, const Game_Definition &game);
