#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "input.h"

// Collects one frame of raw input the way the window does: begin_frame() first, then
// the events, then Input::update().
struct Input_Spec_Frame
{
    Input       &input;
    Input_State &state;

    Input_Spec_Frame(Input &input, Input_State &state) : input(input), state(state)
    {
        this->state.begin_frame();
    }

    ~Input_Spec_Frame()
    {
        this->input.update(this->state);
    }
};

//--------------------------------------------------------------------------------------
// Key names
//--------------------------------------------------------------------------------------

TEST_CASE("key_from_name finds keys by their bindings file name", "[input][key_names]")
{
    REQUIRE(key_from_name("Space") == Key::Space);
    REQUIRE(key_from_name("MouseLeft") == Key::MouseLeft);
    REQUIRE(key_from_name("Num0") == Key::Num0);
    REQUIRE(key_from_name("F24") == Key::F24);
    REQUIRE(key_from_name("GamepadRStickDown") == Key::GamepadRStickDown);
}

TEST_CASE("key_from_name ignores case", "[input][key_names]")
{
    REQUIRE(key_from_name("space") == Key::Space);
    REQUIRE(key_from_name("SPACE") == Key::Space);
    REQUIRE(key_from_name("mOuSeRiGhT") == Key::MouseRight);
}

TEST_CASE("key_from_name maps Tilde to GraveAccent", "[input][key_names]")
{
    REQUIRE(key_from_name("Tilde") == Key::GraveAccent);
    REQUIRE(key_name(Key::GraveAccent) == str("GraveAccent"));
}

TEST_CASE("key_from_name returns None for unknown names", "[input][key_names]")
{
    REQUIRE(key_from_name("") == Key::None);
    REQUIRE(key_from_name("None") == Key::None);
    REQUIRE(key_from_name("Spacebar") == Key::None);
    REQUIRE(key_from_name("Spac") == Key::None);
    REQUIRE(key_from_name("Space ") == Key::None);
}

TEST_CASE("key_name returns \"None\" for keys without a name", "[input][key_names]")
{
    REQUIRE(key_name(Key::None) == str("None"));
    REQUIRE(key_name(Key::Count) == str("None"));
}

TEST_CASE("every key has a name that maps back to it", "[input][key_names]")
{
    for (u32 i = (u32)Key::None + 1; i < KEY_COUNT; ++i)
    {
        Key key = (Key)i;
        const char *name = key_name(key);

        INFO("key " << i << " named " << name);
        REQUIRE(str(name) != "None");
        REQUIRE(key_from_name(name) == key);
    }
}

//--------------------------------------------------------------------------------------
// Input_State
//--------------------------------------------------------------------------------------

TEST_CASE("Input_State records key changes", "[input][input_state]")
{
    Input_State state{};

    SECTION("a key going down counts one transition")
    {
        state.set_key(Key::A, true);

        REQUIRE(state.down[(u32)Key::A]);
        REQUIRE(state.transitions[(u32)Key::A] == 1);
    }

    SECTION("a press and release count two transitions")
    {
        state.set_key(Key::A, true);
        state.set_key(Key::A, false);

        REQUIRE_FALSE(state.down[(u32)Key::A]);
        REQUIRE(state.transitions[(u32)Key::A] == 2);
    }

    SECTION("repeats of the same state are ignored")
    {
        state.set_key(Key::A, true);
        state.set_key(Key::A, true);
        state.set_key(Key::A, true);

        REQUIRE(state.down[(u32)Key::A]);
        REQUIRE(state.transitions[(u32)Key::A] == 1);
    }

    SECTION("releasing a key that is up is ignored")
    {
        state.set_key(Key::A, false);

        REQUIRE_FALSE(state.down[(u32)Key::A]);
        REQUIRE(state.transitions[(u32)Key::A] == 0);
    }

    SECTION("other keys are left alone")
    {
        state.set_key(Key::A, true);

        REQUIRE_FALSE(state.down[(u32)Key::B]);
        REQUIRE(state.transitions[(u32)Key::B] == 0);
    }

    SECTION("None and out of range keys are ignored")
    {
        state.set_key(Key::None, true);
        state.set_key(Key::Count, true);

        REQUIRE_FALSE(state.down[(u32)Key::None]);
        REQUIRE(state.transitions[(u32)Key::None] == 0);
    }

    SECTION("transitions stop counting at 255")
    {
        for (u32 i = 0; i < 300; ++i)
        {
            state.set_key(Key::A, i % 2 == 0);
        }

        REQUIRE(state.transitions[(u32)Key::A] == 255);
        REQUIRE_FALSE(state.down[(u32)Key::A]);
    }
}

