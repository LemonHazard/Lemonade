#ifdef RENDERER_VULKAN

#include "../renderer.h"
#include "../window.h"
#include "../logger.h"
#include "surface.h"

#include <cstddef>
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
static const u32 mesh_vert_spv[] =
#include "mesh.vert.inc"
;
static const u32 mesh_frag_spv[] =
#include "mesh.frag.inc"
;

constexpr u32 FRAMES_IN_FLIGHT     = 2;
constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;
// Meshes are copied into a buffer of this size every frame they're drawn in.
constexpr u64 STREAM_BUFFER_SIZE   = 16 * MiB;
// Textures stay on the GPU once drawn: past this many, meshes are drawn untextured.
constexpr u32 MAX_TEXTURES         = 256;
// Texture pixels are sRGB-encoded, so the shaders sample linear colors.
constexpr VkFormat TEXTURE_FORMAT  = VK_FORMAT_R8G8B8A8_SRGB;

struct Native_Renderer_Frame
{
    VkCommandPool   command_pool   = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore     image_acquired = VK_NULL_HANDLE;
    VkFence         in_flight      = VK_NULL_HANDLE;

    // Host-visible vertex and index data for the frame's meshes, filled from the
    // start each frame: the GPU is done with it once in_flight is signaled.
    VkBuffer        stream_buffer  = VK_NULL_HANDLE;
    VkDeviceMemory  stream_memory  = VK_NULL_HANDLE;
    u8             *stream_mapped  = nullptr;
    u64             stream_used    = 0;
};

// A texture copied to the GPU, with the descriptor set the mesh shaders sample it
// through. No descriptor set means it couldn't be created: it's drawn white.
struct Native_Texture
{
    const u8       *pixels         = nullptr; // The game's, to recognize the texture by
    u32             width          = 0;
    u32             height         = 0;
    VkImage         image          = VK_NULL_HANDLE;
    VkDeviceMemory  memory         = VK_NULL_HANDLE;
    VkImageView     view           = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

// Matches the push constant block in shaders/mesh.vert.
struct Mesh_Push_Constants
{
    Matrix4 model_view_projection;
    float   normal_matrix[3][4]; // mat3: three columns, each padded to a vec4
    Vector4 color;
};
static_assert(sizeof(Mesh_Push_Constants) == 128, "Vulkan guarantees only 128 bytes of push constants");

struct Pipeline_Description
{
    const u32 *vertex_code;
    u64        vertex_size;
    const u32 *fragment_code;
    u64        fragment_size;
    const VkPipelineVertexInputStateCreateInfo *vertex_input;
    VkPipelineLayout layout;
    bool       depth_test; // Also writes depth and culls back faces
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
    bool create_texture_resources();
    bool create_pipelines();
    bool create_graphics_pipeline(const Pipeline_Description &description, VkPipeline *pipeline);
    bool create_swapchain(u32 width, u32 height);
    bool create_depth_buffer(VkExtent2D extent);
    bool create_buffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                       VkBuffer *buffer, VkDeviceMemory *memory);
    // Uploads the pixels and waits for it. Leaves `texture` without a descriptor set on failure.
    void create_texture(const u8 *pixels, u32 width, u32 height, Native_Texture *texture);
    void destroy_texture(Native_Texture *texture);
    // The GPU copy of a texture, made the first time it's drawn.
    const Native_Texture *find_texture(const Texture &texture);
    void destroy_swapchain_resources();
    VkShaderModule create_shader_module(const u32 *code, u64 size);
    // Returns UINT32_MAX when no memory type fits.
    u32  find_memory_type(u32 type_bits, VkMemoryPropertyFlags properties);

    Window *window = nullptr;

    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface         = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device = VK_NULL_HANDLE;
    VkDevice                 device          = VK_NULL_HANDLE;
    VkQueue                  queue           = VK_NULL_HANDLE;
    u32                      queue_family    = 0;
    VkSurfaceFormatKHR       surface_format  = {};
    VkFormat                 depth_format    = VK_FORMAT_UNDEFINED;

