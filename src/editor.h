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

// The editor in the level as it's seen, once the frame's camera is set (its
// view_projection): while the window is open, a click on the level, not on ImGui's
// windows, picks the entity under it, or none, and the picked one's model is drawn over
// the level as a wireframe of what the camera sees of it.
void edit_in_view(World *world, Game_Frame *frame);

// Its parts, for a window of the game's own: make ImGui's context current first.

// The level's entities: a list to pick one from, buttons that add one (a copy of the
// picked one, or the level's first model at the origin) and remove the picked one, and
// the picked one's fields, its script's data included. `selected` is the picked one's
// id: the list scrolls to it when it's picked elsewhere.
void edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts);

// The level's lighting, with a button that puts it back as it was loaded or last saved.
void edit_level_lighting(Level *level);
