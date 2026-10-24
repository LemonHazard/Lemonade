#pragma once

#include "core.h"

#include <string_view>

// Whole files at once, as bytes: a str can hold any, zeros included.

// The file's bytes, or none when it can't be read.
str read_file(const char *path);

// Writes `data` to the file, replacing what was there. Returns false when it can't.
bool write_file(const char *path, std::string_view data);

// The file's name in `path`, without the folders before it or its extension:
// "SM_Prop_Barrel_01" for "models/SM_Prop_Barrel_01.glb".
std::string_view file_stem(std::string_view path);
