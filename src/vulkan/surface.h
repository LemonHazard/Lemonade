#pragma once

#ifdef RENDERER_VULKAN

#include <vulkan/vulkan.h>

class Window;

// The platform-specific part of the Vulkan backend, implemented per OS in
// vulkan/<os>/surface.cpp.

// Instance extension that create_vulkan_surface() needs (besides VK_KHR_surface).
const char *vulkan_surface_extension();

bool create_vulkan_surface(VkInstance instance, Window &window, VkSurfaceKHR *surface);

// Tells the window that the renderer presents to it from now on, or no longer.
void set_vulkan_presenting(Window &window, bool presenting);

#endif // RENDERER_VULKAN
