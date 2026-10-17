#ifdef OS_LINUX

#include "../window.h"
#include "../logger.h"

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

#include "window.h"

// Wayland window. GNOME doesn't draw server-side decorations, so the title bar
// (move, resize, close buttons) comes from libdecor, which also creates the
// xdg-shell toplevel for us and draws the title bar with the GTK theme.

static void native_window_scale_changed(Window *window);

static const wl_registry_listener native_window_registry_listener = {
    .global        = Native_Window::registry_global,
    .global_remove = Native_Window::registry_global_remove,
};

static libdecor_interface native_window_decor_interface = {
    .error = Native_Window::decor_error,
};

static libdecor_frame_interface native_window_frame_interface = {
    .configure = Native_Window::frame_configure,
    .close     = Native_Window::frame_close,
    .commit    = Native_Window::frame_commit,
};

static const wl_surface_listener native_window_surface_listener = {
    .enter                      = Native_Window::surface_enter,
    .leave                      = Native_Window::surface_leave,
    .preferred_buffer_scale     = Native_Window::surface_preferred_buffer_scale,
    .preferred_buffer_transform = Native_Window::surface_preferred_buffer_transform,
};

static const wp_fractional_scale_v1_listener native_window_fractional_scale_listener = {
    .preferred_scale = Native_Window::fractional_scale_preferred,
};

static const wl_seat_listener native_window_seat_listener = {
    .capabilities = Native_Window::seat_capabilities,
    .name         = Native_Window::seat_name,
};

static const wl_keyboard_listener native_window_keyboard_listener = {
    .keymap      = Native_Window::keyboard_keymap,
    .enter       = Native_Window::keyboard_enter,
    .leave       = Native_Window::keyboard_leave,
    .key         = Native_Window::keyboard_key,
    .modifiers   = Native_Window::keyboard_modifiers,
    .repeat_info = Native_Window::keyboard_repeat_info,
};

static const wl_pointer_listener native_window_pointer_listener = {
    .enter         = Native_Window::pointer_enter,
    .leave         = Native_Window::pointer_leave,
    .motion        = Native_Window::pointer_motion,
    .button        = Native_Window::pointer_button,
    .axis          = Native_Window::pointer_axis,
    .frame         = Native_Window::pointer_frame,
    .axis_source   = Native_Window::pointer_axis_source,
    .axis_stop     = Native_Window::pointer_axis_stop,
    .axis_discrete = Native_Window::pointer_axis_discrete,
    .axis_value120 = Native_Window::pointer_axis_value120,
};

