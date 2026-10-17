#include "core.h"
#include "vector.h"
#include "window.h"
#include "renderer.h"
#include "logger.h"

int main(int, char**)
{
    log_info("Fresh Lemonade on %s!", OS_NAME);

    Window window(L"Lemonade", 1920, 1080);
    Renderer renderer(window);
    window.show();

    // Linear color: on an sRGB swapchain 0.0037 displays as #0C0C0C.
    const Vector4 clear_color = { 0.0037f, 0.0037f, 0.0037f, 1.0f };

    while (!window.closed)
    {
        window.read_input();

        if (renderer.begin_frame(clear_color))
        {
            renderer.draw_test_triangle();
            renderer.end_frame();
        }
    }

    return 0;
}
