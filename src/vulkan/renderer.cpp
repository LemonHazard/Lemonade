#ifdef RENDERER_VULKAN

#include "../renderer.h"
#include "../window.h"
#include "../logger.h"
#include "surface.h"

#include <algorithm>
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
static const u32 shadow_vert_spv[] =
#include "shadow.vert.inc"
;
static const u32 tonemap_vert_spv[] =
#include "tonemap.vert.inc"
;
static const u32 tonemap_frag_spv[] =
#include "tonemap.frag.inc"
;
static const u32 sky_vert_spv[] =
#include "sky.vert.inc"
;
static const u32 sky_frag_spv[] =
#include "sky.frag.inc"
;
static const u32 environment_frag_spv[] =
#include "environment.frag.inc"
;
static const u32 bloom_down_frag_spv[] =
#include "bloom_down.frag.inc"
;
static const u32 bloom_up_frag_spv[] =
#include "bloom_up.frag.inc"
;
static const u32 ui_vert_spv[] =
#include "ui.vert.inc"
;
static const u32 ui_frag_spv[] =
#include "ui.frag.inc"
;
static const u32 wire_vert_spv[] =
#include "wire.vert.inc"
;
static const u32 wire_frag_spv[] =
#include "wire.frag.inc"
;

constexpr u32 FRAMES_IN_FLIGHT     = 2;
constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;
// Meshes stay on the GPU once drawn, until the game's memory they came from is
// forgotten, in one buffer of this size: past it, or past this many meshes, new ones
// aren't drawn.
constexpr u64 MESH_BUFFER_SIZE     = 512 * MiB;
constexpr u32 MAX_MESHES           = 4096;
constexpr u32 MESH_SLOT_BITS       = 13; // The hash table has twice MAX_MESHES slots
constexpr u32 MESH_SLOTS           = 1u << MESH_SLOT_BITS;
static_assert(MESH_SLOTS >= 2 * MAX_MESHES, "The mesh hash table needs empty slots to end its probes");
// New meshes and textures go through a staging buffer of this size each frame: when
// it's full the rest are drawn from the next frames on. Bigger textures are uploaded a
// band of rows per frame; bigger meshes aren't drawn at all.
constexpr u64 STAGING_BUFFER_SIZE  = 16 * MiB;
// Textures stay on the GPU once drawn: past this many, meshes are drawn untextured.
constexpr u32 MAX_TEXTURES         = 1024;
constexpr u32 TEXTURE_SLOT_BITS    = 11; // The hash table has twice MAX_TEXTURES slots
constexpr u32 TEXTURE_SLOTS        = 1u << TEXTURE_SLOT_BITS;
static_assert(TEXTURE_SLOTS >= 2 * MAX_TEXTURES, "The texture hash table needs empty slots to end its probes");
// Colors are sRGB-encoded, so the shaders sample them linear; normals are read as they are.
constexpr VkFormat COLOR_TEXTURE_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat DATA_TEXTURE_FORMAT  = VK_FORMAT_R8G8B8A8_UNORM;
// Each different set of textures a material uses gets a descriptor set, kept for the
// next draws: past this many, meshes with new ones aren't drawn.
constexpr u32 MAX_MATERIALS        = 1024;
constexpr u32 MATERIAL_SLOT_BITS   = 11;
constexpr u32 MATERIAL_SLOTS       = 1u << MATERIAL_SLOT_BITS;
static_assert(MATERIAL_SLOTS >= 2 * MAX_MATERIALS, "The material hash table needs empty slots to end its probes");
// The scene is rendered in linear colors that can go past 1, then tonemapped into the
// swapchain. Every device can render to and sample this format.
constexpr VkFormat HDR_FORMAT      = VK_FORMAT_R16G16B16A16_SFLOAT;
// The sun's shadows come from this many cascades, each covering a farther part of the
// view, a layer of the shadow map this many texels across.
constexpr u32 SHADOW_CASCADES      = 4;
// Where the cascades split the view: blending evenly spaced splits with geometrically
// spaced ones, which follow how much smaller things get with distance.
constexpr r32 CASCADE_SPLIT_BLEND  = 0.75f;
// The shadow map's layers are this many texels across.
constexpr u32 SHADOW_MAP_SIZE      = 2048;
// Surfaces look themselves up in the shadow map this many texels off along their
// normal, so they don't shadow themselves.
constexpr r32 SHADOW_NORMAL_OFFSET = 2.0f;
// The sky as surfaces reflect it, in cube maps of linear colors that can go past 1:
// specularly, with a mip level for each step of roughness from 0 to 1, and diffusely.
// The sky is smooth, so they can be small.
constexpr u32 ENVIRONMENT_SIZE     = 128;
constexpr u32 ENVIRONMENT_LEVELS   = 6; // 128 down to 4 pixels across
constexpr u32 IRRADIANCE_SIZE      = 16;
// The bloom is the frame blurred wide: its levels go from half the frame's size down
// to this many, or to 8 pixels.
constexpr u32 MAX_BLOOM_LEVELS     = 8;
// UI vertices and indices are copied to a buffer of this size each frame: past it, the
// rest of the frame's UI isn't drawn.
constexpr u64 UI_BUFFER_SIZE       = 8 * MiB;

// Matches the uniform blocks in shaders/mesh.vert and mesh.frag (std140: vec3s padded
// to vec4s).
struct Frame_Uniforms
{
    Matrix4 view_projection;
    Matrix4 light_view_projections[SHADOW_CASCADES]; // World to each shadow cascade's clip space
    Vector4 camera_position;
    Vector4 sun_direction;         // Unit length
    Vector4 sun_color;
    Vector4 sky_color;
    Vector4 ground_color;
    Vector4 shadow;                // 1 when on, unused, a texel in UV, unused
    Vector4 shadow_offsets;        // How far off surfaces each cascade is looked up, in world units
    Vector4 horizon_color;
    Matrix4 inverse_view_projection;
};

// A cube map to render into, face by face and level by level, and sample.
struct Cube_Map
{
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;                // All of it, to sample
    VkImageView    face_views[6 * ENVIRONMENT_LEVELS] = {}; // Level * 6 + face, to render into
    u32            size   = 0;                             // Of its faces' first level
    u32            levels = 0;
};

// Matches the push constant block in shaders/environment.frag.
struct Environment_Push_Constants
{
    u32 face;
    r32 size;
    r32 roughness;
    u32 irradiance;
};

struct Native_Renderer_Frame
{
    VkCommandPool   command_pool   = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore     image_acquired = VK_NULL_HANDLE;
    VkFence         in_flight      = VK_NULL_HANDLE;

    // The frame's draws into the shadow map, recorded alongside command_buffer's and
    // submitted before it, so that the shadows are there for its draws.
    VkCommandBuffer shadow_command_buffer = VK_NULL_HANDLE;

    // Copies of the frame's new meshes to mesh_buffer and of its new textures' rows,
    // submitted before the frame's other commands: rendering has begun there, and
    // copies can't go inside it.
    VkCommandBuffer upload_command_buffer = VK_NULL_HANDLE;
    bool            uploading             = false; // upload_command_buffer has begun

    // Host-visible vertices, indices and pixels of the frame's uploads, filled from
    // the start each frame: the GPU is done with it once in_flight is signaled.
    VkBuffer        staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory  staging_memory = VK_NULL_HANDLE;
    u8             *staging_mapped = nullptr;
    u64             staging_used   = 0;

    // The frame's camera and lighting, written by begin_frame() once the GPU is done
    // with the previous frame that used them, and the set the mesh shaders read them
    // through.
    VkBuffer        uniform_buffer = VK_NULL_HANDLE;
    VkDeviceMemory  uniform_memory = VK_NULL_HANDLE;
    Frame_Uniforms *uniform_mapped = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    // Host-visible vertices and indices of the frame's UI, which its draws read from
    // there: filled from the start each frame, like the staging buffer.
    VkBuffer        ui_buffer      = VK_NULL_HANDLE;
    VkDeviceMemory  ui_memory      = VK_NULL_HANDLE;
    u8             *ui_mapped      = nullptr;
    u64             ui_used        = 0;
};

// A mesh copied to mesh_buffer, recognized by the game's pointers and counts. No
// vertices means the hash table slot is empty.
struct Native_Mesh
{
    const Mesh_Vertex *vertices      = nullptr;
    const u32         *indices       = nullptr;
    u32                vertex_count  = 0;
    u32                index_count   = 0;
    u64                vertex_offset = 0; // In mesh_buffer, where its range of it starts
    u64                index_offset  = 0;
    u64                size          = 0; // Of its range of mesh_buffer
};

// A range of mesh_buffer.
struct Mesh_Range
{
    u64 offset;
    u64 size;
};

// A texture on the GPU, recognized by the game's pixels and the format they're read
// as. No pixels means the hash table slot is empty; no view means it couldn't be
// created: it's drawn as its slot's default. Its pixels are copied over one or more
// frames, from the top row down; meshes are drawn with it once they're all there.
struct Native_Texture
{
    const u8       *pixels         = nullptr; // The game's, to recognize the texture by
    u32             width          = 0;
    u32             height         = 0;
    VkFormat        format         = VK_FORMAT_UNDEFINED;
    u32             mip_levels     = 0;
    u32             rows_uploaded  = 0;       // From the top, with their copies recorded
    bool            replaced       = false;   // Copied again (see update_texture()): earlier frames may still read it
    VkImage         image          = VK_NULL_HANDLE;
    VkDeviceMemory  memory         = VK_NULL_HANDLE;
    VkImageView     view           = VK_NULL_HANDLE;
    VkDescriptorSet ui_set         = VK_NULL_HANDLE; // For UI draws, made with the first one
};

// The descriptor set of a material's textures, set 1 of the mesh shaders, recognized by
// them. No base color texture means the hash table slot is empty.
struct Native_Material
{
    const Native_Texture *base_color     = nullptr;
    const Native_Texture *normal         = nullptr;
    const Native_Texture *emissive       = nullptr;
    VkDescriptorSet       descriptor_set = VK_NULL_HANDLE;
};

// How much nearer a mesh is drawn for each step of its depth_order, in the depth buffer's
// units: a few times what D24 tells apart, and D32 near the far plane, where most of what's
// seen is, so that surfaces at the same depth, rounded apart by their own triangles, are
// still in order. It adds up, so the highest orders are seen through what's just in
// front of them, from far enough.
constexpr r32 DEPTH_ORDER_STEP = 1.0f / (1 << 22);

// Matches the push constant blocks in shaders/mesh.vert and mesh.frag: the transform
// and the material's factors.
struct Mesh_Push_Constants
{
    Matrix4 model;                  // Model to world
    Vector4 base_color;
    Vector4 emissive_metallic;      // Emissive color, metallic
    Vector4 roughness_normal_alpha; // Roughness, normal scale, alpha cutoff (0 for none), 1 when blended
    r32     depth_offset;           // How much nearer it's drawn, in the depth buffer's units
};

// A draw of a blended material, kept for the end of the frame: they're drawn after
// the solid ones, from the farthest to the nearest.
struct Blended_Draw
{
    Mesh_Push_Constants push;
    VkDescriptorSet     material_set;
    VkDeviceSize        vertex_offset; // In mesh_buffer
    VkDeviceSize        vertex_size;
    VkDeviceSize        index_offset;
    u32                 first_index;
    u32                 index_count;
    r32                 distance;      // From the camera to the mesh's origin
};
static_assert(sizeof(Mesh_Push_Constants) <= 128, "Vulkan guarantees only 128 bytes of push constants");

// Matches the push constant block in shaders/shadow.vert.
struct Shadow_Push_Constants
{
    Matrix4 model;
};

// Matches the push constant block in shaders/tonemap.frag.
struct Tonemap_Push_Constants
{
    r32 exposure;
    r32 bloom;
};

// Matches the push constant blocks in shaders/bloom_down.frag and bloom_up.frag.
struct Bloom_Push_Constants
{
    Vector2 source_texel; // Size of a texel of the image read, in UV
    Vector2 target_texel; // And of the level drawn
    r32     first;        // 1 for the first step down
};

// Matches the push constant blocks in shaders/ui.vert and ui.frag.
struct Ui_Push_Constants
{
    Vector2 scale;       // From the vertices' units to clip space
    r32     encode_srgb; // 1 when the swapchain doesn't encode sRGB itself
};

// A UI draw, kept for the end of the frame: they're drawn over it once it's tonemapped.
struct Ui_Draw
{
    VkDescriptorSet texture_set;
    VkDeviceSize    vertex_offset; // In the frame's ui_buffer
    VkDeviceSize    index_offset;
    u32             first_index;
    u32             index_count;
    VkRect2D        scissor;
    r32             scale;         // Pixels per unit of the vertices' positions
};

// Matches the push constant blocks in shaders/wire.vert and wire.frag.
struct Wire_Push_Constants
{
    Matrix4 model;        // Model to world
    Vector4 color;        // sRGB-encoded, alpha not premultiplied
    r32     encode_srgb;  // 1 when the swapchain doesn't encode sRGB itself
    r32     depth_offset; // The mesh's, as it was drawn
};

// A wireframe draw, kept for the end of the frame: they're drawn over it once it's
// tonemapped, before the UI.
struct Wire_Draw
{
    Wire_Push_Constants push;
    VkDeviceSize        vertex_offset; // In mesh_buffer
    VkDeviceSize        vertex_size;
    VkDeviceSize        index_offset;
    u32                 first_index;
    u32                 index_count;
};

enum class Blend_Mode
{
    None,
    Premultiplied, // "Over" what's there
    Additive,
};