// Wayland reports keys as Linux evdev scancodes: physical positions, independent of
// the keyboard layout.
static Key native_window_key_from_scancode(u32 scancode)
{
    switch (scancode)
    {
    case KEY_TAB:        return Key::Tab;
    case KEY_LEFT:       return Key::LeftArrow;
    case KEY_RIGHT:      return Key::RightArrow;
    case KEY_UP:         return Key::UpArrow;
    case KEY_DOWN:       return Key::DownArrow;
    case KEY_PAGEUP:     return Key::PageUp;
    case KEY_PAGEDOWN:   return Key::PageDown;
    case KEY_HOME:       return Key::Home;
    case KEY_END:        return Key::End;
    case KEY_INSERT:     return Key::Insert;
    case KEY_DELETE:     return Key::Delete;
    case KEY_BACKSPACE:  return Key::Backspace;
    case KEY_SPACE:      return Key::Space;
    case KEY_ENTER:      return Key::Enter;
    case KEY_ESC:        return Key::Escape;
    case KEY_LEFTCTRL:   return Key::LeftCtrl;
    case KEY_LEFTSHIFT:  return Key::LeftShift;
    case KEY_LEFTALT:    return Key::LeftAlt;
    case KEY_LEFTMETA:   return Key::LeftSuper;
    case KEY_RIGHTCTRL:  return Key::RightCtrl;
    case KEY_RIGHTSHIFT: return Key::RightShift;
    case KEY_RIGHTALT:   return Key::RightAlt;
    case KEY_RIGHTMETA:  return Key::RightSuper;
    case KEY_COMPOSE:    return Key::Menu;
    case KEY_0:          return Key::Num0;
    case KEY_1:          return Key::Num1;
    case KEY_2:          return Key::Num2;
    case KEY_3:          return Key::Num3;
    case KEY_4:          return Key::Num4;
    case KEY_5:          return Key::Num5;
    case KEY_6:          return Key::Num6;
    case KEY_7:          return Key::Num7;
    case KEY_8:          return Key::Num8;
    case KEY_9:          return Key::Num9;
    case KEY_A:          return Key::A;
    case KEY_B:          return Key::B;
    case KEY_C:          return Key::C;
    case KEY_D:          return Key::D;
    case KEY_E:          return Key::E;
    case KEY_F:          return Key::F;
    case KEY_G:          return Key::G;
    case KEY_H:          return Key::H;
    case KEY_I:          return Key::I;
    case KEY_J:          return Key::J;
    case KEY_K:          return Key::K;
    case KEY_L:          return Key::L;
    case KEY_M:          return Key::M;
    case KEY_N:          return Key::N;
    case KEY_O:          return Key::O;
    case KEY_P:          return Key::P;
    case KEY_Q:          return Key::Q;
    case KEY_R:          return Key::R;
    case KEY_S:          return Key::S;
    case KEY_T:          return Key::T;
    case KEY_U:          return Key::U;
    case KEY_V:          return Key::V;
    case KEY_W:          return Key::W;
    case KEY_X:          return Key::X;
    case KEY_Y:          return Key::Y;
    case KEY_Z:          return Key::Z;
    case KEY_F1:         return Key::F1;
    case KEY_F2:         return Key::F2;
    case KEY_F3:         return Key::F3;
    case KEY_F4:         return Key::F4;
    case KEY_F5:         return Key::F5;
    case KEY_F6:         return Key::F6;
    case KEY_F7:         return Key::F7;
    case KEY_F8:         return Key::F8;
    case KEY_F9:         return Key::F9;
    case KEY_F10:        return Key::F10;
    case KEY_F11:        return Key::F11;
    case KEY_F12:        return Key::F12;
    case KEY_F13:        return Key::F13;
    case KEY_F14:        return Key::F14;
    case KEY_F15:        return Key::F15;
    case KEY_F16:        return Key::F16;
    case KEY_F17:        return Key::F17;
    case KEY_F18:        return Key::F18;
    case KEY_F19:        return Key::F19;
    case KEY_F20:        return Key::F20;
    case KEY_F21:        return Key::F21;
    case KEY_F22:        return Key::F22;
    case KEY_F23:        return Key::F23;
    case KEY_F24:        return Key::F24;
    case KEY_APOSTROPHE: return Key::Apostrophe;
    case KEY_COMMA:      return Key::Comma;
    case KEY_MINUS:      return Key::Minus;
    case KEY_DOT:        return Key::Period;
    case KEY_SLASH:      return Key::Slash;
    case KEY_SEMICOLON:  return Key::Semicolon;
    case KEY_EQUAL:      return Key::Equal;
    case KEY_LEFTBRACE:  return Key::LeftBracket;
    case KEY_BACKSLASH:  return Key::Backslash;
    case KEY_RIGHTBRACE: return Key::RightBracket;
    case KEY_GRAVE:      return Key::GraveAccent;
    case KEY_CAPSLOCK:   return Key::CapsLock;
    case KEY_SCROLLLOCK: return Key::ScrollLock;
    case KEY_NUMLOCK:    return Key::NumLock;
    case KEY_SYSRQ:      return Key::PrintScreen;
    case KEY_PAUSE:      return Key::Pause;
    case KEY_KP0:        return Key::Keypad0;
    case KEY_KP1:        return Key::Keypad1;
    case KEY_KP2:        return Key::Keypad2;
    case KEY_KP3:        return Key::Keypad3;
    case KEY_KP4:        return Key::Keypad4;
    case KEY_KP5:        return Key::Keypad5;
    case KEY_KP6:        return Key::Keypad6;
    case KEY_KP7:        return Key::Keypad7;
    case KEY_KP8:        return Key::Keypad8;
    case KEY_KP9:        return Key::Keypad9;
    case KEY_KPDOT:      return Key::KeypadDecimal;
    case KEY_KPSLASH:    return Key::KeypadDivide;
    case KEY_KPASTERISK: return Key::KeypadMultiply;
    case KEY_KPMINUS:    return Key::KeypadSubtract;
    case KEY_KPPLUS:     return Key::KeypadAdd;
    case KEY_KPENTER:    return Key::KeypadEnter;
    case KEY_KPEQUAL:    return Key::KeypadEqual;
    case KEY_BACK:       return Key::AppBack;
    case KEY_FORWARD:    return Key::AppForward;
    case KEY_102ND:      return Key::Oem102;
    default:             return Key::None;
    }
}

