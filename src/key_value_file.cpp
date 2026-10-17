#include "key_value_file.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

static void key_value_file_ltrim(str &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](u8 ch) {
        return !std::isspace(ch);
    }));
}

static void key_value_file_rtrim(str &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](u8 ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

Key_Values Key_Value_File::read(const str &file_path)
{
    Key_Values key_values;

    std::ifstream file(file_path);
    str           line;

    if (!file.is_open())
    {
        log_warn("Could not open \"%s\"", file_path.c_str());
        return key_values;
    }

    while (std::getline(file, line))
    {
        key_value_file_rtrim(line);
        key_value_file_ltrim(line);

        bool skip_line = line.empty() || line.at(0) == '#';
        if (skip_line)
            continue;

        std::istringstream buffer(line);
        std::vector<str> tokens{
            std::istream_iterator<str>{buffer},
            std::istream_iterator<str>{}
        };

        if (tokens.size() < 2)
            continue;

        key_values[tokens[0]] = std::vector<str>(tokens.begin() + 1, tokens.end());
    }

    return key_values;
}