struct Pipeline_Description
{
    const u32 *vertex_code;
    u64        vertex_size;
    const u32 *fragment_code;  // None for depth only
    u64        fragment_size;
    const VkPipelineVertexInputStateCreateInfo *vertex_input;
    VkPipelineLayout layout;
    VkFormat   color_format;   // Of the attachments it renders into, VK_FORMAT_UNDEFINED for none
    VkFormat   depth_format;
    bool       depth_test;     // Also writes depth
    VkCullModeFlags cull_mode;
    bool       shadow_caster;  // Biases depth by its slope, and clamps it when the device can
    u32        view_mask;      // Of the views it draws, as layers of its attachments: 0 for just one
    Blend_Mode blend;          // When it's not None, depth isn't written
    bool       depth_equal;    // Draws only where the depth that's there is its own: it isn't written
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
    bool create_shadow_resources();
    bool create_environment_resources();
    bool create_cube_map(u32 size, u32 levels, Cube_Map *cube);
    void destroy_cube_map(Cube_Map *cube);
    // Renders the sky's light into environment_map and irradiance_map, from the frame's
    // uniforms, leaving them ready to be sampled.
    void record_environment(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    bool create_frame_descriptors();
    bool create_pipelines();
    bool create_graphics_pipeline(const Pipeline_Description &description, VkPipeline *pipeline);
    bool create_swapchain(u32 width, u32 height);
    // An image to render into, with a view of it: of all its layers, when there are more.
    bool create_attachment(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                           VkImage *image, VkDeviceMemory *memory, VkImageView *view, u32 layers = 1);
    void destroy_attachment(VkImage *image, VkDeviceMemory *memory, VkImageView *view);
    // bloom_image, its levels sized from `extent`, the frame's.
    bool create_bloom_image(VkExtent2D extent);
    bool create_buffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                       VkBuffer *buffer, VkDeviceMemory *memory);
    // Creates the image and its view, without uploading anything. Leaves `texture`
    // without a view on failure.
    void create_texture(const u8 *pixels, u32 width, u32 height, VkFormat format, Native_Texture *texture);
    // Records the copy of as many of the texture's remaining rows as the frame's
    // staging buffer has room for, and its mip levels once the last ones are in.
    void upload_texture(Native_Texture *texture, Native_Renderer_Frame &frame);
    void destroy_texture(Native_Texture *texture);
    // The GPU copy of a texture read as `format`, created the first time it's drawn and
    // uploaded over that frame and the next ones; `fallback` when it has no pixels or
    // can't be made. Returns nullptr while it's being uploaded: draws with it wait.
    Native_Texture *find_texture(const Texture &texture, VkFormat format, Native_Texture *fallback,
                                 Native_Renderer_Frame &frame);
    // The descriptor set of the material's textures, made once they're all on the GPU.
    // Returns VK_NULL_HANDLE until then, or when there are too many (logged).
    VkDescriptorSet find_material(const Material &material, Native_Renderer_Frame &frame);
    // Creates `texture` from `pixels` and uploads it before any frame begins, waiting for it.
    bool create_texture_now(const u8 *pixels, u32 width, u32 height, VkFormat format, Native_Texture *texture);
    // The GPU copy of a mesh, recorded into the frame's uploads the first time it's
    // drawn. Returns nullptr when the mesh can't be drawn this frame (logged).
    const Native_Mesh *find_mesh(const Mesh &mesh, Native_Renderer_Frame &frame);
    // A range of mesh_buffer of `size` bytes, the first one that fits: false when none does.
    bool allocate_mesh_range(u64 size, u64 *offset);
    void free_mesh_range(Mesh_Range range);
    // Begins the frame's upload_command_buffer, unless it already has.
    void begin_uploads(Native_Renderer_Frame &frame);
    // Ends the frame's upload_command_buffer, after a barrier making its mesh copies
    // visible to the draws submitted after it.
    void end_uploads(Native_Renderer_Frame &frame);
    void destroy_swapchain_resources();
    // Ends the frame's commands, submits and presents it. With `capture` the frame is
    // also copied to capture_buffer: returns whether it was, so that waiting for the
    // frame's fence leaves its pixels there.
    bool finish_frame(bool capture);
    // Clears the shadow map and gets the shadow pipeline ready for draws. Every frame
    // has the pass, so the map is always ready to be sampled, if only as all lit.
    void begin_shadow_pass(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    // Leaves the shadow map ready to be sampled by the frame's draws.
    void end_shadow_pass(VkCommandBuffer command_buffer);
    // Draws the scene's colors into the swapchain image, fitted to the display, and the
    // frame's wireframes and UI over them.
    void record_tonemap(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    // Draws the frame's UI draws, in the order they came, inside the tonemapping's rendering.
    void record_ui_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    // Draws the frame's wireframes, inside the tonemapping's rendering before the UI.
    void record_wire_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    // The set UI draws read the texture through, made the first time. VK_NULL_HANDLE
    // when it can't be.
    VkDescriptorSet ui_texture_set(Native_Texture *texture);
    // Blurs the scene into bloom_image, leaving both ready to be sampled.
    void record_bloom(VkCommandBuffer command_buffer);
    // Draws the frame's blended draws over its solid ones, from the farthest on.
    void record_blended_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame);
    bool record_capture(VkCommandBuffer command_buffer);
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
    VkFormat                 shadow_format   = VK_FORMAT_UNDEFINED; // Depth that can be sampled too
    VkFilter                 shadow_filter   = VK_FILTER_NEAREST;   // Linear when shadow_format can be
    bool                     depth_clamp     = false;               // The device feature, enabled when there
    bool                     wire_barycentrics = false;             // fragmentShaderBarycentric, for wireframes: enabled when there

    VkSwapchainKHR swapchain        = VK_NULL_HANDLE;
    VkExtent2D     swapchain_extent = {};
    bool           swapchain_dirty  = false;
    bool           swapchain_readable = false; // Its images can be copied from, for captures
    u32            image_count      = 0;
    VkImage        images[MAX_SWAPCHAIN_IMAGES]          = {};
    VkImageView    image_views[MAX_SWAPCHAIN_IMAGES]     = {};
    // Signaled by rendering, waited on by present. One per swapchain image rather
    // than per frame, since present doesn't tell us when it's done with it.
    VkSemaphore    render_finished[MAX_SWAPCHAIN_IMAGES] = {};

    // Sized like the swapchain. One of each is enough for all frames in flight: the
    // barriers in begin_frame() wait for the previous frame to be done with them.
    VkImage        depth_image  = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView    depth_view   = VK_NULL_HANDLE;
    VkImage        hdr_image    = VK_NULL_HANDLE; // The scene, before it's tonemapped into the swapchain
    VkDeviceMemory hdr_memory   = VK_NULL_HANDLE;
    VkImageView    hdr_view     = VK_NULL_HANDLE;

    // The sun's view of the scene's depth, SHADOW_MAP_SIZE across: shared by the
    // frames like the images above, but not sized like the swapchain.
    VkImage        shadow_image   = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory  = VK_NULL_HANDLE;
    VkImageView    shadow_view    = VK_NULL_HANDLE; // All the layers, to render into and sample
    VkSampler      shadow_sampler = VK_NULL_HANDLE; // Compares depths
    bool           shadows_on     = false;          // This frame's
    Vector3        camera_position;                 // This frame's
    std::vector<Blended_Draw> blended_draws;        // This frame's, so far
    std::vector<Ui_Draw>      ui_draws;             // This frame's, so far
    std::vector<Wire_Draw>    wire_draws;           // This frame's, so far
    // The UI mesh last copied to the frame's ui_buffer and where it went: draws of the
    // same one in a row (ImGui's draw lists) share the copy.
    Ui_Mesh                   ui_copied        = {};
    VkDeviceSize              ui_vertex_offset = 0;
    VkDeviceSize              ui_index_offset  = 0;

    VkPipelineLayout pipeline_layout      = VK_NULL_HANDLE;
    VkPipeline       pipeline             = VK_NULL_HANDLE;
    VkPipelineLayout mesh_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       mesh_pipeline        = VK_NULL_HANDLE;
    VkPipeline       mesh_blend_pipeline  = VK_NULL_HANDLE; // Same layout
    VkPipelineLayout shadow_pipeline_layout  = VK_NULL_HANDLE;
    VkPipeline       shadow_pipeline         = VK_NULL_HANDLE;
    VkPipelineLayout tonemap_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       tonemap_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout environment_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       environment_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout sky_pipeline_layout         = VK_NULL_HANDLE;
    VkPipeline       sky_pipeline                = VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout          = VK_NULL_HANDLE;
    VkPipeline       ui_pipeline                 = VK_NULL_HANDLE;
    VkPipelineLayout wire_pipeline_layout        = VK_NULL_HANDLE;
    VkPipeline       wire_pipeline               = VK_NULL_HANDLE; // None without wire_barycentrics
    VkDescriptorPool ui_pool                     = VK_NULL_HANDLE; // A set for each texture UI draws use
    VkSampler        ui_sampler                  = VK_NULL_HANDLE; // Linear, clamped to the edges

    // The sky's maps, remade when its colors change: sky, horizon, ground.
    Cube_Map         environment_map;
    Cube_Map         irradiance_map;
    Vector3          environment_colors[3] = {};
    bool             environment_made      = false;
    VkDescriptorSetLayout tonemap_set_layout = VK_NULL_HANDLE; // The scene, its bloom
    VkDescriptorSet  tonemap_set             = VK_NULL_HANDLE; // hdr_image and bloom_image, from frame_pool
    VkPipelineLayout bloom_pipeline_layout   = VK_NULL_HANDLE;
    VkPipeline       bloom_down_pipeline     = VK_NULL_HANDLE;
    VkPipeline       bloom_up_pipeline       = VK_NULL_HANDLE;
    VkSampler        bloom_sampler           = VK_NULL_HANDLE; // Linear, clamped to the edges
    // The bloom's levels, sized from the swapchain: each one's view, and sets to read
    // them: [0] reads hdr_image, [1 + i] level i.
    VkImage          bloom_image             = VK_NULL_HANDLE;
    VkDeviceMemory   bloom_memory            = VK_NULL_HANDLE;
    VkImageView      bloom_views[MAX_BLOOM_LEVELS] = {};
    VkExtent2D       bloom_extents[MAX_BLOOM_LEVELS] = {};
    u32              bloom_levels            = 0;
    VkDescriptorSet  bloom_sets[MAX_BLOOM_LEVELS + 1] = {};
    r32              bloom                   = 0.0f;           // This frame's
    r32              exposure                = 1.0f;           // This frame's

    VkDescriptorSetLayout frame_set_layout   = VK_NULL_HANDLE; // Set 0 of the mesh shaders
    VkDescriptorPool      frame_pool         = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_set_layout = VK_NULL_HANDLE; // Set 1: base color, normal, emissive
    VkDescriptorPool      material_pool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout image_set_layout    = VK_NULL_HANDLE; // One image: the tonemapping's
    VkSampler             sampler             = VK_NULL_HANDLE;
    Native_Texture        white_texture;                        // For materials without a color texture
    Native_Texture        flat_texture;                         // For materials without a normal texture
    Native_Texture        textures[TEXTURE_SLOTS];              // Open addressing on the pixels' address
    u32                   texture_count       = 0;
    Native_Material       materials[MATERIAL_SLOTS];            // Open addressing on the textures' addresses
    u32                   material_count      = 0;

    // Device-local vertices and indices of every mesh drawn so far, until it's forgotten.
    VkBuffer                mesh_buffer = VK_NULL_HANDLE;
    VkDeviceMemory          mesh_memory = VK_NULL_HANDLE;
    std::vector<Mesh_Range> mesh_free;         // What no mesh has of it, by offset, none touching another
    Native_Mesh             meshes[MESH_SLOTS]; // Open addressing on the vertices' address
    u32                     mesh_count  = 0;

    // Frames read back for screenshots: copied to a host-visible buffer, then
    // converted to RGBA in capture_pixels. Made for the first capture.
    VkBuffer        capture_buffer = VK_NULL_HANDLE;
    VkDeviceMemory  capture_memory = VK_NULL_HANDLE;
    u8             *capture_mapped = nullptr;
    u64             capture_size   = 0;
    VkExtent2D      capture_extent = {};
    std::vector<u8> capture_pixels;

    Native_Renderer_Frame frames[FRAMES_IN_FLIGHT];
    u32  frame_index   = 0;
    u32  image_index   = 0;
    bool frame_started = false;
    bool ready         = false;
    bool mesh_too_big_logged  = false;
    bool meshes_full_logged   = false;
    bool textures_full_logged = false;
    bool materials_full_logged = false;
    bool ui_full_logged        = false;
};

// Fibonacci hashing of an address into a table of 2^bits slots: the low bits are
// alignment, always zero, so the multiply moves the varying ones to the top.
static u32 hash_address(const void *address, u32 bits)
{
    return (u32)(((u64)(uintptr_t)address * 11400714819323198485ull) >> (64 - bits));
}

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
           && this->native->create_shadow_resources()
           && this->native->create_environment_resources()
           && this->native->create_frame_descriptors()
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
        if (n->mesh_blend_pipeline)  vkDestroyPipeline(n->device, n->mesh_blend_pipeline, nullptr);
        if (n->mesh_pipeline_layout) vkDestroyPipelineLayout(n->device, n->mesh_pipeline_layout, nullptr);
        if (n->shadow_pipeline)         vkDestroyPipeline(n->device, n->shadow_pipeline, nullptr);
        if (n->shadow_pipeline_layout)  vkDestroyPipelineLayout(n->device, n->shadow_pipeline_layout, nullptr);
        if (n->tonemap_pipeline)        vkDestroyPipeline(n->device, n->tonemap_pipeline, nullptr);
        if (n->bloom_down_pipeline)     vkDestroyPipeline(n->device, n->bloom_down_pipeline, nullptr);
        if (n->bloom_up_pipeline)       vkDestroyPipeline(n->device, n->bloom_up_pipeline, nullptr);
        if (n->bloom_pipeline_layout)   vkDestroyPipelineLayout(n->device, n->bloom_pipeline_layout, nullptr);
        if (n->bloom_sampler)           vkDestroySampler(n->device, n->bloom_sampler, nullptr);
        if (n->tonemap_set_layout)      vkDestroyDescriptorSetLayout(n->device, n->tonemap_set_layout, nullptr);
        if (n->tonemap_pipeline_layout) vkDestroyPipelineLayout(n->device, n->tonemap_pipeline_layout, nullptr);
        if (n->ui_pipeline)             vkDestroyPipeline(n->device, n->ui_pipeline, nullptr);
        if (n->ui_pipeline_layout)      vkDestroyPipelineLayout(n->device, n->ui_pipeline_layout, nullptr);
        if (n->wire_pipeline)           vkDestroyPipeline(n->device, n->wire_pipeline, nullptr);
        if (n->wire_pipeline_layout)    vkDestroyPipelineLayout(n->device, n->wire_pipeline_layout, nullptr);
        if (n->ui_pool)                 vkDestroyDescriptorPool(n->device, n->ui_pool, nullptr); // Frees the sets
        if (n->ui_sampler)              vkDestroySampler(n->device, n->ui_sampler, nullptr);