static Key native_window_key_from_button(u32 button)
{
    switch (button)
    {
    case BTN_LEFT:   return Key::MouseLeft;
    case BTN_RIGHT:  return Key::MouseRight;
    case BTN_MIDDLE: return Key::MouseMiddle;
    case BTN_SIDE:   return Key::MouseX1;
    case BTN_EXTRA:  return Key::MouseX2;
    case BTN_BACK:   return Key::MouseX1;
    case BTN_FORWARD:return Key::MouseX2;
    default:         return Key::None;
    }
}

static str native_window_to_utf8(const wstr &text)
{
    str result;
    result.reserve(text.size());

    for (wchar_t wide_char : text)
    {
        u32 c = (u32)wide_char;
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;

        if (c < 0x80)
        {
            result += (char)c;
        }
        else if (c < 0x800)
        {
            result += (char)(0xC0 | (c >> 6));
            result += (char)(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            result += (char)(0xE0 | (c >> 12));
            result += (char)(0x80 | ((c >> 6) & 0x3F));
            result += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            result += (char)(0xF0 | (c >> 18));
            result += (char)(0x80 | ((c >> 12) & 0x3F));
            result += (char)(0x80 | ((c >> 6) & 0x3F));
            result += (char)(0x80 | (c & 0x3F));
        }
    }

    return result;
}

Window::Window(const wstr &title, u32 width, u32 height)
    : width(width), height(height), framebuffer_width(width), framebuffer_height(height), scale(1.0f), closed(false)
{
    this->native = new Native_Window();
    this->native->window = this;

    // On any failure the window is reported as closed, so the main loop exits.
    this->native->display = wl_display_connect(nullptr);
    if (!this->native->display)
    {
        log_error("Could not connect to a Wayland display");
        this->closed = true;
        return;
    }

    this->native->registry = wl_display_get_registry(this->native->display);
    wl_registry_add_listener(this->native->registry, &native_window_registry_listener, this->native);
    wl_display_roundtrip(this->native->display);

    if (!this->native->compositor || !this->native->shm)
    {
        log_error("Wayland compositor doesn't provide wl_compositor or wl_shm");
        this->closed = true;
        return;
    }

    this->native->surface = wl_compositor_create_surface(this->native->compositor);
    wl_surface_add_listener(this->native->surface, &native_window_surface_listener, this);

    if (this->native->viewporter && this->native->fractional_scale_manager)
    {
        this->native->viewport = wp_viewporter_get_viewport(this->native->viewporter, this->native->surface);
        this->native->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            this->native->fractional_scale_manager, this->native->surface);
        wp_fractional_scale_v1_add_listener(this->native->fractional_scale,
                                            &native_window_fractional_scale_listener, this);
    }

    this->native->decor = libdecor_new(this->native->display, &native_window_decor_interface);
    if (!this->native->decor)
    {
        log_error("Could not initialize libdecor");
        this->closed = true;
        return;
    }

    this->native->use_system_app_mode();

    this->native->frame = libdecor_decorate(this->native->decor,
                                            this->native->surface,
                                            &native_window_frame_interface,
                                            this);
    if (!this->native->frame)
    {
        log_error("Could not create a libdecor frame");
        this->closed = true;
        return;
    }

    libdecor_frame_set_title(this->native->frame, native_window_to_utf8(title).c_str());

    log_info("Window created (%dx%d)", this->width, this->height);
}

