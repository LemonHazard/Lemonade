#include "level.h"
#include "file.h"
#include "json.h"
#include "logger.h"
#include "render_commands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Level files are written this wide, so each entity fits on a line.
constexpr u32 LEVEL_FILE_WIDTH = 160;

static u32 field_size(Script_Field_Type type)
{
    switch (type)
    {
        case Script_Field_Type::R32:     return sizeof(r32);
        case Script_Field_Type::Vector3: return sizeof(Vector3);
        case Script_Field_Type::Entity:  return sizeof(u32);
    }
    return 0;
}

// A whole number above 0 that fits a u32, or NO_ID.
static u32 id_from_json(const Json &json)
{
    r64 number = json.number_or(0.0);
    return number >= 1.0 && number <= 0xFFFFFFFF && number == (r64)(u32)number ? (u32)number : NO_ID;
}

// Reads the "data" of an entity's script into its data, by the script's fields.
static void read_script_data(Entity *entity, const Script_Info &script, const Json &json, const char *path)
{
    for (const Json_Member &member : json.members)
    {
        const Script_Field *field = nullptr;
        for (const Script_Field &f : script.fields)
            if (member.key == f.name) field = &f;

        if (!field)
        {
            log_error("\"%s\" has data \"%s\" the script \"%s\" doesn't have", path, member.key.c_str(), script.name);
            continue;
        }
        if (field->offset + field_size(field->type) > SCRIPT_DATA_SIZE)
        {
            log_error("The script \"%s\" has its field \"%s\" past its data's %u bytes", script.name, field->name,
                      SCRIPT_DATA_SIZE);
            continue;
        }

        u8 *at = entity->data + field->offset;
        switch (field->type)
        {
            case Script_Field_Type::R32:
            {
                r32 value = member.value.float_or(0.0f);
                memcpy(at, &value, sizeof(value));
            } break;
            case Script_Field_Type::Vector3:
            {
                Vector3 value = member.value.vector3_or({});
                memcpy(at, &value, sizeof(value));
            } break;
            case Script_Field_Type::Entity:
            {
                u32 id = id_from_json(member.value);
                if (id == NO_ID) log_error("\"%s\" has \"%s\" that isn't an entity's id", path, member.key.c_str());
                memcpy(at, &id, sizeof(id));
            } break;
        }
    }
}

// The script's fields in the entity's data that aren't 0, as read_script_data() reads
// them: null when they're all 0.
static Json script_data_to_json(const Entity &entity, const Script_Info &script)
{
    Json json;
    for (const Script_Field &field : script.fields)
    {
        if (field.offset + field_size(field.type) > SCRIPT_DATA_SIZE) continue;

        const u8 *at = entity.data + field.offset;
        Json      value;
        switch (field.type)
        {
            case Script_Field_Type::R32:
            {
                r32 number;
                memcpy(&number, at, sizeof(number));
                if (number) value = Json::make_number(number);
            } break;
            case Script_Field_Type::Vector3:
            {
                Vector3 vector;
                memcpy(&vector, at, sizeof(vector));
                if (vector.x || vector.y || vector.z) value = Json::make_vector3(vector);
            } break;
            case Script_Field_Type::Entity:
            {
                u32 id;
                memcpy(&id, at, sizeof(id));
                if (id != NO_ID) value = Json::make_number(id);
            } break;
        }

        if (value.is_null()) continue;
        if (json.is_null()) json = Json::make_object();
        json.set(field.name, value);
    }
    return json;
}

u32 asset_model(Level_Assets *assets, const char *path)
{
    u32 index;
    u64 used = library_model(&assets->models, path, &assets->textures, &index, assets->storage + assets->storage_used,
                             assets->storage_size - assets->storage_used);
    if (index == NO_MODEL || !used) return index; // Logged, or loaded before

    assets->storage_used += used;
    const Model &model = assets->models.models[index];
    for (u32 i = 0; i < model.part_count; ++i) assets->model_emissive[index][i] = model.parts[i].material.emissive;
    assets->model_bounds[index] = mesh_bounds(model.mesh);
    return index;
}

u32 asset_animation(Level_Assets *assets, const char *path)
{
    u32 index;
    u64 used = library_animation(&assets->animations, path, &index, assets->storage + assets->storage_used,
                                 assets->storage_size - assets->storage_used);
    assets->storage_used += used;
    return index;
}

const Mesh_Vertex *push_entity_pose(Render_Commands *commands, const Entity &entity, const Level_Assets &assets)
{
    if (entity.model == NO_MODEL || entity.animation == NO_ANIMATION) return nullptr;
    return push_posed_vertices(commands, assets.models.models[entity.model],
                               assets.animations.animations[entity.animation], entity.animation_time);
}

