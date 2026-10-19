#include "input.h"
#include "logger.h"

#include <cstring>

// Names in bindings files; key_name() returns the first entry for a key.
static const struct
{
    const char *name;
    Key         key;
}
input_key_names[] = {
    { "Tab",                 Key::Tab },
    { "LeftArrow",           Key::LeftArrow },
    { "RightArrow",          Key::RightArrow },
    { "UpArrow",             Key::UpArrow },
    { "DownArrow",           Key::DownArrow },
    { "PageUp",              Key::PageUp },
    { "PageDown",            Key::PageDown },
    { "Home",                Key::Home },
    { "End",                 Key::End },
    { "Insert",              Key::Insert },
    { "Delete",              Key::Delete },
    { "Backspace",           Key::Backspace },
    { "Space",               Key::Space },
    { "Enter",               Key::Enter },
    { "Escape",              Key::Escape },
    { "LeftCtrl",            Key::LeftCtrl },
    { "LeftShift",           Key::LeftShift },
    { "LeftAlt",             Key::LeftAlt },
    { "LeftSuper",           Key::LeftSuper },
    { "RightCtrl",           Key::RightCtrl },
    { "RightShift",          Key::RightShift },
    { "RightAlt",            Key::RightAlt },
    { "RightSuper",          Key::RightSuper },
    { "Shift",               Key::Shift },
    { "Ctrl",                Key::Ctrl },
    { "Alt",                 Key::Alt },
    { "Super",               Key::Super },
    { "Menu",                Key::Menu },
    { "Num0",                Key::Num0 },
    { "Num1",                Key::Num1 },
    { "Num2",                Key::Num2 },
    { "Num3",                Key::Num3 },
    { "Num4",                Key::Num4 },
    { "Num5",                Key::Num5 },
    { "Num6",                Key::Num6 },
    { "Num7",                Key::Num7 },
    { "Num8",                Key::Num8 },
    { "Num9",                Key::Num9 },
    { "A",                   Key::A },
    { "B",                   Key::B },
    { "C",                   Key::C },
    { "D",                   Key::D },
    { "E",                   Key::E },
    { "F",                   Key::F },
    { "G",                   Key::G },
    { "H",                   Key::H },
    { "I",                   Key::I },
    { "J",                   Key::J },
    { "K",                   Key::K },
    { "L",                   Key::L },
    { "M",                   Key::M },
    { "N",                   Key::N },
    { "O",                   Key::O },
    { "P",                   Key::P },
    { "Q",                   Key::Q },
    { "R",                   Key::R },
    { "S",                   Key::S },
    { "T",                   Key::T },
    { "U",                   Key::U },
    { "V",                   Key::V },
    { "W",                   Key::W },
    { "X",                   Key::X },
    { "Y",                   Key::Y },
    { "Z",                   Key::Z },
    { "F1",                  Key::F1 },
    { "F2",                  Key::F2 },
    { "F3",                  Key::F3 },
    { "F4",                  Key::F4 },
    { "F5",                  Key::F5 },
    { "F6",                  Key::F6 },
    { "F7",                  Key::F7 },
    { "F8",                  Key::F8 },
    { "F9",                  Key::F9 },
    { "F10",                 Key::F10 },
    { "F11",                 Key::F11 },
    { "F12",                 Key::F12 },
    { "F13",                 Key::F13 },
    { "F14",                 Key::F14 },
    { "F15",                 Key::F15 },
    { "F16",                 Key::F16 },
    { "F17",                 Key::F17 },
    { "F18",                 Key::F18 },
    { "F19",                 Key::F19 },
    { "F20",                 Key::F20 },
    { "F21",                 Key::F21 },
    { "F22",                 Key::F22 },
    { "F23",                 Key::F23 },
    { "F24",                 Key::F24 },
    { "Apostrophe",          Key::Apostrophe },
    { "Comma",               Key::Comma },
    { "Minus",               Key::Minus },
    { "Period",              Key::Period },
    { "Slash",               Key::Slash },
    { "Semicolon",           Key::Semicolon },
    { "Equal",               Key::Equal },
    { "LeftBracket",         Key::LeftBracket },
    { "Backslash",           Key::Backslash },
    { "RightBracket",        Key::RightBracket },
    { "GraveAccent",         Key::GraveAccent },
    { "CapsLock",            Key::CapsLock },
    { "ScrollLock",          Key::ScrollLock },
    { "NumLock",             Key::NumLock },
    { "PrintScreen",         Key::PrintScreen },
    { "Pause",               Key::Pause },
    { "Keypad0",             Key::Keypad0 },
    { "Keypad1",             Key::Keypad1 },
    { "Keypad2",             Key::Keypad2 },
    { "Keypad3",             Key::Keypad3 },
    { "Keypad4",             Key::Keypad4 },
    { "Keypad5",             Key::Keypad5 },
    { "Keypad6",             Key::Keypad6 },
    { "Keypad7",             Key::Keypad7 },
    { "Keypad8",             Key::Keypad8 },
    { "Keypad9",             Key::Keypad9 },
    { "KeypadDecimal",       Key::KeypadDecimal },
    { "KeypadDivide",        Key::KeypadDivide },
    { "KeypadMultiply",      Key::KeypadMultiply },
    { "KeypadSubtract",      Key::KeypadSubtract },
    { "KeypadAdd",           Key::KeypadAdd },
    { "KeypadEnter",         Key::KeypadEnter },
    { "KeypadEqual",         Key::KeypadEqual },
    { "AppBack",             Key::AppBack },
    { "AppForward",          Key::AppForward },
    { "Oem102",              Key::Oem102 },
    { "GamepadStart",        Key::GamepadStart },
    { "GamepadBack",         Key::GamepadBack },
    { "GamepadFaceLeft",     Key::GamepadFaceLeft },
    { "GamepadFaceRight",    Key::GamepadFaceRight },
    { "GamepadFaceUp",       Key::GamepadFaceUp },
    { "GamepadFaceDown",     Key::GamepadFaceDown },
    { "GamepadDpadLeft",     Key::GamepadDpadLeft },
    { "GamepadDpadRight",    Key::GamepadDpadRight },
    { "GamepadDpadUp",       Key::GamepadDpadUp },
    { "GamepadDpadDown",     Key::GamepadDpadDown },
    { "GamepadL1",           Key::GamepadL1 },
    { "GamepadR1",           Key::GamepadR1 },
    { "GamepadL2",           Key::GamepadL2 },
    { "GamepadR2",           Key::GamepadR2 },
    { "GamepadL3",           Key::GamepadL3 },
    { "GamepadR3",           Key::GamepadR3 },
    { "GamepadLStickLeft",   Key::GamepadLStickLeft },
    { "GamepadLStickRight",  Key::GamepadLStickRight },
    { "GamepadLStickUp",     Key::GamepadLStickUp },
    { "GamepadLStickDown",   Key::GamepadLStickDown },
    { "GamepadRStickLeft",   Key::GamepadRStickLeft },
    { "GamepadRStickRight",  Key::GamepadRStickRight },
    { "GamepadRStickUp",     Key::GamepadRStickUp },
    { "GamepadRStickDown",   Key::GamepadRStickDown },
    { "MouseLeft",           Key::MouseLeft },
    { "MouseRight",          Key::MouseRight },
    { "MouseMiddle",         Key::MouseMiddle },
    { "MouseX1",             Key::MouseX1 },
    { "MouseX2",             Key::MouseX2 },
    { "MouseWheelX",         Key::MouseWheelX },
    { "MouseWheelY",         Key::MouseWheelY },
    { "Tilde",               Key::GraveAccent }, // alias
};