Window::~Window()
{
    if (this->native->cursor_surface) wl_surface_destroy(this->native->cursor_surface);
    if (this->native->cursor_theme)   wl_cursor_theme_destroy(this->native->cursor_theme);
    if (this->native->keyboard)
    {
        if (this->native->seat_version >= WL_KEYBOARD_RELEASE_SINCE_VERSION) wl_keyboard_release(this->native->keyboard);
        else wl_keyboard_destroy(this->native->keyboard);
    }
    if (this->native->pointer)
    {
        if (this->native->seat_version >= WL_POINTER_RELEASE_SINCE_VERSION) wl_pointer_release(this->native->pointer);
        else wl_pointer_destroy(this->native->pointer);
    }
    if (this->native->seat)
    {
        if (this->native->seat_version >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(this->native->seat);
        else wl_seat_destroy(this->native->seat);
    }

    if (this->native->frame)      libdecor_frame_unref(this->native->frame);
    if (this->native->decor)      libdecor_unref(this->native->decor);
    if (this->native->buffer)     wl_buffer_destroy(this->native->buffer);

    if (this->native->fractional_scale)         wp_fractional_scale_v1_destroy(this->native->fractional_scale);
    if (this->native->viewport)                 wp_viewport_destroy(this->native->viewport);
    if (this->native->fractional_scale_manager) wp_fractional_scale_manager_v1_destroy(this->native->fractional_scale_manager);
    if (this->native->viewporter)               wp_viewporter_destroy(this->native->viewporter);

    if (this->native->surface)    wl_surface_destroy(this->native->surface);
    if (this->native->shm)        wl_shm_destroy(this->native->shm);
    if (this->native->compositor) wl_compositor_destroy(this->native->compositor);
    if (this->native->registry)   wl_registry_destroy(this->native->registry);
    if (this->native->display)    wl_display_disconnect(this->native->display);

    delete this->native;
}

void Window::show()
{
    if (!this->native->frame) return;

    libdecor_frame_map(this->native->frame);

    // Wait for the first configure, so the window has its size and the renderer
    // may present to it right after show() returns.
    while (!this->native->configured && !this->closed)
    {
        if (libdecor_dispatch(this->native->decor, -1) < 0)
        {
            log_error("Lost connection to the Wayland display");
            this->closed = true;
        }
    }
}

void Window::read_input()
{
    this->input_state.begin_frame();

    if (!this->native->decor) return;

    // Non-blocking: send our requests, then dispatch whatever events have arrived.
    wl_display_flush(this->native->display);
    if (libdecor_dispatch(this->native->decor, 0) < 0)
    {
        log_error("Lost connection to the Wayland display");
        this->closed = true;
    }
}

void Native_Window::registry_global(void *data, wl_registry *registry, u32 name, const char *interface, u32 version)
{
    auto native = (Native_Window *)data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        // Version 6 adds wl_surface.preferred_buffer_scale.
        native->compositor_version = version < 6 ? version : 6;
        native->compositor = (wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface,
                                                               native->compositor_version);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        native->shm = (wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, wp_viewporter_interface.name) == 0)
    {
        native->viewporter = (wp_viewporter *)wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
    {
        native->fractional_scale_manager = (wp_fractional_scale_manager_v1 *)
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0 && !native->seat)
    {
        // Version 8 adds wl_pointer.axis_value120 (scroll in wheel notches).
        native->seat_version = version < 8 ? version : 8;
        native->seat = (wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, native->seat_version);
        wl_seat_add_listener(native->seat, &native_window_seat_listener, native);
    }
}

void Native_Window::registry_global_remove(void *, wl_registry *, u32)
{
}

void Native_Window::decor_error(libdecor *, libdecor_error error, const char *message)
{
    log_error("libdecor error %d: %s", (int)error, message);
}

void Native_Window::frame_configure(libdecor_frame *frame, libdecor_configuration *configuration, void *data)
{
    auto window = (Window *)data;

    int width  = 0;
    int height = 0;
    if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height) ||
        width <= 0 || height <= 0)
    {
        // The compositor leaves the size to us (e.g. on the first configure).
        width  = window->width;
        height = window->height;
    }

    window->width  = (u32)width;
    window->height = (u32)height;
    window->native->apply_scale(window);

    libdecor_state *state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);

    window->native->configured = true;

    if (!window->native->has_renderer)
    {
        window->native->present_blank(window->framebuffer_width, window->framebuffer_height);
    }
}

void Native_Window::frame_close(libdecor_frame *, void *data)
{
    auto window = (Window *)data;
    window->closed = true;
}

void Native_Window::frame_commit(libdecor_frame *, void *data)
{
    auto window = (Window *)data;
    wl_surface_commit(window->native->surface);
}

void Native_Window::surface_enter(void *, wl_surface *, wl_output *)
{
}

void Native_Window::surface_leave(void *, wl_surface *, wl_output *)
{
}

