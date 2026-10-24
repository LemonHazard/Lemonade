#include "file.h"

#include <cstdio>

str read_file(const char *path)
{
    str   data;
    FILE *file = fopen(path, "rb");
    if (!file) return data;

    // The size is only a hint: reading goes on to the end either way.
    if (fseek(file, 0, SEEK_END) == 0)
    {
        long size = ftell(file);
        if (size > 0) data.reserve((u64)size);
        if (fseek(file, 0, SEEK_SET) != 0) clearerr(file);
    }

    char buffer[64 * 1024];
    u64  read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) data.append(buffer, read);
    if (ferror(file)) data.clear();
    fclose(file);
    return data;
}

bool write_file(const char *path, std::string_view data)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;

    bool written = fwrite(data.data(), 1, data.size(), file) == data.size();
    if (fclose(file) != 0) written = false;
    return written;
}

std::string_view file_stem(std::string_view path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    size_t dot = path.rfind('.');
    if (dot != std::string_view::npos && dot) path = path.substr(0, dot);
    return path;
}