void release_assets(Level_Assets *assets, const Assets_Mark &mark, Render_Commands *commands)
{
    if (mark.storage_used < assets->storage_used)
        push_forget_memory(commands, assets->storage + mark.storage_used, assets->storage + assets->storage_used);

    assets->storage_used   = mark.storage_used;
    assets->models.count   = mark.model_count;
    assets->textures.count = mark.texture_count;
    assets->animations.count = mark.animation_count;
}

void load_level(Level *level, Level_Assets *assets, const char *path, std::span<const Script_Info> scripts)
{
    // `path` may be the level's own, to load it again.
    char file[LEVEL_PATH_SIZE];
    snprintf(file, sizeof(file), "%s", path);
    *level = {};
    snprintf(level->path, sizeof(level->path), "%s", file);
    path = level->path;

    Json json;
    str  text = read_file(path);
    if (text.empty())
        log_error("Could not read the level \"%s\"", path);
    else if (!Json::parse(text, &json))
        log_error("Could not parse the level \"%s\"", path);

    u64 used_before   = assets->storage_used;
    u32 models_before = assets->models.count;

    for (const Json_Member &member : json["models"].members)
    {
        if (level->model_count == LEVEL_MAX_MODELS)
        {
            log_error("\"%s\" has more than %u models", path, LEVEL_MAX_MODELS);
            break;
        }
        if (member.key.size() >= LEVEL_NAME_SIZE)
        {
            log_error("\"%s\" has a model with a name longer than %u: \"%s\"", path, LEVEL_NAME_SIZE - 1, member.key.c_str());
            continue;
        }

        u32 index = asset_model(assets, member.value["file"].string.c_str());
        if (index == NO_MODEL) continue; // Logged: it's tried again on the next load

        // Unity's emission of 1 is as bright as a lit wall: flames are far brighter, enough
        // to glow through the bloom in daylight.
        Level_Model &level_model = level->models[level->model_count++];
        snprintf(level_model.name, sizeof(level_model.name), "%s", member.key.c_str());
        level_model.model          = index;
        level_model.emissive_scale = member.value["emissive_scale"].float_or(1.0f);
    }
    scale_level_emission(*level, assets);

    // NO_MODEL when the level has none of that name (logged).
    auto find_model = [&](std::string_view name) -> u32
    {
        for (u32 i = 0; i < level->model_count; ++i)
            if (level->models[i].name == name) return level->models[i].model;
        log_error("\"%s\" has no model \"%.*s\"", path, (int)name.size(), name.data());
        return NO_MODEL;
    };

    level->saved_lighting = lighting_from_json(json["lighting"]);
    level->lighting       = level->saved_lighting;

    for (const Json &item : json["entities"].items)
    {
        if (level->entity_count == LEVEL_MAX_ENTITIES)
        {
            log_error("\"%s\" has more than %u entities", path, LEVEL_MAX_ENTITIES);
            break;
        }

        Entity           entity = {};
        std::string_view model  = item["model"].string;
        entity.model = model.empty() ? NO_MODEL : find_model(model);
        if (!model.empty() && entity.model == NO_MODEL) continue;

        entity.id = id_from_json(item["id"]);
        if (entity.id == NO_ID && !item["id"].is_null()) log_error("\"%s\" has an entity with an id that isn't one", path);

        Vector3 rotation  = item["rotation"].vector3_or({});
        entity.position   = item["position"].vector3_or({});
        entity.rotation   = { rotation.x, rotation.y, rotation.z };
        entity.scale      = item["scale"].vector3_or(entity.scale);
        entity.visible    = item["visible"].bool_or(true);
        entity.selectable = item["selectable"].bool_or(true);

        const Json      &script = item["script"];
        std::string_view name   = script["name"].string;
        if (!name.empty())
        {
            u32 kind = 0;
            while (kind < scripts.size() && name != scripts[kind].name) ++kind;
            if (kind < scripts.size())
            {
                entity.script = Script(kind);
                read_script_data(&entity, scripts[kind], script["data"], path);
            }
            else
                log_error("\"%s\" has no script \"%.*s\"", path, (int)name.size(), name.data());
        }

        level->entities[level->entity_count++] = entity;
    }

    // New ids, after the highest, for those without one and those with another's.
    for (u32 i = 0; i < level->entity_count; ++i)
        if (level->entities[i].id > level->last_id) level->last_id = level->entities[i].id;
    for (u32 i = 0; i < level->entity_count; ++i)
    {
        Entity &entity = level->entities[i];
        bool    taken  = false;
        for (u32 j = 0; j < i && entity.id != NO_ID && !taken; ++j) taken = level->entities[j].id == entity.id;
        if (taken) log_error("\"%s\" has more than one entity with the id %u", path, entity.id);
        if (taken || entity.id == NO_ID) entity.id = ++level->last_id;
    }

    log_info("Loaded the level \"%s\": %u entities, %u new models (%.0f MiB), %u models in all (%.0f MiB)", path,
             level->entity_count, assets->models.count - models_before, (r64)(assets->storage_used - used_before) / MiB, assets->models.count,
             (r64)assets->storage_used / MiB);
}

