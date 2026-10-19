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
    // Nothing is drawn while the mesh or the texture is still on its way to the GPU,
    // which can take a few frames after their first draw.
    void draw_mesh(const Mesh &mesh, const Matrix4 &transform, const Matrix4 &view_projection, const Vector4 &color,
                   const Texture &texture);
    void end_frame();
    // Ends the frame like end_frame(), then waits for the GPU to finish it and reads
    // back what it drew, for screenshots: 8-bit RGBA, sRGB-encoded as displayed, rows
    // from the top, in memory the renderer keeps until the next capture. Returns no
    // pixels when the frame can't be read back (logged); it's presented all the same.
    Texture end_frame_and_capture();

    Native_Renderer *native;
};