    VkSwapchainKHR swapchain        = VK_NULL_HANDLE;
    VkExtent2D     swapchain_extent = {};
    bool           swapchain_dirty  = false;
    u32            image_count      = 0;
    VkImage        images[MAX_SWAPCHAIN_IMAGES]          = {};
    VkImageView    image_views[MAX_SWAPCHAIN_IMAGES]     = {};
    // Signaled by rendering, waited on by present. One per swapchain image rather
    // than per frame, since present doesn't tell us when it's done with it.
    VkSemaphore    render_finished[MAX_SWAPCHAIN_IMAGES] = {};

    // Sized like the swapchain. One is enough for all frames in flight: the barrier
    // in begin_frame() waits for the previous frame's depth writes.
    VkImage        depth_image  = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView    depth_view   = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout      = VK_NULL_HANDLE;
    VkPipeline       pipeline             = VK_NULL_HANDLE;
    VkPipelineLayout mesh_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       mesh_pipeline        = VK_NULL_HANDLE;

    VkDescriptorSetLayout texture_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool      texture_pool       = VK_NULL_HANDLE;
    VkSampler             sampler            = VK_NULL_HANDLE;
    VkCommandPool         upload_pool        = VK_NULL_HANDLE; // For texture uploads
    Native_Texture        white_texture;                       // For meshes drawn without one
    Native_Texture        textures[MAX_TEXTURES];
    u32                   texture_count      = 0;

    Native_Renderer_Frame frames[FRAMES_IN_FLIGHT];
    u32  frame_index   = 0;
    u32  image_index   = 0;
    bool frame_started = false;
    bool ready         = false;
    bool stream_full_logged   = false;
    bool textures_full_logged = false;
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
           && this->native->create_texture_resources()
           && this->native->create_pipelines();

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

        if (n->pipeline)             vkDestroyPipeline(n->device, n->pipeline, nullptr);
        if (n->pipeline_layout)      vkDestroyPipelineLayout(n->device, n->pipeline_layout, nullptr);
        if (n->mesh_pipeline)        vkDestroyPipeline(n->device, n->mesh_pipeline, nullptr);
        if (n->mesh_pipeline_layout) vkDestroyPipelineLayout(n->device, n->mesh_pipeline_layout, nullptr);

        for (u32 i = 0; i < n->texture_count; ++i) n->destroy_texture(&n->textures[i]);
        n->destroy_texture(&n->white_texture);
        if (n->texture_pool)       vkDestroyDescriptorPool(n->device, n->texture_pool, nullptr); // Frees the sets
        if (n->texture_set_layout) vkDestroyDescriptorSetLayout(n->device, n->texture_set_layout, nullptr);
        if (n->sampler)            vkDestroySampler(n->device, n->sampler, nullptr);
        if (n->upload_pool)        vkDestroyCommandPool(n->device, n->upload_pool, nullptr);

        n->destroy_swapchain_resources();
        if (n->swapchain) vkDestroySwapchainKHR(n->device, n->swapchain, nullptr);

