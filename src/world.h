#pragma once

#include "core.h"
#include "level.h"
#include "matrix.h"
#include "orbit_camera.h"
#include "palette.h"

struct Game_Frame;
struct Game_Memory;

// What a game made of levels keeps: the level it's in, the models and textures loaded
// for it, how long it's been played (not while the editor's open: the game waits then),
// and the editor's state. Everything else that's the game's is its entities' script
// data. It's the start of the game's permanent memory, where it outlives reloads of the
// game's code (see game.h), and the models' storage follows it.
struct World
{
    bool         initialized;   // The first level is loaded (see run_world())
    r64          play_time;     // Seconds, the editor's open ones left out
    r64          editor_time;   // Seconds the editor's been open, which its wireframes breathe by
    u32          fps;           // Frames in the last whole second, editing or not
    u32          second_frames; // Frames so far in this second
    r32          second_time;   // Seconds so far in this second

    Level_Assets assets;
    Level        level;

    bool         editing;  // The editor's window is open, over the game, which waits
    u32          selected; // The id of the entity the editor shows

    // The editor sees the game's level through its own camera, put where the game's
    // last was while its distance is 0 (see view_editor()).
    Orbit_Camera camera;
    Matrix4      game_view_projection; // The game's camera, as it last saw the level
    Vector3      game_camera_position;

    Palette      palette;  // The editor's models to place, and what it shows of them
    bool         placing;  // A click on the level places a `placed` there (see edit_in_view())
    u32          placed;   // The model, into Level_Assets::models
};

// The world at the start of the game's permanent memory, with the rest of it for the
// models' storage.
World *game_world(Game_Memory *memory);

// Counts the frame's time into the play time, or the editor's while it's open, and the
// frames a second.
void begin_world_frame(World *world, const Game_Frame *frame);
