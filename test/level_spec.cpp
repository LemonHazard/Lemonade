#include <catch2/catch_test_macros.hpp>

#include "file.h"
#include "level.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace
{
    // A game's scripts, as it would define them.
    enum class Spec_Script : u32
    {
        none,
        mover,
        marker,
        count,
    };

    struct Mover
    {
        r32     speed;
        Vector3 heading;
    };

    const Script_Field MOVER_FIELDS[] = {
        { "speed", Script_Field_Type::R32, offsetof(Mover, speed), 0.1f, 0.0f, 10.0f },
        { "heading", Script_Field_Type::Vector3, offsetof(Mover, heading), 0.1f, -1.0f, 1.0f },
    };

    const Script_Info SCRIPTS[(u32)Spec_Script::count] = {
        { "none" },
        { "mover", MOVER_FIELDS },
        { "marker" },
    };

    std::string level_path(const char *name)
    {
        return (std::filesystem::temp_directory_path() / name).string();
    }
}

TEST_CASE("save_level writes what load_level reads", "[level]")
{
    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    level->lighting          = default_lighting();
    level->lighting.exposure = 1.5f;

    Entity &mover  = level->entities[level->entity_count++];
    mover.position = { 1.0f, 2.0f, 3.0f };
    mover.rotation = { 10.0f, 20.0f, 30.0f };
    mover.scale    = { 2.0f, 2.0f, 2.0f };
    mover.script   = Script(Spec_Script::mover);
    script_data<Mover>(&mover)->speed   = 4.5f;
    script_data<Mover>(&mover)->heading = { 0.0f, 0.0f, -1.0f };

    Entity &marker = level->entities[level->entity_count++];
    marker.script  = Script(Spec_Script::marker);

    std::string path = level_path("lemonade_level_spec.json");
    REQUIRE(save_level(level.get(), *assets, path.c_str(), SCRIPTS));
    REQUIRE(level->saved_lighting.exposure == 1.5f);

    auto read = std::make_unique<Level>();
    load_level(read.get(), assets.get(), path.c_str(), SCRIPTS);
    std::filesystem::remove(path);

    REQUIRE(std::string(read->path) == path);
    REQUIRE(read->lighting.exposure == 1.5f);
    REQUIRE(read->saved_lighting.exposure == 1.5f);
    REQUIRE(read->entity_count == 2);

    const Entity &a = read->entities[0];
    REQUIRE(a.model == NO_MODEL);
    REQUIRE(a.position.z == 3.0f);
    REQUIRE(a.rotation.pitch == 10.0f);
    REQUIRE(a.rotation.yaw == 20.0f);
    REQUIRE(a.rotation.roll == 30.0f);
    REQUIRE(a.scale.y == 2.0f);
    REQUIRE(a.script == Script(Spec_Script::mover));
    REQUIRE(script_data<Mover>(&a)->speed == 4.5f);
    REQUIRE(script_data<Mover>(&a)->heading.z == -1.0f);

    const Entity &b = read->entities[1];
    REQUIRE(b.script == Script(Spec_Script::marker));
    REQUIRE(b.scale.x == 1.0f);
}

TEST_CASE("save_level leaves out what's 0 or its default", "[level]")
{
    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    level->lighting = default_lighting();

    Entity &mover = level->entities[level->entity_count++];
    mover.script  = Script(Spec_Script::mover);
    script_data<Mover>(&mover)->speed = 2.0f;
    level->entities[level->entity_count++] = {};

    std::string path = level_path("lemonade_level_spec_defaults.json");
    REQUIRE(save_level(level.get(), *assets, path.c_str(), SCRIPTS));
    str text = read_file(path.c_str());
    std::filesystem::remove(path);

    REQUIRE(text.find(R"({ "position": [0, 0, 0], "script": { "name": "mover", "data": { "speed": 2 } } })") != str::npos);
    REQUIRE(text.find(R"({ "position": [0, 0, 0] })") != str::npos);
    REQUIRE(text.find("rotation") == str::npos);
    REQUIRE(text.find("scale") == str::npos);
    REQUIRE(text.find("heading") == str::npos);
}

TEST_CASE("load_level leaves out scripts and data it doesn't know, and 0s what's missing", "[level]")
{
    std::string path = level_path("lemonade_level_spec_unknown.json");
    REQUIRE(write_file(path.c_str(), R"({ "entities": [
        { "position": [1, 0, 0], "script": { "name": "flyer", "data": { "speed": 3 } } },
        { "script": { "name": "mover", "data": { "speed": 3, "colour": 5 } } },
        { "script": { "name": "mover" } },
        { "model": "nothing" }
    ] })"));

    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    load_level(level.get(), assets.get(), path.c_str(), SCRIPTS);
    std::filesystem::remove(path);

    // The one with a model the level doesn't have is left out.
    REQUIRE(level->entity_count == 3);
    REQUIRE(level->entities[0].script == NO_SCRIPT);
    REQUIRE(level->entities[0].position.x == 1.0f);
    REQUIRE(level->entities[1].script == Script(Spec_Script::mover));
    REQUIRE(script_data<Mover>(&level->entities[1])->speed == 3.0f);
    REQUIRE(script_data<Mover>(&level->entities[2])->speed == 0.0f);
    REQUIRE(script_data<Mover>(&level->entities[2])->heading.y == 0.0f);
}

TEST_CASE("load_level can load the level again from its own path", "[level]")
{
    std::string path = level_path("lemonade_level_spec_again.json");
    REQUIRE(write_file(path.c_str(), R"({ "entities": [{ "position": [1, 2, 3] }] })"));

    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    load_level(level.get(), assets.get(), path.c_str(), SCRIPTS);
    level->entities[0].position.x = 9.0f;
    load_level(level.get(), assets.get(), level->path, SCRIPTS);
    std::filesystem::remove(path);

    REQUIRE(std::string(level->path) == path);
    REQUIRE(level->entity_count == 1);
    REQUIRE(level->entities[0].position.x == 1.0f);
}

TEST_CASE("find_entity finds the last entity with a script", "[level]")
{
    auto level = std::make_unique<Level>();
    REQUIRE(find_entity(*level, Script(Spec_Script::marker)) == NO_ENTITY);

    level->entity_count = 4;
    level->entities[1].script = Script(Spec_Script::marker);
    level->entities[2].script = Script(Spec_Script::mover);
    level->entities[3].script = Script(Spec_Script::marker);

    REQUIRE(find_entity(*level, Script(Spec_Script::marker)) == 3);
    REQUIRE(find_entity(*level, Script(Spec_Script::mover)) == 2);
    REQUIRE(find_entity(*level, NO_SCRIPT) == 0);
}
