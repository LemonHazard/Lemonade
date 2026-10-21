#pragma once

#include "level.h"

struct Game_Frame;
struct World;

// The editor: a Dear ImGui window over the game that changes its world while it runs.

// Makes the frame's ImGui context current (see imgui_host.h) and keeps the keys from
// the game while the editor's text fields have them; the "Editor" key opens and closes
// the window, which shows the frame's time, the level's file with buttons to load it
// again and save it, and the level's entities and lighting, as below.
void update_editor(World *world, Game_Frame *frame, std::span<const Script_Info> scripts);

// Its parts, for a window of the game's own: make ImGui's context current first.

// The level's entities: a list to pick one from, buttons that add one (a copy of the
// picked one, or the level's first model at the origin) and remove the picked one, and
// the picked one's fields, its script's data included. `selected` is the picked one's
// id.
void edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts);

// The level's lighting, with a button that puts it back as it was loaded or last saved.
void edit_level_lighting(Level *level);
