#ifdef RENDERER_VULKAN

#include "../renderer.h"
#include "../window.h"
#include "../logger.h"
#include "surface.h"

#include <cstring>
#include <vector>

#include <vulkan/vulkan.h>

// Vulkan 1.3 renderer: dynamic rendering (no render passes or framebuffers) and
// synchronization2. Presents with FIFO, i.e. vsync.

// SPIR-V compiled from vulkan/shaders by glslc at build time (see premake5.lua).
static const u32 triangle_vert_spv[] =
#include "triangle.vert.inc"
;
static const u32 triangle_frag_spv[] =
#include "triangle.frag.inc"
;

constexpr u32 FRAMES_IN_FLIGHT     = 2;
constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;

struct Native_Renderer_Frame
{
    VkCommandPool   command_pool   = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore     image_acquired = VK_NULL_HANDLE;
    VkFence         in_flight      = VK_NULL_HANDLE;
};

class Native_Renderer
{
public:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT types,
                                                         const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                         void *user_data);

    bool create_instance();
    bool pick_physical_device();
    bool create_device();
    bool create_frames();
    bool create_pipeline();
    bool create_swapchain(u32 width, u32 height);
    void destroy_swapchain_resources();
    VkShaderModule create_shader_module(const u32 *code, u64 size);

    Window *window = nullptr;

    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface         = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device = VK_NULL_HANDLE;
    VkDevice                 device          = VK_NULL_HANDLE;
    VkQueue                  queue           = VK_NULL_HANDLE;
    u32                      queue_family    = 0;
    VkSurfaceFormatKHR       surface_format  = {};

    VkSwapchainKHR swapchain        = VK_NULL_HANDLE;
    VkExtent2D     swapchain_extent = {};
    bool           swapchain_dirty  = false;
    u32            image_count      = 0;
    VkImage        images[MAX_SWAPCHAIN_IMAGES]          = {};
    VkImageView    image_views[MAX_SWAPCHAIN_IMAGES]     = {};
    // Signaled by rendering, waited on by present. One per swapchain image rather
    // than per frame, since present doesn't tell us when it's done with it.
    VkSemaphore    render_finished[MAX_SWAPCHAIN_IMAGES] = {};

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       pipeline        = VK_NULL_HANDLE;

    Native_Renderer_Frame frames[FRAMES_IN_FLIGHT];
    u32  frame_index   = 0;
    u32  image_index   = 0;
    bool frame_started = false;
    bool ready         = false;
};

static bool vk_check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) return true;

    log_error("%s failed (VkResult %d)", what, (int)result);
    return false;
}

Renderer::Renderer(Window &window)
{
    this->native = new Native_Renderer();
    this->native->window = &window;

    if (window.closed) return;

    // On any failure the renderer stays not ready: begin_frame() returns false and
    // the window keeps showing its own blank buffer.
    bool ok = this->native->create_instance()
           && create_vulkan_surface(this->native->instance, window, &this->native->surface)
           && this->native->pick_physical_device()
           && this->native->create_device()
           && this->native->create_frames()
           && this->native->create_pipeline();

    if (!ok)
    {
        log_error("Could not initialize the Vulkan renderer");
        return;
    }

    this->native->ready = true;
    set_vulkan_presenting(window, true);
}

