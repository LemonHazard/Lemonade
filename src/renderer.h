#pragma once

#include "core.h"
#include "vector.h"

class Window;
class Native_Renderer;

class Renderer
{
public:
    Renderer(Window &window);
    ~Renderer();

    // Returns false when there is nothing to draw this frame (e.g. the window is
    // minimized or the swapchain is being recreated); skip drawing and end_frame().
    bool begin_frame(const Vector4 &clear_color);
    void draw_test_triangle();
    void end_frame();

    Native_Renderer *native;
};
