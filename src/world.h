#pragma once

#include "core.h"
#include "level.h"
#include "matrix.h"
#include "orbit_camera.h"
#include "palette.h"

struct Game_Frame;
struct Game_Memory;

// A drag of the editor's gizmo (see gizmo.h), kept from frame to frame.
struct Gizmo_Drag
{
    u32     axes;    // Moved along: bits 1, 2 and 4 for x, y and z, two for a plane, none while it isn't dragged
    Vector3 from;    // Where it was when the drag started
    Vector3 grabbed; // The point on the gizmo the drag started from
};

// What a game made of levels keeps: the level it's in, the models and textures loaded
// for it, how long it's been played (not while the editor's open: the game waits then),
// the frames' times for the stats overlay, and the editor's state. Everything else that's the game's is its entities' script
// data. It's the start of the game's permanent memory, where it outlives reloads of the
// game's code (see game.h), and the models' storage follows it.
struct World
{
    bool         initialized;   // The first level is loaded (see run_world())
    r64          play_time;     // Seconds, the editor's open ones left out
    r64          editor_time;   // Seconds the editor's been open, which its wireframes breathe by

    // The frames of the last whole second, editing or not, and of this one so far.
    u32          fps;
    r32          frame_min;     // Seconds, the shortest, the average and the longest
    r32          frame_avg;
    r32          frame_max;
    u32          second_frames;
    r32          second_time;
    r32          second_min;
    r32          second_max;
    bool         stats_shown;   // The overlay shows them (see run_world())

    Level_Assets assets;
    Level        level;

    bool         editing;  // The editor's window is open, over the game, which waits
    u32          selected; // The id of the entity the editor shows
    Gizmo_Drag   gizmo;    // Moves the selected one (see edit_in_view())

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

// Counts the frame's time into the play time, or the editor's while it's open, and into
// the frames a second, with their shortest, average and longest times.
void begin_world_frame(World *world, const Game_Frame *frame);