void Native_Window::surface_preferred_buffer_scale(void *data, wl_surface *, i32 factor)
{
    auto window = (Window *)data;
    if (factor < 1 || factor == window->native->buffer_scale) return;

    window->native->buffer_scale = factor;
    native_window_scale_changed(window);
}

void Native_Window::surface_preferred_buffer_transform(void *, wl_surface *, u32)
{
}

void Native_Window::fractional_scale_preferred(void *data, wp_fractional_scale_v1 *, u32 scale)
{
    auto window = (Window *)data;
    if (scale == 0 || scale == window->native->scale_120) return;

    window->native->scale_120 = scale;
    native_window_scale_changed(window);
}

// Updates the window's scale and pixel size from its logical size and the preferred
// scale, and tells the compositor how the buffer maps to the surface. The new state
// takes effect with the next buffer. Returns whether the pixel size changed.
bool Native_Window::apply_scale(Window *window)
{
    u32 old_width  = window->framebuffer_width;
    u32 old_height = window->framebuffer_height;

    if (this->viewport && this->scale_120)
    {
        // Fractional: render at the exact pixel size and let the viewport map the
        // buffer onto the logical size. Rounding is half away from zero, as the
        // protocol specifies.
        window->scale              = (r32)this->scale_120 / 120.0f;
        window->framebuffer_width  = (window->width  * this->scale_120 + 60) / 120;
        window->framebuffer_height = (window->height * this->scale_120 + 60) / 120;
        wp_viewport_set_destination(this->viewport, (i32)window->width, (i32)window->height);
    }
    else
    {
        // Integer: the buffer is buffer_scale times the logical size.
        window->scale              = (r32)this->buffer_scale;
        window->framebuffer_width  = window->width  * (u32)this->buffer_scale;
        window->framebuffer_height = window->height * (u32)this->buffer_scale;
        if (this->compositor_version >= 3)
        {
            wl_surface_set_buffer_scale(this->surface, this->buffer_scale);
        }
    }

    return window->framebuffer_width != old_width || window->framebuffer_height != old_height;
}

// A scale change outside of a configure: the renderer picks up the new size on its
// next frame, without a renderer the blank buffer has to be replaced here.
static void native_window_scale_changed(Window *window)
{
    bool resized = window->native->apply_scale(window);
    if (resized && window->native->configured && !window->native->has_renderer)
    {
        window->native->present_blank(window->framebuffer_width, window->framebuffer_height);
    }

    window->native->update_cursor();
}

void Native_Window::seat_capabilities(void *data, wl_seat *seat, u32 capabilities)
{
    auto native = (Native_Window *)data;

    bool has_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
    if (has_keyboard && !native->keyboard)
    {
        native->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(native->keyboard, &native_window_keyboard_listener, native);
    }
    else if (!has_keyboard && native->keyboard)
    {
        if (native->seat_version >= WL_KEYBOARD_RELEASE_SINCE_VERSION) wl_keyboard_release(native->keyboard);
        else wl_keyboard_destroy(native->keyboard);
        native->keyboard = nullptr;
        native->window->input_state.release_keyboard();
    }

    bool has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !native->pointer)
    {
        native->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(native->pointer, &native_window_pointer_listener, native);
    }
    else if (!has_pointer && native->pointer)
    {
        if (native->seat_version >= WL_POINTER_RELEASE_SINCE_VERSION) wl_pointer_release(native->pointer);
        else wl_pointer_destroy(native->pointer);
        native->pointer            = nullptr;
        native->pointer_on_surface = false;
        native->window->input_state.mouse_inside = false;
        native->window->input_state.release_mouse_buttons();
    }
}

void Native_Window::seat_name(void *, wl_seat *, const char *)
{
}

// Keys are mapped from scancodes, so the layout (keymap) isn't needed until there is
// text input.
void Native_Window::keyboard_keymap(void *, wl_keyboard *, u32, i32 fd, u32)
{
    close(fd);
}

void Native_Window::keyboard_enter(void *, wl_keyboard *, u32, wl_surface *, wl_array *)
{
    // Keys already held when the window gets focus are ignored: they were pressed
    // for another window.
}

void Native_Window::keyboard_leave(void *data, wl_keyboard *, u32, wl_surface *)
{
    // No release events come after losing focus, so nothing stays stuck down.
    auto native = (Native_Window *)data;
    native->window->input_state.release_keyboard();
}

