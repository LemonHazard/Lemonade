#pragma once

#include "level.h"

#include <span>
#include <string_view>
#include <vector>

// The editor's palette: the models in a folder and the folders under it, to look
// through a group of them at a time, laid out in a level of their own, and pick one to
// place in the game's level.

// A group has this many models at most: a bigger one is split.
constexpr u32 PALETTE_GROUP_SIZE = 64;

// The palette's camera sees this many degrees up and down.
constexpr r32 PALETTE_FIELD_OF_VIEW = 50.0f;

// Models in a folder that have the same category, as Synty's POLYGON models name it:
// their names are kind_Category_..., "Wep" for SM_Wep_Sword_01 and "Hair" for
// Chr_Hair_13_Static, or the whole name when it has one part. Those of a category past
// PALETTE_GROUP_SIZE are split by name into "Wep 1", "Wep 2"...
struct Palette_Group
{
    str              name;
    std::vector<str> files; // The .glb files' paths, by name
};

struct Palette_Folder
{
    str                        name;   // Its path in the palette's folder, or the folder's own for it
    std::vector<Palette_Group> groups; // By category
};

// The folders in `folder` that have .glb files in them, `folder` included, by path,
// their files grouped: read from the disk on each call.
std::vector<Palette_Folder> read_palette(const char *folder);

// The category a model's name groups it by: see Palette_Group.
std::string_view palette_category(std::string_view name);

// What the palette shows, kept in the world (see world.h) while the editor's open.
struct Palette
{
    bool    open;                   // Its window is open, and the world shows its level instead of the game's
    Level   level;                  // The group's models, on the ground
    char    group[LEVEL_PATH_SIZE]; // Its folder and name: none before one is shown
    u32     ground;                 // The id of the ground's entity in the level: NO_ID when it has none
    u32     selected;               // The id of the model picked in it

    // Its camera turns `yaw` degrees around `focus` (0 looks down -Z), `pitch` degrees
    // above it, from `distance` away.
    Vector3 focus;
    r32     yaw;
    r32     pitch;
    r32     distance;
};

// Makes the palette's level the models in `files`, loaded into `assets` if they weren't,
// on the model in `ground` (none when null) at the origin, lit by `lighting`. They stand
// on the ground (y = 0) in rows from -Z to +Z, left to right in each, with room
// around each for its size, the rows around the origin. Its camera looks at all of them.
void show_palette_models(Palette *palette, Level_Assets *assets, std::span<const str> files, const char *ground,
                         const Lighting &lighting);
