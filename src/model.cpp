#include "model.h"
#include "gltf.h"
#include "png.h"
#include "logger.h"

#include <cstring>

u64 library_texture(Texture_Library *library, const char *path, Texture *texture, void *storage, u64 storage_size)
{
    *texture = {};

    for (u32 i = 0; i < library->count; ++i)
    {
        if (strcmp(library->paths[i], path) == 0)
        {
            *texture = library->textures[i];
            return 0;
        }
    }

    if (library->count == TEXTURE_LIBRARY_SIZE)
    {
        log_error("Can't load \"%s\": the texture library is full (%u textures)", path, TEXTURE_LIBRARY_SIZE);
        return 0;
    }
    if (strlen(path) >= TEXTURE_PATH_SIZE)
    {
        log_error("Can't load \"%s\": its path is longer than %u bytes", path, TEXTURE_PATH_SIZE - 1);
        return 0;
    }

    // Kept even when it fails, so it isn't tried again for the next model.
    u32 index = library->count++;
    strcpy(library->paths[index], path);
    u64 used = load_png(path, &library->textures[index], storage, storage_size);
    *texture = library->textures[index];
    return used;
}

u64 library_model(Model_Library *library, const char *path, Texture_Library *textures, u32 *index, void *storage,
                  u64 storage_size)
{
    for (u32 i = 0; i < library->count; ++i)
    {
        if (strcmp(library->paths[i], path) == 0)
        {
            *index = i;
            return 0;
        }
    }

    *index = NO_MODEL;
    if (library->count == MODEL_LIBRARY_SIZE)
    {
        log_error("Can't load \"%s\": the model library is full (%u models)", path, MODEL_LIBRARY_SIZE);
        return 0;
    }
    if (strlen(path) >= MODEL_PATH_SIZE)
    {
        log_error("Can't load \"%s\": its path is longer than %u bytes", path, MODEL_PATH_SIZE - 1);
        return 0;
    }

    u64 used = load_glb(path, &library->models[library->count], textures, storage, storage_size);
    if (!used) return 0;

    *index = library->count++;
    strcpy(library->paths[*index], path);
    return used;
}
