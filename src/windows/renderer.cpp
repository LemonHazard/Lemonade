#ifdef OS_WINDOWS

#include "../renderer.h"
#include "../logger.h"

// Placeholder until the DirectX 11 renderer: nothing is drawn.

class Native_Renderer
{
};

Renderer::Renderer(Window &)
{
    this->native = new Native_Renderer();
    log_warn("Renderer is not implemented on Windows yet");
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

#endif // OS_WINDOWS