        for (auto &frame : n->frames)
        {
            if (frame.in_flight)      vkDestroyFence(n->device, frame.in_flight, nullptr);
            if (frame.image_acquired) vkDestroySemaphore(n->device, frame.image_acquired, nullptr);
            if (frame.command_pool)   vkDestroyCommandPool(n->device, frame.command_pool, nullptr);
            if (frame.stream_buffer)  vkDestroyBuffer(n->device, frame.stream_buffer, nullptr);
            if (frame.stream_memory)  vkFreeMemory(n->device, frame.stream_memory, nullptr); // Unmaps it
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
    frame.stream_used = 0;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(frame.command_buffer, &begin_info);

    const VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    VkImageMemoryBarrier2 to_attachment[2] = {
        {
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
        },
        {
            // After the previous frame's depth tests, which use the same image.
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = depth_stages,
            .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = depth_stages,
            .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = n->depth_image,
            .subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers    = to_attachment,
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
    VkRenderingAttachmentInfo depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = n->depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = { .depthStencil = { 1.0f, 0 } }, // Far
    };
    VkRenderingInfo rendering = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { { 0, 0 }, n->swapchain_extent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
        .pDepthAttachment     = &depth_attachment,
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

void Renderer::draw_mesh(const Mesh &mesh, const Matrix4 &transform, const Matrix4 &view_projection, const Vector4 &color,
                         const Texture &texture)
{
    Native_Renderer *n = this->native;
    if (!n->frame_started || mesh.index_count == 0) return;

    Native_Renderer_Frame &frame = n->frames[n->frame_index];

    // Vertices, then indices, both at 16-byte aligned offsets.
    auto align = [](u64 value) { return (value + 15) & ~(u64)15; };
    u64 vertices_size  = (u64)mesh.vertex_count * sizeof(Mesh_Vertex);
    u64 indices_size   = (u64)mesh.index_count * sizeof(u32);
    u64 vertex_offset  = frame.stream_used;
    u64 index_offset   = align(vertex_offset + vertices_size);
    u64 end            = align(index_offset + indices_size);

    if (end > STREAM_BUFFER_SIZE)
    {
        if (!n->stream_full_logged)
        {
            log_warn("Meshes past %llu MiB a frame aren't drawn", (unsigned long long)(STREAM_BUFFER_SIZE / MiB));
            n->stream_full_logged = true;
        }
        return;
    }

    // Before anything is recorded for the mesh: a texture's first draw uploads it.
    const Native_Texture *native_texture = n->find_texture(texture);

    memcpy(frame.stream_mapped + vertex_offset, mesh.vertices, vertices_size);
    memcpy(frame.stream_mapped + index_offset,  mesh.indices,  indices_size);
    frame.stream_used = end;

    Mesh_Push_Constants push = {};
    push.model_view_projection = view_projection * transform;
    push.color                 = color;

    Matrix4 normals = normal_matrix(transform);
    for (u32 column = 0; column < 3; ++column)
        for (u32 row = 0; row < 3; ++row)
            push.normal_matrix[column][row] = normals.at(row, column);

    VkCommandBuffer command_buffer = frame.command_buffer;
    VkDeviceSize    offset         = vertex_offset;
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->mesh_pipeline);
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &frame.stream_buffer, &offset);
    vkCmdBindIndexBuffer(command_buffer, frame.stream_buffer, index_offset, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->mesh_pipeline_layout,
                            0, 1, &native_texture->descriptor_set, 0, nullptr);
    vkCmdPushConstants(command_buffer, n->mesh_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, 0, 0, 0);
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

    // One of these two is always supported as a depth attachment.
    this->depth_format = VK_FORMAT_X8_D24_UNORM_PACK32;
    VkFormatProperties depth_properties;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, VK_FORMAT_D32_SFLOAT, &depth_properties);
    if (depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        this->depth_format = VK_FORMAT_D32_SFLOAT;

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

        // Coherent, so writes need no flush before the submit.
        if (!this->create_buffer(STREAM_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &frame.stream_buffer, &frame.stream_memory)) return false;
        if (!vk_check(vkMapMemory(this->device, frame.stream_memory, 0, VK_WHOLE_SIZE, 0, (void **)&frame.stream_mapped),
                      "vkMapMemory")) return false;
    }

    return true;
}

u32 Native_Renderer::find_memory_type(u32 type_bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(this->physical_device, &memory);

    for (u32 i = 0; i < memory.memoryTypeCount; ++i)
    {
        if ((type_bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    return UINT32_MAX;
}

bool Native_Renderer::create_buffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                    VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo buffer_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!vk_check(vkCreateBuffer(this->device, &buffer_info, nullptr, buffer), "vkCreateBuffer")) return false;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(this->device, *buffer, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, properties);
    if (memory_type == UINT32_MAX)
    {
        log_error("No Vulkan memory type fits a buffer");
        return false;
    }

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, memory), "vkAllocateMemory")) return false;
    return vk_check(vkBindBufferMemory(this->device, *buffer, *memory, 0), "vkBindBufferMemory");
}

