#pragma once

#include "core.h"

#include <unordered_map>
#include <vector>

// Text file with one key and its values per line, separated by whitespace; lines
// starting with '#' are comments:
//
//   # Controls
//   Jump    Space MouseRight
typedef std::unordered_map<str, std::vector<str>> Key_Values;

class Key_Value_File
{
public:
    // Lines without a value are skipped. A missing file gives an empty map.
    static Key_Values read(const str &file_path);

private:
    Key_Value_File();
};
