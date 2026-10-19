#pragma once

#include "core.h"
#include "vector.h"

// How a frame's meshes are lit: a sun, and light from the sky and the ground that
// reaches everything. Colors are linear and include their intensity, so they can go
// past 1: the frame is tonemapped to fit the display, which leaves colors below 0.8 as
// they are and compresses brighter ones.

struct Lighting
{
    Vector3 sun_direction; // Towards the sun, in world space; needn't be unit length
    Vector3 sun_color;
    Vector3 sky_color;     // Ambient light on surfaces facing up (+Y)
    Vector3 ground_color;  // Ambient light on surfaces facing down; blended in between
    r32     exposure;      // Scales all of the frame's light before it's tonemapped
};

// A sun up and to the right, behind a camera looking down -Z, and a dim sky.
inline Lighting default_lighting()
{
    Lighting lighting;
    lighting.sun_direction = { 0.5f, 1.0f, 0.7f };
    lighting.sun_color     = { 0.85f, 0.85f, 0.85f };
    lighting.sky_color     = { 0.18f, 0.18f, 0.18f };
    lighting.ground_color  = { 0.02f, 0.02f, 0.02f };
    lighting.exposure      = 1.0f;
    return lighting;
}
