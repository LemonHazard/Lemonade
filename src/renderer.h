#pragma once

#include "core.h"
#include "vector.h"
#include "matrix.h"
#include "mesh.h"

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
    // Lit by a fixed directional light, depth-tested and back-face culled.
    void draw_mesh(const Mesh &mesh, const Matrix4 &transform, const Matrix4 &view_projection, const Vector4 &color);
    void end_frame();

    Native_Renderer *native;
};