bool Native_Renderer::create_texture_resources()
{
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };
    if (!vk_check(vkCreateDescriptorSetLayout(this->device, &layout_info, nullptr, &this->texture_set_layout),
                  "vkCreateDescriptorSetLayout")) return false;

    // One set per texture, and one for the white texture.
    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_TEXTURES + 1,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_TEXTURES + 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };
    if (!vk_check(vkCreateDescriptorPool(this->device, &pool_info, nullptr, &this->texture_pool),
                  "vkCreateDescriptorPool")) return false;

    // glTF's default sampler: linear filtering, repeating.
    VkSamplerCreateInfo sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod       = VK_LOD_CLAMP_NONE,
    };
    if (!vk_check(vkCreateSampler(this->device, &sampler_info, nullptr, &this->sampler), "vkCreateSampler")) return false;

    VkCommandPoolCreateInfo upload_pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = this->queue_family,
    };
    if (!vk_check(vkCreateCommandPool(this->device, &upload_pool_info, nullptr, &this->upload_pool),
                  "vkCreateCommandPool")) return false;

    static const u8 white[4] = { 255, 255, 255, 255 };
    this->create_texture(white, 1, 1, &this->white_texture);
    return this->white_texture.descriptor_set != VK_NULL_HANDLE;
}

void Native_Renderer::create_texture(const u8 *pixels, u32 width, u32 height, Native_Texture *texture)
{
    *texture = {};
    texture->pixels = pixels;
    texture->width  = width;
    texture->height = height;

    // Each mip level is blitted from the previous one, which needs linear filtering of
    // the format: without it the texture has just its full-size level.
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, TEXTURE_FORMAT, &format_properties);
    const VkFormatFeatureFlags blit_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    u32 mip_levels = 1;
    if ((format_properties.optimalTilingFeatures & blit_features) == blit_features)
    {
        for (u32 size = width > height ? width : height; size > 1; size /= 2) ++mip_levels;
    }

    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = TEXTURE_FORMAT,
        .extent        = { width, height, 1 },
        .mipLevels     = mip_levels,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, &texture->image), "vkCreateImage")) return;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, texture->image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, &texture->memory), "vkAllocateMemory")) return;
    if (!vk_check(vkBindImageMemory(this->device, texture->image, texture->memory, 0), "vkBindImageMemory")) return;

    // The pixels go through a host-visible staging buffer.
    u64            size           = texture_storage_size(width, height);
    VkBuffer       staging        = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void          *staging_mapped = nullptr;
    bool ok = this->create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &staging, &staging_memory)
           && vk_check(vkMapMemory(this->device, staging_memory, 0, VK_WHOLE_SIZE, 0, &staging_mapped), "vkMapMemory");

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (ok)
    {
        memcpy(staging_mapped, pixels, size);

        VkCommandBufferAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = this->upload_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        ok = vk_check(vkAllocateCommandBuffers(this->device, &allocate_info, &command_buffer), "vkAllocateCommandBuffers");
    }

    if (ok)
    {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(command_buffer, &begin_info);

        // A barrier moving mip levels [first, first + count) between layouts.
        auto barrier = [&](u32 first, u32 count, VkImageLayout old_layout, VkImageLayout new_layout,
                           VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
        {
            VkImageMemoryBarrier2 image_barrier = {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask        = src_stage,
                .srcAccessMask       = src_access,
                .dstStageMask        = dst_stage,
                .dstAccessMask       = dst_access,
                .oldLayout           = old_layout,
                .newLayout           = new_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = texture->image,
                .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, first, count, 0, 1 },
            };
            VkDependencyInfo dependency = {
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers    = &image_barrier,
            };
            vkCmdPipelineBarrier2(command_buffer, &dependency);
        };

        const VkPipelineStageFlags2 transfer = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        barrier(0, mip_levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0, transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy copy = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent      = { width, height, 1 },
        };
        vkCmdCopyBufferToImage(command_buffer, staging, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Each level is half the previous one (at least 1 pixel), filtered down from it.
        i32 level_width  = (i32)width;
        i32 level_height = (i32)height;
        for (u32 level = 1; level < mip_levels; ++level)
        {
            barrier(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, transfer, VK_ACCESS_2_TRANSFER_READ_BIT);

            i32 next_width  = level_width  > 1 ? level_width  / 2 : 1;
            i32 next_height = level_height > 1 ? level_height / 2 : 1;
            VkImageBlit blit = {
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 },
                .srcOffsets     = { { 0, 0, 0 }, { level_width, level_height, 1 } },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                .dstOffsets     = { { 0, 0, 0 }, { next_width, next_height, 1 } },
            };
            vkCmdBlitImage(command_buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            level_width  = next_width;
            level_height = next_height;
        }

        // All levels but the last were blit sources; the last was only written.
        const VkPipelineStageFlags2 fragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        if (mip_levels > 1)
        {
            barrier(0, mip_levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    transfer, VK_ACCESS_2_TRANSFER_READ_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        barrier(mip_levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        vkEndCommandBuffer(command_buffer);

        // Waiting keeps it simple: textures are uploaded once, usually while loading.
        VkCommandBufferSubmitInfo command_buffer_info = {
            .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command_buffer,
        };
        VkSubmitInfo2 submit = {
            .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos    = &command_buffer_info,
        };
        ok = vk_check(vkQueueSubmit2(this->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2")
          && vk_check(vkQueueWaitIdle(this->queue), "vkQueueWaitIdle");
    }

    if (command_buffer) vkFreeCommandBuffers(this->device, this->upload_pool, 1, &command_buffer);
    if (staging)        vkDestroyBuffer(this->device, staging, nullptr);
    if (staging_memory) vkFreeMemory(this->device, staging_memory, nullptr); // Unmaps it
    if (!ok) return;

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = texture->image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = TEXTURE_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1 },
    };
    if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &texture->view), "vkCreateImageView")) return;

    VkDescriptorSetAllocateInfo set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = this->texture_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &this->texture_set_layout,
    };
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    if (!vk_check(vkAllocateDescriptorSets(this->device, &set_info, &descriptor_set), "vkAllocateDescriptorSets")) return;

    VkDescriptorImageInfo image_descriptor = {
        .sampler     = this->sampler,
        .imageView   = texture->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptor_set,
        .dstBinding      = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image_descriptor,
    };
    vkUpdateDescriptorSets(this->device, 1, &write, 0, nullptr);

    texture->descriptor_set = descriptor_set;
}

