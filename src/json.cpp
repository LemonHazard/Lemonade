#include "json.h"
#include "logger.h"

#include <charconv>
#include <cstring>

// Deeper documents are rejected rather than overflowing the stack.
constexpr u32 JSON_MAX_DEPTH = 128;

struct Json_Parser
{
    const char *at;
    const char *end;
    const char *error;
};

static void json_skip_whitespace(Json_Parser *p)
{
    while (p->at < p->end && (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r'))
        ++p->at;
}

static bool json_fail(Json_Parser *p, const char *error)
{
    if (!p->error) p->error = error;
    return false;
}

static bool json_match(Json_Parser *p, const char *word)
{
    u64 length = strlen(word);
    if ((u64)(p->end - p->at) < length || memcmp(p->at, word, length) != 0) return false;
    p->at += length;
    return true;
}

static void json_append_utf8(str *out, u32 code_point)
{
    if (code_point < 0x80)
    {
        out->push_back((char)code_point);
    }
    else if (code_point < 0x800)
    {
        out->push_back((char)(0xC0 | (code_point >> 6)));
        out->push_back((char)(0x80 | (code_point & 0x3F)));
    }
    else if (code_point < 0x10000)
    {
        out->push_back((char)(0xE0 | (code_point >> 12)));
        out->push_back((char)(0x80 | ((code_point >> 6) & 0x3F)));
        out->push_back((char)(0x80 | (code_point & 0x3F)));
    }
    else
    {
        out->push_back((char)(0xF0 | (code_point >> 18)));
        out->push_back((char)(0x80 | ((code_point >> 12) & 0x3F)));
        out->push_back((char)(0x80 | ((code_point >> 6) & 0x3F)));
        out->push_back((char)(0x80 | (code_point & 0x3F)));
    }
}

static bool json_parse_hex4(Json_Parser *p, u32 *out)
{
    if (p->end - p->at < 4) return json_fail(p, "truncated \\u escape");

    u32 value = 0;
    for (int i = 0; i < 4; ++i)
    {
        char c = *p->at++;
        value <<= 4;
        if      (c >= '0' && c <= '9') value |= (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= (u32)(c - 'A' + 10);
        else return json_fail(p, "bad \\u escape");
    }
    *out = value;
    return true;
}

static bool json_parse_string(Json_Parser *p, str *out)
{
    ++p->at; // Opening quote

    while (p->at < p->end)
    {
        char c = *p->at++;

        if (c == '"') return true;
        if ((u8)c < 0x20) return json_fail(p, "control character in string");

        if (c != '\\')
        {
            out->push_back(c);
            continue;
        }

        if (p->at >= p->end) break;
        char escape = *p->at++;
        switch (escape)
        {
            case '"':  out->push_back('"');  break;
            case '\\': out->push_back('\\'); break;
            case '/':  out->push_back('/');  break;
            case 'b':  out->push_back('\b'); break;
            case 'f':  out->push_back('\f'); break;
            case 'n':  out->push_back('\n'); break;
            case 'r':  out->push_back('\r'); break;
            case 't':  out->push_back('\t'); break;
            case 'u':
            {
                u32 code_point;
                if (!json_parse_hex4(p, &code_point)) return false;

                // A UTF-16 surrogate pair: the high half, then \u and the low half.
                if (code_point >= 0xD800 && code_point < 0xDC00 && json_match(p, "\\u"))
                {
                    u32 low;
                    if (!json_parse_hex4(p, &low)) return false;
                    if (low < 0xDC00 || low >= 0xE000) return json_fail(p, "bad surrogate pair");
                    code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                }
                json_append_utf8(out, code_point);
                break;
            }
            default:
                return json_fail(p, "bad escape in string");
        }
    }

    return json_fail(p, "unterminated string");
}

static bool json_parse_number(Json_Parser *p, r64 *out)
{
    // from_chars takes no leading '+', which JSON doesn't allow either.
    auto [next, error] = std::from_chars(p->at, p->end, *out);
    if (error != std::errc() || next == p->at) return json_fail(p, "bad number");
    p->at = next;
    return true;
}

static bool json_parse_value(Json_Parser *p, Json *out, u32 depth)
{
    if (depth > JSON_MAX_DEPTH) return json_fail(p, "nested too deeply");

    json_skip_whitespace(p);
    if (p->at >= p->end) return json_fail(p, "unexpected end");

    char c = *p->at;

    if (c == '{')
    {
        out->type = Json_Type::Object;
        ++p->at;
        json_skip_whitespace(p);
        if (p->at < p->end && *p->at == '}') { ++p->at; return true; }

        while (true)
        {
            json_skip_whitespace(p);
            if (p->at >= p->end || *p->at != '"') return json_fail(p, "expected a key");

            Json_Member &member = out->members.emplace_back();
            if (!json_parse_string(p, &member.key)) return false;

            json_skip_whitespace(p);
            if (p->at >= p->end || *p->at != ':') return json_fail(p, "expected ':'");
            ++p->at;

            if (!json_parse_value(p, &member.value, depth + 1)) return false;

            json_skip_whitespace(p);
            if (p->at < p->end && *p->at == ',') { ++p->at; continue; }
            if (p->at < p->end && *p->at == '}') { ++p->at; return true; }
            return json_fail(p, "expected ',' or '}'");
        }
    }

    if (c == '[')
    {
        out->type = Json_Type::Array;
        ++p->at;
        json_skip_whitespace(p);
        if (p->at < p->end && *p->at == ']') { ++p->at; return true; }

        while (true)
        {
            if (!json_parse_value(p, &out->items.emplace_back(), depth + 1)) return false;

            json_skip_whitespace(p);
            if (p->at < p->end && *p->at == ',') { ++p->at; continue; }
            if (p->at < p->end && *p->at == ']') { ++p->at; return true; }
            return json_fail(p, "expected ',' or ']'");
        }
    }

    if (c == '"')
    {
        out->type = Json_Type::String;
        return json_parse_string(p, &out->string);
    }

    if (c == '-' || (c >= '0' && c <= '9'))
    {
        out->type = Json_Type::Number;
        return json_parse_number(p, &out->number);
    }

    if (json_match(p, "true"))  { out->type = Json_Type::Bool; out->boolean = true;  return true; }
    if (json_match(p, "false")) { out->type = Json_Type::Bool; out->boolean = false; return true; }
    if (json_match(p, "null"))  { out->type = Json_Type::Null; return true; }

    return json_fail(p, "unexpected character");
}

bool Json::parse(std::string_view text, Json *out)
{
    *out = Json();

    Json_Parser parser = { text.data(), text.data() + text.size(), nullptr };

    bool ok = json_parse_value(&parser, out, 0);
    if (ok)
    {
        json_skip_whitespace(&parser);
        if (parser.at != parser.end) ok = json_fail(&parser, "trailing characters");
    }

    if (!ok)
    {
        log_error("JSON: %s at offset %lld", parser.error, (long long)(parser.at - text.data()));
        *out = Json();
    }
    return ok;
}

static const Json json_null_value;

const Json &Json::operator[](std::string_view key) const
{
    if (type != Json_Type::Object) return json_null_value;

    for (const Json_Member &member : members)
    {
        if (member.key == key) return member.value;
    }
    return json_null_value;
}

const Json &Json::operator[](u64 index) const
{
    if (type != Json_Type::Array || index >= items.size()) return json_null_value;
    return items[index];
}
