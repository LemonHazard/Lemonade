#include "editor.h"
#include "file.h"
#include "game.h"
#include "logger.h"
#include "world.h"
#include "world_game.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

static const char *script_name(std::span<const Script_Info> scripts, Script script)
{
    return (u32)script < scripts.size() ? scripts[(u32)script].name : "unknown";
}

// An entity as the editor lists it: its id, and its model's name or, when it has
// none (a camera), its script's.
static void entity_label(const Level &level, const Entity &entity, std::span<const Script_Info> scripts, char *label,
                         u32 size)
{
    u32 model = level_model(level, entity.model);
    snprintf(label, size, "%u  %s", entity.id,
             model < level.model_count ? level.models[model].name : script_name(scripts, entity.script));
}

// A script field's name as a label: "turn_speed" as "Turn speed".
static void field_label(const char *name, char *label, u32 size)
{
    snprintf(label, size, "%s", name);
    for (char *c = label; *c; ++c)
        if (*c == '_') *c = ' ';
    label[0] = (char)std::toupper((unsigned char)label[0]);
}

static void edit_script_data(const Level &level, Entity *entity, const Script_Info &script,
                             std::span<const Script_Info> scripts)
{
    for (const Script_Field &field : script.fields)
    {
        char label[LEVEL_NAME_SIZE];
        field_label(field.name, label, sizeof(label));

        u8 *at = entity->data + field.offset;
        switch (field.type)
        {
            case Script_Field_Type::R32:
            {
                if (field.offset + sizeof(r32) > SCRIPT_DATA_SIZE) break;
                r32 value;
                memcpy(&value, at, sizeof(value));
                if (ImGui::DragFloat(label, &value, field.step, field.min, field.max)) memcpy(at, &value, sizeof(value));
            } break;
            case Script_Field_Type::Vector3:
            {
                if (field.offset + sizeof(Vector3) > SCRIPT_DATA_SIZE) break;
                Vector3 value;
                memcpy(&value, at, sizeof(value));
                if (ImGui::DragFloat3(label, &value.x, field.step, field.min, field.max)) memcpy(at, &value, sizeof(value));
            } break;
            case Script_Field_Type::Entity:
            {
                if (field.offset + sizeof(u32) > SCRIPT_DATA_SIZE) break;
                u32 id;
                memcpy(&id, at, sizeof(id));

                // One the level doesn't have (any more) shows as its id alone.
                char          shown[LEVEL_NAME_SIZE + 16] = "none";
                const Entity *other = entity_with_id(level, id);
                if (other)
                    entity_label(level, *other, scripts, shown, sizeof(shown));
                else if (id != NO_ID)
                    snprintf(shown, sizeof(shown), "%u  (gone)", id);

                if (ImGui::BeginCombo(label, shown))
                {
                    if (ImGui::Selectable("none", id == NO_ID)) id = NO_ID;
                    for (u32 i = 0; i < level.entity_count; ++i)
                    {
                        char option[LEVEL_NAME_SIZE + 16];
                        entity_label(level, level.entities[i], scripts, option, sizeof(option));
                        if (ImGui::Selectable(option, level.entities[i].id == id)) id = level.entities[i].id;
                    }
                    ImGui::EndCombo();
                    memcpy(at, &id, sizeof(id));
                }
            } break;
        }
    }
}

void edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts)
{
    if (ImGui::BeginListBox("##Entities", { -FLT_MIN, 8.0f * ImGui::GetTextLineHeightWithSpacing() }))
    {
        // The list remembers the one it last showed picked, to see one picked elsewhere (in
        // the level's view, or added) and scroll to it when it's out of sight.
        ImGuiStorage *storage = ImGui::GetStateStorage();
        ImGuiID       shown   = ImGui::GetID("Shown");
        bool          moved   = (u32)storage->GetInt(shown, (int)NO_ID) != *selected;

        for (u32 i = 0; i < level->entity_count; ++i)
        {
            const Entity &entity = level->entities[i];
            char          label[LEVEL_NAME_SIZE + 16];
            entity_label(*level, entity, scripts, label, sizeof(label));
            if (ImGui::Selectable(label, *selected == entity.id)) *selected = entity.id;
            else if (moved && *selected == entity.id && !ImGui::IsItemVisible()) ImGui::SetScrollHereY();
        }
        storage->SetInt(shown, (int)*selected);
        ImGui::EndListBox();
    }

    Entity *picked = entity_with_id(level, *selected);
    if (ImGui::Button("Add"))
    {
        Entity entity = {};
        entity.model  = level->model_count ? level->models[0].model : NO_MODEL;
        if (picked) entity = *picked;
        if (Entity *added = add_entity(level, entity)) *selected = added->id;
    }
    ImGui::SameLine();
    // The one after it is picked next, or the one before when it was the last.
    if (ImGui::Button("Remove") && picked)
    {
        u32 index = (u32)(picked - level->entities);
        for (u32 i = index + 1; i < level->entity_count; ++i) level->entities[i - 1] = level->entities[i];
        --level->entity_count;
        if (index == level->entity_count && index) --index;
        *selected = index < level->entity_count ? level->entities[index].id : NO_ID;
    }

    picked = entity_with_id(level, *selected);
    if (!picked) return;
    Entity &entity = *picked;

    u32 model = level_model(*level, entity.model);
    if (ImGui::BeginCombo("Model", model < level->model_count ? level->models[model].name : "[none]"))
    {
        if (ImGui::Selectable("[none]", model == level->model_count)) entity.model = NO_MODEL;
        for (u32 i = 0; i < level->model_count; ++i)
            if (ImGui::Selectable(level->models[i].name, i == model)) entity.model = level->models[i].model;
        ImGui::EndCombo();
    }
    ImGui::DragFloat3("Position", &entity.position.x, 0.05f);
    ImGui::DragFloat3("Rotation", &entity.rotation.pitch, 1.0f, -180.0f, 180.0f, "%.0f");
    ImGui::DragFloat3("Scale", &entity.scale.x, 0.01f, 0.0f, 100.0f);

    // Another script starts with its data all 0.
    if (ImGui::BeginCombo("Script", script_name(scripts, entity.script)))
    {
        for (u32 i = 0; i < scripts.size(); ++i)
        {
            if (ImGui::Selectable(scripts[i].name, Script(i) == entity.script) && Script(i) != entity.script)
            {
                entity.script = Script(i);
                memset(entity.data, 0, sizeof(entity.data));
            }
        }
        ImGui::EndCombo();
    }
    if ((u32)entity.script < scripts.size()) edit_script_data(*level, &entity, scripts[(u32)entity.script], scripts);
}

void edit_level_lighting(Level *level)
{
    // Linear colors, which can go past 1.
    const ImGuiColorEditFlags hdr = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
    Lighting &lighting = level->lighting;
    ImGui::DragFloat3("Sun direction", &lighting.sun_direction.x, 0.01f, -1.0f, 1.0f);
    ImGui::ColorEdit3("Sun", &lighting.sun_color.x, hdr);
    ImGui::ColorEdit3("Sky", &lighting.sky_color.x, hdr);
    ImGui::ColorEdit3("Horizon", &lighting.horizon_color.x, hdr);
    ImGui::ColorEdit3("Ground", &lighting.ground_color.x, hdr);
    ImGui::SliderFloat("Exposure", &lighting.exposure, 0.0f, 4.0f);
    ImGui::SliderFloat("Bloom", &lighting.bloom, 0.0f, 0.2f);
    ImGui::SliderFloat("Shadow distance", &lighting.shadow_distance, 0.0f, 150.0f, "%.0f m");
    if (ImGui::Button("Reset")) lighting = level->saved_lighting;
}

// The palette's folders, as the disk had them when they were last read: again after
// the game's code is reloaded.
static std::vector<Palette_Folder> palette_folders;
static bool                        palette_read;

static void read_palette_folders(const Game_Definition &game)
{
    if (palette_read) return;
    palette_folders = read_palette(game.model_folder);
    palette_read    = true;
}

// The group the palette showed last, loaded again as its camera left it, or none.
static void open_palette(World *world, const Game_Definition &game, Render_Commands *commands)
{
    Palette &palette = world->palette;
    palette.open = true;
    read_palette_folders(game);

    const Palette_Group *shown = nullptr;
    for (const Palette_Folder &folder : palette_folders)
        for (const Palette_Group &group : folder.groups)
            if (folder.name + ": " + group.name == palette.group) shown = &group;

    if (!shown)
    {
        palette.group[0] = 0;
        show_palette_models(world, {}, game.palette_ground, commands);
        return;
    }

    Orbit_Camera camera = palette.camera;
    show_palette_models(world, shown->files, game.palette_ground, commands);
    palette.camera = camera;
}

