#include "editor.h"
#include "game.h"
#include "world.h"

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
    if (ImGui::BeginCombo("Model", model < level->model_count ? level->models[model].name : "none"))
    {
        if (ImGui::Selectable("none", model == level->model_count)) entity.model = NO_MODEL;
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

void update_editor(World *world, Game_Frame *frame, std::span<const Script_Info> scripts)
{
    Input *input = frame->input;
    ImGui::SetCurrentContext(frame->imgui);
    input->enabled = !ImGui::GetIO().WantCaptureKeyboard;

    if (input->was_pressed("Editor")) world->editing = !world->editing;
    if (!world->editing) return;

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

// Its color `time` seconds into the play, sRGB-encoded.
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

void edit_in_view(World *world, Game_Frame *frame)
{
    if (!world->editing) return;
    ImGui::SetCurrentContext(frame->imgui);
    ImGuiIO &io = ImGui::GetIO();

    Level         &level         = world->level;
    const Matrix4 &world_to_clip = frame->render->view_projection;

    // The ray under the pointer, from the near plane to the far one.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse && ImGui::IsMousePosValid() &&
        io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
    {
        r32     x             = 2.0f * io.MousePos.x / io.DisplaySize.x - 1.0f;
        r32     y             = 2.0f * io.MousePos.y / io.DisplaySize.y - 1.0f;
        Matrix4 clip_to_world = inverse(world_to_clip);
        Vector4 near_point    = clip_to_world * Vector4{ x, y, 0.0f, 1.0f };
        Vector4 far_point     = clip_to_world * Vector4{ x, y, 1.0f, 1.0f };
        Vector3 from          = near_point.xyz * (1.0f / near_point.w);
        Vector3 to            = far_point.xyz * (1.0f / far_point.w);
        world->selected       = entity_on_ray(level, world->assets, from, to - from);
    }

    const Entity *picked = entity_with_id(level, world->selected);
    if (picked && picked->model != NO_MODEL)
        push_wireframe(frame->render, world->assets.models.models[picked->model].mesh, entity_transform(*picked),
                       picked_color(world->play_time));
}
