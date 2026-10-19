#include "imgui_host.h"
#include "window.h"
#include "input.h"
#include "render_commands.h"

#include <cfloat>
#include <cstddef>

#include <imgui.h>

// ImGui's draw lists go to the renderer as they are.
static_assert(sizeof(ImDrawVert) == sizeof(Ui_Vertex) && offsetof(ImDrawVert, pos) == offsetof(Ui_Vertex, position) &&
              offsetof(ImDrawVert, uv) == offsetof(Ui_Vertex, uv) && offsetof(ImDrawVert, col) == offsetof(Ui_Vertex, color),
              "ImDrawVert must be laid out as Ui_Vertex");
static_assert(sizeof(ImDrawIdx) == sizeof(u32), "imconfig.h makes ImGui's indices 32-bit, as Ui_Mesh's");

// Input's keys are in ImGui's order, with the either-side modifiers (Shift, Ctrl, Alt,
// Super) between RightSuper and Menu.
static_assert((int)Key::RightSuper - (int)Key::Tab == ImGuiKey_RightSuper - ImGuiKey_Tab, "Keys must be in ImGui's order");
static_assert((int)Key::Oem102 - (int)Key::Menu == ImGuiKey_Oem102 - ImGuiKey_Menu, "Keys must be in ImGui's order");

static ImGuiKey imgui_host_key(Key key)
{
    if (key >= Key::Tab && key <= Key::RightSuper) return (ImGuiKey)(ImGuiKey_Tab + ((int)key - (int)Key::Tab));
    if (key >= Key::Menu && key <= Key::Oem102)    return (ImGuiKey)(ImGuiKey_Menu + ((int)key - (int)Key::Menu));
    return ImGuiKey_None;
}

// Calls `send` with the key's changes since the last frame, in order, so that a press
// and release between two frames is seen too.
template<typename Send>
static void imgui_host_send_key(const Input &input, Key key, Send send)
{
    bool down = input.is_down(key);
    if (down ? input.was_released(key) : input.was_pressed(key)) send(!down);
    if (down ? input.was_pressed(key) : input.was_released(key)) send(down);
}

// Creating and updating textures is copying their pixels again, where ImGui keeps them;
// the Texture they're drawn with is the ImTextureID.
static void imgui_host_update_texture(ImTextureData *data, Render_Commands *commands)
{
    switch (data->Status)
    {
    case ImTextureStatus_WantCreate:
    case ImTextureStatus_WantUpdates:
    {
        IM_ASSERT(data->Format == ImTextureFormat_RGBA32);
        auto *texture = (Texture *)data->BackendUserData;
        if (!texture)
        {
            texture = new Texture();
            data->BackendUserData = texture;
        }
        *texture = { data->Pixels, (u32)data->Width, (u32)data->Height };

        // Created ones too: other pixels the renderer still has may have been at that address.
        push_texture_update(commands, *texture);
        data->SetTexID((ImTextureID)(uintptr_t)texture);
        data->SetStatus(ImTextureStatus_OK);
        break;
    }

    case ImTextureStatus_WantDestroy:
        // The renderer keeps its copy until exit, as it does every texture's.
        delete (Texture *)data->BackendUserData;
        data->BackendUserData = nullptr;
        data->SetTexID(ImTextureID_Invalid);
        data->SetStatus(ImTextureStatus_Destroyed);
        break;

    default:
        break;
    }
}

Imgui_Host::Imgui_Host()
{
    IMGUI_CHECKVERSION();
    this->context = ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "Lemonade";
    io.BackendRendererName = "Lemonade";
    // 32-bit indices make VtxOffset unneeded.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // A mistake in the game's ImGui calls shows up as a tooltip and in the log, rather
    // than an assert ending the run along with the game's state.
    io.ConfigErrorRecoveryEnableAssert   = false;
    io.ConfigErrorRecoveryEnableDebugLog = true;
    io.ConfigErrorRecoveryEnableTooltip  = true;
}

