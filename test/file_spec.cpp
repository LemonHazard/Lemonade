#include <catch2/catch_test_macros.hpp>

#include "file.h"

#include <filesystem>

TEST_CASE("write_file and read_file round-trip any bytes", "[file]")
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lemonade_file_spec.bin";
    str data = "line\n";
    data.push_back('\0');
    data += "after a zero";

    REQUIRE(write_file(path.string().c_str(), data));
    REQUIRE(read_file(path.string().c_str()) == data);

    // Replaced, not appended to.
    REQUIRE(write_file(path.string().c_str(), "short"));
    REQUIRE(read_file(path.string().c_str()) == "short");

    std::filesystem::remove(path);
}

TEST_CASE("read_file gives nothing for a file that isn't there", "[file]")
{
    REQUIRE(read_file("lemonade_file_spec_missing/none.bin").empty());
    REQUIRE_FALSE(write_file("lemonade_file_spec_missing/none.bin", "data"));
}