TEST_CASE("Input_State keeps either-side modifiers in sync", "[input][input_state]")
{
    struct Modifier
    {
        Key both;
        Key left;
        Key right;
    };

    Modifier modifier = GENERATE(Modifier{ Key::Shift, Key::LeftShift, Key::RightShift },
                                 Modifier{ Key::Ctrl,  Key::LeftCtrl,  Key::RightCtrl },
                                 Modifier{ Key::Alt,   Key::LeftAlt,   Key::RightAlt },
                                 Modifier{ Key::Super, Key::LeftSuper, Key::RightSuper });
    INFO(key_name(modifier.both));

    Input_State state{};
    u32 both = (u32)modifier.both;

    SECTION("left side goes down and up")
    {
        state.set_key(modifier.left, true);
        REQUIRE(state.down[both]);

        state.set_key(modifier.left, false);
        REQUIRE_FALSE(state.down[both]);
        REQUIRE(state.transitions[both] == 2);
    }

    SECTION("right side goes down and up")
    {
        state.set_key(modifier.right, true);
        REQUIRE(state.down[both]);

        state.set_key(modifier.right, false);
        REQUIRE_FALSE(state.down[both]);
    }

    SECTION("stays down while either side is held")
    {
        state.set_key(modifier.left, true);
        state.set_key(modifier.right, true);
        state.set_key(modifier.left, false);
        REQUIRE(state.down[both]);
        REQUIRE(state.transitions[both] == 1);

        state.set_key(modifier.right, false);
        REQUIRE_FALSE(state.down[both]);
        REQUIRE(state.transitions[both] == 2);
    }
}

TEST_CASE("Input_State::release_keyboard releases only keyboard keys", "[input][input_state]")
{
    Input_State state{};
    state.set_key(Key::W, true);
    state.set_key(Key::LeftShift, true);
    state.set_key(Key::Oem102, true);
    state.set_key(Key::MouseLeft, true);
    state.begin_frame();

    state.release_keyboard();

    REQUIRE_FALSE(state.down[(u32)Key::W]);
    REQUIRE_FALSE(state.down[(u32)Key::LeftShift]);
    REQUIRE_FALSE(state.down[(u32)Key::Shift]);
    REQUIRE_FALSE(state.down[(u32)Key::Oem102]);
    REQUIRE(state.transitions[(u32)Key::W] == 1);
    REQUIRE(state.transitions[(u32)Key::Shift] == 1);

    REQUIRE(state.down[(u32)Key::MouseLeft]);
    REQUIRE(state.transitions[(u32)Key::MouseLeft] == 0);

    SECTION("keys that were up get no transition")
    {
        REQUIRE(state.transitions[(u32)Key::A] == 0);
    }
}

TEST_CASE("Input_State::release_mouse_buttons releases only mouse buttons", "[input][input_state]")
{
    Input_State state{};
    state.set_key(Key::MouseLeft, true);
    state.set_key(Key::MouseX2, true);
    state.set_key(Key::Space, true);
    state.begin_frame();

    state.release_mouse_buttons();

    REQUIRE_FALSE(state.down[(u32)Key::MouseLeft]);
    REQUIRE_FALSE(state.down[(u32)Key::MouseX2]);
    REQUIRE(state.transitions[(u32)Key::MouseLeft] == 1);

    REQUIRE(state.down[(u32)Key::Space]);
    REQUIRE(state.transitions[(u32)Key::Space] == 0);
}

