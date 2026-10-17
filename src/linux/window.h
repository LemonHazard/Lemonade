#pragma once

#ifdef OS_LINUX

#include "../core.h"

#include <libdecor.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

class Window;

// Wayland side of Window, shared with the renderer, which presents to the surface.
class Native_Window
{
public:
    static void registry_global(void *data, wl_registry *registry, u32 name, const char *interface, u32 version);
    static void registry_global_remove(void *data, wl_registry *registry, u32 name);

    static void decor_error(libdecor *context, libdecor_error error, const char *message);
    static void frame_configure(libdecor_frame *frame, libdecor_configuration *configuration, void *data);
    static void frame_close(libdecor_frame *frame, void *data);
    static void frame_commit(libdecor_frame *frame, void *data);

    static void surface_enter(void *data, wl_surface *surface, wl_output *output);
    static void surface_leave(void *data, wl_surface *surface, wl_output *output);
    static void surface_preferred_buffer_scale(void *data, wl_surface *surface, i32 factor);
    static void surface_preferred_buffer_transform(void *data, wl_surface *surface, u32 transform);
    static void fractional_scale_preferred(void *data, wp_fractional_scale_v1 *fractional_scale, u32 scale);

    static void seat_capabilities(void *data, wl_seat *seat, u32 capabilities);
    static void seat_name(void *data, wl_seat *seat, const char *name);

    static void keyboard_keymap(void *data, wl_keyboard *keyboard, u32 format, i32 fd, u32 size);
    static void keyboard_enter(void *data, wl_keyboard *keyboard, u32 serial, wl_surface *surface, wl_array *keys);
    static void keyboard_leave(void *data, wl_keyboard *keyboard, u32 serial, wl_surface *surface);
    static void keyboard_key(void *data, wl_keyboard *keyboard, u32 serial, u32 time, u32 key, u32 state);
    static void keyboard_modifiers(void *data, wl_keyboard *keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group);
    static void keyboard_repeat_info(void *data, wl_keyboard *keyboard, i32 rate, i32 delay);

    static void pointer_enter(void *data, wl_pointer *pointer, u32 serial, wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
    static void pointer_leave(void *data, wl_pointer *pointer, u32 serial, wl_surface *surface);
    static void pointer_motion(void *data, wl_pointer *pointer, u32 time, wl_fixed_t x, wl_fixed_t y);
    static void pointer_button(void *data, wl_pointer *pointer, u32 serial, u32 time, u32 button, u32 state);
    static void pointer_axis(void *data, wl_pointer *pointer, u32 time, u32 axis, wl_fixed_t value);
    static void pointer_frame(void *data, wl_pointer *pointer);
    static void pointer_axis_source(void *data, wl_pointer *pointer, u32 source);
    static void pointer_axis_stop(void *data, wl_pointer *pointer, u32 time, u32 axis);
    static void pointer_axis_discrete(void *data, wl_pointer *pointer, u32 axis, i32 discrete);
    static void pointer_axis_value120(void *data, wl_pointer *pointer, u32 axis, i32 value120);

    void use_system_app_mode();
    bool apply_scale(Window *window);
    bool present_blank(u32 width, u32 height);
    void update_cursor();

    Window         *window     = nullptr; // for the listeners
    wl_display     *display    = nullptr;
    wl_registry    *registry   = nullptr;
    wl_compositor  *compositor = nullptr;
    wl_shm         *shm        = nullptr;
    wl_surface     *surface    = nullptr;
    wl_buffer      *buffer     = nullptr;
    libdecor       *decor      = nullptr;
    libdecor_frame *frame      = nullptr;

    // Display scaling: fractional (wp_fractional_scale_v1 + wp_viewporter) when the
    // compositor supports it, otherwise the integer wl_surface buffer scale.
    u32                             compositor_version       = 0;
    wp_viewporter                  *viewporter               = nullptr;
    wp_fractional_scale_manager_v1 *fractional_scale_manager = nullptr;
    wp_viewport                    *viewport                 = nullptr;
    wp_fractional_scale_v1         *fractional_scale         = nullptr;
    u32                             scale_120                = 0; // fractional scale * 120, 0 until known
    i32                             buffer_scale             = 1;

    // Input
    wl_seat     *seat               = nullptr;
    u32          seat_version       = 0;
    wl_keyboard *keyboard           = nullptr;
    wl_pointer  *pointer            = nullptr;
    u32          pointer_serial     = 0;     // of the last enter, for set_cursor
    bool         pointer_on_surface = false; // and not on a decoration
    // Scroll is reported per axis (0 = vertical, 1 = horizontal) and applied on
    // wl_pointer.frame: in wheel notches (value120) when a wheel sends them,
    // otherwise as continuous motion (touchpads).
    r32          frame_scroll[2]    = {};
    i32          frame_value120[2]  = {};
    bool         frame_has_value120[2] = {};

    wl_cursor_theme *cursor_theme       = nullptr;
    i32              cursor_theme_scale = 0;
    wl_surface      *cursor_surface     = nullptr;

    // Set after the first configure: nothing may be attached to the surface before it.
    bool configured   = false;
    // Set by the renderer once it presents to the surface, so the window stops
    // attaching its own blank buffer.
    bool has_renderer = false;
};

#endif // OS_LINUX
