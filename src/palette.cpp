#include "palette.h"
#include "file.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

std::string_view palette_category(std::string_view name)
{
    size_t first = name.find('_');
    if (first == std::string_view::npos) return name;

    size_t           second   = name.find('_', first + 1);
    std::string_view category = name.substr(first + 1, second == std::string_view::npos ? second : second - first - 1);
    return category.empty() ? name : category;
}

// The folder's files, by name, in groups of their categories.
static std::vector<Palette_Group> palette_groups(std::vector<str> files)
{
    std::sort(files.begin(), files.end(),
              [](const str &a, const str &b) { return file_stem(a) < file_stem(b); });

    std::map<str, std::vector<str>> categories;
    for (str &file : files) categories[str(palette_category(file_stem(file)))].push_back(std::move(file));

    // A big one is split evenly, rather than into full groups and the rest.
    std::vector<Palette_Group> groups;
    for (auto &[category, models] : categories)
    {
        u32 count = ((u32)models.size() + PALETTE_GROUP_SIZE - 1) / PALETTE_GROUP_SIZE;
        if (count == 1)
        {
            groups.push_back({ category, std::move(models) });
            continue;
        }
        for (u32 i = 0; i < count; ++i)
        {
            u64 first = models.size() * i / count;
            u64 last  = models.size() * (i + 1) / count;
            groups.push_back({ category + " " + std::to_string(i + 1),
                               std::vector<str>(models.begin() + first, models.begin() + last) });
        }
    }
    return groups;
}

std::vector<Palette_Folder> read_palette(const char *folder)
{
    namespace fs = std::filesystem;

    fs::path root = fs::path(folder).lexically_normal();
    if (!root.has_filename()) root = root.parent_path();

    // By their paths in the folder, the folder's own ("") first.
    std::map<str, std::vector<str>> files;
    std::error_code                 error;
    for (fs::recursive_directory_iterator file(root, fs::directory_options::skip_permission_denied, error), end;
         !error && file != end; file.increment(error))
    {
        std::error_code type_error;
        if (!file->is_regular_file(type_error) || file->path().extension() != ".glb") continue;

        fs::path in = file->path().parent_path().lexically_relative(root);
        files[in == "." ? str() : in.generic_string()].push_back(file->path().generic_string());
    }
    if (error) log_error("Could not read all of the models in \"%s\": %s", folder, error.message().c_str());

    std::vector<Palette_Folder> folders;
    for (auto &[name, models] : files)
        folders.push_back({ name.empty() ? root.filename().generic_string() : name, palette_groups(std::move(models)) });
    return folders;
}

// Room around a model in the palette: this much, and this much of its size across.
constexpr r32 PALETTE_MARGIN      = 0.25f;
constexpr r32 PALETTE_SIZE_MARGIN = 0.15f;

// The rows are about this many times as wide as they'd be in a square.
constexpr r32 PALETTE_ROWS_WIDTH = 1.5f;

// The palette's camera, looking down this many degrees.
constexpr r32 PALETTE_PITCH = 35.0f;

// How far the camera, looking down `pitch` degrees at the origin, has to be for all of
// the box from (-half.x, 0, -half.z) to (half.x, half.y, half.z) to be in its view, at
// 16:9 and with a margin around it.
static r32 palette_distance(r32 pitch, Vector3 half)
{
    constexpr r32 seen = 0.9f; // Of the view, from its middle to its edges

    Matrix4 projection = perspective(radians(PALETTE_FIELD_OF_VIEW), 16.0f / 9.0f, 0.01f, 10000.0f);
    Vector3 behind     = { 0.0f, std::sin(radians(pitch)), std::cos(radians(pitch)) };
    auto    fits       = [&](r32 distance)
    {
        Matrix4 world_to_clip = projection * look_at(behind * distance, {}, { 0.0f, 1.0f, 0.0f });
        for (u32 corner = 0; corner < 8; ++corner)
        {
            Vector4 at = { corner & 1 ? half.x : -half.x, corner & 2 ? half.y : 0.0f, corner & 4 ? half.z : -half.z,
                           1.0f };
            Vector4 clip = world_to_clip * at;
            if (clip.w <= 0.0f || std::fabs(clip.x) > seen * clip.w || std::fabs(clip.y) > seen * clip.w) return false;
        }
        return true;
    };

    // It fits from anywhere further than as close as it fits.
    r32 closest  = 1.0f;
    r32 furthest = 5000.0f;
    if (fits(closest)) return closest;
    for (u32 i = 0; i < 32; ++i)
    {
        r32 middle = 0.5f * (closest + furthest);
        if (fits(middle))
            furthest = middle;
        else
            closest = middle;
    }
    return furthest;
}

