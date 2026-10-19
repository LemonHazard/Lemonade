#pragma once

#include "core.h"
#include "vector.h"
#include "matrix.h"
#include "mesh.h"
#include "texture.h"

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
    // Lit by a fixed directional light, depth-tested and back-face culled. Its color
    // is `color` times `texture` (white when it has no pixels), mipmapped and repeated.
    void draw_mesh(const Mesh &mesh, const Matrix4 &transform, const Matrix4 &view_projection, const Vector4 &color,
                   const Texture &texture);
    void end_frame();

    Native_Renderer *native;
};
