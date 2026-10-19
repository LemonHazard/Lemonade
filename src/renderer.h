#pragma once

#include "core.h"
#include "vector.h"
#include "matrix.h"
#include "mesh.h"
#include "texture.h"
#include "render_commands.h"

class Window;
class Native_Renderer;

class Renderer
{
public:
    Renderer(Window &window);
    ~Renderer();

    // Returns false when there is nothing to draw this frame (e.g. the window is
    // minimized or the swapchain is being recreated); skip drawing and end_frame().
    // Takes the clear color, camera and lighting from `commands`, for the whole frame;
    // its commands are drawn by the caller, with the calls below.
    bool begin_frame(const Render_Commands &commands);
    void draw_test_triangle();
    // Draws index_count of the mesh's indices from first_index on, with the material
    // (see material.h; its textures mipmapped and repeated), lit by the frame's
    // lighting, shadowed by the meshes drawn in the frame (when its shadows are on),
    // depth-tested and back-face culled. Nothing is drawn while the mesh or a texture
    // is still on its way to the GPU, which can take a few frames after their first draw.
    void draw_mesh(const Mesh &mesh, u32 first_index, u32 index_count, const Matrix4 &transform, const Material &material);
    void end_frame();
    // Ends the frame like end_frame(), then waits for the GPU to finish it and reads
    // back what it drew, for screenshots: 8-bit RGBA, sRGB-encoded as displayed, rows
    // from the top, in memory the renderer keeps until the next capture. Returns no
    // pixels when the frame can't be read back (logged); it's presented all the same.
    Texture end_frame_and_capture();

    Native_Renderer *native;
};