// Back to the editor, placing the model picked in the palette, if one is: the rest of
// what it loaded is let go of.
static void close_palette(World *world, Render_Commands *commands)
{
    Palette      &palette = world->palette;
    const Entity *picked  = entity_with_id(palette.level, palette.selected);
    palette.open = false;
    if (picked && picked->model != NO_MODEL)
    {
        world->placing = true;
        world->placed  = picked->model;
    }
    release_palette(world, commands);
}

// Its window: what's picked, how much is loaded, and the folders' groups to show.
static void edit_palette(World *world, const Game_Definition &game, Render_Commands *commands)
{
    Palette            &palette = world->palette;
    const Level_Assets &assets  = world->assets;
    read_palette_folders(game);

    ImGui::SetNextWindowPos({ 16.0f, 16.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({ 360.0f, 480.0f }, ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Palette", &open))
    {
        const Entity *picked = entity_with_id(palette.level, palette.selected);
        u32           named  = picked ? level_model(palette.level, picked->model) : palette.level.model_count;
        if (ImGui::Button("Back to editor")) open = false;
        ImGui::SameLine();
        if (named < palette.level.model_count)
            ImGui::Text("Places %s", palette.level.models[named].name);
        else
            ImGui::TextDisabled("Click a model to place it");

        // With the group's models, let go of as another is shown or the palette closes.
        ImGui::TextDisabled("%u of %u models loaded, %.0f of %.0f MiB", assets.models.count, MODEL_LIBRARY_SIZE,
                            (r64)assets.storage_used / MiB, (r64)assets.storage_size / MiB);
        ImGui::TextDisabled("WASD moves, the arrows turn and zoom");
        ImGui::TextDisabled("Right-drag turns too, and the wheel zooms");

        const ImGuiStyle &style = ImGui::GetStyle();
        for (const Palette_Folder &folder : palette_folders)
        {
            if (!ImGui::CollapsingHeader(folder.name.c_str())) continue;
            ImGui::PushID(folder.name.c_str());

            // The buttons fill a line, then the next.
            r32 right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            for (u64 i = 0; i < folder.groups.size(); ++i)
            {
                const Palette_Group &group = folder.groups[i];
                char                 shown[LEVEL_PATH_SIZE];
                snprintf(shown, sizeof(shown), "%s: %s", folder.name.c_str(), group.name.c_str());

                r32 width = ImGui::CalcTextSize(group.name.c_str()).x + 2.0f * style.FramePadding.x;
                if (i && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) ImGui::SameLine();

                bool current = strcmp(shown, palette.group) == 0;
                if (current) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button(group.name.c_str()))
                {
                    show_palette_models(world, group.files, game.palette_ground, commands);
                    snprintf(palette.group, sizeof(palette.group), "%s", shown);
                }
                if (current) ImGui::PopStyleColor();

                if (ImGui::IsItemHovered() && !group.files.empty())
                {
                    std::string_view first = file_stem(group.files.front());
                    std::string_view last  = file_stem(group.files.back());
                    ImGui::SetTooltip("%u models, %.*s to %.*s", (u32)group.files.size(), (int)first.size(),
                                      first.data(), (int)last.size(), last.data());
                }
            }
            ImGui::PopID();
        }

        // For models added or removed while the game runs.
        ImGui::Spacing();
        if (ImGui::SmallButton("Read the folders again")) palette_read = false;
    }
    ImGui::End();

    if (!open) close_palette(world, commands);
}

void update_editor(World *world, Game_Frame *frame, const Game_Definition &game)
{
    Input *input = frame->input;
    ImGui::SetCurrentContext(frame->imgui);
    input->enabled = !ImGui::GetIO().WantCaptureKeyboard;

    if (input->was_pressed("Editor")) world->editing = !world->editing;
    if (!world->editing) return;
    if (world->palette.open)
    {
        edit_palette(world, game, frame->render);
        return;
    }

    std::span<const Script_Info> scripts = game.scripts;

    ImGui::SetNextWindowPos({ 16.0f, 16.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({ 360.0f, 0.0f }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Editor", &world->editing))
    {
        ImGui::Text("%.1f ms a frame, %u fps, %.0f s of play", frame->dt * 1000.0f, world->fps, world->play_time);

        // Reload starts the level again as the file is, losing what's changed here. Save
        // writes what's changed here to the file, with the entities where they are now.
        Level &level = world->level;
        ImGui::TextUnformatted(level.path);
        ImGui::SameLine();
        if (ImGui::Button("Reload")) load_level(&level, &world->assets, level.path, scripts);
        ImGui::SameLine();
        if (ImGui::Button("Save")) save_level(&level, world->assets, level.path, scripts);
        ImGui::SameLine();
        ImGui::BeginDisabled(!game.model_folder);
        if (ImGui::Button("Palette")) open_palette(world, game, frame->render);
        ImGui::EndDisabled();

        ImGui::TextDisabled("WASD moves, the arrows turn and zoom");
        ImGui::SameLine();
        if (ImGui::SmallButton("Game's view")) world->camera.distance = 0.0f; // See view_editor()
        ImGui::TextDisabled("Right-drag turns too, and the wheel zooms");

        if (world->placing)
        {
            std::string_view name = file_stem(world->assets.models.paths[world->placed]);
            ImGui::Text("Placing %.*s", (int)name.size(), name.data());
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) world->placing = false;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("Click the level to place it there, with Shift to go on placing; right-click to stop");
            ImGui::PopStyleColor();
        }

        if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen))
            edit_level_entities(&level, &world->selected, scripts);

        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
            edit_level_lighting(&level);
    }
    ImGui::End();
}

// The picked one's wireframe breathes, from violet to white, back to violet, to black
// and back again, so it stands out on light and dark alike: it's mostly white for
// PICKED_SHADE_SECONDS, mostly violet for PICKED_VIOLET_SECONDS, mostly black, and
// mostly violet again.
constexpr r64 PICKED_SHADE_SECONDS  = 0.5;
constexpr r64 PICKED_VIOLET_SECONDS = 1.0;

// Its color `time` seconds into the editor's being open, sRGB-encoded.
static Vector4 picked_color(r64 time)
{
    const Vector3 violet = { 0.6f, 0.2f, 1.0f };
    const Vector3 white  = { 1.0f, 1.0f, 1.0f };
    const Vector3 black  = { 0.0f, 0.0f, 0.0f };

    // A sine wave, white at its top and black at its bottom, whose quarters around them
    // (where it's past 0.7 either way) go by in PICKED_SHADE_SECONDS, and whose quarters
    // around violet in PICKED_VIOLET_SECONDS. It starts at its white quarter.
    const r64 seconds[4] = {
        PICKED_SHADE_SECONDS,
        PICKED_VIOLET_SECONDS,
        PICKED_SHADE_SECONDS,
        PICKED_VIOLET_SECONDS
    };

    r64 at = std::fmod(time, 2.0 * (PICKED_SHADE_SECONDS + PICKED_VIOLET_SECONDS));

    u32 quarter = 0;
    while (quarter < 3 && at >= seconds[quarter])
    {
        at -= seconds[quarter++];
    }

    r64 angle = 0.5 * PI * (quarter + 0.5 + std::min(at / seconds[quarter], 1.0));
    r32 wave  = (r32)std::sin(angle);

    Vector3 to    = wave > 0.0f ? white : black;
    Vector3 color = violet + (to - violet) * std::fabs(wave);

    return { color.x, color.y, color.z, 1.0f };
}

// The ray under the pointer, from the near plane to the far one, as `world_to_clip`
// sees it: false when the pointer is on ImGui's windows or off the level.
static bool ray_under_mouse(const Matrix4 &world_to_clip, Vector3 *from, Vector3 *along)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid() || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return false;

    r32     x             = 2.0f * io.MousePos.x / io.DisplaySize.x - 1.0f;
    r32     y             = 2.0f * io.MousePos.y / io.DisplaySize.y - 1.0f;
    Matrix4 clip_to_world = inverse(world_to_clip);
    Vector4 near_point    = clip_to_world * Vector4{ x, y, 0.0f, 1.0f };
    Vector4 far_point     = clip_to_world * Vector4{ x, y, 1.0f, 1.0f };
    *from                 = near_point.xyz * (1.0f / near_point.w);
    *along                = far_point.xyz * (1.0f / far_point.w) - *from;
    return true;
}

// Where the ray first meets the level's models, or the ground's plane (y = 0) when it
// misses them: false when it meets neither.
static bool point_on_level(const World &world, Vector3 from, Vector3 along, Vector3 *at)
{
    r32 t;
    entity_on_ray(world.level, world.assets, from, along, &t);
    if (t == INFINITY && along.y != 0.0f && -from.y / along.y >= 0.0f) t = -from.y / along.y;
    if (t == INFINITY) return false;

    *at = from + along * t;
    return true;
}

// The right button let go of where it was pressed, not dragged to turn the camera.
static bool right_clicked()
{
    ImGuiIO &io = ImGui::GetIO();
    return ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
           io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < io.MouseDragThreshold * io.MouseDragThreshold;
}

// The model from the palette, where the pointer is on the level, and put there on a click.
static void place_in_view(World *world, Game_Frame *frame, Vector3 from, Vector3 along)
{
    Level  &level = world->level;
    Entity  placed = {};
    Vector3 at;
    if (!point_on_level(*world, from, along, &at)) return;

    // Its bottom on the point, wherever its origin is.
    placed.model    = world->placed;
    placed.position = at - Vector3{ 0.0f, world->assets.model_bounds[placed.model].min.y, 0.0f };

    const Model &model = world->assets.models.models[placed.model];
    push_model(frame->render, model, entity_transform(placed));
    push_wireframe(frame->render, model.mesh, entity_transform(placed), picked_color(world->editor_time));

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    Entity *added = nullptr;
    if (add_level_model(&level, world->assets, placed.model) < level.model_count) added = add_entity(&level, placed);
    if (added)
        world->selected = added->id;
    else
        log_error("Can't place another model in \"%s\": it has %u models and %u entities at most", level.path,
                  LEVEL_MAX_MODELS, LEVEL_MAX_ENTITIES);
    if (!ImGui::GetIO().KeyShift) world->placing = false;
}

void edit_in_view(World *world, Game_Frame *frame)
{
    if (!world->editing) return;
    ImGui::SetCurrentContext(frame->imgui);

    Level  &level = world->level;
    Vector3 from, along;
    bool    on_level = ray_under_mouse(frame->render->view_projection, &from, &along);

    if (world->placing)
    {
        if (on_level) place_in_view(world, frame, from, along);
        if (on_level && right_clicked()) world->placing = false;
    }
    else if (on_level && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        world->selected = entity_on_ray(level, world->assets, from, along);

    const Entity *picked = entity_with_id(level, world->selected);
    if (picked && picked->model != NO_MODEL)
        push_wireframe(frame->render, world->assets.models.models[picked->model].mesh, entity_transform(*picked),
                       picked_color(world->editor_time));
}

bool palette_shown(const World &world)
{
    return world.editing && world.palette.open;
}

// The editor's cameras turn this many degrees for each pixel the pointer is dragged,
// come this much closer for each notch of the wheel, and move this many times as far as
// they are from what they look at each second. With the arrow keys, they turn this many
// degrees a second, and come closer or go further by this much of how far they are.
constexpr r32 EDITOR_CAMERA_TURN_SPEED     = 0.3f;
constexpr r32 EDITOR_CAMERA_ZOOM_STEP      = 0.85f;
constexpr r32 EDITOR_CAMERA_MOVE_SPEED     = 0.75f;
constexpr r32 EDITOR_CAMERA_KEY_TURN_SPEED = 90.0f;
constexpr r32 EDITOR_CAMERA_KEY_ZOOM_SPEED = 1.5f;

// Moves the camera as the pointer and the keys say, those ImGui doesn't have, and sets
// the frame's camera to it: see view_palette() in editor.h.
static void fly_camera(Orbit_Camera *camera, Game_Frame *frame)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantCaptureMouse)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            camera->yaw  -= io.MouseDelta.x * EDITOR_CAMERA_TURN_SPEED;
            camera->pitch = clamp(camera->pitch + io.MouseDelta.y * EDITOR_CAMERA_TURN_SPEED, -30.0f, 89.0f);
        }
        camera->distance *= std::pow(EDITOR_CAMERA_ZOOM_STEP, io.MouseWheel);
    }

    // As the follow camera's keys: up and down arrows bring it closer and further, left
    // and right turn it clockwise and counter-clockwise around what it looks at.
    if (!io.WantCaptureKeyboard)
    {
        r32 zoom = std::exp(EDITOR_CAMERA_KEY_ZOOM_SPEED * frame->dt);
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    camera->distance /= zoom;
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  camera->distance *= zoom;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  camera->yaw -= EDITOR_CAMERA_KEY_TURN_SPEED * frame->dt;
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) camera->yaw += EDITOR_CAMERA_KEY_TURN_SPEED * frame->dt;
    }
    camera->yaw      = std::remainder(camera->yaw, 360.0f);
    camera->distance = clamp(camera->distance, 0.25f, 2000.0f);

    r32     yaw     = radians(camera->yaw);
    r32     pitch   = radians(camera->pitch);
    Vector3 forward = { -std::sin(yaw), 0.0f, -std::cos(yaw) };
    Vector3 right   = { std::cos(yaw), 0.0f, -std::sin(yaw) };
    if (!io.WantCaptureKeyboard)
    {
        Vector3 move = {};
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
        camera->focus += normalize(move) * (camera->distance * EDITOR_CAMERA_MOVE_SPEED * frame->dt);
    }

    // Its near and far planes go with how far it is, so what's small is seen up close
    // and what's big from far away.
    Vector3 behind = { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    Vector3 eye    = camera->focus + behind * camera->distance;
    r32     aspect = frame->height ? (r32)frame->width / (r32)frame->height : 1.0f;
    r32     z_near = std::max(0.01f * camera->distance, 0.02f);
    r32     z_far  = std::max(8.0f * camera->distance, 150.0f);
    frame->render->view_projection = perspective(radians(ORBIT_CAMERA_FIELD_OF_VIEW), aspect, z_near, z_far) *
                                     look_at(eye, camera->focus, { 0.0f, 1.0f, 0.0f });
    frame->render->camera_position = eye;
}