static bool input_names_equal_ignore_case(const char *a, const char *b)
{
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; };
    while (*a && lower(*a) == lower(*b))
    {
        ++a;
        ++b;
    }
    return *a == *b;
}

Key key_from_name(const char *name)
{
    for (const auto &entry : input_key_names)
    {
        if (input_names_equal_ignore_case(entry.name, name)) return entry.key;
    }
    return Key::None;
}

const char *key_name(Key key)
{
    for (const auto &entry : input_key_names)
    {
        if (entry.key == key) return entry.name;
    }
    return "None";
}

//--------------------------------------------------------------------------------------
// Input_State
//--------------------------------------------------------------------------------------

static void input_state_set(Input_State &state, Key key, bool is_down)
{
    u32 index = (u32)key;
    if (key == Key::None || index >= KEY_COUNT || state.down[index] == is_down) return;

    state.down[index] = is_down;
    if (state.transitions[index] < 255) state.transitions[index] += 1;
}

void Input_State::set_key(Key key, bool is_down)
{
    input_state_set(*this, key, is_down);

    auto either = [this](Key both, Key left, Key right) {
        input_state_set(*this, both, this->down[(u32)left] || this->down[(u32)right]);
    };

    switch (key)
    {
    case Key::LeftShift: case Key::RightShift: either(Key::Shift, Key::LeftShift, Key::RightShift); break;
    case Key::LeftCtrl:  case Key::RightCtrl:  either(Key::Ctrl,  Key::LeftCtrl,  Key::RightCtrl);  break;
    case Key::LeftAlt:   case Key::RightAlt:   either(Key::Alt,   Key::LeftAlt,   Key::RightAlt);   break;
    case Key::LeftSuper: case Key::RightSuper: either(Key::Super, Key::LeftSuper, Key::RightSuper); break;
    default: break;
    }
}

static void input_state_release(Input_State &state, Key first, Key last)
{
    for (u32 i = (u32)first; i <= (u32)last; ++i)
    {
        if (state.down[i]) state.set_key((Key)i, false);
    }
}

void Input_State::release_keyboard()
{
    input_state_release(*this, Key::Tab, Key::Oem102);
}

