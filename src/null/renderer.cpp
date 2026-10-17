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

bool Renderer::begin_frame(const Vector4 &)
{
    return false;
}

void Renderer::draw_test_triangle()
{
}

void Renderer::end_frame()
{
}

#endif // RENDERER_NULL
