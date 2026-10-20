#include <catch2/catch_test_macros.hpp>

#include "json.h"

#include <limits>

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

TEST_CASE("Json::write puts what fits on one line", "[json]")
{
    Json json = Json::make_object();
    json.set("name", Json::make_string("barrel"));
    Json &position = json.set("position", Json::make_array());
    position.push(Json::make_number(0.0));
    position.push(Json::make_number(-2.5));
    position.push(Json::make_number(15.0));
    json.set("empty", Json::make_object());
    json.set("on", Json::make_bool(true));

    REQUIRE(json.write() == "{ \"name\": \"barrel\", \"position\": [0, -2.5, 15], \"empty\": {}, \"on\": true }\n");
}

TEST_CASE("Json::write breaks what doesn't fit, one item or member a line", "[json]")
{
    Json json = Json::make_object();
    Json &items = json.set("items", Json::make_array());
    items.push(Json::make_string("first"));
    items.push(Json::make_array()).push(Json::make_number(1.0));
    json.set("last", Json());

    REQUIRE(json.write(20) == "{\n"
                              "    \"items\": [\n"
                              "        \"first\",\n"
                              "        [1]\n"
                              "    ],\n"
                              "    \"last\": null\n"
                              "}\n");
}

TEST_CASE("Json::write writes floats and doubles as the shortest that reads back the same", "[json]")
{
    Json json = Json::make_array();
    json.push(Json::make_number(0.37f)); // A float: 0.3700000047683716 as a double
    json.push(Json::make_number(0.1));   // Not a float
    json.push(Json::make_number(std::numeric_limits<r64>::infinity()));

    REQUIRE(json.write() == "[0.37, 0.1, null]\n");
}

TEST_CASE("Json::write escapes strings, and what it writes parses back", "[json]")
{
    Json json = Json::make_object();
    json.set("text", Json::make_string("a\"b\\c\n\x01\xC3\xA9"));
    json.set("text", Json::make_string("a\"b\\c\n\x01\xC3\xA9 again")); // Replaces the member
    REQUIRE(json.members.size() == 1);

    str text = json.write();
    REQUIRE(text == "{ \"text\": \"a\\\"b\\\\c\\n\\u0001\xC3\xA9 again\" }\n");

    Json parsed;
    REQUIRE(Json::parse(text, &parsed));
    REQUIRE(parsed["text"].string == json["text"].string);
}

TEST_CASE("Json::push and Json::set leave other types alone", "[json]")
{
    Json number = Json::make_number(1.0);
    number.push(Json::make_number(2.0)).number = 3.0;
    number.set("key", Json::make_number(2.0));

    REQUIRE(number.is_number());
    REQUIRE(number.number == 1.0);
    REQUIRE(number.items.empty());
    REQUIRE(number.members.empty());
}

TEST_CASE("Json reads floats and vector3s, falling back for what isn't there", "[json]")
{
    Json json;
    REQUIRE(Json::parse(R"({ "f": 0.25, "v": [1, 2, 3], "short": [4], "mixed": [5, "x", 6] })", &json));

    REQUIRE(json["f"].float_or(9.0f) == 0.25f);
    REQUIRE(json["missing"].float_or(9.0f) == 9.0f);

    Vector3 fallback = { 7.0f, 8.0f, 9.0f };
    Vector3 v = json["v"].vector3_or(fallback);
    REQUIRE((v.x == 1.0f && v.y == 2.0f && v.z == 3.0f));
    Vector3 short_one = json["short"].vector3_or(fallback);
    REQUIRE((short_one.x == 4.0f && short_one.y == 8.0f && short_one.z == 9.0f));
    Vector3 mixed = json["mixed"].vector3_or(fallback);
    REQUIRE((mixed.x == 5.0f && mixed.y == 8.0f && mixed.z == 6.0f));
    Vector3 missing = json["missing"].vector3_or(fallback);
    REQUIRE((missing.x == 7.0f && missing.y == 8.0f && missing.z == 9.0f));
}

TEST_CASE("Json::make_vector3 writes what vector3_or reads", "[json]")
{
    Json json = Json::make_vector3({ 0.37f, -1.0f, 2.5f });
    REQUIRE(json.write() == "[0.37, -1, 2.5]\n");

    Json read;
    REQUIRE(Json::parse(json.write(), &read));
    Vector3 v = read.vector3_or({});
    REQUIRE((v.x == 0.37f && v.y == -1.0f && v.z == 2.5f));
}