Renderer::~Renderer()
{
    Native_Renderer *n = this->native;

    if (n->device)
    {
        vkDeviceWaitIdle(n->device);

        if (n->pipeline)        vkDestroyPipeline(n->device, n->pipeline, nullptr);
        if (n->pipeline_layout) vkDestroyPipelineLayout(n->device, n->pipeline_layout, nullptr);

        n->destroy_swapchain_resources();
        if (n->swapchain) vkDestroySwapchainKHR(n->device, n->swapchain, nullptr);

        for (auto &frame : n->frames)
        {
            if (frame.in_flight)      vkDestroyFence(n->device, frame.in_flight, nullptr);
            if (frame.image_acquired) vkDestroySemaphore(n->device, frame.image_acquired, nullptr);
            if (frame.command_pool)   vkDestroyCommandPool(n->device, frame.command_pool, nullptr);
        }

        vkDestroyDevice(n->device, nullptr);
    }

    if (n->surface) vkDestroySurfaceKHR(n->instance, n->surface, nullptr);

    if (n->debug_messenger)
    {
        auto destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(n->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger) destroy_messenger(n->instance, n->debug_messenger, nullptr);
    }

    if (n->instance) vkDestroyInstance(n->instance, nullptr);

    if (n->ready) set_vulkan_presenting(*n->window, false);

    delete n;
}

bool Renderer::begin_frame(const Vector4 &clear_color)
{
    Native_Renderer *n = this->native;
    n->frame_started = false;

    if (!n->ready) return false;

    u32 width  = n->window->framebuffer_width;
    u32 height = n->window->framebuffer_height;
    if (width == 0 || height == 0) return false;

    if (!n->swapchain || n->swapchain_dirty ||
        n->swapchain_extent.width != width || n->swapchain_extent.height != height)
    {
        vkDeviceWaitIdle(n->device);
        if (!n->create_swapchain(width, height)) return false;
    }

    Native_Renderer_Frame &frame = n->frames[n->frame_index];
    vkWaitForFences(n->device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(n->device, n->swapchain, UINT64_MAX,
                                            frame.image_acquired, VK_NULL_HANDLE, &n->image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        n->swapchain_dirty = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        // Still usable: draw this frame, recreate before the next one.
        n->swapchain_dirty = true;
    }
    else if (!vk_check(result, "vkAcquireNextImageKHR"))
    {
        return false;
    }

    vkResetFences(n->device, 1, &frame.in_flight);
    vkResetCommandPool(n->device, frame.command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(frame.command_buffer, &begin_info);

    VkImageMemoryBarrier2 to_attachment = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = n->images[n->image_index],
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_attachment,
    };
    vkCmdPipelineBarrier2(frame.command_buffer, &dependency);

    VkRenderingAttachmentInfo color_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = n->image_views[n->image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = { { clear_color.r, clear_color.g, clear_color.b, clear_color.a } } },
    };
    VkRenderingInfo rendering = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { { 0, 0 }, n->swapchain_extent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
    };
    vkCmdBeginRendering(frame.command_buffer, &rendering);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)n->swapchain_extent.width,
        .height   = (float)n->swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { { 0, 0 }, n->swapchain_extent };
    vkCmdSetViewport(frame.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(frame.command_buffer, 0, 1, &scissor);

    n->frame_started = true;
    return true;
}

void Renderer::draw_test_triangle()
{
    Native_Renderer *n = this->native;
    if (!n->frame_started) return;

    VkCommandBuffer command_buffer = n->frames[n->frame_index].command_buffer;
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->pipeline);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
}

void Renderer::end_frame()
{
    Native_Renderer *n = this->native;
    if (!n->frame_started) return;
    n->frame_started = false;

    Native_Renderer_Frame &frame = n->frames[n->frame_index];

    vkCmdEndRendering(frame.command_buffer);

    VkImageMemoryBarrier2 to_present = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask       = 0,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = n->images[n->image_index],
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_present,
    };
    vkCmdPipelineBarrier2(frame.command_buffer, &dependency);

    vkEndCommandBuffer(frame.command_buffer);

    VkSemaphoreSubmitInfo wait_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.image_acquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphoreSubmitInfo signal_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = n->render_finished[n->image_index],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkCommandBufferSubmitInfo command_buffer_info = {
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.command_buffer,
    };
    VkSubmitInfo2 submit = {
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &command_buffer_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_info,
    };
    vk_check(vkQueueSubmit2(n->queue, 1, &submit, frame.in_flight), "vkQueueSubmit2");

    VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &n->render_finished[n->image_index],
        .swapchainCount     = 1,
        .pSwapchains        = &n->swapchain,
        .pImageIndices      = &n->image_index,
    };
    VkResult result = vkQueuePresentKHR(n->queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        n->swapchain_dirty = true;
    }
    else
    {
        vk_check(result, "vkQueuePresentKHR");
    }

    n->frame_index = (n->frame_index + 1) % FRAMES_IN_FLIGHT;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Native_Renderer::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                               VkDebugUtilsMessageTypeFlagsEXT,
                                                               const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                               void *)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        log_error("Vulkan: %s", data->pMessage);
    }
    else
    {
        log_warn("Vulkan: %s", data->pMessage);
    }
    return VK_FALSE;
}

