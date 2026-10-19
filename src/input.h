#pragma once

#include "core.h"

#include <unordered_map>
#include <vector>

// Keyboard keys, gamepad buttons and mouse buttons share one key space, so an action
// can be bound to any of them: `Jump Space MouseRight` works like `Jump Space`.
// Keyboard keys are physical positions (WASD stays in place on any layout).
// Gamepad input isn't implemented yet, and MouseWheelX/Y aren't produced: scrolling
// is Input::scroll_x/scroll_y.
enum class Key : u8
{
    // Keyboard
    None = 0,
    Tab = 1,
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    PageUp,
    PageDown,
    Home,
    End,
    Insert,
    Delete,
    Backspace,
    Space,
    Enter,
    Escape,
    LeftCtrl, LeftShift, LeftAlt, LeftSuper,
    RightCtrl, RightShift, RightAlt, RightSuper,
    Shift,
    Ctrl,  // Ctrl (non-macOS), Cmd (macOS)
    Alt,   // Option/Menu
    Super, // Windows/Super (non-macOS), Ctrl (macOS)
    Menu,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    A, B, C, D, E, F, G, H, I, J,
    K, L, M, N, O, P, Q, R, S, T,
    U, V, W, X, Y, Z,
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18,
    F19, F20, F21, F22, F23, F24,
    Apostrophe,        // '
    Comma,             // ,
    Minus,             // -
    Period,            // .
    Slash,             // /
    Semicolon,         // ;
    Equal,             // =
    LeftBracket,       // [
    Backslash,         // \ (this text inhibit multiline comment caused by backslash)
    RightBracket,      // ]
    GraveAccent,       // `
    // Tilde,          // No need to uncomment. It's just an alias for GraveAccent key, handled by StringToKey function
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadEqual,
    AppBack,               // Available on some keyboard/mouses. Often referred as "Browser Back"
    AppForward,
    Oem102,                // Non-US backslash.

    // Gamepad (not implemented yet)
    // (analog values are 0.0f to 1.0f)
    // (download controller mapping PNG/PSD at http://dearimgui.com/controls_sheets)
    //                     // XBOX        | SWITCH  | PLAYSTA. | -> ACTION
    GamepadStart,          // Menu        | +       | Options  |
    GamepadBack,           // View        | -       | Share    |
    GamepadFaceLeft,       // X           | Y       | Square   | Tap: Toggle Menu. Hold: Windowing mode (Focus/Move/Resize windows)
    GamepadFaceRight,      // B           | A       | Circle   | Cancel / Close / Exit
    GamepadFaceUp,         // Y           | X       | Triangle | Text Input / On-screen Keyboard
    GamepadFaceDown,       // A           | B       | Cross    | Activate / Open / Toggle / Tweak
    GamepadDpadLeft,       // D-pad Left  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GamepadDpadRight,      // D-pad Right | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GamepadDpadUp,         // D-pad Up    | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GamepadDpadDown,       // D-pad Down  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GamepadL1,             // L Bumper    | L       | L1       | Tweak Slower / Focus Previous (in Windowing mode)
    GamepadR1,             // R Bumper    | R       | R1       | Tweak Faster / Focus Next (in Windowing mode)
    GamepadL2,             // L Trigger   | ZL      | L2       | [Analog]
    GamepadR2,             // R Trigger   | ZR      | R2       | [Analog]
    GamepadL3,             // L Stick     | L3      | L3       |
    GamepadR3,             // R Stick     | R3      | R3       |
    GamepadLStickLeft,     //             |         |          | [Analog] Move Window (in Windowing mode)
    GamepadLStickRight,    //             |         |          | [Analog] Move Window (in Windowing mode)
    GamepadLStickUp,       //             |         |          | [Analog] Move Window (in Windowing mode)
    GamepadLStickDown,     //             |         |          | [Analog] Move Window (in Windowing mode)
    GamepadRStickLeft,     //             |         |          | [Analog]
    GamepadRStickRight,    //             |         |          | [Analog]
    GamepadRStickUp,       //             |         |          | [Analog]
    GamepadRStickDown,     //             |         |          | [Analog]