TEST_CASE("Input_State::begin_frame clears transitions and scroll only", "[input][input_state]")
{
    Input_State state{};
    state.set_key(Key::A, true);
    state.mouse_x      = 10.0f;
    state.mouse_y      = 20.0f;
    state.mouse_inside = true;
    state.scroll_x     = 1.5f;
    state.scroll_y     = -2.0f;

    state.begin_frame();

    REQUIRE(state.transitions[(u32)Key::A] == 0);
    REQUIRE(state.scroll_x == 0.0f);
    REQUIRE(state.scroll_y == 0.0f);

    REQUIRE(state.down[(u32)Key::A]);
    REQUIRE(state.mouse_x == 10.0f);
    REQUIRE(state.mouse_y == 20.0f);
    REQUIRE(state.mouse_inside);
}

//--------------------------------------------------------------------------------------
// Input: keys
//--------------------------------------------------------------------------------------

TEST_CASE("Input starts with nothing down", "[input][keys]")
{
    Input input;

    for (u32 i = 0; i < KEY_COUNT; ++i)
    {
        REQUIRE_FALSE(input.is_down((Key)i));
        REQUIRE_FALSE(input.was_pressed((Key)i));
        REQUIRE_FALSE(input.was_released((Key)i));
    }
    REQUIRE(input.mouse_x == 0.0f);
    REQUIRE(input.mouse_y == 0.0f);
    REQUIRE_FALSE(input.mouse_inside);
    REQUIRE(input.scroll_x == 0.0f);
    REQUIRE(input.scroll_y == 0.0f);
}

TEST_CASE("Input tracks a key over frames", "[input][keys]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::Space, true);
    }
    REQUIRE(input.is_down(Key::Space));
    REQUIRE(input.was_pressed(Key::Space));
    REQUIRE_FALSE(input.was_released(Key::Space));

    {
        Input_Spec_Frame frame(input, state); // held
    }
    REQUIRE(input.is_down(Key::Space));
    REQUIRE_FALSE(input.was_pressed(Key::Space));
    REQUIRE_FALSE(input.was_released(Key::Space));

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::Space, false);
    }
    REQUIRE_FALSE(input.is_down(Key::Space));
    REQUIRE_FALSE(input.was_pressed(Key::Space));
    REQUIRE(input.was_released(Key::Space));

    {
        Input_Spec_Frame frame(input, state); // up
    }
    REQUIRE_FALSE(input.is_down(Key::Space));
    REQUIRE_FALSE(input.was_pressed(Key::Space));
    REQUIRE_FALSE(input.was_released(Key::Space));
}

TEST_CASE("Input doesn't miss changes between two frames", "[input][keys]")
{
    Input       input;
    Input_State state{};

    SECTION("a tap: press and release")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::E, true);
            state.set_key(Key::E, false);
        }
        REQUIRE_FALSE(input.is_down(Key::E));
        REQUIRE(input.was_pressed(Key::E));
        REQUIRE(input.was_released(Key::E));
    }

    SECTION("a held key let go and pressed again")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::E, true);
        }
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::E, false);
            state.set_key(Key::E, true);
        }
        REQUIRE(input.is_down(Key::E));
        REQUIRE(input.was_pressed(Key::E));
        REQUIRE(input.was_released(Key::E));
    }

    SECTION("two taps")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::E, true);
            state.set_key(Key::E, false);
            state.set_key(Key::E, true);
            state.set_key(Key::E, false);
        }
        REQUIRE_FALSE(input.is_down(Key::E));
        REQUIRE(input.was_pressed(Key::E));
        REQUIRE(input.was_released(Key::E));
    }
}

TEST_CASE("Input keeps its own copy of the state", "[input][keys]")
{
    Input       input;
    Input_State state{};

    state.set_key(Key::A, true);
    input.update(state);

    state.begin_frame();
    state.set_key(Key::A, false);
    state.set_key(Key::B, true);

    REQUIRE(input.is_down(Key::A));
    REQUIRE(input.was_pressed(Key::A));
    REQUIRE_FALSE(input.is_down(Key::B));
}

