#pragma once

#ifdef OS_LINUX

#include "../core.h"

#include <libdecor.h>
#include <wayland-client.h>

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

    void use_system_app_mode();
    bool apply_scale(Window *window);
    bool present_blank(u32 width, u32 height);

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

    // Set after the first configure: nothing may be attached to the surface before it.
    bool configured   = false;
    // Set by the renderer once it presents to the surface, so the window stops
    // attaching its own blank buffer.
    bool has_renderer = false;
};

#endif // OS_LINUX