void Input_State::release_mouse_buttons()
{
    input_state_release(*this, Key::MouseLeft, Key::MouseWheelY);
}

void Input_State::add_text(u32 code_point)
{
    bool control   = code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0);
    bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
    if (control || surrogate || code_point > 0x10FFFF || this->text_length == INPUT_MAX_TEXT) return;

    this->text[this->text_length++] = code_point;
}

void Input_State::begin_frame()
{
    memset(this->transitions, 0, sizeof(this->transitions));
    this->scroll_x    = 0.0f;
    this->scroll_y    = 0.0f;
    this->text_length = 0;
}

//--------------------------------------------------------------------------------------
// Input
//--------------------------------------------------------------------------------------

Input::Input() : state{}
{
}

bool Input::bind(const char *action, Key key)
{
    if (key == Key::None) return false;

    if (strlen(action) >= INPUT_MAX_ACTION_NAME)
    {
        log_warn("Action name \"%s\" is longer than %u characters", action, INPUT_MAX_ACTION_NAME - 1);
        return false;
    }

    Action *found = (Action *)this->find_action(action);
    if (!found)
    {
        if (this->action_count == INPUT_MAX_ACTIONS)
        {
            log_warn("Can't bind \"%s\": more than %u actions", action, INPUT_MAX_ACTIONS);
            return false;
        }
        found = &this->actions[this->action_count++];
        strcpy(found->name, action);
        found->key_count = 0;
    }

    for (u32 i = 0; i < found->key_count; ++i)
    {
        if (found->keys[i] == key) return true;
    }

    if (found->key_count == INPUT_MAX_KEYS_PER_ACTION)
    {
        log_warn("Can't bind %s to \"%s\": at most %u keys per action", key_name(key), action, INPUT_MAX_KEYS_PER_ACTION);
        return false;
    }

    found->keys[found->key_count++] = key;
    return true;
}

void Input::clear_bindings()
{
    this->action_count = 0;
}

void Input::use_bindings(const Action_Key_Map &bindings)
{
    for (const auto &[action, key_names] : bindings)
    {
        for (const auto &name : key_names)
        {
            Key key = key_from_name(name.c_str());
            if (key == Key::None)
            {
                log_warn("Unknown key/button name \"%s\" for action \"%s\"", name.c_str(), action.c_str());
                continue;
            }

            this->bind(action.c_str(), key);
        }
    }
}

void Input::update(const Input_State &new_state)
{
    bool was_inside = this->state.mouse_inside;
    r32  last_x     = this->state.mouse_x;
    r32  last_y     = this->state.mouse_y;

    this->state = new_state;

    this->mouse_x      = new_state.mouse_x;
    this->mouse_y      = new_state.mouse_y;
    this->mouse_inside = new_state.mouse_inside;
    this->scroll_x     = new_state.scroll_x;
    this->scroll_y     = new_state.scroll_y;
    this->text_length  = new_state.text_length;
    memcpy(this->text, new_state.text, new_state.text_length * sizeof(u32));

    // No delta across leaving and re-entering the window, where the position jumps.
    bool tracking = was_inside && new_state.mouse_inside;
    this->mouse_delta_x = tracking ? new_state.mouse_x - last_x : 0.0f;
    this->mouse_delta_y = tracking ? new_state.mouse_y - last_y : 0.0f;
}

const Input::Action *Input::find_action(const char *name) const
{
    for (u32 i = 0; i < this->action_count; ++i)
    {
        if (strcmp(this->actions[i].name, name) == 0) return &this->actions[i];
    }
    return nullptr;
}

bool Input::is_down(Key key) const
{
    return this->state.down[(u32)key];
}

// A transition count tells pressed from released even when both happened between two
// frames: a key that is down now went down at least once; one that is up now only did
// if it changed at least twice.
bool Input::was_pressed(Key key) const
{
    u32 index = (u32)key;
    u8  count = this->state.transitions[index];
    return this->state.down[index] ? count >= 1 : count >= 2;
}

bool Input::was_released(Key key) const
{
    u32 index = (u32)key;
    u8  count = this->state.transitions[index];
    return this->state.down[index] ? count >= 2 : count >= 1;
}

bool Input::is_down(const char *action) const
{
    const Action *found = this->find_action(action);
    if (!found) return false;

    for (u32 i = 0; i < found->key_count; ++i)
    {
        if (this->is_down(found->keys[i])) return true;
    }
    return false;
}

bool Input::was_pressed(const char *action) const
{
    const Action *found = this->find_action(action);
    if (!found) return false;

    for (u32 i = 0; i < found->key_count; ++i)
    {
        if (this->was_pressed(found->keys[i])) return true;
    }
    return false;
}

bool Input::was_released(const char *action) const
{
    const Action *found = this->find_action(action);
    if (!found) return false;

    for (u32 i = 0; i < found->key_count; ++i)
    {
        if (this->was_released(found->keys[i])) return true;
    }
    return false;
}
