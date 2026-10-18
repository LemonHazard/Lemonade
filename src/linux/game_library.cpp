#ifdef OS_LINUX

#include "../game_library.h"
#include "../logger.h"

#include <dlfcn.h>
#include <link.h>

std::filesystem::path game_library_path(const str &name)
{
    // If the executable can't be found, the library is looked up in the working directory.
    std::error_code error;
    std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);

    return executable.parent_path() / ("lib" + name + ".so");
}

void *load_game_library(const std::filesystem::path &path)
{
    void *library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library)
        log_error("Can't load the game library: %s", dlerror());

    return library;
}

void *find_game_library_symbol(void *library, const char *name)
{
    return dlsym(library, name);
}

void unload_game_library(void *library)
{
    str name;
    link_map *map = nullptr;
    if (dlinfo(library, RTLD_DI_LINKMAP, &map) == 0 && map)
        name = map->l_name;

    dlclose(library);

    // dlclose() leaves a library loaded while something still holds on to it, e.g.
    // GNU unique symbols, which the game is built without (-fno-gnu-unique).
    if (void *still_loaded = dlopen(name.c_str(), RTLD_LAZY | RTLD_NOLOAD))
    {
        log_warn("%s is still loaded after unloading it, its code stays in memory", name.c_str());
        dlclose(still_loaded);
    }
}

#endif // OS_LINUX