    // Mouse Buttons
    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseX1,
    MouseX2,
    MouseWheelX,
    MouseWheelY,

    Count
};

constexpr u32 KEY_COUNT = (u32)Key::Count;

// Key name as written in bindings files ("Space", "MouseLeft"), or None if unknown.
// Names are case-insensitive.
Key key_from_name(const char *name);
const char *key_name(Key key);

// Characters typed in a frame past this many are dropped.
constexpr u32 INPUT_MAX_TEXT = 64;

// Raw input the window collects between two frames.
struct Input_State
{
    bool down[KEY_COUNT];        // Whether the key is down now
    u8   transitions[KEY_COUNT]; // Up/down changes since the last frame, so a press and
                                 // release between two frames isn't missed

    // Pointer position in window coordinates (Window::width/height), valid while
    // mouse_inside.
    r32  mouse_x;
    r32  mouse_y;
    bool mouse_inside;

    // Scroll since the last frame, in wheel notches: +y is away from the user (up),
    // +x is right. Touchpads report fractions.
    r32  scroll_x;
    r32  scroll_y;

    // Text typed since the last frame, as Unicode code points: what the keys make in
    // the keyboard's layout, repeated while one is held, for text fields. Control
    // characters (Enter, Tab, Backspace) aren't in it: they're keys.
    u32  text[INPUT_MAX_TEXT];
    u32  text_length;

    // Records a key going down or up. Ignores repeats of the same state and keeps
    // the either-side modifiers (Shift, Ctrl, Alt, Super) in sync.
    void set_key(Key key, bool is_down);
    // Adds a typed character, unless it's a control character or the frame's text is full.
    void add_text(u32 code_point);
    // Releases the keys or buttons that are down, e.g. when the window loses
    // keyboard focus or the pointer leaves it.
    void release_keyboard();
    void release_mouse_buttons();
    // Starts a new frame: clears transitions, scroll and text.
    void begin_frame();
};

constexpr u32 INPUT_MAX_ACTIONS         = 128;
constexpr u32 INPUT_MAX_KEYS_PER_ACTION = 4;
constexpr u32 INPUT_MAX_ACTION_NAME     = 32;

// Action name -> names of the keys bound to it, e.g. { "Jump", { "Space", "MouseRight" } }.
typedef std::unordered_map<str, std::vector<str>> Action_Key_Map;

// Actions bound to keys, queried by name: is_down("Jump") is true whichever of the
// action's keys or buttons is held. Where the bindings come from (e.g. a
// Key_Value_File) is up to the caller.
class Input
{
public:
    Input();

    // Binds each action to its keys by name; unknown names are logged and skipped.
    // Adds to the existing bindings.
    void use_bindings(const Action_Key_Map &bindings);
    bool bind(const char *action, Key key);
    void clear_bindings();

    // Call once per frame, after Window::read_input().
    void update(const Input_State &state);

    bool is_down(const char *action) const;
    bool was_pressed(const char *action) const;   // Went down since the last frame
    bool was_released(const char *action) const;  // Went up since the last frame

    bool is_down(Key key) const;
    bool was_pressed(Key key) const;
    bool was_released(Key key) const;

    r32  mouse_x       = 0.0f;
    r32  mouse_y       = 0.0f;
    r32  mouse_delta_x = 0.0f;
    r32  mouse_delta_y = 0.0f;
    bool mouse_inside  = false;
    r32  scroll_x      = 0.0f;
    r32  scroll_y      = 0.0f;
    u32  text[INPUT_MAX_TEXT] = {}; // Typed this frame, see Input_State::text
    u32  text_length   = 0;

private:
    struct Action
    {
        char name[INPUT_MAX_ACTION_NAME];
        Key  keys[INPUT_MAX_KEYS_PER_ACTION];
        u32  key_count;
    };

    const Action *find_action(const char *name) const;

    Action      actions[INPUT_MAX_ACTIONS];
    u32         action_count = 0;
    Input_State state;
};