TEST_CASE("Input sees modifier keys from either side", "[input][keys]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::RightCtrl, true);
    }
    REQUIRE(input.is_down(Key::Ctrl));
    REQUIRE(input.was_pressed(Key::Ctrl));
    REQUIRE(input.is_down(Key::RightCtrl));
    REQUIRE_FALSE(input.is_down(Key::LeftCtrl));
}

TEST_CASE("Input sees keys released on focus loss", "[input][keys]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::W, true);
        state.set_key(Key::MouseLeft, true);
    }
    {
        Input_Spec_Frame frame(input, state);
        state.release_keyboard();
    }
    REQUIRE_FALSE(input.is_down(Key::W));
    REQUIRE(input.was_released(Key::W));
    REQUIRE(input.is_down(Key::MouseLeft));

    {
        Input_Spec_Frame frame(input, state);
        state.release_mouse_buttons();
    }
    REQUIRE_FALSE(input.is_down(Key::MouseLeft));
    REQUIRE(input.was_released(Key::MouseLeft));
}

//--------------------------------------------------------------------------------------
// Input: actions
//--------------------------------------------------------------------------------------

TEST_CASE("Input actions follow any of their keys", "[input][actions]")
{
    Input       input;
    Input_State state{};

    REQUIRE(input.bind("Jump", Key::Space));
    REQUIRE(input.bind("Jump", Key::MouseRight));

    SECTION("nothing held")
    {
        input.update(state);

        REQUIRE_FALSE(input.is_down("Jump"));
        REQUIRE_FALSE(input.was_pressed("Jump"));
        REQUIRE_FALSE(input.was_released("Jump"));
    }

    SECTION("keyboard key")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::Space, true);
        }
        REQUIRE(input.is_down("Jump"));
        REQUIRE(input.was_pressed("Jump"));
    }

    SECTION("mouse button")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::MouseRight, true);
        }
        REQUIRE(input.is_down("Jump"));
        REQUIRE(input.was_pressed("Jump"));
    }

    SECTION("released by one key while another is held")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::Space, true);
            state.set_key(Key::MouseRight, true);
        }
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::Space, false);
        }
        REQUIRE(input.is_down("Jump"));
        REQUIRE(input.was_released("Jump"));
        REQUIRE_FALSE(input.was_pressed("Jump"));
    }

    SECTION("unrelated keys")
    {
        {
            Input_Spec_Frame frame(input, state);
            state.set_key(Key::Enter, true);
            state.set_key(Key::MouseLeft, true);
        }
        REQUIRE_FALSE(input.is_down("Jump"));
        REQUIRE_FALSE(input.was_pressed("Jump"));
    }
}

TEST_CASE("Input unknown actions are never down", "[input][actions]")
{
    Input       input;
    Input_State state{};
    input.bind("Jump", Key::Space);

    state.set_key(Key::Space, true);
    input.update(state);

    REQUIRE_FALSE(input.is_down("Fire"));
    REQUIRE_FALSE(input.was_pressed("Fire"));
    REQUIRE_FALSE(input.was_released("Fire"));
    REQUIRE_FALSE(input.is_down(""));
}

TEST_CASE("Input action names are case-sensitive", "[input][actions]")
{
    Input       input;
    Input_State state{};
    input.bind("Jump", Key::Space);

    state.set_key(Key::Space, true);
    input.update(state);

    REQUIRE(input.is_down("Jump"));
    REQUIRE_FALSE(input.is_down("jump"));
}

TEST_CASE("Input actions can share a key", "[input][actions]")
{
    Input       input;
    Input_State state{};
    input.bind("Confirm", Key::Enter);
    input.bind("Next", Key::Enter);

    state.set_key(Key::Enter, true);
    input.update(state);

    REQUIRE(input.is_down("Confirm"));
    REQUIRE(input.is_down("Next"));
}

