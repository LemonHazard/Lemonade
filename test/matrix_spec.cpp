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