// The editor's camera where the game's last was, looking at what's in the middle of its
// view, on the level's models or the ground's plane, or 10 m ahead when it meets neither.
// Before the game's camera has seen the level, it looks down at the origin.
static void look_from_game_camera(World *world)
{
    Orbit_Camera &camera        = world->camera;
    Matrix4       clip_to_world = inverse(world->game_view_projection);
    Vector4       near_point    = clip_to_world * Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
    Vector4       far_point     = clip_to_world * Vector4{ 0.0f, 0.0f, 1.0f, 1.0f };
    Vector3       eye           = world->game_camera_position;
    Vector3       along         = far_point.xyz * (1.0f / far_point.w) - near_point.xyz * (1.0f / near_point.w);
    Vector3       forward       = normalize(along);
    if (near_point.w == 0.0f || far_point.w == 0.0f || !(length(forward) > 0.0f))
    {
        camera = { { 0.0f, 0.0f, 0.0f }, 0.0f, 35.0f, 20.0f };
        return;
    }

    Vector3 focus;
    if (!point_on_level(*world, eye, along, &focus)) focus = eye + forward * 10.0f;
    camera.focus    = focus;
    camera.distance = length(focus - eye);
    camera.yaw      = std::atan2(-forward.x, -forward.z) * (180.0f / PI);
    camera.pitch    = std::asin(clamp(-forward.y, -1.0f, 1.0f)) * (180.0f / PI);
}