void Native_Renderer::destroy_texture(Native_Texture *texture)
{
    if (texture->view)   vkDestroyImageView(this->device, texture->view, nullptr);
    if (texture->image)  vkDestroyImage(this->device, texture->image, nullptr);
    if (texture->memory) vkFreeMemory(this->device, texture->memory, nullptr);
    *texture = {};
}

const Native_Texture *Native_Renderer::find_texture(const Texture &texture)
{
    if (!texture.pixels || texture.width == 0 || texture.height == 0) return &this->white_texture;

    const Native_Texture *found = nullptr;
    for (u32 i = 0; i < this->texture_count && !found; ++i)
    {
        const Native_Texture &candidate = this->textures[i];
        if (candidate.pixels == texture.pixels && candidate.width == texture.width && candidate.height == texture.height)
            found = &candidate;
    }

    if (!found)
    {
        if (this->texture_count == MAX_TEXTURES)
        {
            if (!this->textures_full_logged)
            {
                log_warn("Textures past the first %u are drawn white", MAX_TEXTURES);
                this->textures_full_logged = true;
            }
            return &this->white_texture;
        }

        // Kept even when it fails, so it isn't tried again every frame.
        Native_Texture *created = &this->textures[this->texture_count++];
        this->create_texture(texture.pixels, texture.width, texture.height, created);
        if (!created->descriptor_set)
            log_error("Could not upload a %ux%u texture: it's drawn white", texture.width, texture.height);
        found = created;
    }

    return found->descriptor_set ? found : &this->white_texture;
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

bool Native_Renderer::create_pipelines()
{
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (!vk_check(vkCreatePipelineLayout(this->device, &layout_info, nullptr, &this->pipeline_layout), "vkCreatePipelineLayout")) return false;

    VkPipelineVertexInputStateCreateInfo no_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    Pipeline_Description triangle = {
        .vertex_code   = triangle_vert_spv,
        .vertex_size   = sizeof(triangle_vert_spv),
        .fragment_code = triangle_frag_spv,
        .fragment_size = sizeof(triangle_frag_spv),
        .vertex_input  = &no_vertex_input,
        .layout        = this->pipeline_layout,
        .depth_test    = false,
    };
    if (!this->create_graphics_pipeline(triangle, &this->pipeline)) return false;

    VkPushConstantRange push_constants = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Mesh_Push_Constants),
    };
    VkPipelineLayoutCreateInfo mesh_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->texture_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &mesh_layout_info, nullptr, &this->mesh_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    VkVertexInputBindingDescription binding = {
        .binding   = 0,
        .stride    = sizeof(Mesh_Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[3] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Mesh_Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Mesh_Vertex, normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Mesh_Vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo mesh_vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions    = attributes,
    };
    Pipeline_Description mesh = {
        .vertex_code   = mesh_vert_spv,
        .vertex_size   = sizeof(mesh_vert_spv),
        .fragment_code = mesh_frag_spv,
        .fragment_size = sizeof(mesh_frag_spv),
        .vertex_input  = &mesh_vertex_input,
        .layout        = this->mesh_pipeline_layout,
        .depth_test    = true,
    };
    return this->create_graphics_pipeline(mesh, &this->mesh_pipeline);
}

