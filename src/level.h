#pragma once

#include "animation.h"
#include "core.h"
#include "lighting.h"
#include "matrix.h"
#include "model.h"
#include "vector.h"

#include <span>
#include <type_traits>

struct Entity;
struct Game_Frame;
struct Render_Commands;
struct World;

// A level: the models it names, its lighting, and the entities placed in it, loaded
// from a JSON file and saved back to one. It's plain data, so the game can keep it in
// its memory across reloads (see game.h).
//
// What entities do is the game's: each has a script, a kind of behavior the game
// defines and runs every frame, and the data that script keeps for it. The engine
// knows scripts only through the table of them the game passes in, to read, write and
// edit their data by name, and to run their code. The table is in the game's code,
// which moves on reload, so it's passed to each call and never kept.

// The game's, defined as
//   enum class Script : u32 { none, ..., count };
// with none first: the game's table of scripts describes Script(i) at i.
enum class Script : u32;
constexpr Script NO_SCRIPT = Script(0);

enum class Script_Field_Type : u8
{
    R32,
    Vector3,
    Entity, // Another entity, as a u32 of its id: NO_ID for none
};

// One of the fields of a script's data, which is a struct of them.
struct Script_Field
{
    const char       *name;   // What level files call it
    Script_Field_Type type;
    u32               offset; // Into the struct, from offsetof()
    // How the editor drags a number: by `step` a pixel, from `min` to `max`.
    r32               step;
    r32               min;
    r32               max;
};

// What a script does for an entity of its kind, in the world (see world.h).
typedef void Script_Update(World *world, Entity *entity, const Game_Frame *frame);

struct Script_Info
{
    const char                   *name; // What level files call it
    std::span<const Script_Field> fields;
    // Each frame, in the level's order, then after every entity's update for what goes
    // after others, as a camera following what it looks at. Either can be null.
    Script_Update                *update;
    Script_Update                *late_update;
};

constexpr u32 SCRIPT_DATA_SIZE = 32;

// Entities are known to each other by their ids, which stay theirs as others are
// added and removed, and are saved with them: their indices into the level don't.
constexpr u32 NO_ID = 0;

// Something in the level: a model where it stands, and what it does. One without a
// model, as a camera, is only a place for its script. A skinned model is posed by the
// entity's animation, which its script plays: neither is in level files.
struct Entity
{
    u32       id         = NO_ID;    // Unique in its level, once it's in one
    u32       model      = NO_MODEL; // Into Level_Assets::models
    u32       animation  = NO_ANIMATION; // Into Level_Assets::animations, or none for its model's rest pose
    r32       animation_time = 0.0f;     // Seconds into the animation, which loops
    Vector3   position   = {};
    Rotation3 rotation   = {};       // Degrees, as rotation(Rotation3) turns them
    Vector3   scale      = { 1.0f, 1.0f, 1.0f };
    Script    script     = NO_SCRIPT;
    bool      visible    = true;     // Drawn: one that isn't is never in the way of a ray either
    bool      selectable = true;     // Picked by clicking on it in the editor's view, not only from its list
    // The script's, as its struct: see script_data(). All 0 to begin with.
    alignas(8) u8 data[SCRIPT_DATA_SIZE] = {};
};

// An entity's script data as the struct `T` its fields are in. `T` must stay plain
// data that fits.
template <typename T>
T *script_data(Entity *entity)
{
    static_assert(std::is_trivially_copyable_v<T>, "Script data is kept as bytes");
    static_assert(sizeof(T) <= SCRIPT_DATA_SIZE, "Script data must fit in SCRIPT_DATA_SIZE");
    static_assert(alignof(T) <= alignof(Entity), "Script data is aligned to 8 at most");
    return reinterpret_cast<T *>(entity->data);
}

template <typename T>
const T *script_data(const Entity *entity)
{
    return script_data<T>(const_cast<Entity *>(entity));
}

// Where it stands, turned and scaled: scaled first, then turned, then moved.
inline Matrix4 entity_transform(const Entity &entity)
{
    return translation(entity.position) * rotation(entity.rotation) * scaling(entity.scale);
}

// The models, textures and animations levels have, loaded once each into `storage` and
// kept there from one level to the next: the renderer knows meshes and textures by their address
// and keeps them, so their memory can't go to others. `storage` is the game's memory,
// set before the first level is loaded.
struct Level_Assets
{
    u8             *storage;
    u64             storage_size;
    u64             storage_used;
    Model_Library   models;
    Vector3         model_emissive[MODEL_LIBRARY_SIZE][MODEL_MAX_PARTS]; // Their parts' emission as loaded, before levels scale it
    Bounds3         model_bounds[MODEL_LIBRARY_SIZE];                    // Around their meshes, in their own space
    Texture_Library textures;
    Animation_Library animations;
};

constexpr u32 LEVEL_MAX_MODELS   = 256;
constexpr u32 LEVEL_MAX_ENTITIES = 1024;
constexpr u32 LEVEL_NAME_SIZE    = 64;
constexpr u32 LEVEL_PATH_SIZE    = 256;

// A model, by the level's name for it.
struct Level_Model
{
    char name[LEVEL_NAME_SIZE];
    u32  model;          // Into Level_Assets::models
    r32  emissive_scale;
};

