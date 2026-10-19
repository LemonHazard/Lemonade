#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "matrix.h"

using Catch::Approx;

TEST_CASE("orthographic maps its box to Vulkan clip space", "[matrix]")
{
    // Looking down -Z: the near plane is at z = -1, the far one at z = -5.
    Matrix4 m = orthographic(-2.0f, 4.0f, -1.0f, 3.0f, 1.0f, 5.0f);

    Vector3 near_bottom_left = transform_point(m, { -2.0f, -1.0f, -1.0f });
    REQUIRE(near_bottom_left.x == Approx(-1.0f));
    REQUIRE(near_bottom_left.y == Approx(1.0f)); // +Y down
    REQUIRE(near_bottom_left.z == Approx(0.0f));

    Vector3 far_top_right = transform_point(m, { 4.0f, 3.0f, -5.0f });
    REQUIRE(far_top_right.x == Approx(1.0f));
    REQUIRE(far_top_right.y == Approx(-1.0f));
    REQUIRE(far_top_right.z == Approx(1.0f));
}

TEST_CASE("orthographic takes a near plane behind the camera", "[matrix]")
{
    Matrix4 m = orthographic(-1.0f, 1.0f, -1.0f, 1.0f, -3.0f, 3.0f);

    REQUIRE(transform_point(m, { 0.0f, 0.0f,  3.0f }).z == Approx(0.0f));
    REQUIRE(transform_point(m, { 0.0f, 0.0f,  0.0f }).z == Approx(0.5f));
    REQUIRE(transform_point(m, { 0.0f, 0.0f, -3.0f }).z == Approx(1.0f));
}

TEST_CASE("inverse undoes a camera's view and projection", "[matrix]")
{
    Matrix4 m = perspective(0.9f, 1.5f, 0.1f, 100.0f) * look_at({ 3.0f, 4.0f, 5.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    Matrix4 product = inverse(m) * m;

    for (u32 row = 0; row < 4; ++row)
        for (u32 column = 0; column < 4; ++column)
            REQUIRE(product.at(row, column) == Approx(row == column ? 1.0f : 0.0f).margin(1e-4));
}

TEST_CASE("inverse leaves matrices without one as they are", "[matrix]")
{
    Matrix4 flat = scaling({ 1.0f, 0.0f, 1.0f });
    REQUIRE(inverse(flat).at(0, 0) == 1.0f);
    REQUIRE(inverse(flat).at(1, 1) == 0.0f);
}
