#pragma once

#ifdef OS_LINUX

#include "../core.h"

#include <libdecor.h>
#include <wayland-client.h>

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

    void use_system_app_mode();
    bool present_blank(u32 width, u32 height);

    wl_display     *display    = nullptr;
    wl_registry    *registry   = nullptr;
    wl_compositor  *compositor = nullptr;
    wl_shm         *shm        = nullptr;
    wl_surface     *surface    = nullptr;
    wl_buffer      *buffer     = nullptr;
    libdecor       *decor      = nullptr;
    libdecor_frame *frame      = nullptr;

    // Set after the first configure: nothing may be attached to the surface before it.
    bool configured   = false;
    // Set by the renderer once it presents to the surface, so the window stops
    // attaching its own blank buffer.
    bool has_renderer = false;
};

#endif // OS_LINUX