bool Native_Renderer::create_instance()
{
    const char *extensions[3] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        vulkan_surface_extension(),
    };
    u32 extension_count = 2;

    const char *layers[1] = {};
    u32 layer_count = 0;

    VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = Native_Renderer::debug_callback,
    };
    bool validation = false;

#if defined(DEBUG)
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";

    u32 available_count = 0;
    vkEnumerateInstanceLayerProperties(&available_count, nullptr);
    std::vector<VkLayerProperties> available(available_count);
    vkEnumerateInstanceLayerProperties(&available_count, available.data());

    for (const auto &layer : available)
    {
        if (strcmp(layer.layerName, validation_layer) == 0) validation = true;
    }

    if (validation)
    {
        layers[layer_count++]         = validation_layer;
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    else
    {
        log_warn("Vulkan validation layers not found (install vulkan-validationlayers to enable them)");
    }
#endif

    VkApplicationInfo application = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "Lemonade",
        .applicationVersion = 1,
        .pEngineName        = "Lemonade",
        .engineVersion      = 1,
        .apiVersion         = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        // Also report problems in vkCreateInstance/vkDestroyInstance themselves.
        .pNext                   = validation ? &messenger_info : nullptr,
        .pApplicationInfo        = &application,
        .enabledLayerCount       = layer_count,
        .ppEnabledLayerNames     = layers,
        .enabledExtensionCount   = extension_count,
        .ppEnabledExtensionNames = extensions,
    };
    if (!vk_check(vkCreateInstance(&info, nullptr, &this->instance), "vkCreateInstance")) return false;

    if (validation)
    {
        auto create_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(this->instance, "vkCreateDebugUtilsMessengerEXT");
        if (create_messenger)
        {
            create_messenger(this->instance, &messenger_info, nullptr, &this->debug_messenger);
        }
        log_info("Vulkan validation layers enabled");
    }

    return true;
}

