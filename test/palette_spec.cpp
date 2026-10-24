#include <catch2/catch_test_macros.hpp>

#include "file.h"
#include "palette.h"

#include <filesystem>

TEST_CASE("palette_category is the second part of a model's name", "[palette]")
{
    REQUIRE(palette_category("SM_Wep_Sword_01") == "Wep");
    REQUIRE(palette_category("Chr_Hair_13_Static") == "Hair");
    REQUIRE(palette_category("FX_Sphere") == "Sphere");
    REQUIRE(palette_category("cube") == "cube");
    REQUIRE(palette_category("odd_") == "odd_");
}

TEST_CASE("read_palette groups each folder's models by their categories", "[palette]")
{
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "lemonade_palette_spec";
    fs::remove_all(root);
    fs::create_directories(root / "pack" / "empty");

    // Only .glb files are read, not their contents.
    write_file((root / "cube.glb").string().c_str(), "");
    write_file((root / "notes.txt").string().c_str(), "");
    write_file((root / "pack" / "SM_Wep_Sword_01.glb").string().c_str(), "");
    write_file((root / "pack" / "SM_Wep_Axe_01.glb").string().c_str(), "");
    write_file((root / "pack" / "SM_Env_Tree_01.glb").string().c_str(), "");
    for (u32 i = 0; i < PALETTE_GROUP_SIZE + 2; ++i)
    {
        char name[64];
        snprintf(name, sizeof(name), "SM_Prop_Thing_%03u.glb", i);
        write_file((root / "pack" / name).string().c_str(), "");
    }

    std::vector<Palette_Folder> folders = read_palette(root.string().c_str());
    fs::remove_all(root);

    REQUIRE(folders.size() == 2);
    REQUIRE(folders[0].name == "lemonade_palette_spec");
    REQUIRE(folders[0].groups.size() == 1);
    REQUIRE(folders[0].groups[0].name == "cube");
    REQUIRE(folders[0].groups[0].files == std::vector<str>{ (root / "cube.glb").generic_string() });

    const std::vector<Palette_Group> &groups = folders[1].groups;
    REQUIRE(folders[1].name == "pack");
    REQUIRE(groups.size() == 4);
    REQUIRE(groups[0].name == "Env");
    REQUIRE(groups[1].name == "Prop 1");
    REQUIRE(groups[2].name == "Prop 2");
    REQUIRE(groups[3].name == "Wep");

    // Split evenly, by name.
    REQUIRE(groups[1].files.size() == PALETTE_GROUP_SIZE / 2 + 1);
    REQUIRE(groups[2].files.size() == PALETTE_GROUP_SIZE / 2 + 1);
    REQUIRE(file_stem(groups[1].files.front()) == "SM_Prop_Thing_000");
    REQUIRE(file_stem(groups[2].files.back()) == "SM_Prop_Thing_065");
    REQUIRE(file_stem(groups[3].files[0]) == "SM_Wep_Axe_01");
    REQUIRE(file_stem(groups[3].files[1]) == "SM_Wep_Sword_01");
}
