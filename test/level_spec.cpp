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
        u32     follows; // An entity's id
    };

    const Script_Field MOVER_FIELDS[] = {
        { "speed", Script_Field_Type::R32, offsetof(Mover, speed), 0.1f, 0.0f, 10.0f },
        { "heading", Script_Field_Type::Vector3, offsetof(Mover, heading), 0.1f, -1.0f, 1.0f },
        { "follows", Script_Field_Type::Entity, offsetof(Mover, follows) },
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

    Entity &mover  = *add_entity(level.get(), {});
    Entity &marker = *add_entity(level.get(), {});
    mover.position = { 1.0f, 2.0f, 3.0f };
    mover.rotation = { 10.0f, 20.0f, 30.0f };
    mover.scale    = { 2.0f, 2.0f, 2.0f };
    mover.script   = Script(Spec_Script::mover);
    script_data<Mover>(&mover)->speed   = 4.5f;
    script_data<Mover>(&mover)->heading = { 0.0f, 0.0f, -1.0f };
    script_data<Mover>(&mover)->follows = marker.id;
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
    REQUIRE(read->last_id == 2);

    const Entity &a = read->entities[0];
    REQUIRE(a.id == mover.id);
    REQUIRE(a.model == NO_MODEL);
    REQUIRE(a.position.z == 3.0f);
    REQUIRE(a.rotation.pitch == 10.0f);
    REQUIRE(a.rotation.yaw == 20.0f);
    REQUIRE(a.rotation.roll == 30.0f);
    REQUIRE(a.scale.y == 2.0f);
    REQUIRE(a.script == Script(Spec_Script::mover));
    REQUIRE(script_data<Mover>(&a)->speed == 4.5f);
    REQUIRE(script_data<Mover>(&a)->heading.z == -1.0f);
    REQUIRE(entity_with_id(*read, script_data<Mover>(&a)->follows) == &read->entities[1]);

    const Entity &b = read->entities[1];
    REQUIRE(b.id == marker.id);
    REQUIRE(b.script == Script(Spec_Script::marker));
    REQUIRE(b.scale.x == 1.0f);
}

TEST_CASE("save_level leaves out what's 0 or its default", "[level]")
{
    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    level->lighting = default_lighting();

    Entity &mover = *add_entity(level.get(), {});
    mover.script  = Script(Spec_Script::mover);
    script_data<Mover>(&mover)->speed = 2.0f;
    add_entity(level.get(), {});

    std::string path = level_path("lemonade_level_spec_defaults.json");
    REQUIRE(save_level(level.get(), *assets, path.c_str(), SCRIPTS));
    str text = read_file(path.c_str());
    std::filesystem::remove(path);

    REQUIRE(text.find(R"({ "id": 1, "position": [0, 0, 0], "script": { "name": "mover", "data": { "speed": 2 } } })") != str::npos);
    REQUIRE(text.find(R"({ "id": 2, "position": [0, 0, 0] })") != str::npos);
    REQUIRE(text.find("rotation") == str::npos);
    REQUIRE(text.find("scale") == str::npos);
    REQUIRE(text.find("heading") == str::npos);
    REQUIRE(text.find("follows") == str::npos);
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

TEST_CASE("load_level gives new ids to entities without one or with another's", "[level]")
{
    std::string path = level_path("lemonade_level_spec_ids.json");
    REQUIRE(write_file(path.c_str(), R"({ "entities": [
        { "id": 7 },
        { },
        { "id": 3 },
        { "id": 7 },
        { "id": -2 },
        { "id": 4, "script": { "name": "mover", "data": { "follows": 3 } } },
        { "script": { "name": "mover", "data": { "follows": "seven" } } }
    ] })"));

    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    load_level(level.get(), assets.get(), path.c_str(), SCRIPTS);
    std::filesystem::remove(path);

    REQUIRE(level->entity_count == 7);
    REQUIRE(level->entities[0].id == 7);
    REQUIRE(level->entities[1].id == 8);
    REQUIRE(level->entities[2].id == 3);
    REQUIRE(level->entities[3].id == 9);
    REQUIRE(level->entities[4].id == 10);
    REQUIRE(level->entities[5].id == 4);
    REQUIRE(level->entities[6].id == 11);
    REQUIRE(level->last_id == 11);

    REQUIRE(entity_with_id(*level, script_data<Mover>(&level->entities[5])->follows) == &level->entities[2]);
    REQUIRE(script_data<Mover>(&level->entities[6])->follows == NO_ID);
}