bool Native_Renderer::pick_physical_device()
{
    u32 device_count = 0;
    vkEnumeratePhysicalDevices(this->instance, &device_count, nullptr);
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(this->instance, &device_count, devices.data());

    int best_score = 0;

    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;

        VkPhysicalDeviceVulkan13Features features13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features13,
        };
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features13.dynamicRendering || !features13.synchronization2) continue;

        u32 extension_count = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, extensions.data());

        bool has_swapchain = false;
        for (const auto &extension : extensions)
        {
            if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) has_swapchain = true;
        }
        if (!has_swapchain) continue;

        u32 family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

        // One queue for both graphics and present keeps the synchronization simple.
        int family = -1;
        for (u32 i = 0; i < family_count && family < 0; ++i)
        {
            VkBool32 can_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, this->surface, &can_present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && can_present) family = (int)i;
        }
        if (family < 0) continue;

        int score = 1;
        switch (properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 4; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 2; break;
        default:                                     score = 1; break; // CPU (e.g. llvmpipe)
        }

        if (score > best_score)
        {
            best_score            = score;
            this->physical_device = candidate;
            this->queue_family    = (u32)family;
        }
    }

    if (!this->physical_device)
    {
        log_error("No Vulkan 1.3 device with dynamic rendering that can present to the window");
        return false;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(this->physical_device, &properties);
    log_info("Vulkan device: %s (Vulkan %u.%u.%u)",
             properties.deviceName,
             VK_API_VERSION_MAJOR(properties.apiVersion),
             VK_API_VERSION_MINOR(properties.apiVersion),
             VK_API_VERSION_PATCH(properties.apiVersion));

    // Prefer an sRGB format, so shaders output linear colors and the display gets
    // them gamma-corrected.
    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(this->physical_device, this->surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(this->physical_device, this->surface, &format_count, formats.data());
    if (formats.empty())
    {
        log_error("Vulkan surface reports no formats");
        return false;
    }

    this->surface_format = formats[0];
    for (const auto &format : formats)
    {
        bool srgb = format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB;
        if (srgb && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            this->surface_format = format;
            break;
        }
    }

    return true;
}

bool Native_Renderer::create_device()
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = this->queue_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &features13,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_info,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = extensions,
    };
    if (!vk_check(vkCreateDevice(this->physical_device, &info, nullptr, &this->device), "vkCreateDevice")) return false;

    vkGetDeviceQueue(this->device, this->queue_family, 0, &this->queue);
    return true;
}

bool Native_Renderer::create_frames()
{
    for (auto &frame : this->frames)
    {
        VkCommandPoolCreateInfo pool_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = this->queue_family,
        };
        if (!vk_check(vkCreateCommandPool(this->device, &pool_info, nullptr, &frame.command_pool), "vkCreateCommandPool")) return false;

        VkCommandBufferAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = frame.command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (!vk_check(vkAllocateCommandBuffers(this->device, &allocate_info, &frame.command_buffer), "vkAllocateCommandBuffers")) return false;

        VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (!vk_check(vkCreateSemaphore(this->device, &semaphore_info, nullptr, &frame.image_acquired), "vkCreateSemaphore")) return false;

        // Signaled, so the first wait on it doesn't block.
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (!vk_check(vkCreateFence(this->device, &fence_info, nullptr, &frame.in_flight), "vkCreateFence")) return false;
    }

    return true;
}

VkShaderModule Native_Renderer::create_shader_module(const u32 *code, u64 size)
{
    VkShaderModuleCreateInfo info = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = code,
    };

    VkShaderModule module = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(this->device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

bool Native_Renderer::create_pipeline()
{
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (!vk_check(vkCreatePipelineLayout(this->device, &layout_info, nullptr, &this->pipeline_layout), "vkCreatePipelineLayout")) return false;

    VkShaderModule vertex   = this->create_shader_module(triangle_vert_spv, sizeof(triangle_vert_spv));
    VkShaderModule fragment = this->create_shader_module(triangle_frag_spv, sizeof(triangle_frag_spv));

    bool ok = false;
    if (vertex && fragment)
    {
        VkPipelineShaderStageCreateInfo stages[2] = {
            {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex,
                .pName  = "main",
            },
            {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment,
                .pName  = "main",
            },
        };

        VkPipelineVertexInputStateCreateInfo vertex_input = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };
        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        VkPipelineViewportStateCreateInfo viewport = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
        };
        VkPipelineRasterizationStateCreateInfo rasterization = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode    = VK_CULL_MODE_NONE,
            .frontFace   = VK_FRONT_FACE_CLOCKWISE,
            .lineWidth   = 1.0f,
        };
        VkPipelineMultisampleStateCreateInfo multisample = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkPipelineColorBlendAttachmentState blend_attachment = {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        VkPipelineColorBlendStateCreateInfo blend = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &blend_attachment,
        };
        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates    = dynamic_states,
        };
        VkPipelineRenderingCreateInfo rendering = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &this->surface_format.format,
        };

        VkGraphicsPipelineCreateInfo info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &rendering,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState      = &viewport,
            .pRasterizationState = &rasterization,
            .pMultisampleState   = &multisample,
            .pColorBlendState    = &blend,
            .pDynamicState       = &dynamic,
            .layout              = this->pipeline_layout,
        };
        ok = vk_check(vkCreateGraphicsPipelines(this->device, VK_NULL_HANDLE, 1, &info, nullptr, &this->pipeline),
                      "vkCreateGraphicsPipelines");
    }

    if (vertex)   vkDestroyShaderModule(this->device, vertex, nullptr);
    if (fragment) vkDestroyShaderModule(this->device, fragment, nullptr);

    return ok;
}