struct Level
{
    char        path[LEVEL_PATH_SIZE]; // The file it was loaded from
    Level_Model models[LEVEL_MAX_MODELS];
    u32         model_count;
    Lighting    lighting;              // As it is now
    Lighting    saved_lighting;        // As it was loaded or last saved
    Entity      entities[LEVEL_MAX_ENTITIES];
    u32         entity_count;
    u32         last_id;               // The highest it has given: the next entity added gets the one after
};

// How much of the assets is loaded, to let go of what's loaded after it (see
// release_assets()).
struct Assets_Mark
{
    u64 storage_used;
    u32 model_count;
    u32 texture_count;
    u32 animation_count;
};

inline Assets_Mark mark_assets(const Level_Assets &assets)
{
    return { assets.storage_used, assets.models.count, assets.textures.count, assets.animations.count };
}

// Lets go of the models, textures and animations loaded since `mark`, their storage to be used for
// others, and has the renderer forget them (see push_forget_memory()). Their indices
// are others' from then on: nothing may still have them.
void release_assets(Level_Assets *assets, const Assets_Mark &mark, Render_Commands *commands);

// The .glb at `path` in the assets, loaded into their storage the first time it's asked
// for, as library_model() loads it: its index into the assets' models, or NO_MODEL when
// it can't be loaded (logged).
u32 asset_model(Level_Assets *assets, const char *path);

// The .glb animation at `path` in the assets, loaded into their storage the first time
// it's asked for, as library_animation() loads it: its index into the assets'
// animations, or NO_ANIMATION when the library is full (logged).
u32 asset_animation(Level_Assets *assets, const char *path);

// The entity's model's vertices, posed by its animation, in the frame's commands (see
// push_posed_vertices()): null when it has no animation, or its model no skin.
const Mesh_Vertex *push_entity_pose(Render_Commands *commands, const Entity &entity, const Level_Assets &assets);

// Loads the JSON level file at `path` over whatever was in `level`, and the models it
// has that weren't loaded yet. In it:
//   "models":   by name, each {"file": a .glb, "emissive_scale": what its materials'
//               emission is multiplied by (1)}; a file is loaded once, however many
//               names it has and however many levels have it
//   "lighting": as lighting_from_json() reads it
//   "entities": each {"id": a number above 0, unique in the level, "model": a model's
//               name (none for no model), "position": [x, y, z], "rotation": [pitch,
//               yaw, roll] in degrees, "scale": [x, y, z] (1s), "visible" and
//               "selectable": false when it isn't (true), "script": {"name": a
//               script's, "data": its fields by name, an entity by its id}}
// Anything missing is 0 or its default, but an entity without an id (or with one
// another has, logged) gets a new one; what's wrong is logged and left out.
void load_level(Level *level, Level_Assets *assets, const char *path, std::span<const Script_Info> scripts);

// Writes the level as load_level() reads it, with its entities and lighting as they
// are now, to `path`, leaving out what's 0 or its default. Returns false when it can't
// (logged).
bool save_level(Level *level, const Level_Assets &assets, const char *path, std::span<const Script_Info> scripts);

constexpr u32 NO_ENTITY = 0xFFFFFFFF;

// The last of the level's entities with `script`, into Level::entities, or NO_ENTITY.
u32 find_entity(const Level &level, Script script);

// The level's entity with `id`, or null when it has none (NO_ID's included).
Entity       *entity_with_id(Level *level, u32 id);
const Entity *entity_with_id(const Level &level, u32 id);

// Puts `entity` at the end of the level's entities with a new id, and returns it as
// added: null when the level is full.
Entity *add_entity(Level *level, const Entity &entity);

// The level's first name for `model`, into Level::models, or model_count.
u32 level_model(const Level &level, u32 model);

// As level_model(), but a model the level has no name for gets one, from its file's
// name ("SM_Prop_Barrel_01" for models/SM_Prop_Barrel_01.glb, with _2, _3... after it
// when another has that one), and its emission as loaded: model_count when the level is
// full.
u32 add_level_model(Level *level, const Level_Assets &assets, u32 model);

// Sets the level's models' emission to theirs as loaded times the level's
// emissive_scale for them. Levels share models, and each has its own scales: it's done
// as a level is loaded, and again whenever it's shown after another.
void scale_level_emission(const Level &level, Level_Assets *assets);

// Which entities a ray can meet: all those that are visible, or only those of them that
// are selectable too, to pick one by clicking through those that aren't.
enum class Ray_Hits : u8
{
    Visible,
    Selectable,
};

// The id of the nearest of the level's entities whose model is in the way of the ray
// from `origin` along `direction`, through either side of its triangles, of those
// `hits` has: NO_ID when none is. Those without a model are never in the way.
// `distance`, when given, is how far along the ray it is, in `direction`s: INFINITY
// when none is. The one with the `ignored` id is never in the way.
u32 entity_on_ray(const Level &level, const Level_Assets &assets, Vector3 origin, Vector3 direction, Ray_Hits hits,
                  r32 *distance = nullptr, u32 ignored = NO_ID);

// What the level looks like: its visible entities' models, posed by their animations,
// and its lighting.
void push_level(Render_Commands *commands, const Level &level, const Level_Assets &assets);