bool save_level(Level *level, const Level_Assets &assets, const char *path, std::span<const Script_Info> scripts)
{
    level->saved_lighting = level->lighting;

    Json json = Json::make_object();
    {
        Json &models = json.set("models", Json::make_object());
        for (u32 i = 0; i < level->model_count; ++i)
        {
            const Level_Model &model = level->models[i];
            Json &entry = models.set(model.name, Json::make_object());
            entry.set("file", Json::make_string(assets.models.paths[model.model]));
            if (model.emissive_scale != 1.0f) entry.set("emissive_scale", Json::make_number(model.emissive_scale));
        }
    }
    json.set("lighting", lighting_to_json(level->lighting));
    {
        Json &entities = json.set("entities", Json::make_array());
        for (u32 i = 0; i < level->entity_count; ++i)
        {
            const Entity    &entity   = level->entities[i];
            const Rotation3 &rotation = entity.rotation;
            Json            &item     = entities.push(Json::make_object());
            u32              model    = level_model(*level, entity.model);
            item.set("id", Json::make_number(entity.id));
            if (model < level->model_count) item.set("model", Json::make_string(level->models[model].name));
            item.set("position", Json::make_vector3(entity.position));
            if (rotation.pitch || rotation.yaw || rotation.roll)
                item.set("rotation", Json::make_vector3({ rotation.pitch, rotation.yaw, rotation.roll }));
            if (entity.scale.x != 1.0f || entity.scale.y != 1.0f || entity.scale.z != 1.0f)
                item.set("scale", Json::make_vector3(entity.scale));
            if (!entity.visible) item.set("visible", Json::make_bool(false));
            if (!entity.selectable) item.set("selectable", Json::make_bool(false));

            u32 kind = (u32)entity.script;
            if (entity.script != NO_SCRIPT && kind < scripts.size())
            {
                Json &script = item.set("script", Json::make_object());
                script.set("name", Json::make_string(scripts[kind].name));
                Json data = script_data_to_json(entity, scripts[kind]);
                if (!data.is_null()) script.set("data", data);
            }
        }
    }

    bool saved = write_file(path, json.write(LEVEL_FILE_WIDTH));

    if (saved)
        log_info("Saved the level \"%s\": %u entities", path, level->entity_count);
    else
        log_error("Could not save the level \"%s\"", path);
    return saved;
}

u32 find_entity(const Level &level, Script script)
{
    u32 found = NO_ENTITY;
    for (u32 i = 0; i < level.entity_count; ++i)
        if (level.entities[i].script == script) found = i;
    return found;
}

const Entity *entity_with_id(const Level &level, u32 id)
{
    if (id == NO_ID) return nullptr;
    for (u32 i = 0; i < level.entity_count; ++i)
        if (level.entities[i].id == id) return &level.entities[i];
    return nullptr;
}

Entity *entity_with_id(Level *level, u32 id)
{
    return const_cast<Entity *>(entity_with_id(*level, id));
}

Entity *add_entity(Level *level, const Entity &entity)
{
    if (level->entity_count == LEVEL_MAX_ENTITIES) return nullptr;

    Entity &added = level->entities[level->entity_count++];
    added    = entity;
    added.id = ++level->last_id;
    return &added;
}

u32 level_model(const Level &level, u32 model)
{
    u32 i = 0;
    while (i < level.model_count && level.models[i].model != model) ++i;
    return i;
}

u32 add_level_model(Level *level, const Level_Assets &assets, u32 model)
{
    u32 index = level_model(*level, model);
    if (index < level->model_count || level->model_count == LEVEL_MAX_MODELS) return index;

    // A name another model has, or a file without one, gets a number after it.
    std::string_view file = file_stem(assets.models.paths[model]);
    char             name[LEVEL_NAME_SIZE];
    snprintf(name, sizeof(name), "%.*s", (int)file.size(), file.data());
    for (u32 number = 2;; ++number)
    {
        bool taken = !name[0];
        for (u32 i = 0; i < level->model_count && !taken; ++i) taken = strcmp(level->models[i].name, name) == 0;
        if (!taken) break;
        snprintf(name, sizeof(name), "%.*s_%u", (int)std::min(file.size(), (size_t)LEVEL_NAME_SIZE - 12), file.data(), number);
    }

    Level_Model &added = level->models[level->model_count];
    snprintf(added.name, sizeof(added.name), "%s", name);
    added.model          = model;
    added.emissive_scale = 1.0f;
    return level->model_count++;
}

