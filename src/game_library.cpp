#include "game_library.h"
#include "logger.h"

namespace fs = std::filesystem;

static GAME_UPDATE_AND_RENDER(game_update_and_render_stub)
{
    (void)memory;
    (void)frame;
}

class Native_Game_Library
{
public:
    fs::path           path;          // What the build writes
    fs::path           lock_path;     // Exists while the build writes `path`
    void              *library = nullptr;
    fs::path           loaded_copy;   // The copy `library` was loaded from
    fs::file_time_type tried_time;    // Modification time of the last library loaded or tried
    u32                load_count = 0;
    bool               reported_missing = false;
};

Game_Library::Game_Library(const str &name)
{
    this->native = new Native_Game_Library();
    this->native->path      = game_library_path(name);
    this->native->lock_path = this->native->path;
    this->native->lock_path += ".lock";

    this->update_and_render = game_update_and_render_stub;
}

Game_Library::~Game_Library()
{
    if (this->native->library)
    {
        std::error_code error;
        unload_game_library(this->native->library);
        fs::remove(this->native->loaded_copy, error);
    }

    delete this->native;
}

bool Game_Library::reload_if_changed()
{
    Native_Game_Library *native = this->native;
    std::error_code error;

    if (fs::exists(native->lock_path, error))
        return false;

    fs::file_time_type time = fs::last_write_time(native->path, error);
    if (error)
    {
        if (!native->library && !native->reported_missing)
            log_warn("Running without game code until %s is built", native->path.string().c_str());
        native->reported_missing = true;
        return false;
    }
    if (time == native->tried_time)
        return false;

    // A new name for every load: a library still loaded under the same name would be
    // returned again instead of loading the new one.
    str file_name = native->path.stem().string() + "-loaded-" + std::to_string(native->load_count + 1)
                  + native->path.extension().string();
    fs::path copy = native->path.parent_path() / file_name;

    if (!fs::copy_file(native->path, copy, fs::copy_options::overwrite_existing, error))
    {
        log_error("Can't copy %s to %s: %s", native->path.string().c_str(), copy.string().c_str(),
                  error.message().c_str());
        native->tried_time = time;
        return false;
    }

    // The build may have started over during the copy; try again next time.
    if (fs::exists(native->lock_path, error) || fs::last_write_time(native->path, error) != time)
    {
        fs::remove(copy, error);
        return false;
    }

    native->tried_time = time;

    void *library = load_game_library(copy);
    Game_Update_And_Render *update_and_render = nullptr;
    if (library)
    {
        update_and_render = (Game_Update_And_Render *)find_game_library_symbol(library, GAME_UPDATE_AND_RENDER_SYMBOL);
        if (!update_and_render)
        {
            log_error("%s doesn't export %s()", native->path.string().c_str(), GAME_UPDATE_AND_RENDER_SYMBOL);
            unload_game_library(library);
        }
    }

    if (!update_and_render)
    {
        fs::remove(copy, error);
        if (native->library)
            log_warn("Keeping the previous game code");
        return false;
    }

    if (native->library)
    {
        unload_game_library(native->library);
        fs::remove(native->loaded_copy, error);
    }

    // Where the OS lets a loaded library be deleted (Linux) the copy goes away now;
    // elsewhere it's deleted after unloading it.
    fs::remove(copy, error);

    native->library     = library;
    native->loaded_copy = copy;
    native->load_count += 1;
    this->update_and_render = update_and_render;

    log_info("%s game code from %s", native->load_count == 1 ? "Loaded" : "Reloaded",
             native->path.filename().string().c_str());

    return true;
}
