#pragma once

#include "core.h"

#include <string_view>
#include <vector>

// A parsed JSON document, e.g. a glTF file's. Lookups never fail: a missing key or
// index, or one into a value of another type, gives a null value, so
//   json["accessors"][3]["count"].number_or(0)
// is 0 whenever any part of the path isn't there.

enum class Json_Type : u8
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct Json_Member;

struct Json
{
    Json_Type type    = Json_Type::Null;
    bool      boolean = false;
    r64       number  = 0.0;
    str       string;                 // Unescaped, UTF-8
    std::vector<Json>        items;   // Array
    std::vector<Json_Member> members; // Object, in file order

    // Returns false on malformed JSON (logged), leaving `out` null.
    static bool parse(std::string_view text, Json *out);

    const Json &operator[](std::string_view key) const;
    const Json &operator[](u64 index) const;

    bool is_null()   const { return type == Json_Type::Null; }
    bool is_number() const { return type == Json_Type::Number; }
    bool is_array()  const { return type == Json_Type::Array; }
    bool is_object() const { return type == Json_Type::Object; }

    // Elements of an array, 0 for anything else.
    u64 size() const { return type == Json_Type::Array ? items.size() : 0; }

    r64 number_or(r64 fallback) const { return type == Json_Type::Number ? number : fallback; }
};

struct Json_Member
{
    str  key;
    Json value;
};