        for (auto &texture : n->textures) n->destroy_texture(&texture);
        n->destroy_texture(&n->white_texture);
        n->destroy_texture(&n->flat_texture);
        if (n->material_pool)       vkDestroyDescriptorPool(n->device, n->material_pool, nullptr); // Frees the sets
        if (n->material_set_layout) vkDestroyDescriptorSetLayout(n->device, n->material_set_layout, nullptr);
        if (n->image_set_layout)    vkDestroyDescriptorSetLayout(n->device, n->image_set_layout, nullptr);
        if (n->frame_pool)         vkDestroyDescriptorPool(n->device, n->frame_pool, nullptr);
        if (n->frame_set_layout)   vkDestroyDescriptorSetLayout(n->device, n->frame_set_layout, nullptr);
        if (n->sampler)            vkDestroySampler(n->device, n->sampler, nullptr);

        n->destroy_attachment(&n->shadow_image, &n->shadow_memory, &n->shadow_view);
        n->destroy_cube_map(&n->environment_map);
        n->destroy_cube_map(&n->irradiance_map);
        if (n->environment_pipeline)        vkDestroyPipeline(n->device, n->environment_pipeline, nullptr);
        if (n->environment_pipeline_layout) vkDestroyPipelineLayout(n->device, n->environment_pipeline_layout, nullptr);
        if (n->sky_pipeline)                vkDestroyPipeline(n->device, n->sky_pipeline, nullptr);
        if (n->sky_pipeline_layout)         vkDestroyPipelineLayout(n->device, n->sky_pipeline_layout, nullptr);
        if (n->shadow_sampler) vkDestroySampler(n->device, n->shadow_sampler, nullptr);

        if (n->mesh_buffer) vkDestroyBuffer(n->device, n->mesh_buffer, nullptr);
        if (n->mesh_memory) vkFreeMemory(n->device, n->mesh_memory, nullptr);

        if (n->capture_buffer) vkDestroyBuffer(n->device, n->capture_buffer, nullptr);
        if (n->capture_memory) vkFreeMemory(n->device, n->capture_memory, nullptr); // Unmaps it

        n->destroy_swapchain_resources();
        if (n->swapchain) vkDestroySwapchainKHR(n->device, n->swapchain, nullptr);

