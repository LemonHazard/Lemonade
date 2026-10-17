#ifdef OS_LINUX

#include "../window.h"
#include "../logger.h"

#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>

#include <libdecor.h>
#include <wayland-client.h>

// Wayland window. GNOME doesn't draw server-side decorations, so the title bar
// (move, resize, close buttons) comes from libdecor, which also creates the
// xdg-shell toplevel for us and draws the title bar with the GTK theme.

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
};

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

Window::Window(const wstr &title, u32 width, u32 height) : width(width), height(height), closed(false)
{
    this->native = new Native_Window();

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
    if (this->native->frame)      libdecor_frame_unref(this->native->frame);
    if (this->native->decor)      libdecor_unref(this->native->decor);
    if (this->native->buffer)     wl_buffer_destroy(this->native->buffer);
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
}

void Window::read_input()
{
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
        u32 bind_version = version < 4 ? version : 4;
        native->compositor = (wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        native->shm = (wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
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

    libdecor_state *state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);

    window->native->present_blank(window->width, window->height);
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
