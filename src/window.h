#pragma once

#include "core.h"

struct Native_Window;

struct Window {
    wstr title;
    u32  width;
    u32  height;
    bool closed;

    Native_Window *native;
};

void window_create(Window *window);
void window_show(Window *window);
void window_read_input(Window *window);
void window_destroy(Window *window);
