#pragma once

#include "core.h"

struct Native_Window;

class Window
{
public:
    Window(const wstr &title, u32 width, u32 height);
    ~Window();

    void show();
    void read_input();

    u32  width;
    u32  height;
    bool closed;

    Native_Window *native;
};
