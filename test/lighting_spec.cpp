#include <catch2/catch_test_macros.hpp>

#include "json.h"
#include "lighting.h"

TEST_CASE("lighting_to_json writes what lighting_from_json reads", "[lighting]")
{
    Lighting lighting = default_lighting();
    lighting.sun_direction   = { 0.1f, 0.8f, -0.3f };
    lighting.sun_color       = { 3.0f, 2.5f, 2.0f };
    lighting.sky_color       = { 0.4f, 0.5f, 0.9f };
    lighting.horizon_color   = { 0.7f, 0.7f, 0.75f };
    lighting.ground_color    = { 0.2f, 0.15f, 0.1f };
    lighting.exposure        = 1.5f;
    lighting.bloom           = 0.04f;
    lighting.shadow_distance = 60.0f;

    Json json;
    REQUIRE(Json::parse(lighting_to_json(lighting).write(), &json));
    Lighting read = lighting_from_json(json);

    REQUIRE(read.sun_direction.y == 0.8f);
    REQUIRE(read.sun_color.x == 3.0f);
    REQUIRE(read.sky_color.z == 0.9f);
    REQUIRE(read.horizon_color.z == 0.75f);
    REQUIRE(read.ground_color.y == 0.15f);
    REQUIRE(read.exposure == 1.5f);
    REQUIRE(read.bloom == 0.04f);
    REQUIRE(read.shadow_distance == 60.0f);
}

TEST_CASE("lighting_from_json keeps the defaults for what isn't there", "[lighting]")
{
    Json json;
    REQUIRE(Json::parse(R"({ "exposure": 2 })", &json));
    Lighting read     = lighting_from_json(json);
    Lighting defaults = default_lighting();

    REQUIRE(read.exposure == 2.0f);
    REQUIRE(read.sun_direction.x == defaults.sun_direction.x);
    REQUIRE(read.sky_color.y == defaults.sky_color.y);
    REQUIRE(read.shadow_distance == defaults.shadow_distance);
}
