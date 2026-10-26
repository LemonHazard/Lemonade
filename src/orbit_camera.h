#pragma once

#include "core.h"
#include "vector.h"

// The editor's cameras, for its view of the game's level and its palette's: each turns
// `yaw` degrees around `focus` (0 looks down -Z), `pitch` degrees above it, from
// `distance` away. They're moved as view_palette() in editor.h says.
struct Orbit_Camera
{
    Vector3 focus;
    r32     yaw;
    r32     pitch;
    r32     distance;
};

// They see this many degrees up and down.
constexpr r32 ORBIT_CAMERA_FIELD_OF_VIEW = 50.0f;