        for (auto &frame : n->frames)
        {
            if (frame.in_flight)      vkDestroyFence(n->device, frame.in_flight, nullptr);
            if (frame.image_acquired) vkDestroySemaphore(n->device, frame.image_acquired, nullptr);
            if (frame.command_pool)   vkDestroyCommandPool(n->device, frame.command_pool, nullptr);
            if (frame.staging_buffer) vkDestroyBuffer(n->device, frame.staging_buffer, nullptr);
            if (frame.staging_memory) vkFreeMemory(n->device, frame.staging_memory, nullptr); // Unmaps it
            if (frame.uniform_buffer) vkDestroyBuffer(n->device, frame.uniform_buffer, nullptr);
            if (frame.uniform_memory) vkFreeMemory(n->device, frame.uniform_memory, nullptr);
            if (frame.ui_buffer)      vkDestroyBuffer(n->device, frame.ui_buffer, nullptr);
            if (frame.ui_memory)      vkFreeMemory(n->device, frame.ui_memory, nullptr); // Unmaps it
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

bool Renderer::begin_frame(const Render_Commands &commands)
{
    Native_Renderer *n = this->native;
    const Lighting  &lighting = commands.lighting;
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

    Vector3 sun_direction = normalize(lighting.sun_direction);

    // Each shadow cascade looks along the sunlight at the sphere around a slice of the
    // camera's view, from far enough to see all of it.
    n->shadows_on = lighting.shadow_distance > 0.0f;
    Matrix4 light_view_projections[SHADOW_CASCADES];
    r32     shadow_offsets[SHADOW_CASCADES] = {};
    for (Matrix4 &matrix : light_view_projections) matrix = identity();
    if (n->shadows_on)
    {
        // The view's corners, near and far, and how far along the view they are.
        Matrix4 clip_to_world = inverse(commands.view_projection);
        Vector3 near_corners[4], far_corners[4];
        for (u32 i = 0; i < 4; ++i)
        {
            r32 x = (i & 1) ? 1.0f : -1.0f;
            r32 y = (i & 2) ? 1.0f : -1.0f;
            auto unproject = [&](r32 z)
            {
                Vector4 p = clip_to_world * Vector4{ x, y, z, 1.0f };
                return Vector3{ p.x / p.w, p.y / p.w, p.z / p.w };
            };
            near_corners[i] = unproject(0.0f);
            far_corners[i]  = unproject(1.0f);
        }
        Vector3 near_center = (near_corners[0] + near_corners[1] + near_corners[2] + near_corners[3]) * 0.25f;
        Vector3 far_center  = (far_corners[0] + far_corners[1] + far_corners[2] + far_corners[3]) * 0.25f;
        Vector3 forward     = normalize(far_center - near_center);
        r32     near        = dot(near_center - commands.camera_position, forward);
        r32     far         = dot(far_center - commands.camera_position, forward);
        r32     last        = std::min(lighting.shadow_distance, far);
        if (near <= 0.0f) near = 0.001f;

        // From the origin, so the view stays the same wherever the cascades are.
        Vector3 up         = std::fabs(sun_direction.y) > 0.99f ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
        Matrix4 light_view = look_at({}, sun_direction * -1.0f, up);

        for (u32 cascade = 0; cascade < SHADOW_CASCADES; ++cascade)
        {
            // The slice of the view between two splits, as its eight corners.
            auto split = [&](u32 i)
            {
                r32 t = (r32)i / (r32)SHADOW_CASCADES;
                r32 even = near + (last - near) * t;
                r32 geometric = near * std::pow(last / near, t);
                return geometric + (even - geometric) * (1.0f - CASCADE_SPLIT_BLEND);
            };
            r32 from = split(cascade), to = split(cascade + 1);
            Vector3 corners[8];
            for (u32 i = 0; i < 4; ++i)
            {
                Vector3 ray = far_corners[i] - near_corners[i];
                corners[i]     = near_corners[i] + ray * ((from - near) / (far - near));
                corners[i + 4] = near_corners[i] + ray * ((to - near) / (far - near));
            }

            // Its bounding sphere stays the same size however the camera turns, so the
            // shadow map's texels do too.
            Vector3 center = {};
            for (const Vector3 &corner : corners) center += corner * 0.125f;
            r32 radius = 0.0f;
            for (const Vector3 &corner : corners) radius = std::max(radius, length(corner - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            // On whole texels, so that the shadows' edges don't crawl when the camera moves.
            r32 texel = 2.0f * radius / (r32)SHADOW_MAP_SIZE;
            Vector3 light_center = transform_point(light_view, center);
            light_center.x = std::floor(light_center.x / texel) * texel;
            light_center.y = std::floor(light_center.y / texel) * texel;

            r32 distance = -light_center.z; // In front of the light, which looks down -Z
            Matrix4 projection = orthographic(light_center.x - radius, light_center.x + radius,
                                              light_center.y - radius, light_center.y + radius,
                                              distance - radius, distance + radius);
            light_view_projections[cascade] = projection * light_view;
            shadow_offsets[cascade]         = SHADOW_NORMAL_OFFSET * texel;
        }
    }

    auto to_vector4 = [](Vector3 v) { return Vector4{ v.x, v.y, v.z, 0.0f }; };
    *frame.uniform_mapped = {
        .view_projection       = commands.view_projection,
        .light_view_projections = { light_view_projections[0], light_view_projections[1],
                                    light_view_projections[2], light_view_projections[3] },
        .camera_position       = to_vector4(commands.camera_position),
        .sun_direction         = to_vector4(sun_direction),
        .sun_color             = to_vector4(lighting.sun_color),
        .sky_color             = to_vector4(lighting.sky_color),
        .ground_color          = to_vector4(lighting.ground_color),
        .shadow                = { n->shadows_on ? 1.0f : 0.0f, 0.0f, 1.0f / (r32)SHADOW_MAP_SIZE, 0.0f },
        .shadow_offsets        = { shadow_offsets[0], shadow_offsets[1], shadow_offsets[2], shadow_offsets[3] },
        .horizon_color         = to_vector4(lighting.horizon_color),
        .inverse_view_projection = inverse(commands.view_projection),
    };
    n->exposure        = lighting.exposure;
    n->bloom           = lighting.bloom;
    n->camera_position = commands.camera_position;
    n->blended_draws.clear();
    n->ui_draws.clear();
    n->wire_draws.clear();
    n->ui_copied = {};
    vkResetCommandPool(n->device, frame.command_pool, 0); // Both of its command buffers
    frame.uploading    = false;
    frame.staging_used = 0;
    frame.ui_used      = 0;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(frame.shadow_command_buffer, &begin_info);
    n->begin_shadow_pass(frame.shadow_command_buffer, frame);
    vkBeginCommandBuffer(frame.command_buffer, &begin_info);

    // The sky's maps, the first time and when its colors change.
    const Vector3 sky_colors[3] = { lighting.sky_color, lighting.horizon_color, lighting.ground_color };
    if (!n->environment_made || memcmp(sky_colors, n->environment_colors, sizeof(sky_colors)) != 0)
    {
        n->record_environment(frame.command_buffer, frame);
        memcpy(n->environment_colors, sky_colors, sizeof(sky_colors));
        n->environment_made = true;
    }

    const VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    VkImageMemoryBarrier2 to_attachment[2] = {
        {
            // After the previous frame's tonemapping read it.
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = n->hdr_image,
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
        .imageView   = n->hdr_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = { { commands.clear_color.r, commands.clear_color.g, commands.clear_color.b,
                                      commands.clear_color.a } } },
    };
    VkRenderingAttachmentInfo depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = n->depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE, // For the wireframes, over the tonemapped frame
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

    if (commands.sky_background)
    {
        vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->sky_pipeline);
        vkCmdBindDescriptorSets(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->sky_pipeline_layout,
                                0, 1, &frame.descriptor_set, 0, nullptr);
        vkCmdDraw(frame.command_buffer, 3, 1, 0, 0);
    }

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

void Renderer::draw_mesh(const Mesh &mesh, u32 first_index, u32 index_count, const Matrix4 &transform,
                         const Material &material, u32 depth_order)
{
    Native_Renderer *n = this->native;
    if (!n->frame_started || index_count == 0) return;
    if (first_index > mesh.index_count || index_count > mesh.index_count - first_index) return;

    Native_Renderer_Frame &frame = n->frames[n->frame_index];

    const Native_Mesh *native_mesh = n->find_mesh(mesh, frame);
    if (!native_mesh) return;

    // Not drawn until its textures are all there, which can take a few frames.
    VkDescriptorSet material_set = n->find_material(material, frame);
    if (!material_set) return;

    bool blended = material.alpha_mode == Alpha_Mode::Blend;
    r32  cutoff  = material.alpha_mode == Alpha_Mode::Mask ? material.alpha_cutoff : 0.0f;
    Mesh_Push_Constants push = {
        .model                  = transform,
        .base_color             = material.base_color,
        .emissive_metallic      = { material.emissive.x, material.emissive.y, material.emissive.z, material.metallic },
        .roughness_normal_alpha = { material.roughness, material.normal_scale, cutoff, blended ? 1.0f : 0.0f },
        .depth_offset           = (r32)depth_order * DEPTH_ORDER_STEP,
    };

    // With their size, so that later frames' uploads to the rest of mesh_buffer don't
    // look like they could overwrite what this draw reads.
    VkCommandBuffer command_buffer = frame.command_buffer;
    VkDeviceSize    offset         = native_mesh->vertex_offset;
    VkDeviceSize    size           = (VkDeviceSize)mesh.vertex_count * sizeof(Mesh_Vertex);

    // Blended surfaces cast no shadow.
    if (blended)
    {
        Vector3 origin = { transform.at(0, 3), transform.at(1, 3), transform.at(2, 3) };
        n->blended_draws.push_back({ push, material_set, offset, size, native_mesh->index_offset, first_index, index_count,
                                     length(origin - n->camera_position) });
        return;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->mesh_pipeline);
    vkCmdBindVertexBuffers2(command_buffer, 0, 1, &n->mesh_buffer, &offset, &size, nullptr);
    vkCmdBindIndexBuffer(command_buffer, n->mesh_buffer, native_mesh->index_offset, VK_INDEX_TYPE_UINT32);
    VkDescriptorSet descriptor_sets[2] = { frame.descriptor_set, material_set };
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, n->mesh_pipeline_layout,
                            0, 2, descriptor_sets, 0, nullptr);
    vkCmdPushConstants(command_buffer, n->mesh_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDrawIndexed(command_buffer, index_count, 1, first_index, 0, 0);

    if (n->shadows_on)
    {
        Shadow_Push_Constants shadow_push = { transform };
        VkCommandBuffer shadow_command_buffer = frame.shadow_command_buffer;
        vkCmdBindVertexBuffers2(shadow_command_buffer, 0, 1, &n->mesh_buffer, &offset, &size, nullptr);
        vkCmdBindIndexBuffer(shadow_command_buffer, n->mesh_buffer, native_mesh->index_offset, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(shadow_command_buffer, n->shadow_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(shadow_push), &shadow_push);
        vkCmdDrawIndexed(shadow_command_buffer, index_count, 1, first_index, 0, 0);
    }
}

void Renderer::draw_wireframe(const Mesh &mesh, u32 first_index, u32 index_count, const Matrix4 &transform,
                              const Vector4 &color, u32 depth_order)
{
    Native_Renderer *n = this->native;
    if (!n->frame_started || !n->wire_pipeline || index_count == 0) return;
    if (first_index > mesh.index_count || index_count > mesh.index_count - first_index) return;

    Native_Renderer_Frame &frame = n->frames[n->frame_index];
    const Native_Mesh *native_mesh = n->find_mesh(mesh, frame);
    if (!native_mesh) return;

    bool srgb = n->surface_format.format == VK_FORMAT_B8G8R8A8_SRGB || n->surface_format.format == VK_FORMAT_R8G8B8A8_SRGB;
    Wire_Push_Constants push = { .model = transform, .color = color, .encode_srgb = srgb ? 0.0f : 1.0f,
                                 .depth_offset = (r32)depth_order * DEPTH_ORDER_STEP };
    n->wire_draws.push_back({ push, native_mesh->vertex_offset, (VkDeviceSize)mesh.vertex_count * sizeof(Mesh_Vertex),
                              native_mesh->index_offset, first_index, index_count });
}

void Renderer::draw_ui(const Ui_Mesh &mesh, u32 first_index, u32 index_count, const Texture &texture,
                       const Vector4 &clip, r32 scale)
{
    Native_Renderer *n = this->native;
    if (!n->frame_started || index_count == 0 || !mesh.vertices || !mesh.indices) return;
    if (first_index > mesh.index_count || index_count > mesh.index_count - first_index) return;

    Native_Renderer_Frame &frame = n->frames[n->frame_index];

    // Not drawn until its texture is there, which can take a few frames.
    Native_Texture *native_texture = n->find_texture(texture, COLOR_TEXTURE_FORMAT, &n->white_texture, frame);
    if (!native_texture) return;
    VkDescriptorSet texture_set = n->ui_texture_set(native_texture);
    if (!texture_set) return;

    // The clip rectangle in pixels, inside the frame, on whole pixels around it.
    r32 left   = std::max(std::floor(clip.x * scale), 0.0f);
    r32 top    = std::max(std::floor(clip.y * scale), 0.0f);
    r32 right  = std::min(std::ceil(clip.z * scale), (r32)n->swapchain_extent.width);
    r32 bottom = std::min(std::ceil(clip.w * scale), (r32)n->swapchain_extent.height);
    if (right <= left || bottom <= top) return;
    VkRect2D scissor = { { (i32)left, (i32)top }, { (u32)(right - left), (u32)(bottom - top) } };

    // Vertices, then indices, at 16-byte aligned offsets, unless they're the last ones copied.
    bool copied = mesh.vertices == n->ui_copied.vertices && mesh.indices == n->ui_copied.indices &&
                  mesh.vertex_count == n->ui_copied.vertex_count && mesh.index_count == n->ui_copied.index_count;
    if (!copied)
    {
        auto align = [](u64 value) { return (value + 15) & ~(u64)15; };
        u64 vertices_start = align(frame.ui_used);
        u64 indices_start  = align(vertices_start + (u64)mesh.vertex_count * sizeof(Ui_Vertex));
        u64 end            = indices_start + (u64)mesh.index_count * sizeof(u32);
        if (end > UI_BUFFER_SIZE)
        {
            if (!n->ui_full_logged)
            {
                log_warn("UI past %llu MiB of vertices and indices in a frame isn't drawn",
                         (unsigned long long)(UI_BUFFER_SIZE / MiB));
                n->ui_full_logged = true;
            }
            return;
        }

        memcpy(frame.ui_mapped + vertices_start, mesh.vertices, (u64)mesh.vertex_count * sizeof(Ui_Vertex));
        memcpy(frame.ui_mapped + indices_start,  mesh.indices,  (u64)mesh.index_count * sizeof(u32));
        frame.ui_used       = end;
        n->ui_copied        = mesh;
        n->ui_vertex_offset = vertices_start;
        n->ui_index_offset  = indices_start;
    }

    n->ui_draws.push_back({ texture_set, n->ui_vertex_offset, n->ui_index_offset, first_index, index_count, scissor, scale });
}

void Renderer::update_texture(const Texture &texture)
{
    Native_Renderer *n = this->native;
    if (!n->ready || !texture.pixels) return;

    // Whatever formats it's read as: they're all in the probe from its pixels' slot.
    for (u32 slot = hash_address(texture.pixels, TEXTURE_SLOT_BITS);; slot = (slot + 1) & (TEXTURE_SLOTS - 1))
    {
        Native_Texture &candidate = n->textures[slot];
        if (!candidate.pixels) break;
        if (candidate.pixels == texture.pixels && candidate.width == texture.width && candidate.height == texture.height &&
            candidate.view)
        {
            candidate.rows_uploaded = 0;
            candidate.replaced      = true;
        }
    }
}

void Renderer::forget_memory(const void *begin, const void *end)
{
    Native_Renderer *n = this->native;
    if (!n->ready) return;

    auto from = [&](const void *data) { return data >= begin && data < end; };

    u32 meshes   = 0;
    u32 textures = 0;
    for (const Native_Mesh &mesh : n->meshes) meshes += mesh.vertices && (from(mesh.vertices) || from(mesh.indices));
    for (const Native_Texture &texture : n->textures) textures += texture.pixels && from(texture.pixels);
    if (!meshes && !textures) return;

    // The frames being drawn may still use them.
    vk_check(vkDeviceWaitIdle(n->device), "vkDeviceWaitIdle");

    // Each table is filled again with what's kept: taking some out would break the
    // probes past them.
    std::vector<Native_Mesh> kept_meshes;
    for (Native_Mesh &mesh : n->meshes)
    {
        if (!mesh.vertices) continue;
        if (from(mesh.vertices) || from(mesh.indices))
            n->free_mesh_range({ mesh.vertex_offset, mesh.size });
        else
            kept_meshes.push_back(mesh);
        mesh = {};
    }
    for (const Native_Mesh &mesh : kept_meshes)
    {
        u32 slot = hash_address(mesh.vertices, MESH_SLOT_BITS);
        while (n->meshes[slot].vertices) slot = (slot + 1) & (MESH_SLOTS - 1);
        n->meshes[slot] = mesh;
    }
    n->mesh_count         = (u32)kept_meshes.size();
    n->meshes_full_logged = false;

    if (textures)
    {
        std::vector<Native_Texture> kept_textures;
        for (Native_Texture &texture : n->textures)
        {
            if (!texture.pixels) continue;
            if (from(texture.pixels))
            {
                if (texture.ui_set) vkFreeDescriptorSets(n->device, n->ui_pool, 1, &texture.ui_set);
                n->destroy_texture(&texture);
            }
            else
                kept_textures.push_back(texture);
            texture = {};
        }
        for (const Native_Texture &texture : kept_textures)
        {
            u32 slot = hash_address(texture.pixels, TEXTURE_SLOT_BITS);
            while (n->textures[slot].pixels) slot = (slot + 1) & (TEXTURE_SLOTS - 1);
            n->textures[slot] = texture;
        }
        n->texture_count        = (u32)kept_textures.size();
        n->textures_full_logged = false;

        // Materials know their textures by where they were in the table: they're all made
        // again as they're drawn.
        vk_check(vkResetDescriptorPool(n->device, n->material_pool, 0), "vkResetDescriptorPool");
        for (Native_Material &material : n->materials) material = {};
        n->material_count        = 0;
        n->materials_full_logged = false;
    }

    log_info("Forgot %u meshes and %u textures", meshes, textures);
}

void Renderer::end_frame()
{
    this->native->finish_frame(false);
}

Texture Renderer::end_frame_and_capture()
{
    Native_Renderer *n = this->native;
    if (!n->frame_started) return {};

    Native_Renderer_Frame &frame = n->frames[n->frame_index];
    if (!n->finish_frame(true)) return {};

    vkWaitForFences(n->device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);

    // The swapchain's bytes, as displayed: BGRA or RGBA. Alpha is dropped by the
    // compositor (the swapchain is opaque), so it's made opaque here too.
    u32  width  = n->capture_extent.width;
    u32  height = n->capture_extent.height;
    bool bgra   = n->surface_format.format == VK_FORMAT_B8G8R8A8_SRGB ||
                  n->surface_format.format == VK_FORMAT_B8G8R8A8_UNORM;

    n->capture_pixels.resize(texture_storage_size(width, height));
    const u8 *source = n->capture_mapped;
    u8       *pixels = n->capture_pixels.data();
    for (u64 i = 0, count = (u64)width * height; i < count; ++i, source += 4, pixels += 4)
    {
        pixels[0] = bgra ? source[2] : source[0];
        pixels[1] = source[1];
        pixels[2] = bgra ? source[0] : source[2];
        pixels[3] = 255;
    }

    return { n->capture_pixels.data(), width, height };
}

bool Native_Renderer::finish_frame(bool capture)
{
    if (!this->frame_started) return false;
    this->frame_started = false;

    Native_Renderer_Frame &frame = this->frames[this->frame_index];

    this->record_blended_draws(frame.command_buffer, frame);
    vkCmdEndRendering(frame.command_buffer);
    this->record_bloom(frame.command_buffer);
    this->record_tonemap(frame.command_buffer, frame);
    this->end_shadow_pass(frame.shadow_command_buffer);
    vkEndCommandBuffer(frame.shadow_command_buffer);

    // A capture leaves the image laid out for its copy, after the copy's reads.
    bool captured = capture && this->record_capture(frame.command_buffer);

    VkImageMemoryBarrier2 to_present = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = captured ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = captured ? (VkAccessFlags2)0 : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask       = 0,
        .oldLayout           = captured ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = this->images[this->image_index],
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_present,
    };
    vkCmdPipelineBarrier2(frame.command_buffer, &dependency);

    vkEndCommandBuffer(frame.command_buffer);

    if (frame.uploading) this->end_uploads(frame);

    VkSemaphoreSubmitInfo wait_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.image_acquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphoreSubmitInfo signal_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = this->render_finished[this->image_index],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    // The uploads first, when there are any, then the shadows.
    VkCommandBufferSubmitInfo command_buffer_infos[3] = {
        { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = frame.upload_command_buffer },
        { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = frame.shadow_command_buffer },
        { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = frame.command_buffer },
    };
    VkSubmitInfo2 submit = {
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_info,
        .commandBufferInfoCount   = frame.uploading ? 3u : 2u,
        .pCommandBufferInfos      = frame.uploading ? &command_buffer_infos[0] : &command_buffer_infos[1],
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_info,
    };
    // Without the submit the fence is never signaled: there's nothing to wait for.
    if (!vk_check(vkQueueSubmit2(this->queue, 1, &submit, frame.in_flight), "vkQueueSubmit2")) captured = false;

    VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &this->render_finished[this->image_index],
        .swapchainCount     = 1,
        .pSwapchains        = &this->swapchain,
        .pImageIndices      = &this->image_index,
    };
    VkResult result = vkQueuePresentKHR(this->queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        this->swapchain_dirty = true;
    }
    else
    {
        vk_check(result, "vkQueuePresentKHR");
    }

    this->frame_index = (this->frame_index + 1) % FRAMES_IN_FLIGHT;
    return captured;
}

void Native_Renderer::begin_uploads(Native_Renderer_Frame &frame)
{
    if (frame.uploading) return;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(frame.upload_command_buffer, &begin_info);
    frame.uploading = true;
}

void Native_Renderer::end_uploads(Native_Renderer_Frame &frame)
{
    // Covers the draws of this frame and the later ones, which come after it in
    // submission order. Textures have their own barriers, in upload_texture().
    VkMemoryBarrier2 to_vertex_input = {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT,
    };
    VkDependencyInfo dependency = {
        .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers    = &to_vertex_input,
    };
    vkCmdPipelineBarrier2(frame.upload_command_buffer, &dependency);
    vkEndCommandBuffer(frame.upload_command_buffer);
}

void Native_Renderer::begin_shadow_pass(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    const VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    // After the previous frame's draws sampled it.
    VkImageMemoryBarrier2 to_attachment = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = depth_stages,
        .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = this->shadow_image,
        .subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADES },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_attachment,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);

    VkRenderingAttachmentInfo depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = this->shadow_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .depthStencil = { 1.0f, 0 } }, // Far: nothing casts a shadow
    };
    // A view per cascade, each drawn into its layer.
    VkRenderingInfo rendering = {
        .sType            = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea       = { { 0, 0 }, { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE } },
        .layerCount       = 1,
        .viewMask         = (1u << SHADOW_CASCADES) - 1,
        .pDepthAttachment = &depth_attachment,
    };
    vkCmdBeginRendering(command_buffer, &rendering);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)SHADOW_MAP_SIZE,
        .height   = (float)SHADOW_MAP_SIZE,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { { 0, 0 }, { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE } };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->shadow_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->shadow_pipeline_layout,
                            0, 1, &frame.descriptor_set, 0, nullptr);
}

void Native_Renderer::end_shadow_pass(VkCommandBuffer command_buffer)
{
    vkCmdEndRendering(command_buffer);

    // The frame's draws come after it in submission order.
    VkImageMemoryBarrier2 to_sampled = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = this->shadow_image,
        .subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADES },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_sampled,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