void show_palette_models(Palette *palette, Level_Assets *assets, std::span<const str> files, const char *ground,
                         const Lighting &lighting)
{
    Level &level = palette->level;
    level                = {};
    level.lighting       = lighting;
    level.saved_lighting = lighting;
    palette->ground      = NO_ID;
    palette->selected    = NO_ID;

    if (ground)
    {
        Entity entity = {};
        entity.model  = asset_model(assets, ground);
        if (entity.model != NO_MODEL && add_level_model(&level, *assets, entity.model) < level.model_count)
            palette->ground = add_entity(&level, entity)->id;
    }

    // Each model in its cell, its bounds and the room around them.
    struct Cell
    {
        Entity *entity;
        Bounds3 bounds;
        r32     width;
        r32     depth;
        r32     x;     // Where its cell starts, from the left of the rows
    };
    std::vector<Cell> cells;
    r32               area    = 0.0f;
    r32               widest  = 0.0f;
    r32               tallest = 0.0f;

    for (const str &file : files)
    {
        Entity entity = {};
        entity.model  = asset_model(assets, file.c_str());
        if (entity.model == NO_MODEL) continue; // Logged
        if (add_level_model(&level, *assets, entity.model) == level.model_count) break; // Full

        Entity *added = add_entity(&level, entity);
        if (!added) break;

        const Bounds3 &bounds = assets->model_bounds[entity.model];
        Vector3        size   = bounds.max - bounds.min;
        r32            margin = PALETTE_MARGIN + PALETTE_SIZE_MARGIN * std::max(size.x, size.z);
        Cell           cell   = { added, bounds, size.x + 2.0f * margin, size.z + 2.0f * margin };

        area    += cell.width * cell.depth;
        widest   = std::max(widest, cell.width);
        tallest  = std::max(tallest, size.y);
        cells.push_back(cell);
    }

    // Rows from -Z to +Z, each as deep as its deepest cell, the cells centered in it.
    r32 rows_width = std::max(widest, PALETTE_ROWS_WIDTH * std::sqrt(area));
    r32 width      = 0.0f;
    r32 depth      = 0.0f;
    for (u64 first = 0; first < cells.size();)
    {
        u64 last      = first;
        r32 x         = 0.0f;
        r32 row_depth = 0.0f;
        while (last < cells.size() && (last == first || x + cells[last].width <= rows_width))
        {
            cells[last].x  = x;
            x             += cells[last].width;
            row_depth      = std::max(row_depth, cells[last].depth);
            ++last;
        }

        for (u64 i = first; i < last; ++i)
        {
            const Cell &cell   = cells[i];
            Vector3     center = (cell.bounds.min + cell.bounds.max) * 0.5f;
            cell.entity->position = { cell.x + 0.5f * cell.width - center.x, -cell.bounds.min.y,
                                      depth + 0.5f * row_depth - center.z };
        }

        width  = std::max(width, x);
        depth += row_depth;
        first  = last;
    }

    // Around the origin, under the ground's middle.
    for (const Cell &cell : cells) cell.entity->position -= Vector3{ 0.5f * width, 0.0f, 0.5f * depth };

    // Close enough that what's in the rows fills the view, at 16:9, without any of it
    // going past its edges.
    Vector3 half = { 0.5f * width, tallest, 0.5f * depth };
    palette->focus    = { 0.0f, 0.0f, 0.0f };
    palette->yaw      = 0.0f;
    palette->pitch    = PALETTE_PITCH;
    palette->distance = palette_distance(palette->pitch, half);
}
