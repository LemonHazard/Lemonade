#include "editor.h"
#include "game.h"
#include "world.h"

#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <cstring>

static const char *script_name(std::span<const Script_Info> scripts, Script script)
{
    return (u32)script < scripts.size() ? scripts[(u32)script].name : "unknown";
}

// A script field's name as a label: "turn_speed" as "Turn speed".
static void field_label(const char *name, char *label, u32 size)
{
    snprintf(label, size, "%s", name);
    for (char *c = label; *c; ++c)
        if (*c == '_') *c = ' ';
    label[0] = (char)std::toupper((unsigned char)label[0]);
}

static void edit_script_data(Entity *entity, const Script_Info &script)
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
        }
    }
}

void edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts)
{
    if (ImGui::BeginListBox("##Entities", { -FLT_MIN, 8.0f * ImGui::GetTextLineHeightWithSpacing() }))
    {
        for (u32 i = 0; i < level->entity_count; ++i)
        {
            // By its model, or its script when it has none (a camera).
            const Entity &entity = level->entities[i];
            u32           model  = level_model(*level, entity.model);
            char          label[LEVEL_NAME_SIZE + 16];
            snprintf(label, sizeof(label), "%u  %s", i,
                     model < level->model_count ? level->models[model].name : script_name(scripts, entity.script));
            if (ImGui::Selectable(label, *selected == i)) *selected = i;
        }
        ImGui::EndListBox();
    }

    if (ImGui::Button("Add") && level->entity_count < LEVEL_MAX_ENTITIES)
    {
        Entity entity = {};
        entity.model  = level->model_count ? level->models[0].model : NO_MODEL;
        if (*selected < level->entity_count) entity = level->entities[*selected];
        *selected = level->entity_count;
        level->entities[level->entity_count++] = entity;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && *selected < level->entity_count)
    {
        for (u32 i = *selected + 1; i < level->entity_count; ++i) level->entities[i - 1] = level->entities[i];
        --level->entity_count;
        if (*selected == level->entity_count && *selected) --*selected;
    }

    if (*selected >= level->entity_count) return;
    Entity &entity = level->entities[*selected];

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
    if ((u32)entity.script < scripts.size()) edit_script_data(&entity, scripts[(u32)entity.script]);
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