void Native_Renderer::record_blended_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    if (this->blended_draws.empty()) return;

    // Sorted by their meshes' origins, which is close enough for separate panes of glass;
    // the triangles of one draw aren't sorted.
    std::stable_sort(this->blended_draws.begin(), this->blended_draws.end(),
                     [](const Blended_Draw &a, const Blended_Draw &b) { return a.distance > b.distance; });

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_blend_pipeline);
    for (const Blended_Draw &draw : this->blended_draws)
    {
        vkCmdBindVertexBuffers2(command_buffer, 0, 1, &this->mesh_buffer, &draw.vertex_offset, &draw.vertex_size, nullptr);
        vkCmdBindIndexBuffer(command_buffer, this->mesh_buffer, draw.index_offset, VK_INDEX_TYPE_UINT32);
        VkDescriptorSet descriptor_sets[2] = { frame.descriptor_set, draw.material_set };
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_pipeline_layout,
                                0, 2, descriptor_sets, 0, nullptr);
        vkCmdPushConstants(command_buffer, this->mesh_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw.push), &draw.push);
        vkCmdDrawIndexed(command_buffer, draw.index_count, 1, draw.first_index, 0, 0);
    }
}

void Native_Renderer::record_bloom(VkCommandBuffer command_buffer)
{
    // A barrier moving bloom levels [first, first + count), or the scene with
    // VK_REMAINING_MIP_LEVELS, between layouts.
    auto barrier = [&](u32 first, u32 count, VkImageLayout old_layout, VkImageLayout new_layout,
                       VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                       VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
    {
        bool scene = count == VK_REMAINING_MIP_LEVELS;
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
            .image               = scene ? this->hdr_image : this->bloom_image,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, scene ? 0 : first, scene ? 1 : count, 0, 1 },
        };
        VkDependencyInfo dependency = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &image_barrier,
        };
        vkCmdPipelineBarrier2(command_buffer, &dependency);
    };

    const VkPipelineStageFlags2 color_output = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags2 fragment     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkAccessFlags2        write        = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags2        read_write   = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags2        sampled      = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    // After the scene's draws; the levels after the previous frame's tonemapping read them.
    barrier(0, VK_REMAINING_MIP_LEVELS, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            color_output, write, fragment, sampled);
    barrier(0, this->bloom_levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            fragment, 0, color_output, read_write);

    // Draws level `level` from what `source` reads, whose texels are `source_size`.
    auto draw = [&](VkPipeline pipeline, u32 level, VkDescriptorSet source, VkExtent2D source_size, bool first, bool load)
    {
        VkExtent2D size = this->bloom_extents[level];
        VkRenderingAttachmentInfo color_attachment = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = this->bloom_views[level],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        };
        VkRenderingInfo rendering = {
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = { { 0, 0 }, size },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_attachment,
        };
        vkCmdBeginRendering(command_buffer, &rendering);

        VkViewport viewport = { 0.0f, 0.0f, (float)size.width, (float)size.height, 0.0f, 1.0f };
        VkRect2D   scissor  = { { 0, 0 }, size };
        Bloom_Push_Constants push = {
            .source_texel = { 1.0f / (float)source_size.width, 1.0f / (float)source_size.height },
            .target_texel = { 1.0f / (float)size.width, 1.0f / (float)size.height },
            .first        = first ? 1.0f : 0.0f,
        };
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->bloom_pipeline_layout,
                                0, 1, &source, 0, nullptr);
        vkCmdPushConstants(command_buffer, this->bloom_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);

        vkCmdEndRendering(command_buffer);
    };

    // Down: each level from the one before it, the first from the scene.
    for (u32 level = 0; level < this->bloom_levels; ++level)
    {
        VkExtent2D source_size = level == 0 ? this->swapchain_extent : this->bloom_extents[level - 1];
        draw(this->bloom_down_pipeline, level, this->bloom_sets[level], source_size, level == 0, false);
        barrier(level, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                color_output, write, fragment, sampled);
    }

    // Up: each level added to the one before it, from the smallest.
    for (u32 level = this->bloom_levels - 1; level > 0; --level)
    {
        barrier(level - 1, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                fragment, 0, color_output, read_write);
        draw(this->bloom_up_pipeline, level - 1, this->bloom_sets[level + 1], this->bloom_extents[level], false, true);
        barrier(level - 1, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                color_output, read_write, fragment, sampled);
    }
}

void Native_Renderer::record_tonemap(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    // The scene and its bloom are ready to be read: record_bloom() left them so. Its depth
    // is read by the wireframes' tests.
    const VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    VkImageMemoryBarrier2 barriers[2] = {
        {
            // After the acquire, which the submit waits for at this stage.
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = this->images[this->image_index],
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = depth_stages,
            .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = depth_stages,
            .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = this->depth_image,
            .subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        },
    };
    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers    = barriers,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);

    // Every pixel is drawn over, so what was there doesn't matter.
    VkRenderingAttachmentInfo color_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = this->image_views[this->image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingAttachmentInfo depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = this->depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp     = VK_ATTACHMENT_STORE_OP_NONE,
    };
    VkRenderingInfo rendering = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { { 0, 0 }, this->swapchain_extent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
        .pDepthAttachment     = &depth_attachment,
    };
    vkCmdBeginRendering(command_buffer, &rendering);

    // The bloom's passes left theirs.
    VkViewport viewport = { 0.0f, 0.0f, (float)this->swapchain_extent.width, (float)this->swapchain_extent.height, 0.0f, 1.0f };
    VkRect2D   scissor  = { { 0, 0 }, this->swapchain_extent };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    Tonemap_Push_Constants push = { .exposure = this->exposure, .bloom = this->bloom };
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->tonemap_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->tonemap_pipeline_layout,
                            0, 1, &this->tonemap_set, 0, nullptr);
    vkCmdPushConstants(command_buffer, this->tonemap_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    this->record_wire_draws(command_buffer, frame);
    this->record_ui_draws(command_buffer, frame);

    vkCmdEndRendering(command_buffer);
}

void Native_Renderer::record_ui_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    if (this->ui_draws.empty()) return;

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->ui_pipeline);

    bool srgb = this->surface_format.format == VK_FORMAT_B8G8R8A8_SRGB || this->surface_format.format == VK_FORMAT_R8G8B8A8_SRGB;
    VkDeviceSize vertex_offset = UINT64_MAX;
    VkDeviceSize index_offset  = UINT64_MAX;
    for (const Ui_Draw &draw : this->ui_draws)
    {
        if (draw.vertex_offset != vertex_offset)
        {
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &frame.ui_buffer, &draw.vertex_offset);
            vertex_offset = draw.vertex_offset;
        }
        if (draw.index_offset != index_offset)
        {
            vkCmdBindIndexBuffer(command_buffer, frame.ui_buffer, draw.index_offset, VK_INDEX_TYPE_UINT32);
            index_offset = draw.index_offset;
        }

        Ui_Push_Constants push = {
            .scale       = { 2.0f * draw.scale / (r32)this->swapchain_extent.width,
                             2.0f * draw.scale / (r32)this->swapchain_extent.height },
            .encode_srgb = srgb ? 0.0f : 1.0f,
        };
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->ui_pipeline_layout,
                                0, 1, &draw.texture_set, 0, nullptr);
        vkCmdPushConstants(command_buffer, this->ui_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdSetScissor(command_buffer, 0, 1, &draw.scissor);
        vkCmdDrawIndexed(command_buffer, draw.index_count, 1, draw.first_index, 0, 0);
    }
}

void Native_Renderer::record_wire_draws(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    if (this->wire_draws.empty()) return;

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->wire_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->wire_pipeline_layout,
                            0, 1, &frame.descriptor_set, 0, nullptr);
    for (const Wire_Draw &draw : this->wire_draws)
    {
        vkCmdBindVertexBuffers2(command_buffer, 0, 1, &this->mesh_buffer, &draw.vertex_offset, &draw.vertex_size, nullptr);
        vkCmdBindIndexBuffer(command_buffer, this->mesh_buffer, draw.index_offset, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(command_buffer, this->wire_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw.push), &draw.push);
        vkCmdDrawIndexed(command_buffer, draw.index_count, 1, draw.first_index, 0, 0);
    }
}

VkDescriptorSet Native_Renderer::ui_texture_set(Native_Texture *texture)
{
    if (texture->ui_set) return texture->ui_set;

    VkDescriptorSetAllocateInfo set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = this->ui_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &this->image_set_layout,
    };
    if (!vk_check(vkAllocateDescriptorSets(this->device, &set_info, &texture->ui_set), "vkAllocateDescriptorSets"))
        return VK_NULL_HANDLE;

    VkDescriptorImageInfo image = {
        .sampler     = this->ui_sampler,
        .imageView   = texture->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = texture->ui_set,
        .dstBinding      = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image,
    };
    vkUpdateDescriptorSets(this->device, 1, &write, 0, nullptr);
    return texture->ui_set;
}

bool Native_Renderer::record_capture(VkCommandBuffer command_buffer)
{
    if (!this->swapchain_readable)
    {
        log_error("Can't capture the frame: the swapchain's images can't be copied from");
        return false;
    }

    VkFormat format = this->surface_format.format;
    if (format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_B8G8R8A8_UNORM &&
        format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8A8_UNORM)
    {
        log_error("Can't capture the frame: its format (VkFormat %d) isn't 8-bit RGBA or BGRA", (int)format);
        return false;
    }

    // The previous capture's frame was waited for, so its buffer is free.
    u64 size = (u64)this->swapchain_extent.width * this->swapchain_extent.height * 4;
    if (size != this->capture_size)
    {
        if (this->capture_buffer) vkDestroyBuffer(this->device, this->capture_buffer, nullptr);
        if (this->capture_memory) vkFreeMemory(this->device, this->capture_memory, nullptr);
        this->capture_buffer = VK_NULL_HANDLE;
        this->capture_memory = VK_NULL_HANDLE;
        this->capture_mapped = nullptr;
        this->capture_size   = 0;

        bool ok = this->create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      &this->capture_buffer, &this->capture_memory)
               && vk_check(vkMapMemory(this->device, this->capture_memory, 0, VK_WHOLE_SIZE, 0,
                                       (void **)&this->capture_mapped), "vkMapMemory");
        if (!ok)
        {
            log_error("Can't capture the frame: no buffer to copy it to");
            return false;
        }
        this->capture_size = size;
    }
    this->capture_extent = this->swapchain_extent;

    VkImageMemoryBarrier2 to_copy = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = this->images[this->image_index],
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo before = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &to_copy,
    };
    vkCmdPipelineBarrier2(command_buffer, &before);

    VkBufferImageCopy copy = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent      = { this->capture_extent.width, this->capture_extent.height, 1 },
    };
    vkCmdCopyImageToBuffer(command_buffer, this->images[this->image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           this->capture_buffer, 1, &copy);

    // The fence alone doesn't make the copy's writes visible to the host.
    VkMemoryBarrier2 to_host = {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    };
    VkDependencyInfo after = {
        .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers    = &to_host,
    };
    vkCmdPipelineBarrier2(command_buffer, &after);

    return true;
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
        VkPhysicalDeviceVulkan11Features features11 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &features13,
        };
        VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features11,
        };
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features13.dynamicRendering || !features13.synchronization2 || !features11.multiview) continue;

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

    // The shadow map also needs sampling, which D16 always has; filtering its
    // comparisons is up to the device.
    const VkFormatFeatureFlags shadow_features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    this->shadow_format = (depth_properties.optimalTilingFeatures & shadow_features) == shadow_features
                        ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
    VkFormatProperties shadow_properties;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, this->shadow_format, &shadow_properties);
    if (shadow_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        this->shadow_filter = VK_FILTER_LINEAR;

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
    // Multiview draws the shadow cascades all at once.
    VkPhysicalDeviceVulkan11Features features11 = {
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext     = &features13,
        .multiview = VK_TRUE,
    };

    // Depth clamping lets what's between the sun and a shadow cascade cast shadows into
    // it; nearly every desktop device has it.
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(this->physical_device, &supported);
    this->depth_clamp = supported.depthClamp;
    // Wireframes find triangles' edges by where fragments are in their triangles, which
    // most desktop devices tell (the extension's feature).
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR,
    };
    {
        u32 extension_count = 0;
        vkEnumerateDeviceExtensionProperties(this->physical_device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        vkEnumerateDeviceExtensionProperties(this->physical_device, nullptr, &extension_count, available.data());
        for (const auto &extension : available)
        {
            if (strcmp(extension.extensionName, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME) == 0)
            {
                VkPhysicalDeviceFeatures2 query = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &barycentric };
                vkGetPhysicalDeviceFeatures2(this->physical_device, &query);
            }
        }
    }
    this->wire_barycentrics = barycentric.fragmentShaderBarycentric;
    if (!this->wire_barycentrics) log_warn("The device can't tell where fragments are in their triangles: wireframes aren't drawn");
    barycentric.pNext = &features11;

    VkPhysicalDeviceFeatures2 features = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = this->wire_barycentrics ? (void *)&barycentric : (void *)&features11,
        .features = { .depthClamp = this->depth_clamp },
    };

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME };

    VkDeviceCreateInfo info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &features,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_info,
        .enabledExtensionCount   = this->wire_barycentrics ? 2u : 1u,
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
        if (!vk_check(vkAllocateCommandBuffers(this->device, &allocate_info, &frame.upload_command_buffer), "vkAllocateCommandBuffers")) return false;
        if (!vk_check(vkAllocateCommandBuffers(this->device, &allocate_info, &frame.shadow_command_buffer), "vkAllocateCommandBuffers")) return false;

        VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (!vk_check(vkCreateSemaphore(this->device, &semaphore_info, nullptr, &frame.image_acquired), "vkCreateSemaphore")) return false;

        // Signaled, so the first wait on it doesn't block.
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (!vk_check(vkCreateFence(this->device, &fence_info, nullptr, &frame.in_flight), "vkCreateFence")) return false;

        // Coherent, so writes need no flush before the submit.
        if (!this->create_buffer(STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &frame.staging_buffer, &frame.staging_memory)) return false;
        if (!vk_check(vkMapMemory(this->device, frame.staging_memory, 0, VK_WHOLE_SIZE, 0, (void **)&frame.staging_mapped),
                      "vkMapMemory")) return false;

        if (!this->create_buffer(sizeof(Frame_Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &frame.uniform_buffer, &frame.uniform_memory)) return false;
        if (!vk_check(vkMapMemory(this->device, frame.uniform_memory, 0, VK_WHOLE_SIZE, 0, (void **)&frame.uniform_mapped),
                      "vkMapMemory")) return false;

        if (!this->create_buffer(UI_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &frame.ui_buffer, &frame.ui_memory)) return false;
        if (!vk_check(vkMapMemory(this->device, frame.ui_memory, 0, VK_WHOLE_SIZE, 0, (void **)&frame.ui_mapped),
                      "vkMapMemory")) return false;
    }

    this->mesh_free = { { 0, MESH_BUFFER_SIZE } };
    return this->create_buffer(MESH_BUFFER_SIZE,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &this->mesh_buffer, &this->mesh_memory);
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
    VkDescriptorSetLayoutBinding image_binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo image_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &image_binding,
    };
    if (!vk_check(vkCreateDescriptorSetLayout(this->device, &image_layout_info, nullptr, &this->image_set_layout),
                  "vkCreateDescriptorSetLayout")) return false;

    // The scene and its bloom.
    VkDescriptorSetLayoutBinding tonemap_bindings[2] = { image_binding, image_binding };
    tonemap_bindings[1].binding = 1;
    VkDescriptorSetLayoutCreateInfo tonemap_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings    = tonemap_bindings,
    };
    if (!vk_check(vkCreateDescriptorSetLayout(this->device, &tonemap_layout_info, nullptr, &this->tonemap_set_layout),
                  "vkCreateDescriptorSetLayout")) return false;

    // Blurring reads past the edges, which should stay there rather than wrap around.
    VkSamplerCreateInfo bloom_sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (!vk_check(vkCreateSampler(this->device, &bloom_sampler_info, nullptr, &this->bloom_sampler), "vkCreateSampler"))
        return false;

    // Base color, normal and emissive textures, in that order.
    VkDescriptorSetLayoutBinding material_bindings[3];
    for (u32 i = 0; i < 3; ++i)
    {
        material_bindings[i] = image_binding;
        material_bindings[i].binding = i;
    }
    VkDescriptorSetLayoutCreateInfo material_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings    = material_bindings,
    };
    if (!vk_check(vkCreateDescriptorSetLayout(this->device, &material_layout_info, nullptr, &this->material_set_layout),
                  "vkCreateDescriptorSetLayout")) return false;

    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 3 * MAX_MATERIALS,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_MATERIALS,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };
    if (!vk_check(vkCreateDescriptorPool(this->device, &pool_info, nullptr, &this->material_pool),
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

    // UI draws a texture's edges, as a font's, without the other side bleeding in.
    VkSamplerCreateInfo ui_sampler_info = sampler_info;
    ui_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ui_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ui_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!vk_check(vkCreateSampler(this->device, &ui_sampler_info, nullptr, &this->ui_sampler), "vkCreateSampler")) return false;

    // Every texture, and the white one, may be drawn as UI.
    VkDescriptorPoolSize ui_pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_TEXTURES + 1,
    };
    VkDescriptorPoolCreateInfo ui_pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, // Forgotten textures' sets are freed
        .maxSets       = MAX_TEXTURES + 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &ui_pool_size,
    };
    if (!vk_check(vkCreateDescriptorPool(this->device, &ui_pool_info, nullptr, &this->ui_pool), "vkCreateDescriptorPool"))
        return false;

    // The defaults are there from the first frame on.
    static const u8 white[4] = { 255, 255, 255, 255 };
    static const u8 flat[4]  = { 128, 128, 255, 255 }; // (0, 0, 1): straight out of the surface
    return this->create_texture_now(white, 1, 1, COLOR_TEXTURE_FORMAT, &this->white_texture)
        && this->create_texture_now(flat, 1, 1, DATA_TEXTURE_FORMAT, &this->flat_texture);
}

