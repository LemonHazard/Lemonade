#pragma once

#include "level.h"

// Dear ImGui widgets that change a level while it runs, for the game's editor window:
// make ImGui's context current first (see imgui_host.h).

// The level's entities: a list to pick one from, buttons that add one (a copy of the
// picked one, or the level's first model at the origin) and remove the picked one, and
// the picked one's fields, its script's data included. `selected` is the picked one's
// index. Returns true when entities were added or removed or a script was changed, for
// what the game finds by their scripts.
bool edit_level_entities(Level *level, u32 *selected, std::span<const Script_Info> scripts);

// The level's lighting, with a button that puts it back as it was loaded or last saved.
void edit_level_lighting(Level *level);
