#include <catch2/catch_test_macros.hpp>

#include "json.h"

TEST_CASE("Json::parse reads nested objects and arrays", "[json]")
{
    Json json;
    REQUIRE(Json::parse(R"( { "a": [1, -2.5e1, true, null], "b": { "c": "d" } } )", &json));

    REQUIRE(json.is_object());
    REQUIRE(json["a"].size() == 4);
    REQUIRE(json["a"][0].number == 1.0);
    REQUIRE(json["a"][1].number == -25.0);
    REQUIRE(json["a"][2].boolean);
    REQUIRE(json["a"][3].is_null());
    REQUIRE(json["b"]["c"].string == "d");
}

TEST_CASE("Json lookups that miss give null", "[json]")
{
    Json json;
    REQUIRE(Json::parse(R"({ "a": [1] })", &json));

    REQUIRE(json["missing"].is_null());
    REQUIRE(json["a"][5].is_null());
    REQUIRE(json["a"]["key"].is_null());
    REQUIRE(json["missing"][0]["deeper"].number_or(7.0) == 7.0);
}

TEST_CASE("Json::parse unescapes strings", "[json]")
{
    Json json;
    REQUIRE(Json::parse(R"(["a\"b\\c\n", "é", "😀"])", &json));

    REQUIRE(json[0].string == "a\"b\\c\n");
    REQUIRE(json[1].string == "\xC3\xA9");         // é
    REQUIRE(json[2].string == "\xF0\x9F\x98\x80"); // U+1F600
}

TEST_CASE("Json::parse rejects malformed JSON", "[json]")
{
    Json json;
    REQUIRE_FALSE(Json::parse("{ \"a\": 1, }", &json));
    REQUIRE_FALSE(Json::parse("[1, 2", &json));
    REQUIRE_FALSE(Json::parse("\"unterminated", &json));
    REQUIRE_FALSE(Json::parse("1 2", &json));
    REQUIRE_FALSE(Json::parse("", &json));
    REQUIRE(json.is_null());
}