TEST_CASE("Input::bind limits", "[input][actions]")
{
    Input       input;
    Input_State state{};

    SECTION("None can't be bound")
    {
        REQUIRE_FALSE(input.bind("Jump", Key::None));
    }

    SECTION("binding the same key twice takes one slot")
    {
        REQUIRE(input.bind("Move", Key::W));
        REQUIRE(input.bind("Move", Key::W));
        REQUIRE(input.bind("Move", Key::UpArrow));
        REQUIRE(input.bind("Move", Key::Keypad8));
        REQUIRE(input.bind("Move", Key::GamepadDpadUp));
    }

    SECTION("at most INPUT_MAX_KEYS_PER_ACTION keys per action")
    {
        Key keys[] = { Key::A, Key::B, Key::C, Key::D };
        static_assert(sizeof(keys) / sizeof(keys[0]) == INPUT_MAX_KEYS_PER_ACTION);
        for (Key key : keys)
        {
            REQUIRE(input.bind("Move", key));
        }

        REQUIRE_FALSE(input.bind("Move", Key::E));

        state.set_key(Key::E, true);
        input.update(state);
        REQUIRE_FALSE(input.is_down("Move"));
    }

    SECTION("action names up to INPUT_MAX_ACTION_NAME - 1 characters")
    {
        str longest(INPUT_MAX_ACTION_NAME - 1, 'a');
        str too_long(INPUT_MAX_ACTION_NAME, 'b');

        REQUIRE(input.bind(longest.c_str(), Key::Space));
        REQUIRE_FALSE(input.bind(too_long.c_str(), Key::Space));

        state.set_key(Key::Space, true);
        input.update(state);
        REQUIRE(input.is_down(longest.c_str()));
        REQUIRE_FALSE(input.is_down(too_long.c_str()));
    }

    SECTION("at most INPUT_MAX_ACTIONS actions")
    {
        for (u32 i = 0; i < INPUT_MAX_ACTIONS; ++i)
        {
            str name = "Action" + std::to_string(i);
            REQUIRE(input.bind(name.c_str(), Key::Space));
        }

        REQUIRE_FALSE(input.bind("OneTooMany", Key::Space));

        // Existing actions can still get more keys.
        REQUIRE(input.bind("Action0", Key::Enter));

        state.set_key(Key::Enter, true);
        input.update(state);
        REQUIRE(input.is_down("Action0"));
        REQUIRE_FALSE(input.is_down("OneTooMany"));
    }
}

TEST_CASE("Input::clear_bindings removes every action", "[input][actions]")
{
    Input       input;
    Input_State state{};
    input.bind("Jump", Key::Space);
    input.bind("Quit", Key::Escape);

    input.clear_bindings();

    state.set_key(Key::Space, true);
    state.set_key(Key::Escape, true);
    input.update(state);

    REQUIRE_FALSE(input.is_down("Jump"));
    REQUIRE_FALSE(input.is_down("Quit"));
    REQUIRE(input.is_down(Key::Space));

    SECTION("and frees their slots")
    {
        for (u32 i = 0; i < INPUT_MAX_ACTIONS; ++i)
        {
            str name = "Action" + std::to_string(i);
            REQUIRE(input.bind(name.c_str(), Key::Space));
        }
    }
}