TEST_CASE("add_entity gives each entity a new id, kept as others are removed", "[level]")
{
    auto level = std::make_unique<Level>();
    level->last_id = 5;

    Entity entity   = {};
    entity.id       = 2;
    entity.position = { 1.0f, 0.0f, 0.0f };
    Entity *added   = add_entity(level.get(), entity);
    REQUIRE(added == &level->entities[0]);
    REQUIRE(added->id == 6);
    REQUIRE(added->position.x == 1.0f);
    REQUIRE(add_entity(level.get(), {})->id == 7);

    REQUIRE(entity_with_id(level.get(), 7) == &level->entities[1]);
    REQUIRE(entity_with_id(level.get(), 2) == nullptr);
    REQUIRE(entity_with_id(level.get(), NO_ID) == nullptr);

    level->entity_count = LEVEL_MAX_ENTITIES;
    REQUIRE(add_entity(level.get(), {}) == nullptr);
}

TEST_CASE("entity_on_ray finds the nearest entity whose model is in the way", "[level]")
{
    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    alignas(4) u8 storage[256];
    build_plane(&assets->models.models[0].mesh, 1.0f, 1.0f, storage, sizeof(storage));
    assets->model_bounds[0] = mesh_bounds(assets->models.models[0].mesh);
    assets->models.count    = 1;

    // Two squares, one above the other, and one turned upright to the side.
    Entity &low  = *add_entity(level.get(), {});
    Entity &high = *add_entity(level.get(), {});
    Entity &wall = *add_entity(level.get(), {});
    low.model     = high.model = wall.model = 0;
    high.position = { 0.0f, 2.0f, 0.0f };
    wall.position = { 5.0f, 0.0f, 0.0f };
    wall.rotation = { 0.0f, 0.0f, 90.0f };
    wall.scale    = { 3.0f, 1.0f, 3.0f };
    add_entity(level.get(), {}); // No model: never in the way

    r32 distance = 0.0f;
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, &distance) == high.id);
    REQUIRE(distance == 8.0f);
    REQUIRE(entity_on_ray(*level, *assets, { 0.5f, 1.0f, 0.5f }, { 0.0f, -1.0f, 0.0f }) == low.id);
    // From under, through their backs.
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, -10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == low.id);
    // Behind where the ray starts.
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == NO_ID);
    REQUIRE(entity_on_ray(*level, *assets, { 2.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, &distance) == NO_ID);
    REQUIRE(distance == INFINITY);
    // Turned upright and 3 times as big: from -3 to 3 up and across.
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, 2.5f, 2.5f }, { 4.0f, 0.0f, 0.0f }) == wall.id);
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, 3.5f, 2.5f }, { 4.0f, 0.0f, 0.0f }) == NO_ID);

    wall.scale = { 0.0f, 1.0f, 3.0f };
    REQUIRE(entity_on_ray(*level, *assets, { 0.0f, 2.5f, 2.5f }, { 4.0f, 0.0f, 0.0f }) == NO_ID);
}

TEST_CASE("scale_level_emission scales the models' emission as loaded by the level's", "[level]")
{
    auto level  = std::make_unique<Level>();
    auto assets = std::make_unique<Level_Assets>();
    assets->models.count                = 1;
    assets->models.models[0].part_count = 1;
    assets->model_emissive[0][0]        = { 1.0f, 0.5f, 0.0f };
    level->models[0]                    = { "lamp", 0, 4.0f };
    level->model_count                  = 1;

    scale_level_emission(*level, assets.get());
    Vector3 emissive = assets->models.models[0].parts[0].material.emissive;
    REQUIRE(emissive.x == 4.0f);
    REQUIRE(emissive.y == 2.0f);
    REQUIRE(emissive.z == 0.0f);

    // Another level's scale for it, then this one's again.
    level->models[0].emissive_scale = 1.0f;
    scale_level_emission(*level, assets.get());
    REQUIRE(assets->models.models[0].parts[0].material.emissive.x == 1.0f);
}
