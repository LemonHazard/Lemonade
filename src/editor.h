#pragma once

#include "level.h"

struct Game_Definition;
struct Game_Frame;
struct World;

// The editor: a Dear ImGui window over the game that changes its world while it runs.

// Makes the frame's ImGui context current (see imgui_host.h) and keeps the keys from
// the game while the editor's text fields have them; the "Editor" key opens and closes
// the window, which shows the frame's time, the level's file with buttons to load it
// again, save it and open the palette, the model it places, and the level's entities
// and lighting, as below.
//
// The palette (see palette.h) takes the window's place while it's open: its window has
// a header for each of the game's model folders, with a button for each group of models
// in it that shows them on their own, in the palette's level (see view_palette()). Back
// in the editor, the model picked in it is placed in the game's level (see
// edit_in_view()).
void update_editor(World *world, Game_Frame *frame, const Game_Definition &game);

// The editor in the level as it's seen, once the frame's camera is set (its
// view_projection): while the window is open, a click on the level, not on ImGui's
// windows, picks the entity under it, or none, and the picked one's model is drawn over
// the level as a wireframe of what the camera sees of it.
//
// While the editor places a model from the palette, it's drawn where the pointer is on
// the level instead, standing on its models or the ground's plane (y = 0): a click puts
// it there as a new entity and picks it, and stops placing unless Shift is down, and a
// right-click stops placing.
void edit_in_view(World *world, Game_Frame *frame);

// Whether the editor shows its palette's level instead of the game's.
bool palette_shown(const World &world);

// The palette's level as its camera sees it, while it's shown: sets the frame's camera,
// which a drag with the right button turns, the wheel brings closer and further, and
// W, A, S and D move. Pointing at a model names it, a click on it picks it, or none,
// drawn as a wireframe like the editor's, and a double-click goes back to the editor
// to place it. It's lit as the game's level is.
void view_palette(World *world, Game_Frame *frame);

// Its parts, for a window of the game's own: make ImGui's context current first.

// The level's entities: a list to pick one from, buttons that add one (a copy of the
// picked one, or the level's first model at the origin) and remove the picked one, and
// the picked one's fields, its script's data included. `selected` is the picked one's
// id: the list scrolls to it when it's picked elsewhere.
void edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts);

// The level's lighting, with a button that puts it back as it was loaded or last saved.
void edit_level_lighting(Level *level);