void view_editor(World *world, Game_Frame *frame)
{
    ImGui::SetCurrentContext(frame->imgui);
    if (world->camera.distance <= 0.0f) look_from_game_camera(world);
    fly_camera(&world->camera, frame);
}

void view_palette(World *world, Game_Frame *frame)
{
    ImGui::SetCurrentContext(frame->imgui);
    Palette &palette = world->palette;
    palette.level.lighting = world->level.lighting;
    fly_camera(&palette.camera, frame);

    Vector3 from, along;
    if (ray_under_mouse(frame->render->view_projection, &from, &along))
    {
        u32           id      = entity_on_ray(palette.level, world->assets, from, along);
        const Entity *pointed = id == palette.ground ? nullptr : entity_with_id(palette.level, id);
        u32           named   = pointed ? level_model(palette.level, pointed->model) : palette.level.model_count;
        if (named < palette.level.model_count) ImGui::SetTooltip("%s", palette.level.models[named].name);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) palette.selected = pointed ? pointed->id : NO_ID;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && pointed) close_palette(world, frame->render);
    }

    const Entity *picked = entity_with_id(palette.level, palette.selected);
    if (picked && picked->model != NO_MODEL)
        push_wireframe(frame->render, world->assets.models.models[picked->model].mesh, entity_transform(*picked),
                       picked_color(world->editor_time));
}