void Native_Window::keyboard_key(void *data, wl_keyboard *, u32, u32, u32 key, u32 state)
{
    auto native = (Native_Window *)data;
    native->window->input_state.set_key(native_window_key_from_scancode(key),
                                        state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

void Native_Window::keyboard_modifiers(void *, wl_keyboard *, u32, u32, u32, u32, u32)
{
    // Modifier keys also arrive as key events.
}

void Native_Window::keyboard_repeat_info(void *, wl_keyboard *, i32, i32)
{
}

void Native_Window::pointer_enter(void *data, wl_pointer *, u32 serial, wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    auto native = (Native_Window *)data;

    // The pointer also enters libdecor's title bar and borders, which are separate
    // surfaces: only the window content counts as inside.
    if (surface != native->surface) return;

    native->pointer_serial     = serial;
    native->pointer_on_surface = true;

    Input_State &input = native->window->input_state;
    input.mouse_inside = true;
    input.mouse_x      = (r32)wl_fixed_to_double(x);
    input.mouse_y      = (r32)wl_fixed_to_double(y);

    native->update_cursor();
}

void Native_Window::pointer_leave(void *data, wl_pointer *, u32, wl_surface *surface)
{
    auto native = (Native_Window *)data;
    if (surface != native->surface) return;

    native->pointer_on_surface = false;
    native->window->input_state.mouse_inside = false;
    native->window->input_state.release_mouse_buttons();
}

void Native_Window::pointer_motion(void *data, wl_pointer *, u32, wl_fixed_t x, wl_fixed_t y)
{
    auto native = (Native_Window *)data;
    if (!native->pointer_on_surface) return;

    native->window->input_state.mouse_x = (r32)wl_fixed_to_double(x);
    native->window->input_state.mouse_y = (r32)wl_fixed_to_double(y);
}

void Native_Window::pointer_button(void *data, wl_pointer *, u32, u32, u32 button, u32 state)
{
    auto native = (Native_Window *)data;
    if (!native->pointer_on_surface) return;

    native->window->input_state.set_key(native_window_key_from_button(button),
                                        state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void Native_Window::pointer_axis(void *data, wl_pointer *, u32, u32 axis, wl_fixed_t value)
{
    auto native = (Native_Window *)data;
    if (axis > 1) return;

    native->frame_scroll[axis] += (r32)wl_fixed_to_double(value);
}

void Native_Window::pointer_axis_value120(void *data, wl_pointer *, u32 axis, i32 value120)
{
    auto native = (Native_Window *)data;
    if (axis > 1) return;

    native->frame_value120[axis]    += value120;
    native->frame_has_value120[axis] = true;
}

void Native_Window::pointer_frame(void *data, wl_pointer *)
{
    auto native = (Native_Window *)data;

    if (native->pointer_on_surface)
    {
        // Wayland's +vertical is down, Input's is up. Continuous scroll is in surface
        // units; about 10 of them make a wheel notch.
        r32 notches[2];
        for (u32 axis = 0; axis < 2; ++axis)
        {
            notches[axis] = native->frame_has_value120[axis] ? (r32)native->frame_value120[axis] / 120.0f
                                                             : native->frame_scroll[axis] / 10.0f;
        }

        Input_State &input = native->window->input_state;
        input.scroll_y -= notches[WL_POINTER_AXIS_VERTICAL_SCROLL];
        input.scroll_x += notches[WL_POINTER_AXIS_HORIZONTAL_SCROLL];
    }

    for (u32 axis = 0; axis < 2; ++axis)
    {
        native->frame_scroll[axis]       = 0.0f;
        native->frame_value120[axis]     = 0;
        native->frame_has_value120[axis] = false;
    }
}

void Native_Window::pointer_axis_source(void *, wl_pointer *, u32)
{
}

void Native_Window::pointer_axis_stop(void *, wl_pointer *, u32, u32)
{
}

void Native_Window::pointer_axis_discrete(void *, wl_pointer *, u32, i32)
{
    // Replaced by axis_value120 in the seat version we bind.
}

// Wayland has no default cursor: the window sets one whenever the pointer enters it,
// otherwise the last one (e.g. a resize arrow from the border) stays. The theme is
// loaded at the window's integer buffer scale so the cursor is sharp.
void Native_Window::update_cursor()
{
    if (!this->pointer || !this->pointer_on_surface || !this->shm) return;

    i32 scale = this->buffer_scale;
    if (this->scale_120)
    {
        i32 fractional_ceil = (i32)((this->scale_120 + 119) / 120);
        if (fractional_ceil > scale) scale = fractional_ceil;
    }
    if (scale < 1) scale = 1;

    if (!this->cursor_theme || this->cursor_theme_scale != scale)
    {
        if (this->cursor_theme) wl_cursor_theme_destroy(this->cursor_theme);

        const char *theme_name = getenv("XCURSOR_THEME");
        const char *size_text  = getenv("XCURSOR_SIZE");
        int size = size_text ? atoi(size_text) : 0;
        if (size <= 0) size = 24;

        this->cursor_theme       = wl_cursor_theme_load(theme_name, size * scale, this->shm);
        this->cursor_theme_scale = scale;
        if (!this->cursor_theme)
        {
            log_warn("Could not load the cursor theme");
            return;
        }
    }

    wl_cursor *cursor = wl_cursor_theme_get_cursor(this->cursor_theme, "default");
    if (!cursor) cursor = wl_cursor_theme_get_cursor(this->cursor_theme, "left_ptr");
    if (!cursor || cursor->image_count == 0) return;

    wl_cursor_image *image  = cursor->images[0];
    wl_buffer       *buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;

    if (!this->cursor_surface) this->cursor_surface = wl_compositor_create_surface(this->compositor);

    wl_pointer_set_cursor(this->pointer, this->pointer_serial, this->cursor_surface,
                          (i32)image->hotspot_x / scale, (i32)image->hotspot_y / scale);
    wl_surface_set_buffer_scale(this->cursor_surface, scale);
    wl_surface_attach(this->cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(this->cursor_surface, 0, 0, (i32)image->width, (i32)image->height);
    wl_surface_commit(this->cursor_surface);
}

// libdecor's GTK plugin draws the title bar with GTK3, which ignores the desktop's
// dark/light preference and only follows the GTK theme. Read the preference from the
// XDG settings portal (org.freedesktop.appearance color-scheme: 0 = no preference,
// 1 = dark, 2 = light) and ask GTK for the theme's dark variant to match.
//
// GTK and GLib are already loaded into the process by the plugin, so their functions
// are looked up at runtime instead of being build dependencies. If the plugin isn't
// GTK-based or the compositor draws decorations itself, nothing here applies.
void Native_Window::use_system_app_mode()
{
    using fn_g_bus_get_sync                = void *(*)(int bus_type, void *cancellable, void **error);
    using fn_g_dbus_connection_call_sync   = void *(*)(void *connection, const char *bus_name, const char *object_path,
                                                       const char *interface_name, const char *method_name,
                                                       void *parameters, const void *reply_type, int flags,
                                                       int timeout_msec, void *cancellable, void **error);
    using fn_g_variant_new                 = void *(*)(const char *format, ...);
    using fn_g_variant_get_child_value     = void *(*)(void *value, size_t index);
    using fn_g_variant_get_variant         = void *(*)(void *value);
    using fn_g_variant_is_of_type          = int (*)(void *value, const void *type);
    using fn_g_variant_get_uint32          = u32 (*)(void *value);
    using fn_g_variant_unref               = void (*)(void *value);
    using fn_g_object_unref                = void (*)(void *object);
    using fn_g_object_set                  = void (*)(void *object, const char *first_property_name, ...);
    using fn_gtk_settings_get_default      = void *(*)();

    constexpr int G_BUS_TYPE_SESSION        = 2;
    constexpr int G_DBUS_CALL_FLAGS_NONE    = 0;
    constexpr u32 COLOR_SCHEME_PREFER_DARK  = 1;

    void *gtk     = dlopen("libgtk-3.so.0",       RTLD_LAZY | RTLD_NOLOAD);
    void *gio     = dlopen("libgio-2.0.so.0",     RTLD_LAZY | RTLD_NOLOAD);
    void *gobject = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
    void *glib    = dlopen("libglib-2.0.so.0",    RTLD_LAZY | RTLD_NOLOAD);

    if (gtk && gio && gobject && glib)
    {
        auto g_bus_get_sync              = (fn_g_bus_get_sync)dlsym(gio, "g_bus_get_sync");
        auto g_dbus_connection_call_sync = (fn_g_dbus_connection_call_sync)dlsym(gio, "g_dbus_connection_call_sync");
        auto g_variant_new               = (fn_g_variant_new)dlsym(glib, "g_variant_new");
        auto g_variant_get_child_value   = (fn_g_variant_get_child_value)dlsym(glib, "g_variant_get_child_value");
        auto g_variant_get_variant       = (fn_g_variant_get_variant)dlsym(glib, "g_variant_get_variant");
        auto g_variant_is_of_type        = (fn_g_variant_is_of_type)dlsym(glib, "g_variant_is_of_type");
        auto g_variant_get_uint32        = (fn_g_variant_get_uint32)dlsym(glib, "g_variant_get_uint32");
        auto g_variant_unref             = (fn_g_variant_unref)dlsym(glib, "g_variant_unref");
        auto g_object_unref              = (fn_g_object_unref)dlsym(gobject, "g_object_unref");
        auto g_object_set                = (fn_g_object_set)dlsym(gobject, "g_object_set");
        auto gtk_settings_get_default    = (fn_gtk_settings_get_default)dlsym(gtk, "gtk_settings_get_default");

        void *settings   = nullptr;
        void *connection = nullptr;

        if (g_bus_get_sync && g_dbus_connection_call_sync && g_variant_new && g_variant_get_child_value &&
            g_variant_get_variant && g_variant_is_of_type && g_variant_get_uint32 && g_variant_unref &&
            g_object_unref && g_object_set && gtk_settings_get_default)
        {
            settings = gtk_settings_get_default();
        }

        if (settings)
        {
            connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        }

        if (settings && connection)
        {
            u32 color_scheme = 0;

            // Settings.Read returns (v); the value inside is wrapped in one more v.
            void *reply = g_dbus_connection_call_sync(connection,
                                                      "org.freedesktop.portal.Desktop",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.Settings",
                                                      "Read",
                                                      g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                                                      "(v)",
                                                      G_DBUS_CALL_FLAGS_NONE,
                                                      500,
                                                      nullptr,
                                                      nullptr);
            if (reply)
            {
                void *value = g_variant_get_child_value(reply, 0);
                while (g_variant_is_of_type(value, "v"))
                {
                    void *inner = g_variant_get_variant(value);
                    g_variant_unref(value);
                    value = inner;
                }
                if (g_variant_is_of_type(value, "u"))
                {
                    color_scheme = g_variant_get_uint32(value);
                }
                g_variant_unref(value);
                g_variant_unref(reply);
            }

            bool dark = color_scheme == COLOR_SCHEME_PREFER_DARK;
            log_info("System app mode: %s", dark ? "Dark" : "Light");
            g_object_set(settings, "gtk-application-prefer-dark-theme", (int)dark, nullptr);

            g_object_unref(connection);
        }
    }

    if (gtk)     dlclose(gtk);
    if (gio)     dlclose(gio);
    if (gobject) dlclose(gobject);
    if (glib)    dlclose(glib);
}

// A Wayland surface isn't shown until it has a buffer. Until a renderer presents
// frames, attach a black one (the Windows window uses a black brush the same way).
bool Native_Window::present_blank(u32 width, u32 height)
{
    u32 stride = width * 4;
    u64 size   = (u64)stride * height;

    int fd = memfd_create("lemonade-window", MFD_CLOEXEC);
    if (fd < 0)
    {
        log_error("Could not create a window buffer");
        return false;
    }

    // Freshly truncated memory is zeroed, which is black in XRGB8888.
    if (ftruncate(fd, (off_t)size) != 0)
    {
        log_error("Could not allocate a %ux%u window buffer", width, height);
        close(fd);
        return false;
    }

    wl_shm_pool *pool   = wl_shm_create_pool(this->shm, fd, (i32)size);
    wl_buffer   *buffer = wl_shm_pool_create_buffer(pool, 0, (i32)width, (i32)height, (i32)stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(this->surface, buffer, 0, 0);
    wl_surface_damage_buffer(this->surface, 0, 0, (i32)width, (i32)height);
    wl_surface_commit(this->surface);

    if (this->buffer) wl_buffer_destroy(this->buffer);
    this->buffer = buffer;

    return true;
}

#endif // OS_LINUX