bool Native_Renderer::create_texture_now(const u8 *pixels, u32 width, u32 height, VkFormat format, Native_Texture *texture)
{
    // Through the first frame's staging buffer, before it begins.
    Native_Renderer_Frame &frame = this->frames[0];
    this->create_texture(pixels, width, height, format, texture);
    if (!texture->view) return false;
    this->upload_texture(texture, frame);
    this->end_uploads(frame);

    VkCommandBufferSubmitInfo command_buffer_info = {
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.upload_command_buffer,
    };
    VkSubmitInfo2 submit = {
        .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos    = &command_buffer_info,
    };
    bool ok = vk_check(vkQueueSubmit2(this->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2")
           && vk_check(vkQueueWaitIdle(this->queue), "vkQueueWaitIdle");

    vkResetCommandPool(this->device, frame.command_pool, 0);
    frame.uploading    = false;
    frame.staging_used = 0;
    return ok;
}

bool Native_Renderer::create_shadow_resources()
{
    if (!this->create_attachment({ SHADOW_MAP_SIZE, SHADOW_MAP_SIZE }, this->shadow_format,
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, &this->shadow_image, &this->shadow_memory, &this->shadow_view,
                                 SHADOW_CASCADES))
        return false;

    // A sample is lit where its reference depth is at most the map's. Off the map it
    // reads the border's depth, the farthest: lit.
    VkSamplerCreateInfo sampler_info = {
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter     = this->shadow_filter,
        .minFilter     = this->shadow_filter,
        .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .compareEnable = VK_TRUE,
        .compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL,
        .borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    return vk_check(vkCreateSampler(this->device, &sampler_info, nullptr, &this->shadow_sampler), "vkCreateSampler");
}

bool Native_Renderer::create_environment_resources()
{
    return this->create_cube_map(ENVIRONMENT_SIZE, ENVIRONMENT_LEVELS, &this->environment_map)
        && this->create_cube_map(IRRADIANCE_SIZE, 1, &this->irradiance_map);
}

bool Native_Renderer::create_cube_map(u32 size, u32 levels, Cube_Map *cube)
{
    cube->size   = size;
    cube->levels = levels;

    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = HDR_FORMAT,
        .extent        = { size, size, 1 },
        .mipLevels     = levels,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, &cube->image), "vkCreateImage")) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, cube->image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, &cube->memory), "vkAllocateMemory")) return false;
    if (!vk_check(vkBindImageMemory(this->device, cube->image, cube->memory, 0), "vkBindImageMemory")) return false;

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cube->image,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = HDR_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 6 },
    };
    if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &cube->view), "vkCreateImageView")) return false;

    for (u32 level = 0; level < levels; ++level)
    {
        for (u32 face = 0; face < 6; ++face)
        {
            view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, face, 1 };
            if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &cube->face_views[level * 6 + face]),
                          "vkCreateImageView")) return false;
        }
    }
    return true;
}

void Native_Renderer::destroy_cube_map(Cube_Map *cube)
{
    for (VkImageView view : cube->face_views)
        if (view) vkDestroyImageView(this->device, view, nullptr);
    if (cube->view)   vkDestroyImageView(this->device, cube->view, nullptr);
    if (cube->image)  vkDestroyImage(this->device, cube->image, nullptr);
    if (cube->memory) vkFreeMemory(this->device, cube->memory, nullptr);
    *cube = {};
}

void Native_Renderer::record_environment(VkCommandBuffer command_buffer, Native_Renderer_Frame &frame)
{
    // After the previous frames' draws sampled them.
    auto barrier = [&](VkImageLayout old_layout, VkImageLayout new_layout,
                       VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                       VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
    {
        VkImageMemoryBarrier2 barriers[2];
        const Cube_Map *cubes[2] = { &this->environment_map, &this->irradiance_map };
        for (u32 i = 0; i < 2; ++i)
        {
            barriers[i] = {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask        = src_stage,
                .srcAccessMask       = src_access,
                .dstStageMask        = dst_stage,
                .dstAccessMask       = dst_access,
                .oldLayout           = old_layout,
                .newLayout           = new_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = cubes[i]->image,
                .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, cubes[i]->levels, 0, 6 },
            };
        }
        VkDependencyInfo dependency = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers    = barriers,
        };
        vkCmdPipelineBarrier2(command_buffer, &dependency);
    };

    const VkPipelineStageFlags2 color_output = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags2 fragment     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            fragment, 0, color_output, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environment_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environment_pipeline_layout,
                            0, 1, &frame.descriptor_set, 0, nullptr);

    // Each face of each level is drawn over whole.
    auto draw_face = [&](const Cube_Map &cube, u32 level, u32 face, const Environment_Push_Constants &push)
    {
        u32 size = cube.size >> level;
        VkRenderingAttachmentInfo color_attachment = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = cube.face_views[level * 6 + face],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        };
        VkRenderingInfo rendering = {
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = { { 0, 0 }, { size, size } },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_attachment,
        };
        vkCmdBeginRendering(command_buffer, &rendering);

        VkViewport viewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };
        VkRect2D   scissor  = { { 0, 0 }, { size, size } };
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
        vkCmdPushConstants(command_buffer, this->environment_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);

        vkCmdEndRendering(command_buffer);
    };

    for (u32 level = 0; level < ENVIRONMENT_LEVELS; ++level)
    {
        for (u32 face = 0; face < 6; ++face)
        {
            r32 roughness = (r32)level / (r32)(ENVIRONMENT_LEVELS - 1);
            draw_face(this->environment_map, level, face, { face, (r32)(ENVIRONMENT_SIZE >> level), roughness, 0 });
        }
    }
    for (u32 face = 0; face < 6; ++face)
        draw_face(this->irradiance_map, 0, face, { face, (r32)IRRADIANCE_SIZE, 1.0f, 1 });

    barrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            color_output, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

bool Native_Renderer::create_frame_descriptors()
{
    // Frame_Uniforms, then the shadow map, environment_map and irradiance_map.
    VkDescriptorSetLayoutBinding bindings[4];
    for (u32 i = 0; i < 4; ++i)
    {
        bindings[i] = {
            .binding         = i,
            .descriptorType  = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = (VkShaderStageFlags)(i == 0 ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                                          : VK_SHADER_STAGE_FRAGMENT_BIT),
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings    = bindings,
    };
    if (!vk_check(vkCreateDescriptorSetLayout(this->device, &layout_info, nullptr, &this->frame_set_layout),
                  "vkCreateDescriptorSetLayout")) return false;

    // A set per frame, the tonemapping's and the bloom's.
    VkDescriptorPoolSize pool_sizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         .descriptorCount = FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 3 * FRAMES_IN_FLIGHT + 2 + MAX_BLOOM_LEVELS + 1 },
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = FRAMES_IN_FLIGHT + 1 + MAX_BLOOM_LEVELS + 1,
        .poolSizeCount = 2,
        .pPoolSizes    = pool_sizes,
    };
    if (!vk_check(vkCreateDescriptorPool(this->device, &pool_info, nullptr, &this->frame_pool),
                  "vkCreateDescriptorPool")) return false;

    for (auto &frame : this->frames)
    {
        VkDescriptorSetAllocateInfo set_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = this->frame_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &this->frame_set_layout,
        };
        if (!vk_check(vkAllocateDescriptorSets(this->device, &set_info, &frame.descriptor_set),
                      "vkAllocateDescriptorSets")) return false;

        VkDescriptorBufferInfo buffer_descriptor = {
            .buffer = frame.uniform_buffer,
            .offset = 0,
            .range  = sizeof(Frame_Uniforms),
        };
        VkDescriptorImageInfo shadow_descriptor = {
            .sampler     = this->shadow_sampler,
            .imageView   = this->shadow_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo environment_descriptor = {
            .sampler     = this->sampler,
            .imageView   = this->environment_map.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo irradiance_descriptor = {
            .sampler     = this->sampler,
            .imageView   = this->irradiance_map.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet writes[4] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = frame.descriptor_set,
                .dstBinding      = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &buffer_descriptor,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = frame.descriptor_set,
                .dstBinding      = 1,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &shadow_descriptor,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = frame.descriptor_set,
                .dstBinding      = 2,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &environment_descriptor,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = frame.descriptor_set,
                .dstBinding      = 3,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &irradiance_descriptor,
            },
        };
        vkUpdateDescriptorSets(this->device, 4, writes, 0, nullptr);
    }

    // Of hdr_image and bloom_image: written by create_swapchain(), which makes them.
    VkDescriptorSetAllocateInfo tonemap_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = this->frame_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &this->tonemap_set_layout,
    };
    if (!vk_check(vkAllocateDescriptorSets(this->device, &tonemap_set_info, &this->tonemap_set),
                  "vkAllocateDescriptorSets")) return false;

    VkDescriptorSetLayout bloom_layouts[MAX_BLOOM_LEVELS + 1];
    for (auto &layout : bloom_layouts) layout = this->image_set_layout;
    VkDescriptorSetAllocateInfo bloom_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = this->frame_pool,
        .descriptorSetCount = MAX_BLOOM_LEVELS + 1,
        .pSetLayouts        = bloom_layouts,
    };
    return vk_check(vkAllocateDescriptorSets(this->device, &bloom_set_info, this->bloom_sets), "vkAllocateDescriptorSets");
}