void scale_level_emission(const Level &level, Level_Assets *assets)
{
    for (u32 i = 0; i < level.model_count; ++i)
    {
        const Level_Model &named = level.models[i];
        Model             &model = assets->models.models[named.model];
        for (u32 part = 0; part < model.part_count; ++part)
            model.parts[part].material.emissive = assets->model_emissive[named.model][part] * named.emissive_scale;
    }
}

// How far along the ray, in `direction`s, it gets into the box, or 0 when it starts in
// it: INFINITY when it misses it, or the box is behind it.
static r32 ray_on_box(const Bounds3 &box, Vector3 origin, Vector3 direction)
{
    r32 enters = 0.0f;
    r32 leaves = INFINITY;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        r32 from = origin[axis];
        r32 step = direction[axis];
        if (step == 0.0f)
        {
            if (from < box.min[axis] || from > box.max[axis]) return INFINITY;
            continue;
        }
        r32 to_min = (box.min[axis] - from) / step;
        r32 to_max = (box.max[axis] - from) / step;
        enters = std::max(enters, std::min(to_min, to_max));
        leaves = std::min(leaves, std::max(to_min, to_max));
        if (enters > leaves) return INFINITY;
    }
    return enters;
}

// How far along the ray, in `direction`s, it first goes through one of the mesh's
// triangles, either side, if that's nearer than `nearest`: `nearest` when it isn't.
static r32 ray_on_mesh(const Mesh &mesh, Vector3 origin, Vector3 direction, r32 nearest)
{
    // Möller and Trumbore's: where the ray crosses the triangle's plane, in the
    // triangle's own coordinates u and v.
    for (u32 i = 0; i + 2 < mesh.index_count; i += 3)
    {
        Vector3 a = mesh.vertices[mesh.indices[i]].position;
        Vector3 b = mesh.vertices[mesh.indices[i + 1]].position;
        Vector3 c = mesh.vertices[mesh.indices[i + 2]].position;

        Vector3 ab          = b - a;
        Vector3 ac          = c - a;
        Vector3 p           = cross(direction, ac);
        r32     determinant = dot(ab, p);
        if (determinant == 0.0f) continue; // Along its plane

        r32     inverse = 1.0f / determinant;
        Vector3 to      = origin - a;
        r32     u       = dot(to, p) * inverse;
        if (u < 0.0f || u > 1.0f) continue;

        Vector3 q = cross(to, ab);
        r32     v = dot(direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f) continue;

        r32 t = dot(ac, q) * inverse;
        if (t >= 0.0f && t < nearest) nearest = t;
    }
    return nearest;
}

u32 entity_on_ray(const Level &level, const Level_Assets &assets, Vector3 origin, Vector3 direction, Ray_Hits hits,
                  r32 *distance, u32 ignored)
{
    // Each model is met in its own space: how far along the ray is the same there.
    u32 found   = NO_ID;
    r32 nearest = INFINITY;
    for (u32 i = 0; i < level.entity_count; ++i)
    {
        const Entity &entity = level.entities[i];
        if (!entity.visible || (hits == Ray_Hits::Selectable && !entity.selectable) || entity.id == ignored) continue;

        Matrix4 transform = entity_transform(entity);
        if (entity.model == NO_MODEL || determinant3x3(transform) == 0.0f) continue; // Not there, or flattened

        // Its triangles are all in its bounds: one whose bounds are further than the
        // nearest so far, or missed, isn't in the way.
        Matrix4 to_model  = inverse(transform);
        Vector3 from      = transform_point(to_model, origin);
        Vector3 along     = transform_vector(to_model, direction);
        if (ray_on_box(assets.model_bounds[entity.model], from, along) >= nearest) continue;

        r32 t = ray_on_mesh(assets.models.models[entity.model].mesh, from, along, nearest);
        if (t < nearest)
        {
            nearest = t;
            found   = entity.id;
        }
    }
    if (distance) *distance = nearest;
    return found;
}

void push_level(Render_Commands *commands, const Level &level, const Level_Assets &assets)
{
    commands->lighting = level.lighting;
    for (u32 i = 0; i < level.entity_count; ++i)
    {
        const Entity &entity = level.entities[i];
        if (entity.visible && entity.model != NO_MODEL)
            push_model(commands, assets.models.models[entity.model], entity_transform(entity), entity.id,
                       push_entity_pose(commands, entity, assets));
    }
}