TEST_CASE("Input::use_bindings binds keys by name", "[input][actions]")
{
    Input       input;
    Input_State state{};

    input.use_bindings({
        { "MoveForward", { "W", "UpArrow" } },
        { "Jump",        { "space", "MOUSERIGHT" } },
        { "Fire",        { "NotAKey", "MouseLeft" } },
        { "Nothing",     { "NotAKey" } },
    });

    SECTION("every named key triggers its action")
    {
        Key key = GENERATE(Key::W, Key::UpArrow);
        state.set_key(key, true);
        input.update(state);

        REQUIRE(input.is_down("MoveForward"));
        REQUIRE_FALSE(input.is_down("Jump"));
    }

    SECTION("names are case-insensitive")
    {
        Key key = GENERATE(Key::Space, Key::MouseRight);
        state.set_key(key, true);
        input.update(state);

        REQUIRE(input.is_down("Jump"));
    }

    SECTION("unknown names are skipped, the rest still bound")
    {
        state.set_key(Key::MouseLeft, true);
        input.update(state);

        REQUIRE(input.is_down("Fire"));
    }

    SECTION("an action with only unknown names isn't created")
    {
        for (u32 i = 0; i < INPUT_MAX_ACTIONS - 3; ++i)
        {
            str name = "Action" + std::to_string(i);
            REQUIRE(input.bind(name.c_str(), Key::Space));
        }
        REQUIRE_FALSE(input.bind("OneTooMany", Key::Space));
    }

    SECTION("adds to the existing bindings")
    {
        input.use_bindings({ { "Jump", { "Enter" } }, { "Quit", { "Escape" } } });

        state.set_key(Key::Space, true);
        state.set_key(Key::Enter, true);
        state.set_key(Key::Escape, true);
        input.update(state);
        REQUIRE(input.is_down("Jump"));
        REQUIRE(input.is_down("Quit"));

        state.begin_frame();
        state.set_key(Key::Space, false);
        input.update(state);
        REQUIRE(input.is_down("Jump"));
    }
}

//--------------------------------------------------------------------------------------
// Input: mouse
//--------------------------------------------------------------------------------------

static void input_spec_move_mouse(Input &input, Input_State &state, r32 x, r32 y, bool inside)
{
    Input_Spec_Frame frame(input, state);
    state.mouse_x      = x;
    state.mouse_y      = y;
    state.mouse_inside = inside;
}

TEST_CASE("Input copies the mouse position", "[input][mouse]")
{
    Input       input;
    Input_State state{};

    input_spec_move_mouse(input, state, 100.0f, 50.0f, true);

    REQUIRE(input.mouse_x == 100.0f);
    REQUIRE(input.mouse_y == 50.0f);
    REQUIRE(input.mouse_inside);
}

TEST_CASE("Input mouse delta", "[input][mouse]")
{
    Input       input;
    Input_State state{};

    SECTION("is zero on the first frame inside")
    {
        input_spec_move_mouse(input, state, 100.0f, 50.0f, true);

        REQUIRE(input.mouse_delta_x == 0.0f);
        REQUIRE(input.mouse_delta_y == 0.0f);
    }

    SECTION("follows the pointer while it stays inside")
    {
        input_spec_move_mouse(input, state, 100.0f, 50.0f, true);
        input_spec_move_mouse(input, state, 110.0f, 45.0f, true);
        REQUIRE(input.mouse_delta_x == 10.0f);
        REQUIRE(input.mouse_delta_y == -5.0f);

        input_spec_move_mouse(input, state, 110.0f, 45.0f, true);
        REQUIRE(input.mouse_delta_x == 0.0f);
        REQUIRE(input.mouse_delta_y == 0.0f);
    }

    SECTION("is zero when the pointer leaves")
    {
        input_spec_move_mouse(input, state, 100.0f, 50.0f, true);
        input_spec_move_mouse(input, state, 0.0f, 0.0f, false);

        REQUIRE_FALSE(input.mouse_inside);
        REQUIRE(input.mouse_delta_x == 0.0f);
        REQUIRE(input.mouse_delta_y == 0.0f);
    }

    SECTION("is zero when the pointer re-enters somewhere else")
    {
        input_spec_move_mouse(input, state, 100.0f, 50.0f, true);
        input_spec_move_mouse(input, state, 100.0f, 50.0f, false);
        input_spec_move_mouse(input, state, 500.0f, 300.0f, true);

        REQUIRE(input.mouse_inside);
        REQUIRE(input.mouse_x == 500.0f);
        REQUIRE(input.mouse_delta_x == 0.0f);
        REQUIRE(input.mouse_delta_y == 0.0f);

        input_spec_move_mouse(input, state, 502.0f, 303.0f, true);
        REQUIRE(input.mouse_delta_x == 2.0f);
        REQUIRE(input.mouse_delta_y == 3.0f);
    }
}