bool Native_Renderer::create_graphics_pipeline(const Pipeline_Description &description, VkPipeline *pipeline)
{
    VkShaderModule vertex   = this->create_shader_module(description.vertex_code, description.vertex_size);
    VkShaderModule fragment = this->create_shader_module(description.fragment_code, description.fragment_size);

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

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        VkPipelineViewportStateCreateInfo viewport = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
        };
        // Meshes are counter-clockwise from the front (see mesh.h); perspective()
        // flips Y, which keeps them counter-clockwise in Vulkan's framebuffer space.
        VkPipelineRasterizationStateCreateInfo rasterization = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode    = description.depth_test ? (VkCullModeFlags)VK_CULL_MODE_BACK_BIT : (VkCullModeFlags)VK_CULL_MODE_NONE,
            .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth   = 1.0f,
        };
        VkPipelineMultisampleStateCreateInfo multisample = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = description.depth_test,
            .depthWriteEnable = description.depth_test,
            .depthCompareOp   = VK_COMPARE_OP_LESS,
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
        // Every pipeline renders into the same attachments as begin_frame() sets up.
        VkPipelineRenderingCreateInfo rendering = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &this->surface_format.format,
            .depthAttachmentFormat   = this->depth_format,
        };

        VkGraphicsPipelineCreateInfo info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &rendering,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = description.vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState      = &viewport,
            .pRasterizationState = &rasterization,
            .pMultisampleState   = &multisample,
            .pDepthStencilState  = &depth_stencil,
            .pColorBlendState    = &blend,
            .pDynamicState       = &dynamic,
            .layout              = description.layout,
        };
        ok = vk_check(vkCreateGraphicsPipelines(this->device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline),
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

    if (!this->create_depth_buffer(extent)) return false;

    this->swapchain_extent = extent;
    this->swapchain_dirty  = false;
    return true;
}

bool Native_Renderer::create_depth_buffer(VkExtent2D extent)
{
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = this->depth_format,
        .extent        = { extent.width, extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, &this->depth_image), "vkCreateImage")) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, this->depth_image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, &this->depth_memory), "vkAllocateMemory")) return false;
    if (!vk_check(vkBindImageMemory(this->device, this->depth_image, this->depth_memory, 0), "vkBindImageMemory")) return false;

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = this->depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = this->depth_format,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    };
    return vk_check(vkCreateImageView(this->device, &view_info, nullptr, &this->depth_view), "vkCreateImageView");
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

    if (this->depth_view)   vkDestroyImageView(this->device, this->depth_view, nullptr);
    if (this->depth_image)  vkDestroyImage(this->device, this->depth_image, nullptr);
    if (this->depth_memory) vkFreeMemory(this->device, this->depth_memory, nullptr);
    this->depth_view   = VK_NULL_HANDLE;
    this->depth_image  = VK_NULL_HANDLE;
    this->depth_memory = VK_NULL_HANDLE;
}

#endif // RENDERER_VULKAN
