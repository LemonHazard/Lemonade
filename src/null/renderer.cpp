#ifdef RENDERER_NULL

#include "../renderer.h"
#include "../logger.h"

// Renderer that draws nothing: the default where no backend is implemented yet
// (Windows, macOS), and usable for headless runs.

class Native_Renderer
{
};

Renderer::Renderer(Window &)
{
    this->native = new Native_Renderer();
    log_warn("Null renderer: nothing is drawn");
}

Renderer::~Renderer()
{
    delete this->native;
}

bool Renderer::begin_frame(const Render_Commands &)
{
    return false;
}

void Renderer::draw_test_triangle()
{
}

void Renderer::draw_mesh(const Mesh &, u32, u32, const Matrix4 &, const Material &)
{
}

void Renderer::draw_ui(const Ui_Mesh &, u32, u32, const Texture &, const Vector4 &, r32)
{
}

void Renderer::draw_wireframe(const Mesh &, u32, u32, const Matrix4 &, const Vector4 &)
{
}

void Renderer::update_texture(const Texture &)
{
}

void Renderer::forget_memory(const void *, const void *)
{
}

void Renderer::end_frame()
{
}

Texture Renderer::end_frame_and_capture()
{
    return {};
}

#endif // RENDERER_NULL
