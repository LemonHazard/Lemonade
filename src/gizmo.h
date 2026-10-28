#pragma once

#include "core.h"
#include "vector.h"

struct Entity;
struct Level;
struct Level_Assets;
struct Render_Commands;

// The editor's gizmo to move what it picked: drawn with ImGui, over the level and under
// ImGui's windows, at the same size on the screen however far it is. It has an arrow
// along each of the world's axes, x red, y green and z blue, which a drag moves along,
// and a square between each two, on the camera's side, which a drag moves on their
// plane. What the pointer's on is yellow. With Ctrl down, what's moved snaps to every
// half meter. With Alt down, its bounds snap to touch the nearest of the level's
// visible models the way it's been dragged along each axis, as a model placed from the
// palette has its bottom on what's under the pointer, when that's within half the
// gizmo's length on the screen.

// Its drag is kept from frame to frame in a Gizmo_Drag, in the World (see world.h):
// it's defined there, as this file's code uses ImGui, which the World's users needn't.
struct Gizmo_Drag;

// The gizmo at the `moved` entity of the `level`, as the frame's camera sees it
// (`render`'s view_projection and camera_position), and its drag, which moves it.
// `on_level` is false when the pointer's on ImGui's windows or off the level, and `from`
// and `along` are the ray under it otherwise. Make ImGui's context current first.
// Returns true while the pointer's on it or drags it, when a click is the gizmo's.
bool move_with_gizmo(Gizmo_Drag *drag, Entity *moved, const Level &level, const Level_Assets &assets,
                     const Render_Commands &render, bool on_level, Vector3 from, Vector3 along);