bool Native_Renderer::create_swapchain(u32 width, u32 height)
{
    VkSurfaceCapabilitiesKHR capabilities;
    if (!vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->physical_device, this->surface, &capabilities),
                  "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return false;

    // Wayland leaves the size to us (currentExtent is 0xFFFFFFFF): use the window's.
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        auto clamp = [](u32 value, u32 min, u32 max) { return value < min ? min : (value > max ? max : value); };
        extent.width  = clamp(width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        extent.height = clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;

    u32 min_image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && min_image_count > capabilities.maxImageCount) min_image_count = capabilities.maxImageCount;
    if (min_image_count > MAX_SWAPCHAIN_IMAGES) min_image_count = MAX_SWAPCHAIN_IMAGES;

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & composite_alpha))
    {
        composite_alpha = (VkCompositeAlphaFlagBitsKHR)(capabilities.supportedCompositeAlpha &
                                                        -capabilities.supportedCompositeAlpha); // lowest set bit
    }

    VkSwapchainKHR old_swapchain = this->swapchain;

    VkSwapchainCreateInfoKHR info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = this->surface,
        .minImageCount    = min_image_count,
        .imageFormat      = this->surface_format.format,
        .imageColorSpace  = this->surface_format.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = capabilities.currentTransform,
        .compositeAlpha   = composite_alpha,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR, // vsync, always supported
        .clipped          = VK_TRUE,
        .oldSwapchain     = old_swapchain,
    };

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (!vk_check(vkCreateSwapchainKHR(this->device, &info, nullptr, &new_swapchain), "vkCreateSwapchainKHR")) return false;

    // The caller waited for the device to be idle, so the old images aren't in use.
    this->destroy_swapchain_resources();
    if (old_swapchain) vkDestroySwapchainKHR(this->device, old_swapchain, nullptr);
    this->swapchain = new_swapchain;

    u32 count = 0;
    vkGetSwapchainImagesKHR(this->device, this->swapchain, &count, nullptr);
    if (count > MAX_SWAPCHAIN_IMAGES)
    {
        log_error("Swapchain has %u images, at most %u are supported", count, MAX_SWAPCHAIN_IMAGES);
        return false;
    }
    vkGetSwapchainImagesKHR(this->device, this->swapchain, &count, this->images);
    this->image_count = count;

    for (u32 i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = this->images[i],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = this->surface_format.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &this->image_views[i]), "vkCreateImageView")) return false;

        VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (!vk_check(vkCreateSemaphore(this->device, &semaphore_info, nullptr, &this->render_finished[i]), "vkCreateSemaphore")) return false;
    }

    this->swapchain_extent = extent;
    this->swapchain_dirty  = false;
    return true;
}

void Native_Renderer::destroy_swapchain_resources()
{
    for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i)
    {
        if (this->image_views[i])     vkDestroyImageView(this->device, this->image_views[i], nullptr);
        if (this->render_finished[i]) vkDestroySemaphore(this->device, this->render_finished[i], nullptr);
        this->image_views[i]     = VK_NULL_HANDLE;
        this->render_finished[i] = VK_NULL_HANDLE;
        this->images[i]          = VK_NULL_HANDLE;
    }
    this->image_count = 0;
}

#endif // RENDERER_VULKAN
