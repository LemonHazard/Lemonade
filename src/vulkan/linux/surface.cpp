#if defined(RENDERER_VULKAN) && defined(OS_LINUX)

#include "../surface.h"
#include "../../window.h"
#include "../../logger.h"
#include "../../linux/window.h"

#include <vulkan/vulkan_wayland.h>

const char *vulkan_surface_extension()
{
    return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
}

bool create_vulkan_surface(VkInstance instance, Window &window, VkSurfaceKHR *surface)
{
    VkWaylandSurfaceCreateInfoKHR info = {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = window.native->display,
        .surface = window.native->surface,
    };

    VkResult result = vkCreateWaylandSurfaceKHR(instance, &info, nullptr, surface);
    if (result != VK_SUCCESS)
    {
        log_error("vkCreateWaylandSurfaceKHR failed (VkResult %d)", (int)result);
        return false;
    }
    return true;
}

// The Wayland window attaches a blank buffer until a renderer presents to it.
void set_vulkan_presenting(Window &window, bool presenting)
{
    window.native->has_renderer = presenting;
}

#endif // RENDERER_VULKAN && OS_LINUX
