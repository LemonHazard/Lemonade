#include "core.h"
#include "vector.h"
#include "window.h"
#include "logger.h"

int main(int, char**)
{
    log_info("Fresh Lemonade on %s!", OS);

    Window window(L"Lemonade", 1920, 1080);
    window.show();

    while (!window.closed)
    {
        window.read_input();
    }

    return 0;
}
