#include "core.h"
#include "vector.h"

#include <cstdio>

int main(int, char**)
{
    printf("Fresh Lemonade on %s!\n", OS);

    Vec3 location = { .x = 1.0f, .y = 2.0f, .z = 3.0f };

    printf("location = Vec2(%.2f, %.2f)\n", location.xy.x, location.xy.y);
    printf("Gibibyte is %llu bytes\n", GiB);

    return 0;
}
