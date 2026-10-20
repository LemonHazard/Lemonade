#pragma once

#include "core.h"
#include "vector.h"

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
    str       string;                 // Unescaped, UTF-8; empty for anything but a string
    std::vector<Json>        items;   // Array
    std::vector<Json_Member> members; // Object, in file order

    // Returns false on malformed JSON (logged), leaving `out` null.
    static bool parse(std::string_view text, Json *out);

    // As JSON text, ending with a newline. An array or object goes on one line when it
    // fits in `width` columns, and has one item or member per line, indented 4 spaces
    // more, when it doesn't. A number that's a float is written as the shortest that
    // reads back as that float (0.37f as 0.37), others as the shortest for the double;
    // infinities and NaN, which JSON doesn't have, as null.
    str write(u32 width = 100) const;

    // Building one, e.g. to write it.
    static Json make_bool(bool value);
    static Json make_number(r64 value);
    static Json make_string(std::string_view value);
    static Json make_vector3(Vector3 value); // An array of its 3 numbers
    static Json make_array();
    static Json make_object();
    // Adds `item` to the end of an array, and returns it as added. Does nothing to
    // anything else, returning a null value.
    Json &push(Json item);
    // Sets the member of an object with that key to `value`, adding it after the others
    // when there's none, and returns it as set. Does nothing to anything else, returning
    // a null value.
    Json &set(std::string_view key, Json value);

    const Json &operator[](std::string_view key) const;
    const Json &operator[](u64 index) const;

    bool is_null()   const { return type == Json_Type::Null; }
    bool is_number() const { return type == Json_Type::Number; }
    bool is_array()  const { return type == Json_Type::Array; }
    bool is_object() const { return type == Json_Type::Object; }

    // Elements of an array, 0 for anything else.
    u64 size() const { return type == Json_Type::Array ? items.size() : 0; }

    r64 number_or(r64 fallback) const { return type == Json_Type::Number ? number : fallback; }
    r32 float_or(r32 fallback) const  { return (r32)number_or(fallback); }
    // From an array of 3 numbers, as make_vector3() writes it: any of them that's
    // missing or not a number is fallback's.
    Vector3 vector3_or(Vector3 fallback) const;
};

struct Json_Member
{
    str  key;
    Json value;
};