void Native_Renderer::create_texture(const u8 *pixels, u32 width, u32 height, VkFormat format, Native_Texture *texture)
{
    *texture = {};
    texture->pixels = pixels;
    texture->width  = width;
    texture->height = height;
    texture->format = format;

    // Each mip level is blitted from the previous one, which needs linear filtering of
    // the format: without it the texture has just its full-size level.
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, format, &format_properties);
    const VkFormatFeatureFlags blit_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    u32 mip_levels = 1;
    if ((format_properties.optimalTilingFeatures & blit_features) == blit_features)
    {
        for (u32 size = width > height ? width : height; size > 1; size /= 2) ++mip_levels;
    }
    texture->mip_levels = mip_levels;

    // On failure the texture keeps just what it's recognized by, and is drawn as the default.
    auto fail = [&]()
    {
        this->destroy_texture(texture);
        texture->pixels = pixels;
        texture->width  = width;
        texture->height = height;
        texture->format = format;
    };

    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { width, height, 1 },
        .mipLevels     = mip_levels,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, &texture->image), "vkCreateImage")) return fail();

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, texture->image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, &texture->memory), "vkAllocateMemory")) return fail();
    if (!vk_check(vkBindImageMemory(this->device, texture->image, texture->memory, 0), "vkBindImageMemory")) return fail();

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = texture->image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1 },
    };
    if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &texture->view), "vkCreateImageView")) return fail();
}

void Native_Renderer::upload_texture(Native_Texture *texture, Native_Renderer_Frame &frame)
{
    // As many of the remaining rows as the staging buffer has room for.
    u64 row_size = (u64)texture->width * 4;
    u32 rows     = texture->height - texture->rows_uploaded;
    u64 room     = (STAGING_BUFFER_SIZE - frame.staging_used) / row_size;
    if (rows > room) rows = (u32)room;
    if (rows == 0) return;

    memcpy(frame.staging_mapped + frame.staging_used, texture->pixels + texture->rows_uploaded * row_size, rows * row_size);

    this->begin_uploads(frame);
    VkCommandBuffer command_buffer = frame.upload_command_buffer;

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

    const VkPipelineStageFlags2 transfer   = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    const u32                   mip_levels = texture->mip_levels;

    // The image stays laid out for copies until its last rows are in: the copies of
    // later frames write other rows, so they need no barrier between them. Copied
    // again, it's written once earlier frames are done reading it, and copying it.
    if (texture->rows_uploaded == 0)
    {
        bool replaced = texture->replaced;
        barrier(0, mip_levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                replaced ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | transfer : VK_PIPELINE_STAGE_2_NONE,
                replaced ? VK_ACCESS_2_TRANSFER_WRITE_BIT : 0, transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        texture->replaced = false;
    }

    VkBufferImageCopy copy = {
        .bufferOffset     = frame.staging_used,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset      = { 0, (i32)texture->rows_uploaded, 0 },
        .imageExtent      = { texture->width, rows, 1 },
    };
    vkCmdCopyBufferToImage(command_buffer, frame.staging_buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy);

    // Meshes are staged at 16-byte aligned offsets.
    frame.staging_used     += (rows * row_size + 15) & ~(u64)15;
    texture->rows_uploaded += rows;
    if (texture->rows_uploaded < texture->height) return;

    // Each level is half the previous one (at least 1 pixel), filtered down from it.
    i32 level_width  = (i32)texture->width;
    i32 level_height = (i32)texture->height;
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

    // All levels but the last were blit sources; the last was only written. The frame's
    // draws are submitted after this, so the barriers cover them.
    const VkPipelineStageFlags2 fragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (mip_levels > 1)
    {
        barrier(0, mip_levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                transfer, VK_ACCESS_2_TRANSFER_READ_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    barrier(mip_levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void Native_Renderer::destroy_texture(Native_Texture *texture)
{
    if (texture->view)   vkDestroyImageView(this->device, texture->view, nullptr);
    if (texture->image)  vkDestroyImage(this->device, texture->image, nullptr);
    if (texture->memory) vkFreeMemory(this->device, texture->memory, nullptr);
    *texture = {};
}

Native_Texture *Native_Renderer::find_texture(const Texture &texture, VkFormat format, Native_Texture *fallback,
                                              Native_Renderer_Frame &frame)
{
    if (!texture.pixels || texture.width == 0 || texture.height == 0) return fallback;

    u32 slot = hash_address(texture.pixels, TEXTURE_SLOT_BITS);

    Native_Texture *found = nullptr;
    for (;; slot = (slot + 1) & (TEXTURE_SLOTS - 1))
    {
        Native_Texture &candidate = this->textures[slot];
        if (!candidate.pixels) break;
        if (candidate.pixels == texture.pixels && candidate.width == texture.width && candidate.height == texture.height &&
            candidate.format == format)
        {
            found = &candidate;
            break;
        }
    }

    // Not created yet: it goes in the empty slot that ended the probe.
    if (!found)
    {
        if (this->texture_count == MAX_TEXTURES)
        {
            if (!this->textures_full_logged)
            {
                log_warn("Textures past the first %u aren't drawn: their defaults are", MAX_TEXTURES);
                this->textures_full_logged = true;
            }
            return fallback;
        }

        // Kept even when it fails, so it isn't tried again every frame.
        found = &this->textures[slot];
        this->texture_count += 1;
        this->create_texture(texture.pixels, texture.width, texture.height, format, found);
        if (!found->view)
            log_error("Could not create a %ux%u texture: its default is drawn instead", texture.width, texture.height);
    }

    if (!found->view) return fallback;

    if (found->rows_uploaded < found->height) this->upload_texture(found, frame);
    return found->rows_uploaded == found->height ? found : nullptr;
}

VkDescriptorSet Native_Renderer::find_material(const Material &material, Native_Renderer_Frame &frame)
{
    // Every texture gets its turn at the frame's uploads, even when another one isn't
    // ready yet.
    const Native_Texture *base_color = this->find_texture(material.base_color_texture, COLOR_TEXTURE_FORMAT,
                                                          &this->white_texture, frame);
    const Native_Texture *normal     = this->find_texture(material.normal_texture, DATA_TEXTURE_FORMAT,
                                                          &this->flat_texture, frame);
    const Native_Texture *emissive   = this->find_texture(material.emissive_texture, COLOR_TEXTURE_FORMAT,
                                                          &this->white_texture, frame);
    if (!base_color || !normal || !emissive) return VK_NULL_HANDLE;

    uintptr_t key = (uintptr_t)base_color ^ ((uintptr_t)normal << 1) ^ ((uintptr_t)emissive << 2);
    u32 slot = hash_address((const void *)key, MATERIAL_SLOT_BITS);
    for (;; slot = (slot + 1) & (MATERIAL_SLOTS - 1))
    {
        const Native_Material &candidate = this->materials[slot];
        if (!candidate.base_color) break;
        if (candidate.base_color == base_color && candidate.normal == normal && candidate.emissive == emissive)
            return candidate.descriptor_set;
    }

    if (this->material_count == MAX_MATERIALS)
    {
        if (!this->materials_full_logged)
        {
            log_warn("Meshes with materials past the first %u sets of textures aren't drawn", MAX_MATERIALS);
            this->materials_full_logged = true;
        }
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = this->material_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &this->material_set_layout,
    };
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    if (!vk_check(vkAllocateDescriptorSets(this->device, &set_info, &descriptor_set), "vkAllocateDescriptorSets"))
        return VK_NULL_HANDLE;

    const Native_Texture *textures[3] = { base_color, normal, emissive };
    VkDescriptorImageInfo images[3];
    VkWriteDescriptorSet  writes[3];
    for (u32 i = 0; i < 3; ++i)
    {
        images[i] = {
            .sampler     = this->sampler,
            .imageView   = textures[i]->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        writes[i] = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = descriptor_set,
            .dstBinding      = i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &images[i],
        };
    }
    vkUpdateDescriptorSets(this->device, 3, writes, 0, nullptr);

    this->materials[slot] = { base_color, normal, emissive, descriptor_set };
    this->material_count += 1;
    return descriptor_set;
}

const Native_Mesh *Native_Renderer::find_mesh(const Mesh &mesh, Native_Renderer_Frame &frame)
{
    if (!mesh.vertices || !mesh.indices) return nullptr;

    u32 slot = hash_address(mesh.vertices, MESH_SLOT_BITS);

    for (;; slot = (slot + 1) & (MESH_SLOTS - 1))
    {
        const Native_Mesh &candidate = this->meshes[slot];
        if (!candidate.vertices) break;
        if (candidate.vertices == mesh.vertices && candidate.indices == mesh.indices &&
            candidate.vertex_count == mesh.vertex_count && candidate.index_count == mesh.index_count)
            return &candidate;
    }

    // Not uploaded yet: vertices, then indices, both at 16-byte aligned offsets, laid
    // out the same in the staging buffer and in mesh_buffer so one copy moves both.
    auto align = [](u64 value) { return (value + 15) & ~(u64)15; };
    u64 vertices_size = (u64)mesh.vertex_count * sizeof(Mesh_Vertex);
    u64 indices_size  = (u64)mesh.index_count * sizeof(u32);
    u64 indices_start = align(vertices_size);
    u64 size          = align(indices_start + indices_size);

    if (size > STAGING_BUFFER_SIZE)
    {
        if (!this->mesh_too_big_logged)
        {
            log_warn("Meshes bigger than %llu MiB aren't drawn", (unsigned long long)(STAGING_BUFFER_SIZE / MiB));
            this->mesh_too_big_logged = true;
        }
        return nullptr;
    }
    u64 offset = 0;
    if (size > STAGING_BUFFER_SIZE - frame.staging_used) return nullptr; // Staging is full: the mesh waits for the next frame
    if (this->mesh_count == MAX_MESHES || !this->allocate_mesh_range(size, &offset))
    {
        if (!this->meshes_full_logged)
        {
            log_warn("Meshes past the first %u or %llu MiB aren't drawn", MAX_MESHES,
                     (unsigned long long)(MESH_BUFFER_SIZE / MiB));
            this->meshes_full_logged = true;
        }
        return nullptr;
    }
    u8 *staged = frame.staging_mapped + frame.staging_used;
    memcpy(staged,                 mesh.vertices, vertices_size);
    memcpy(staged + indices_start, mesh.indices,  indices_size);

    this->begin_uploads(frame);

    VkBufferCopy copy = {
        .srcOffset = frame.staging_used,
        .dstOffset = offset,
        .size      = size,
    };
    vkCmdCopyBuffer(frame.upload_command_buffer, frame.staging_buffer, this->mesh_buffer, 1, &copy);

    Native_Mesh *created = &this->meshes[slot];
    created->vertices      = mesh.vertices;
    created->indices       = mesh.indices;
    created->vertex_count  = mesh.vertex_count;
    created->index_count   = mesh.index_count;
    created->vertex_offset = offset;
    created->index_offset  = offset + indices_start;
    created->size          = size;

    frame.staging_used += size;
    this->mesh_count   += 1;
    return created;
}

bool Native_Renderer::allocate_mesh_range(u64 size, u64 *offset)
{
    for (u64 i = 0; i < this->mesh_free.size(); ++i)
    {
        Mesh_Range &range = this->mesh_free[i];
        if (range.size < size) continue;

        *offset       = range.offset;
        range.offset += size;
        range.size   -= size;
        if (!range.size) this->mesh_free.erase(this->mesh_free.begin() + (i64)i);
        return true;
    }
    return false;
}

void Native_Renderer::free_mesh_range(Mesh_Range range)
{
    // Joined to the free ranges right before and after it.
    auto after = std::lower_bound(this->mesh_free.begin(), this->mesh_free.end(), range.offset,
                                  [](const Mesh_Range &free, u64 offset) { return free.offset < offset; });
    if (after != this->mesh_free.end() && range.offset + range.size == after->offset)
    {
        range.size += after->size;
        after       = this->mesh_free.erase(after);
    }
    if (after != this->mesh_free.begin())
    {
        Mesh_Range &before = *(after - 1);
        if (before.offset + before.size == range.offset)
        {
            before.size += range.size;
            return;
        }
    }
    this->mesh_free.insert(after, range);
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
        .color_format  = HDR_FORMAT,
        .depth_format  = this->depth_format,
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
    };
    if (!this->create_graphics_pipeline(triangle, &this->pipeline)) return false;

    VkPushConstantRange tonemap_push_constants = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Tonemap_Push_Constants),
    };
    VkPipelineLayoutCreateInfo tonemap_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->tonemap_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &tonemap_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &tonemap_layout_info, nullptr, &this->tonemap_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    Pipeline_Description tonemap = {
        .vertex_code   = tonemap_vert_spv,
        .vertex_size   = sizeof(tonemap_vert_spv),
        .fragment_code = tonemap_frag_spv,
        .fragment_size = sizeof(tonemap_frag_spv),
        .vertex_input  = &no_vertex_input,
        .layout        = this->tonemap_pipeline_layout,
        .color_format  = this->surface_format.format,
        .depth_format  = this->depth_format, // The scene's, for wireframes after it
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
    };
    if (!this->create_graphics_pipeline(tonemap, &this->tonemap_pipeline)) return false;

    // The bloom's steps: a screen-covering triangle reading an image, drawing a level.
    VkPushConstantRange bloom_push_constants = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Bloom_Push_Constants),
    };
    VkPipelineLayoutCreateInfo bloom_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->image_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &bloom_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &bloom_layout_info, nullptr, &this->bloom_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    Pipeline_Description bloom_down = {
        .vertex_code   = tonemap_vert_spv,
        .vertex_size   = sizeof(tonemap_vert_spv),
        .fragment_code = bloom_down_frag_spv,
        .fragment_size = sizeof(bloom_down_frag_spv),
        .vertex_input  = &no_vertex_input,
        .layout        = this->bloom_pipeline_layout,
        .color_format  = HDR_FORMAT,
        .depth_format  = VK_FORMAT_UNDEFINED,
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
    };
    if (!this->create_graphics_pipeline(bloom_down, &this->bloom_down_pipeline)) return false;

    Pipeline_Description bloom_up = bloom_down;
    bloom_up.fragment_code = bloom_up_frag_spv;
    bloom_up.fragment_size = sizeof(bloom_up_frag_spv);
    bloom_up.blend         = Blend_Mode::Additive;
    if (!this->create_graphics_pipeline(bloom_up, &this->bloom_up_pipeline)) return false;

    // The sky's maps: a screen-covering triangle for each face, reading the frame's uniforms.
    VkPushConstantRange environment_push_constants = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Environment_Push_Constants),
    };
    VkPipelineLayoutCreateInfo environment_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->frame_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &environment_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &environment_layout_info, nullptr, &this->environment_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    Pipeline_Description environment = {
        .vertex_code   = tonemap_vert_spv,
        .vertex_size   = sizeof(tonemap_vert_spv),
        .fragment_code = environment_frag_spv,
        .fragment_size = sizeof(environment_frag_spv),
        .vertex_input  = &no_vertex_input,
        .layout        = this->environment_pipeline_layout,
        .color_format  = HDR_FORMAT,
        .depth_format  = VK_FORMAT_UNDEFINED,
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
    };
    if (!this->create_graphics_pipeline(environment, &this->environment_pipeline)) return false;

    // The sky behind the scene, drawn first, in the scene's attachments.
    VkPipelineLayoutCreateInfo sky_layout_info = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &this->frame_set_layout,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &sky_layout_info, nullptr, &this->sky_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    Pipeline_Description sky = {
        .vertex_code   = sky_vert_spv,
        .vertex_size   = sizeof(sky_vert_spv),
        .fragment_code = sky_frag_spv,
        .fragment_size = sizeof(sky_frag_spv),
        .vertex_input  = &no_vertex_input,
        .layout        = this->sky_pipeline_layout,
        .color_format  = HDR_FORMAT,
        .depth_format  = this->depth_format,
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
    };
    if (!this->create_graphics_pipeline(sky, &this->sky_pipeline)) return false;

    VkPushConstantRange push_constants = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Mesh_Push_Constants),
    };
    VkDescriptorSetLayout mesh_set_layouts[2] = { this->frame_set_layout, this->material_set_layout };
    VkPipelineLayoutCreateInfo mesh_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 2,
        .pSetLayouts            = mesh_set_layouts,
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
        .color_format  = HDR_FORMAT,
        .depth_format  = this->depth_format,
        .depth_test    = true,
        .cull_mode     = VK_CULL_MODE_BACK_BIT,
    };
    if (!this->create_graphics_pipeline(mesh, &this->mesh_pipeline)) return false;

    Pipeline_Description mesh_blend = mesh;
    mesh_blend.blend = Blend_Mode::Premultiplied;
    if (!this->create_graphics_pipeline(mesh_blend, &this->mesh_blend_pipeline)) return false;

    // UI over the tonemapped frame, in the swapchain.
    VkPushConstantRange ui_push_constants = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Ui_Push_Constants),
    };
    VkPipelineLayoutCreateInfo ui_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->image_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &ui_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &ui_layout_info, nullptr, &this->ui_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    VkVertexInputBindingDescription ui_binding = {
        .binding   = 0,
        .stride    = sizeof(Ui_Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription ui_attributes[3] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,  .offset = offsetof(Ui_Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,  .offset = offsetof(Ui_Vertex, uv) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(Ui_Vertex, color) },
    };
    VkPipelineVertexInputStateCreateInfo ui_vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &ui_binding,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions    = ui_attributes,
    };
    Pipeline_Description ui = {
        .vertex_code   = ui_vert_spv,
        .vertex_size   = sizeof(ui_vert_spv),
        .fragment_code = ui_frag_spv,
        .fragment_size = sizeof(ui_frag_spv),
        .vertex_input  = &ui_vertex_input,
        .layout        = this->ui_pipeline_layout,
        .color_format  = this->surface_format.format,
        .depth_format  = this->depth_format, // In the tonemapping's rendering
        .depth_test    = false,
        .cull_mode     = VK_CULL_MODE_NONE,
        .blend         = Blend_Mode::Premultiplied,
    };
    if (!this->create_graphics_pipeline(ui, &this->ui_pipeline)) return false;

    // Meshes' positions alone, from the same vertices.
    VkPushConstantRange shadow_push_constants = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(Shadow_Push_Constants),
    };
    VkPipelineLayoutCreateInfo shadow_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->frame_set_layout, // For the cascades' matrices
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &shadow_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &shadow_layout_info, nullptr, &this->shadow_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    VkPipelineVertexInputStateCreateInfo shadow_vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions    = &attributes[0],
    };
    // Both sides cast shadows, so open meshes (a plane for a leaf) do too.
    Pipeline_Description shadow = {
        .vertex_code   = shadow_vert_spv,
        .vertex_size   = sizeof(shadow_vert_spv),
        .vertex_input  = &shadow_vertex_input,
        .layout        = this->shadow_pipeline_layout,
        .color_format  = VK_FORMAT_UNDEFINED,
        .depth_format  = this->shadow_format,
        .depth_test    = true,
        .cull_mode     = VK_CULL_MODE_NONE,
        .shadow_caster = true,
        .view_mask     = (1u << SHADOW_CASCADES) - 1,
    };
    if (!this->create_graphics_pipeline(shadow, &this->shadow_pipeline)) return false;

    // Wireframes over the tonemapped frame, in the swapchain, where the scene's depth is
    // theirs: meshes' positions alone, through the frame's camera.
    if (!this->wire_barycentrics) return true;
    VkPushConstantRange wire_push_constants = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(Wire_Push_Constants),
    };
    VkPipelineLayoutCreateInfo wire_layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &this->frame_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &wire_push_constants,
    };
    if (!vk_check(vkCreatePipelineLayout(this->device, &wire_layout_info, nullptr, &this->wire_pipeline_layout),
                  "vkCreatePipelineLayout")) return false;

    Pipeline_Description wire = {
        .vertex_code   = wire_vert_spv,
        .vertex_size   = sizeof(wire_vert_spv),
        .fragment_code = wire_frag_spv,
        .fragment_size = sizeof(wire_frag_spv),
        .vertex_input  = &shadow_vertex_input,
        .layout        = this->wire_pipeline_layout,
        .color_format  = this->surface_format.format,
        .depth_format  = this->depth_format,
        .depth_test    = true,
        .cull_mode     = VK_CULL_MODE_BACK_BIT,
        .blend         = Blend_Mode::Premultiplied,
        .depth_equal   = true,
    };
    return this->create_graphics_pipeline(wire, &this->wire_pipeline);
}

