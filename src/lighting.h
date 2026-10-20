#pragma once

#include "core.h"
#include "vector.h"

struct Json;

// How a frame's meshes are lit: a sun, and the light of the sky and the ground all
// around, which they also reflect. Colors are linear and include their intensity, so
// they can go past 1: each is what a white, rough, non-metallic surface facing only it
// reflects. The frame is tonemapped to fit the display, with Khronos' PBR Neutral:
// colors below 0.8 stay nearly as they are, brighter ones are compressed.

struct Lighting
{
    Vector3 sun_direction; // Towards the sun, in world space; needn't be unit length
    Vector3 sun_color;
    // The sky fades from horizon_color to sky_color straight up (+Y); the ground,
    // below the horizon, is ground_color.
    Vector3 sky_color;
    Vector3 horizon_color;
    Vector3 ground_color;
    r32     exposure;      // Scales all of the frame's light before it's tonemapped
    r32     bloom;         // How much the frame's light spreads around: 0.04 makes what's far
                           // brighter than around it glow, 0 turns it off

    // The sun casts shadows as far as this from the camera, fading out there: further
    // makes each shadow map texel cover more, so shadows get blurrier. 0 turns them off.
    r32     shadow_distance;
};

// A sun up and to the right, behind a camera looking down -Z, a dim even sky, no shadows.
inline Lighting default_lighting()
{
    Lighting lighting;
    lighting.sun_direction = { 0.5f, 1.0f, 0.7f };
    lighting.sun_color     = { 0.85f, 0.85f, 0.85f };
    lighting.sky_color     = { 0.18f, 0.18f, 0.18f };
    lighting.horizon_color = { 0.18f, 0.18f, 0.18f };
    lighting.ground_color  = { 0.02f, 0.02f, 0.02f };
    lighting.exposure      = 1.0f;
    lighting.bloom         = 0.0f;
    lighting.shadow_distance = 0.0f;
    return lighting;
}

// As a JSON object with the fields by name, colors and directions as [x, y, z]. Read
// back, a field the object doesn't have is default_lighting()'s.
Lighting lighting_from_json(const Json &json);
Json     lighting_to_json(const Lighting &lighting);