TEST_CASE("Input scroll lasts one frame", "[input][mouse]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.scroll_x = 0.5f;
        state.scroll_y = -1.0f;
    }
    REQUIRE(input.scroll_x == 0.5f);
    REQUIRE(input.scroll_y == -1.0f);

    {
        Input_Spec_Frame frame(input, state);
    }
    REQUIRE(input.scroll_x == 0.0f);
    REQUIRE(input.scroll_y == 0.0f);
}

TEST_CASE("Input mouse buttons work as keys", "[input][mouse]")
{
    Input       input;
    Input_State state{};

    Key button = GENERATE(Key::MouseLeft, Key::MouseRight, Key::MouseMiddle, Key::MouseX1, Key::MouseX2);
    INFO(key_name(button));

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(button, true);
    }
    REQUIRE(input.is_down(button));
    REQUIRE(input.was_pressed(button));

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(button, false);
    }
    REQUIRE_FALSE(input.is_down(button));
    REQUIRE(input.was_released(button));
}

//--------------------------------------------------------------------------------------
// Text
//--------------------------------------------------------------------------------------

TEST_CASE("Input text lasts one frame", "[input][text]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.add_text('h');
        state.add_text(0x00E9); // é
        state.add_text(0x1F600);
    }
    REQUIRE(input.text_length == 3);
    REQUIRE(input.text[0] == 'h');
    REQUIRE(input.text[1] == 0x00E9);
    REQUIRE(input.text[2] == 0x1F600);

    {
        Input_Spec_Frame frame(input, state);
    }
    REQUIRE(input.text_length == 0);
}

TEST_CASE("Input text leaves out control characters and what isn't Unicode", "[input][text]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        u32 code_point = GENERATE(0x00u, 0x08u, 0x09u, 0x0Du, 0x1Bu, 0x7Fu, 0x9Fu, 0xD800u, 0xDFFFu, 0x110000u);
        INFO(code_point);
        state.add_text(code_point);
    }
    REQUIRE(input.text_length == 0);
}

TEST_CASE("Input text drops what doesn't fit in a frame", "[input][text]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        for (u32 i = 0; i < INPUT_MAX_TEXT + 10; ++i) state.add_text('a' + i % 26);
    }
    REQUIRE(input.text_length == INPUT_MAX_TEXT);
    REQUIRE(input.text[INPUT_MAX_TEXT - 1] == 'a' + (INPUT_MAX_TEXT - 1) % 26);
}

//--------------------------------------------------------------------------------------
// Enabled
//--------------------------------------------------------------------------------------

TEST_CASE("Input disabled reads every key and action as up", "[input][enabled]")
{
    Input       input;
    Input_State state{};
    input.bind("Jump", Key::Space);

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::Space, true);
        state.set_key(Key::MouseLeft, true);
    }
    input.enabled = false;
    REQUIRE_FALSE(input.is_down(Key::Space));
    REQUIRE_FALSE(input.was_pressed(Key::Space));
    REQUIRE_FALSE(input.is_down(Key::MouseLeft));
    REQUIRE_FALSE(input.is_down("Jump"));
    REQUIRE_FALSE(input.was_pressed("Jump"));

    input.enabled = true;
    REQUIRE(input.is_down(Key::Space));
    REQUIRE(input.was_pressed("Jump"));

    input.enabled = false;
    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::Space, false);
    }
    REQUIRE(input.was_released(Key::Space));
}

TEST_CASE("Input is enabled again by the next update", "[input][enabled]")
{
    Input       input;
    Input_State state{};

    {
        Input_Spec_Frame frame(input, state);
        state.set_key(Key::A, true);
    }
    input.enabled = false;
    REQUIRE_FALSE(input.is_down(Key::A));

    {
        Input_Spec_Frame frame(input, state);
    }
    REQUIRE(input.enabled);
    REQUIRE(input.is_down(Key::A));
}
