#pragma once

#include "core.h"
#include "vector.h"

// How a frame's meshes are lit: a sun, and light from the sky and the ground that
// reaches everything. Colors are linear and include their intensity, so they can go
// past 1: each is what a white, rough, non-metallic surface facing it reflects. The
// frame is tonemapped to fit the display, with Khronos' PBR Neutral: colors below 0.8
// stay nearly as they are, brighter ones are compressed.

struct Lighting
{
    Vector3 sun_direction; // Towards the sun, in world space; needn't be unit length
    Vector3 sun_color;
    Vector3 sky_color;     // Ambient light on surfaces facing up (+Y)
    Vector3 ground_color;  // Ambient light on surfaces facing down; blended in between
    r32     exposure;      // Scales all of the frame's light before it's tonemapped

    // The sun casts shadows within this sphere, in world space, and nowhere else: a
    // bigger one covers more of the scene with blurrier shadows. Its center can follow
    // the camera or the player. A radius of 0 turns shadows off.
    Vector3 shadow_center;
    r32     shadow_radius;
};

// A sun up and to the right, behind a camera looking down -Z, a dim sky, no shadows.
inline Lighting default_lighting()
{
    Lighting lighting;
    lighting.sun_direction = { 0.5f, 1.0f, 0.7f };
    lighting.sun_color     = { 0.85f, 0.85f, 0.85f };
    lighting.sky_color     = { 0.18f, 0.18f, 0.18f };
    lighting.ground_color  = { 0.02f, 0.02f, 0.02f };
    lighting.exposure      = 1.0f;
    lighting.shadow_center = {};
    lighting.shadow_radius = 0.0f;
    return lighting;
}
