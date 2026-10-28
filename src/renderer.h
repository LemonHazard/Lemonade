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
    // depth-tested and back-face culled. Blended materials are drawn at the end of the
    // frame, over the rest, from the farthest to the nearest. Where it meets another
    // surface at the same depth, the one with the higher depth_order is seen: it's drawn
    // a hair nearer, by depth_order steps of the depth buffer's precision. Nothing is drawn while the mesh or a texture
    // is still on its way to the GPU, which can take a few frames after their first draw.
    void draw_mesh(const Mesh &mesh, u32 first_index, u32 index_count, const Matrix4 &transform, const Material &material,
                   u32 depth_order = 0);
    // Draws index_count of the UI mesh's indices from first_index on over the frame,
    // after it's tonemapped, in the order of the calls (see Render_Command_Draw_Ui).
    // The vertices and indices are copied right away. Not drawn while the texture is
    // on its way to the GPU, like meshes.
    void draw_ui(const Ui_Mesh &mesh, u32 first_index, u32 index_count, const Texture &texture, const Vector4 &clip,
                 r32 scale);
    // Draws the edges of index_count of the mesh's triangles from first_index on, in
    // `color` (see Render_Command_Draw_Wireframe) over the frame once it's tonemapped,
    // under the UI, unlit: only where the mesh is seen, as drawn in the frame with the
    // same transform and depth_order. Lines are a pixel wide, and half that on the mesh's outline.
    // Nothing is drawn where the device can't tell where fragments are in their
    // triangles (logged), or while the mesh is on its way to the GPU.
    void draw_wireframe(const Mesh &mesh, u32 first_index, u32 index_count, const Matrix4 &transform, const Vector4 &color,
                        u32 depth_order = 0);
    // The texture's pixels changed: they're copied again before it's next drawn. Also
    // between frames, when begin_frame() returned false.
    void update_texture(const Texture &texture);
    // The game's memory from `begin` to `end` is used for other things from now on: the
    // meshes and textures drawn from it are forgotten, their GPU copies freed, so what's
    // put there next is copied anew. Waits for the GPU to finish the frames it's drawing
    // when it has any to forget. Between frames only, before begin_frame().
    void forget_memory(const void *begin, const void *end);
    void end_frame();
    // Ends the frame like end_frame(), then waits for the GPU to finish it and reads
    // back what it drew, for screenshots: 8-bit RGBA, sRGB-encoded as displayed, rows
    // from the top, in memory the renderer keeps until the next capture. Returns no
    // pixels when the frame can't be read back (logged); it's presented all the same.
    Texture end_frame_and_capture();

    Native_Renderer *native;
};