bool Native_Renderer::create_graphics_pipeline(const Pipeline_Description &description, VkPipeline *pipeline)
{
    bool has_fragment = description.fragment_code != nullptr;
    bool has_color    = description.color_format != VK_FORMAT_UNDEFINED;

    VkShaderModule vertex   = this->create_shader_module(description.vertex_code, description.vertex_size);
    VkShaderModule fragment = has_fragment ? this->create_shader_module(description.fragment_code, description.fragment_size)
                                           : VK_NULL_HANDLE;

    bool ok = false;
    if (vertex && (fragment || !has_fragment))
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
        // Shadow casters' depth is pushed back more where it changes faster across
        // a texel, so that sloped surfaces don't shadow themselves.
        VkPipelineRasterizationStateCreateInfo rasterization = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable        = description.shadow_caster && this->depth_clamp,
            .polygonMode             = VK_POLYGON_MODE_FILL,
            .cullMode                = description.cull_mode,
            .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable         = description.shadow_caster,
            .depthBiasSlopeFactor    = description.shadow_caster ? 1.5f : 0.0f,
            .lineWidth               = 1.0f,
        };
        VkPipelineMultisampleStateCreateInfo multisample = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = description.depth_test,
            .depthWriteEnable = description.depth_test && description.blend == Blend_Mode::None && !description.depth_equal,
            .depthCompareOp   = description.depth_equal ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS,
        };
        VkBlendFactor destination = description.blend == Blend_Mode::Additive ? VK_BLEND_FACTOR_ONE
                                                                              : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkPipelineColorBlendAttachmentState blend_attachment = {
            .blendEnable         = description.blend != Blend_Mode::None,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = destination,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = destination,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        VkPipelineColorBlendStateCreateInfo blend = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = has_color ? 1u : 0u,
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
            .colorAttachmentCount    = has_color ? 1u : 0u,
            .pColorAttachmentFormats = &description.color_format,
            .depthAttachmentFormat   = description.depth_format,
        };
        rendering.viewMask = description.view_mask;

        VkGraphicsPipelineCreateInfo info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &rendering,
            .stageCount          = has_fragment ? 2u : 1u,
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

    // Copying from the images, for screenshots, is nearly always supported too.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    this->swapchain_readable = capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (this->swapchain_readable) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainKHR old_swapchain = this->swapchain;

    VkSwapchainCreateInfoKHR info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = this->surface,
        .minImageCount    = min_image_count,
        .imageFormat      = this->surface_format.format,
        .imageColorSpace  = this->surface_format.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = usage,
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

    if (!this->create_attachment(extent, this->depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, &this->depth_image, &this->depth_memory, &this->depth_view))
        return false;
    if (!this->create_attachment(extent, HDR_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, &this->hdr_image, &this->hdr_memory, &this->hdr_view))
        return false;

    if (!this->create_bloom_image(extent)) return false;

    // The tonemapping reads the scene's texels where it draws, the bloom between them;
    // the bloom's first step reads the scene, each other one a level.
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkWriteDescriptorSet>  writes;
    images.reserve(3 + MAX_BLOOM_LEVELS);
    auto write = [&](VkDescriptorSet set, u32 binding, VkImageView view)
    {
        images.push_back({ this->bloom_sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        writes.push_back({
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set,
            .dstBinding      = binding,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &images.back(),
        });
    };
    write(this->tonemap_set, 0, this->hdr_view);
    write(this->tonemap_set, 1, this->bloom_views[0]);
    write(this->bloom_sets[0], 0, this->hdr_view);
    for (u32 level = 0; level < this->bloom_levels; ++level) write(this->bloom_sets[1 + level], 0, this->bloom_views[level]);
    vkUpdateDescriptorSets(this->device, (u32)writes.size(), writes.data(), 0, nullptr);

    this->swapchain_extent = extent;
    this->swapchain_dirty  = false;
    return true;
}

bool Native_Renderer::create_bloom_image(VkExtent2D extent)
{
    // From half the frame down, while levels are at least 8 pixels.
    VkExtent2D size = { extent.width / 2 > 0 ? extent.width / 2 : 1, extent.height / 2 > 0 ? extent.height / 2 : 1 };
    this->bloom_levels = 0;
    for (VkExtent2D level = size; this->bloom_levels < MAX_BLOOM_LEVELS && (this->bloom_levels == 0 ||
         (level.width >= 8 && level.height >= 8)); level = { level.width / 2, level.height / 2 })
    {
        this->bloom_extents[this->bloom_levels++] = level;
    }

    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = HDR_FORMAT,
        .extent        = { size.width, size.height, 1 },
        .mipLevels     = this->bloom_levels,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, &this->bloom_image), "vkCreateImage")) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, this->bloom_image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, &this->bloom_memory), "vkAllocateMemory")) return false;
    if (!vk_check(vkBindImageMemory(this->device, this->bloom_image, this->bloom_memory, 0), "vkBindImageMemory")) return false;

    for (u32 level = 0; level < this->bloom_levels; ++level)
    {
        VkImageViewCreateInfo view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = this->bloom_image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = HDR_FORMAT,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 },
        };
        if (!vk_check(vkCreateImageView(this->device, &view_info, nullptr, &this->bloom_views[level]), "vkCreateImageView"))
            return false;
    }
    return true;
}

bool Native_Renderer::create_attachment(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                                        VkImageAspectFlags aspect, VkImage *image, VkDeviceMemory *memory,
                                        VkImageView *view, u32 layers)
{
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { extent.width, extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = layers,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_check(vkCreateImage(this->device, &image_info, nullptr, image), "vkCreateImage")) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->device, *image, &requirements);

    u32 memory_type = this->find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) memory_type = this->find_memory_type(requirements.memoryTypeBits, 0);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!vk_check(vkAllocateMemory(this->device, &allocate, nullptr, memory), "vkAllocateMemory")) return false;
    if (!vk_check(vkBindImageMemory(this->device, *image, *memory, 0), "vkBindImageMemory")) return false;

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = *image,
        .viewType         = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format           = format,
        .subresourceRange = { aspect, 0, 1, 0, layers },
    };
    return vk_check(vkCreateImageView(this->device, &view_info, nullptr, view), "vkCreateImageView");
}

void Native_Renderer::destroy_attachment(VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
    if (*view)   vkDestroyImageView(this->device, *view, nullptr);
    if (*image)  vkDestroyImage(this->device, *image, nullptr);
    if (*memory) vkFreeMemory(this->device, *memory, nullptr);
    *view   = VK_NULL_HANDLE;
    *image  = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
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

    this->destroy_attachment(&this->depth_image, &this->depth_memory, &this->depth_view);
    this->destroy_attachment(&this->hdr_image, &this->hdr_memory, &this->hdr_view);

    for (VkImageView &view : this->bloom_views)
    {
        if (view) vkDestroyImageView(this->device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (this->bloom_image)  vkDestroyImage(this->device, this->bloom_image, nullptr);
    if (this->bloom_memory) vkFreeMemory(this->device, this->bloom_memory, nullptr);
    this->bloom_image  = VK_NULL_HANDLE;
    this->bloom_memory = VK_NULL_HANDLE;
    this->bloom_levels = 0;
}

#endif // RENDERER_VULKAN
