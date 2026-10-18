#ifdef OS_WINDOWS

#include "../game_library.h"
#include "../logger.h"

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

std::filesystem::path game_library_path(const str &name)
{
    // If the executable can't be found, the library is looked up in the working directory.
    wchar_t executable[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return name + ".dll";

    return std::filesystem::path(executable).parent_path() / (name + ".dll");
}

void *load_game_library(const std::filesystem::path &path)
{
    HMODULE library = LoadLibraryW(path.c_str());
    if (!library)
        log_error("Can't load the game library %s (error %lu)", path.string().c_str(), GetLastError());

    return (void *)library;
}

void *find_game_library_symbol(void *library, const char *name)
{
    return (void *)GetProcAddress((HMODULE)library, name);
}

void unload_game_library(void *library)
{
    FreeLibrary((HMODULE)library);
}

#endif // OS_WINDOWS
