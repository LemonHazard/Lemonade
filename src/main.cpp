#include "core.h"
#include "vector.h"
#include "window.h"
#include "logger.h"

int main(int, char**)
{
    log_info("Fresh Lemonade on %s!", OS);

    Window window = { 
        .title  = L"Lemonade",
        .width  = 1920,
        .height = 1080,
    };
    window_create(&window);
    window_show(&window);

    while (!window.closed)
    {
        window_read_input(&window);
    }

    window_destroy(&window);

    return 0;
}