Imgui_Host::~Imgui_Host()
{
    ImGui::SetCurrentContext(this->context);
    for (ImTextureData *data : ImGui::GetPlatformIO().Textures)
    {
        delete (Texture *)data->BackendUserData;
        data->BackendUserData = nullptr;
    }
    ImGui::DestroyContext(this->context);
}

void Imgui_Host::begin_frame(const Window &window, const Input &input, r32 dt)
{
    ImGui::SetCurrentContext(this->context);
    ImGuiIO &io = ImGui::GetIO();

    io.DisplaySize = { (r32)window.width, (r32)window.height };
    if (window.width > 0 && window.height > 0)
    {
        io.DisplayFramebufferScale = { (r32)window.framebuffer_width / (r32)window.width,
                                       (r32)window.framebuffer_height / (r32)window.height };
    }
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 1000.0f;

    if (input.mouse_inside) io.AddMousePosEvent(input.mouse_x, input.mouse_y);
    else                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
        imgui_host_send_key(input, (Key)((int)Key::MouseLeft + button),
                            [&](bool down) { io.AddMouseButtonEvent(button, down); });

    // ImGui's horizontal wheel goes left, Input's right.
    if (input.scroll_x != 0.0f || input.scroll_y != 0.0f) io.AddMouseWheelEvent(-input.scroll_x, input.scroll_y);

    const struct { Key key; ImGuiKey modifier; } modifiers[] = {
        { Key::Ctrl,  ImGuiMod_Ctrl  },
        { Key::Shift, ImGuiMod_Shift },
        { Key::Alt,   ImGuiMod_Alt   },
        { Key::Super, ImGuiMod_Super },
    };
    for (const auto &modifier : modifiers)
        imgui_host_send_key(input, modifier.key, [&](bool down) { io.AddKeyEvent(modifier.modifier, down); });

    for (int key = (int)Key::Tab; key <= (int)Key::Oem102; ++key)
    {
        ImGuiKey imgui_key = imgui_host_key((Key)key);
        if (imgui_key == ImGuiKey_None) continue;
        imgui_host_send_key(input, (Key)key, [&](bool down) { io.AddKeyEvent(imgui_key, down); });
    }

    for (u32 i = 0; i < input.text_length; ++i) io.AddInputCharacter(input.text[i]);

    ImGui::NewFrame();
}

void Imgui_Host::end_frame(Render_Commands *commands)
{
    ImGui::SetCurrentContext(this->context);
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();

    // The textures first: the draws need their IDs.
    if (draw_data->Textures)
    {
        for (ImTextureData *data : *draw_data->Textures) imgui_host_update_texture(data, commands);
    }

    // In window coordinates, from its top-left corner (DisplayPos is only elsewhere with
    // multiple viewports), each FramebufferScale pixels.
    IM_ASSERT(draw_data->DisplayPos.x == 0.0f && draw_data->DisplayPos.y == 0.0f);
    r32 scale = draw_data->FramebufferScale.x;
    for (const ImDrawList *list : draw_data->CmdLists)
    {
        Ui_Mesh mesh = {
            .vertices     = (Ui_Vertex *)list->VtxBuffer.Data,
            .indices      = (u32 *)list->IdxBuffer.Data,
            .vertex_count = (u32)list->VtxBuffer.Size,
            .index_count  = (u32)list->IdxBuffer.Size,
        };

        for (const ImDrawCmd &command : list->CmdBuffer)
        {
            // The game's own callbacks: its code is loaded until after the frame.
            if (command.UserCallback)
            {
                command.UserCallback(list, &command);
                continue;
            }

            // Only with 16-bit indices.
            IM_ASSERT(command.VtxOffset == 0);

            const Texture *texture = (const Texture *)(uintptr_t)command.GetTexID();
            Vector4 clip = { command.ClipRect.x, command.ClipRect.y, command.ClipRect.z, command.ClipRect.w };
            push_ui(commands, mesh, texture ? *texture : Texture{}, clip, scale, command.IdxOffset, command.ElemCount);
        }
    }
}
