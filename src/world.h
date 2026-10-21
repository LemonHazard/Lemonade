#pragma once

#include "core.h"
#include "level.h"

struct Game_Frame;
struct Game_Memory;

// What a game made of levels keeps: the level it's in, the models and textures loaded
// for it, how long it's been played, and the editor's state. Everything else that's
// the game's is its entities' script data. It's the start of the game's permanent
// memory, where it outlives reloads of the game's code (see game.h), and the models'
// storage follows it.
struct World
{
    bool         initialized;   // Set by the game once it has loaded its first level
    r64          play_time;     // Seconds
    u32          fps;           // Frames in the last whole second of play
    u32          second_frames; // Frames so far in this second
    r32          second_time;   // Seconds so far in this second

    Level_Assets assets;
    Level        level;

    bool         editing;  // The editor's window is open, over the game
    u32          selected; // The id of the entity the editor shows
};

// The world at the start of the game's permanent memory, with the rest of it for the
// models' storage.
World *game_world(Game_Memory *memory);

// Counts the frame's time into the play time and the frames a second.
void begin_world_frame(World *world, const Game_Frame *frame);
