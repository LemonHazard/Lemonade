#pragma once

#include "core.h"

class  Window;
class  Input;
struct Render_Commands;
struct ImGuiContext;

// Dear ImGui for the game's tools and editor. The engine owns its context, for the whole
// run, so ImGui's windows, their state and its font atlas outlive reloads of the game's
// code: the context keeps function pointers (settings handlers, the font loader), which
// must point into the engine, not into the game library.
//
// The engine starts ImGui's frame before the game's update, with the frame's input, and
// ends it after, turning what the game submitted into render commands: Draw_Ui for
// ImGui's triangles, drawn over the game's, and Update_Texture for its font atlas as
// glyphs are added to it. The game has its own copy of ImGui's code (see lemonade.lua),
// whose context is set for the library alone, so it starts its frames with:
//
//   ImGui::SetCurrentContext(frame->imgui);
//
// A texture drawn with ImGui::Image() is given as the address of a Texture the game keeps
// in its memory, until the frame is drawn:
//
//   ImGui::Image((ImTextureID)(uintptr_t)&state->map_texture, { 256.0f, 256.0f });
//
// ImGui's positions are in window coordinates (see Window::width), its fonts drawn at
// the framebuffer's pixel density. Its layout is saved to imgui.ini in the working
// directory. Mistakes in the game's calls (a Begin() without its End()) are recovered
// from and logged, instead of asserting, so they don't end the run.
class Imgui_Host
{
public:
    Imgui_Host();
    ~Imgui_Host();

    // Call after Input::update(), before the game's update.
    void begin_frame(const Window &window, const Input &input, r32 dt);
    // Call after the game's update: its commands are drawn first.
    void end_frame(Render_Commands *commands);

    ImGuiContext *context = nullptr;
};
