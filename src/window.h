#pragma once

#include "core.h"
#include "input.h"

class Native_Window;

class Window
{
public:
    Window(const wstr &title, u32 width, u32 height);
    ~Window();

    void show();
    void read_input();

    // Size in the window system's coordinates: pixels on Windows (the process is
    // per-monitor DPI aware), logical units on Wayland, where they are scaled.
    u32  width;
    u32  height;
    // Size in pixels: what the renderer draws.
    u32  framebuffer_width;
    u32  framebuffer_height;
    // Content scale of the display the window is on, e.g. 1.25 at 125%.
    r32  scale;
    bool closed;

    // Keyboard and mouse input since the previous read_input(); pass it to
    // Input::update() each frame.
    Input_State input_state = {};

    Native_Window *native;
};
