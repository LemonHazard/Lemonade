#include "gltf.h"
#include "animation.h"
#include "file.h"
#include "json.h"
#include "matrix.h"
#include "png.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The .glb container: a 12-byte header, then chunks of (length, type, data), the
// first one JSON and the optional second one the binary buffer.
constexpr u32 GLB_MAGIC      = 0x46546C67; // "glTF"
constexpr u32 GLB_CHUNK_JSON = 0x4E4F534A; // "JSON"
constexpr u32 GLB_CHUNK_BIN  = 0x004E4942; // "BIN\0"

// Node hierarchies are trees: deeper means a cycle in a broken file.
constexpr u32 GLTF_MAX_NODE_DEPTH = 256;

// Accessor component types
constexpr u32 GLTF_UNSIGNED_BYTE  = 5121;
constexpr u32 GLTF_UNSIGNED_SHORT = 5123;
constexpr u32 GLTF_UNSIGNED_INT   = 5125;
constexpr u32 GLTF_FLOAT          = 5126;

constexpr u32 GLTF_MODE_TRIANGLES = 4;

// A primitive without a material.
constexpr u64 GLTF_NO_MATERIAL = UINT64_MAX;

// The indices of one material's primitives, which become a part of the model.
struct Gltf_Part
{
    u64              material; // Index in the file, or GLTF_NO_MATERIAL
    std::vector<u32> indices;
};

// No skin, for a mesh's node without one.
constexpr u64 GLTF_NO_SKIN = UINT64_MAX;

struct Gltf_Loader
{
    const Json *json;
    const u8   *bin;
    u64         bin_size;

    std::vector<Mesh_Vertex>  vertices;
    std::vector<Skin_Weights> weights; // One per vertex: its joints are into the glTF skin's
    std::vector<Gltf_Part>    parts;   // In the order their materials first appear
    u64                       skin = GLTF_NO_SKIN; // The model's: the first a mesh has
    bool                      other_skins_logged = false;
};

// The part for a material's primitives, made when it first appears.
static Gltf_Part &gltf_part(Gltf_Loader *loader, u64 material)
{
    for (Gltf_Part &part : loader->parts)
    {
        if (part.material == material) return part;
    }
    Gltf_Part &part = loader->parts.emplace_back();
    part.material = material;
    return part;
}

// An accessor's data in the binary chunk: `count` elements of `components` values
// of `component_type`, `stride` bytes apart.
struct Gltf_Accessor
{
    const u8 *data;
    u64       count;
    u64       stride;
    u32       component_type;
    u32       components;
};

static u32 gltf_read_u32(const u8 *at)
{
    u32 value;
    memcpy(&value, at, sizeof(value));
    return value;
}

// A non-negative integer, as glTF indices, counts and offsets are.
static bool gltf_integer(const Json &value, u64 *out)
{
    if (!value.is_number() || value.number < 0.0 || value.number > 9007199254740992.0) return false;

    *out = (u64)value.number;
    return (r64)*out == value.number;
}

static u64 gltf_integer_or(const Json &value, u64 fallback)
{
    u64 result;
    return gltf_integer(value, &result) ? result : fallback;
}

static u32 gltf_component_size(u32 component_type)
{
    switch (component_type)
    {
        case 5120: case 5121: return 1; // (unsigned) byte
        case 5122: case 5123: return 2; // (unsigned) short
        case 5125: case 5126: return 4; // unsigned int, float
        default:              return 0;
    }
}

static u32 gltf_component_count(const str &type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2")   return 2;
    if (type == "VEC3")   return 3;
    if (type == "VEC4")   return 4;
    if (type == "MAT2")   return 4;
    if (type == "MAT3")   return 9;
    if (type == "MAT4")   return 16;
    return 0;
}

static bool gltf_accessor(const Gltf_Loader *loader, const Json &index, Gltf_Accessor *out)
{
    const Json &json = *loader->json;

    u64 accessor_index;
    if (!gltf_integer(index, &accessor_index) || !json["accessors"][accessor_index].is_object())
    {
        log_error("glTF: bad accessor index");
        return false;
    }
    const Json &accessor = json["accessors"][accessor_index];

    if (!accessor["sparse"].is_null())
    {
        log_error("glTF: sparse accessors aren't supported");
        return false;
    }

    u64 view_index;
    if (!gltf_integer(accessor["bufferView"], &view_index) || !json["bufferViews"][view_index].is_object())
    {
        log_error("glTF: accessor %llu has no buffer view", (unsigned long long)accessor_index);
        return false;
    }
    const Json &view = json["bufferViews"][view_index];

    if (gltf_integer_or(view["buffer"], 1) != 0 || !json["buffers"][(u64)0]["uri"].is_null())
    {
        log_error("glTF: only data in the .glb's binary chunk is supported");
        return false;
    }

    u32 component_type = (u32)gltf_integer_or(accessor["componentType"], 0);
    u32 component_size = gltf_component_size(component_type);
    u32 components     = gltf_component_count(accessor["type"].string);
    u64 count          = gltf_integer_or(accessor["count"], 0);
    if (!component_size || !components)
    {
        log_error("glTF: accessor %llu has an unknown type", (unsigned long long)accessor_index);
        return false;
    }

    u64 element_size = (u64)component_size * components;
    u64 stride       = gltf_integer_or(view["byteStride"], element_size);
    u64 view_offset  = gltf_integer_or(view["byteOffset"], 0);
    u64 view_length  = gltf_integer_or(view["byteLength"], 0);
    u64 offset       = gltf_integer_or(accessor["byteOffset"], 0);

    bool fits = view_offset <= loader->bin_size && view_length <= loader->bin_size - view_offset;
    if (fits && count > 0)
    {
        fits = stride >= element_size
            && offset <= view_length
            && element_size <= view_length - offset
            && (count - 1) <= (view_length - offset - element_size) / stride;
    }
    if (!fits)
    {
        log_error("glTF: accessor %llu is out of its buffer's bounds", (unsigned long long)accessor_index);
        return false;
    }

    *out = {
        .data           = loader->bin + view_offset + offset,
        .count          = count,
        .stride         = stride,
        .component_type = component_type,
        .components     = components,
    };
    return true;
}

static Vector3 gltf_read_vector3(const Gltf_Accessor &accessor, u64 i)
{
    Vector3 v;
    memcpy(&v.x, accessor.data + i * accessor.stride, 3 * sizeof(float));
    return v;
}

// Texture coordinates are floats, or unsigned bytes or shorts normalized to 0..1.
static Vector2 gltf_read_uv(const Gltf_Accessor &accessor, u64 i)
{
    const u8 *at = accessor.data + i * accessor.stride;
    switch (accessor.component_type)
    {
        case GLTF_UNSIGNED_BYTE:
            return { at[0] / 255.0f, at[1] / 255.0f };
        case GLTF_UNSIGNED_SHORT:
        {
            u16 value[2];
            memcpy(value, at, sizeof(value));
            return { value[0] / 65535.0f, value[1] / 65535.0f };
        }
        default:
        {
            Vector2 uv;
            memcpy(&uv.x, at, 2 * sizeof(float));
            return uv;
        }
    }
}

static u32 gltf_read_index(const Gltf_Accessor &accessor, u64 i)
{
    const u8 *at = accessor.data + i * accessor.stride;
    switch (accessor.component_type)
    {
        case GLTF_UNSIGNED_BYTE:  return *at;
        case GLTF_UNSIGNED_SHORT: { u16 value; memcpy(&value, at, sizeof(value)); return value; }
        default:                  return gltf_read_u32(at);
    }
}

static bool gltf_is_float_vector3(const Gltf_Accessor &accessor)
{
    return accessor.component_type == GLTF_FLOAT && accessor.components == 3;
}

// Joints are unsigned bytes or shorts; weights floats, or unsigned bytes or shorts
// normalized to 0..1.
static Skin_Weights gltf_read_weights(const Gltf_Accessor &joints, const Gltf_Accessor &weights, u64 i)
{
    Skin_Weights result = {};
    const u8 *joint_at  = joints.data + i * joints.stride;
    const u8 *weight_at = weights.data + i * weights.stride;
    for (u32 k = 0; k < 4; ++k)
    {
        if (joints.component_type == GLTF_UNSIGNED_BYTE)
            result.joints[k] = joint_at[k];
        else
            memcpy(&result.joints[k], joint_at + 2 * k, sizeof(u16));

        switch (weights.component_type)
        {
            case GLTF_UNSIGNED_BYTE:  result.weights[k] = weight_at[k] / 255.0f; break;
            case GLTF_UNSIGNED_SHORT: { u16 value; memcpy(&value, weight_at + 2 * k, sizeof(value)); result.weights[k] = value / 65535.0f; } break;
            default:                  memcpy(&result.weights[k], weight_at + 4 * k, sizeof(float)); break;
        }
    }

    // Adding up to 1, as they're stored to the precision of their type.
    r32 total = result.weights[0] + result.weights[1] + result.weights[2] + result.weights[3];
    for (u32 k = 0; k < 4; ++k) result.weights[k] = total > 0.0f ? result.weights[k] / total : 0.0f;
    return result;
}

// With `skinned`, the vertices keep their skin's joints and weights.
static bool gltf_add_primitive(Gltf_Loader *loader, const Json &primitive, const Matrix4 &transform, bool skinned)
{
    if (gltf_integer_or(primitive["mode"], GLTF_MODE_TRIANGLES) != GLTF_MODE_TRIANGLES)
    {
        log_warn("glTF: skipping a primitive that isn't triangles");
        return true;
    }

    const Json &attributes = primitive["attributes"];

    Gltf_Accessor positions;
    if (!gltf_accessor(loader, attributes["POSITION"], &positions)) return false;
    if (!gltf_is_float_vector3(positions))
    {
        log_error("glTF: positions must be float 3D vectors");
        return false;
    }

    Gltf_Accessor normals = {};
    bool has_normals = !attributes["NORMAL"].is_null();
    if (has_normals)
    {
        if (!gltf_accessor(loader, attributes["NORMAL"], &normals)) return false;
        if (!gltf_is_float_vector3(normals) || normals.count != positions.count)
        {
            log_error("glTF: normals must be float 3D vectors, one per position");
            return false;
        }
    }

    Gltf_Accessor uvs = {};
    bool has_uvs = !attributes["TEXCOORD_0"].is_null();
    if (has_uvs)
    {
        if (!gltf_accessor(loader, attributes["TEXCOORD_0"], &uvs)) return false;
        bool valid_type = uvs.component_type == GLTF_FLOAT ||
                          uvs.component_type == GLTF_UNSIGNED_BYTE ||
                          uvs.component_type == GLTF_UNSIGNED_SHORT;
        if (!valid_type || uvs.components != 2 || uvs.count != positions.count)
        {
            log_error("glTF: texture coordinates must be 2D vectors of floats or normalized unsigned integers, one per position");
            return false;
        }
    }

    Gltf_Accessor joints = {}, weights = {};
    if (skinned)
    {
        if (!gltf_accessor(loader, attributes["JOINTS_0"], &joints)) return false;
        if (!gltf_accessor(loader, attributes["WEIGHTS_0"], &weights)) return false;
        bool joint_type  = joints.component_type == GLTF_UNSIGNED_BYTE || joints.component_type == GLTF_UNSIGNED_SHORT;
        bool weight_type = weights.component_type == GLTF_FLOAT || weights.component_type == GLTF_UNSIGNED_BYTE ||
                           weights.component_type == GLTF_UNSIGNED_SHORT;
        if (!joint_type || !weight_type || joints.components != 4 || weights.components != 4 ||
            joints.count != positions.count || weights.count != positions.count)
        {
            log_error("glTF: a skinned primitive needs 4 joints and weights for each position");
            return false;
        }
    }

    u64 base = loader->vertices.size();
    if (base + positions.count > UINT32_MAX)
    {
        log_error("glTF: too many vertices");
        return false;
    }

    Matrix4 normals_transform = normal_matrix(transform);

    for (u64 i = 0; i < positions.count; ++i)
    {
        Mesh_Vertex &vertex = loader->vertices.emplace_back();
        vertex.position = transform_point(transform, gltf_read_vector3(positions, i));
        vertex.normal   = has_normals ? normalize(transform_vector(normals_transform, gltf_read_vector3(normals, i)))
                                      : Vector3 { 0.0f, 0.0f, 0.0f };
        vertex.uv       = has_uvs ? gltf_read_uv(uvs, i) : Vector2 { 0.0f, 0.0f };
        loader->weights.push_back(skinned ? gltf_read_weights(joints, weights, i) : Skin_Weights {});
    }

    std::vector<u32> &part_indices = gltf_part(loader, gltf_integer_or(primitive["material"], GLTF_NO_MATERIAL)).indices;
    u64 first_index = part_indices.size();

    if (!primitive["indices"].is_null())
    {
        Gltf_Accessor indices;
        if (!gltf_accessor(loader, primitive["indices"], &indices)) return false;

        bool unsigned_scalar = indices.components == 1 &&
                               (indices.component_type == GLTF_UNSIGNED_BYTE ||
                                indices.component_type == GLTF_UNSIGNED_SHORT ||
                                indices.component_type == GLTF_UNSIGNED_INT);
        if (!unsigned_scalar)
        {
            log_error("glTF: indices must be unsigned integers");
            return false;
        }

        // A trailing incomplete triangle is dropped.
        for (u64 i = 0; i < indices.count / 3 * 3; ++i)
        {
            u32 index = gltf_read_index(indices, i);
            if (index >= positions.count)
            {
                log_error("glTF: index %u is past the primitive's %llu vertices", index, (unsigned long long)positions.count);
                return false;
            }
            part_indices.push_back((u32)base + index);
        }
    }
    else
    {
        for (u64 i = 0; i < positions.count / 3 * 3; ++i)
            part_indices.push_back((u32)(base + i));
    }

    // A mirroring transform turns the triangles inside out: swap two corners back.
    if (determinant3x3(transform) < 0.0f)
    {
        for (u64 i = first_index; i + 2 < part_indices.size(); i += 3)
            std::swap(part_indices[i + 1], part_indices[i + 2]);
    }

    // Without normals, each vertex gets the average of its triangles' facing.
    if (!has_normals)
    {
        for (u64 i = first_index; i + 2 < part_indices.size(); i += 3)
        {
            Mesh_Vertex &a = loader->vertices[part_indices[i]];
            Mesh_Vertex &b = loader->vertices[part_indices[i + 1]];
            Mesh_Vertex &c = loader->vertices[part_indices[i + 2]];

            Vector3 face = cross(b.position - a.position, c.position - a.position);
            a.normal += face;
            b.normal += face;
            c.normal += face;
        }
        for (u64 i = base; i < loader->vertices.size(); ++i)
            loader->vertices[i].normal = normalize(loader->vertices[i].normal);
    }

    return true;
}

static Vector3 gltf_read_color3(const Json &color, Vector3 fallback)
{
    return color.size() == 3 ? color.vector3_or({}) : fallback;
}

// The image file a textureInfo's texture shows, "" for none.
static const str &gltf_texture_uri(const Json &json, const Json &texture_info)
{
    static const str none;
    if (texture_info.is_null()) return none;

    const Json &texture = json["textures"][gltf_integer_or(texture_info["index"], UINT64_MAX)];
    const Json &image   = json["images"][gltf_integer_or(texture["source"], UINT64_MAX)];
    if (image["uri"].string.empty())
    {
        log_warn("glTF: skipping a texture whose image isn't a file next to the model");
        return none;
    }
    return image["uri"].string;
}

// A texture of the material: the image at `uri`, through the library. Returns the
// storage it took.
static u64 gltf_load_texture(const str &uri, const char *directory, Texture_Library *textures, Texture *texture,
                             void *storage, u64 storage_size)
{
    *texture = {};
    if (uri.empty() || !textures) return 0;

    std::string path = directory[0] ? std::string(directory) + "/" + uri : std::string(uri);
    return library_texture(textures, path.c_str(), texture, storage, storage_size);
}

// The material at `index` in the file, its textures loaded into `storage`. Returns the
// storage they took.
static u64 gltf_material(const Json &json, u64 index, const char *directory, Texture_Library *textures,
                         Material *material, void *storage, u64 storage_size)
{
    *material = plain_material({ 1.0f, 1.0f, 1.0f, 1.0f });
    if (index == GLTF_NO_MATERIAL) return 0;

    const Json &source = json["materials"][index];
    const Json &pbr    = source["pbrMetallicRoughness"];
    const Json &color  = pbr["baseColorFactor"];
    if (color.size() == 4)
    {
        material->base_color = { color[0].float_or(1.0f), color[1].float_or(1.0f),
                                 color[2].float_or(1.0f), color[3].float_or(1.0f) };
    }
    material->metallic     = pbr["metallicFactor"].float_or(1.0f);
    material->roughness    = pbr["roughnessFactor"].float_or(1.0f);
    material->normal_scale = source["normalTexture"]["scale"].float_or(1.0f);
    r32 strength = source["extensions"]["KHR_materials_emissive_strength"]["emissiveStrength"].float_or(1.0f);
    material->emissive = gltf_read_color3(source["emissiveFactor"], {}) * strength;

    const str &alpha_mode  = source["alphaMode"].string;
    material->alpha_mode   = alpha_mode == "MASK" ? Alpha_Mode::Mask : alpha_mode == "BLEND" ? Alpha_Mode::Blend : Alpha_Mode::Opaque;
    material->alpha_cutoff = source["alphaCutoff"].float_or(0.5f);

    u8 *at   = (u8 *)storage;
    u64 used = 0;
    used += gltf_load_texture(gltf_texture_uri(json, pbr["baseColorTexture"]), directory, textures,
                              &material->base_color_texture, at + used, storage_size - used);
    used += gltf_load_texture(gltf_texture_uri(json, source["normalTexture"]), directory, textures,
                              &material->normal_texture, at + used, storage_size - used);
    used += gltf_load_texture(gltf_texture_uri(json, source["emissiveTexture"]), directory, textures,
                              &material->emissive_texture, at + used, storage_size - used);
    return used;
}

static bool gltf_add_mesh(Gltf_Loader *loader, u64 mesh_index, const Matrix4 &transform, bool skinned)
{
    const Json &mesh = (*loader->json)["meshes"][mesh_index];
    if (!mesh.is_object())
    {
        log_error("glTF: bad mesh index");
        return false;
    }

    for (const Json &primitive : mesh["primitives"].items)
    {
        if (!gltf_add_primitive(loader, primitive, transform, skinned)) return false;
    }
    return true;
}

static Matrix4 gltf_node_transform(const Json &node)
{
    const Json &matrix = node["matrix"];
    if (matrix.size() == 16)
    {
        Matrix4 m;
        for (u32 i = 0; i < 16; ++i)
            (&m.columns[0].x)[i] = matrix[i].float_or(0.0f);
        return m;
    }

    const Json &t = node["translation"];
    const Json &r = node["rotation"];
    const Json &s = node["scale"];

    Vector3 translate = t.vector3_or({ 0.0f, 0.0f, 0.0f });
    Vector4 rotate    = { r[0].float_or(0.0f), r[1].float_or(0.0f), r[2].float_or(0.0f), r[3].float_or(1.0f) };
    Vector3 scale     = s.vector3_or({ 1.0f, 1.0f, 1.0f });

    return translation(translate) * rotation(rotate) * scaling(scale);
}

static bool gltf_add_node(Gltf_Loader *loader, const Json &node_index, const Matrix4 &parent, u32 depth)
{
    const Json &node = (*loader->json)["nodes"][gltf_integer_or(node_index, UINT64_MAX)];
    if (!node.is_object())
    {
        log_error("glTF: bad node index");
        return false;
    }
    if (depth > GLTF_MAX_NODE_DEPTH)
    {
        log_error("glTF: node hierarchy too deep (a cycle?)");
        return false;
    }

    Matrix4 transform = parent * gltf_node_transform(node);

    // A skinned mesh is where its skin puts it, wherever its node is.
    u64 skin = gltf_integer_or(node["skin"], GLTF_NO_SKIN);
    if (skin != GLTF_NO_SKIN && !(*loader->json)["skins"][skin].is_object())
    {
        log_error("glTF: bad skin index");
        return false;
    }
    if (!node["mesh"].is_null())
    {
        if (skin != GLTF_NO_SKIN && loader->skin == GLTF_NO_SKIN) loader->skin = skin;
        if (skin != GLTF_NO_SKIN && skin != loader->skin && !loader->other_skins_logged)
        {
            log_warn("glTF: only the first skin is read: meshes with others don't move with it");
            loader->other_skins_logged = true;
        }

        bool skinned = skin != GLTF_NO_SKIN && skin == loader->skin;
        if (!gltf_add_mesh(loader, gltf_integer_or(node["mesh"], UINT64_MAX), skinned ? identity() : transform, skinned))
            return false;
    }

    for (const Json &child : node["children"].items)
    {
        if (!gltf_add_node(loader, child, transform, depth + 1)) return false;
    }
    return true;
}

// A node's transform as a joint's pose: a matrix is taken apart, assuming it doesn't shear.
static Joint_Pose gltf_node_pose(const Json &node)
{
    const Json &matrix = node["matrix"];
    if (matrix.size() != 16)
    {
        const Json &r = node["rotation"];
        return {
            .translation = node["translation"].vector3_or({ 0.0f, 0.0f, 0.0f }),
            .rotation    = { r[0].float_or(0.0f), r[1].float_or(0.0f), r[2].float_or(0.0f), r[3].float_or(1.0f) },
            .scale       = node["scale"].vector3_or({ 1.0f, 1.0f, 1.0f }),
        };
    }

    Matrix4 m = gltf_node_transform(node);
    Vector3 columns[3];
    Vector3 scale;
    for (u32 c = 0; c < 3; ++c)
    {
        columns[c]   = { m.at(0, c), m.at(1, c), m.at(2, c) };
        (&scale.x)[c] = length(columns[c]);
        if ((&scale.x)[c] > 0.0f) columns[c] = columns[c] * (1.0f / (&scale.x)[c]);
    }
    if (dot(columns[0], cross(columns[1], columns[2])) < 0.0f)
    {
        scale.x    = -scale.x;
        columns[0] = columns[0] * -1.0f;
    }

    // The rotation's quaternion, from its largest component, for precision.
    r32 m00 = columns[0].x, m11 = columns[1].y, m22 = columns[2].z;
    r32 trace = m00 + m11 + m22;
    Vector4 q;
    if (trace > 0.0f)
    {
        r32 s = std::sqrt(trace + 1.0f) * 2.0f;
        q = { (columns[1].z - columns[2].y) / s, (columns[2].x - columns[0].z) / s, (columns[0].y - columns[1].x) / s, s / 4 };
    }
    else if (m00 > m11 && m00 > m22)
    {
        r32 s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = { s / 4, (columns[1].x + columns[0].y) / s, (columns[2].x + columns[0].z) / s, (columns[1].z - columns[2].y) / s };
    }
    else if (m11 > m22)
    {
        r32 s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = { (columns[1].x + columns[0].y) / s, s / 4, (columns[2].y + columns[1].z) / s, (columns[2].x - columns[0].z) / s };
    }
    else
    {
        r32 s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = { (columns[2].x + columns[0].z) / s, (columns[2].y + columns[1].z) / s, s / 4, (columns[0].y - columns[1].x) / s };
    }
    return { .translation = { m.at(0, 3), m.at(1, 3), m.at(2, 3) }, .rotation = q, .scale = scale };
}

// The loader's skin: its joints and the nodes above them, parents first, into `joints`,
// and its vertices' joints made into those. Returns false when it can't be read (logged):
// the model has no skin then.
static bool gltf_skin(Gltf_Loader *loader, std::vector<Skin_Joint> *joints)
{
    const Json &json  = *loader->json;
    const Json &nodes = json["nodes"];
    const Json &skin  = json["skins"][loader->skin];

    std::vector<u64> parents(nodes.size(), UINT64_MAX);
    for (u64 i = 0; i < nodes.size(); ++i)
    {
        for (const Json &child : nodes[i]["children"].items)
        {
            u64 c = gltf_integer_or(child, UINT64_MAX);
            if (c < parents.size()) parents[c] = i;
        }
    }
    auto depth = [&](u64 node)
    {
        u32 d = 0;
        for (; parents[node] != UINT64_MAX && d <= GLTF_MAX_NODE_DEPTH; node = parents[node]) ++d;
        return d;
    };

    // The skin's joints, and every node above one.
    std::vector<u64> skin_joints;
    std::vector<u64> kept;
    for (const Json &joint : skin["joints"].items)
    {
        u64 node = gltf_integer_or(joint, UINT64_MAX);
        if (node >= nodes.size() || depth(node) > GLTF_MAX_NODE_DEPTH)
        {
            log_error("glTF: bad joint node index (or a cycle)");
            return false;
        }
        skin_joints.push_back(node);
        for (u64 at = node; at != UINT64_MAX; at = parents[at])
        {
            if (std::find(kept.begin(), kept.end(), at) == kept.end()) kept.push_back(at);
        }
    }
    if (kept.size() > SKIN_MAX_JOINTS)
    {
        log_error("glTF: the skin has %llu joints, with the nodes above them: at most %u are supported",
                  (unsigned long long)kept.size(), SKIN_MAX_JOINTS);
        return false;
    }
    std::stable_sort(kept.begin(), kept.end(), [&](u64 a, u64 b) { return depth(a) < depth(b); });

    Gltf_Accessor inverse_binds = {};
    bool has_inverse_binds = !skin["inverseBindMatrices"].is_null();
    if (has_inverse_binds)
    {
        if (!gltf_accessor(loader, skin["inverseBindMatrices"], &inverse_binds)) return false;
        if (inverse_binds.component_type != GLTF_FLOAT || inverse_binds.components != 16 ||
            inverse_binds.count < skin_joints.size())
        {
            log_error("glTF: inverse bind matrices must be float 4x4 matrices, one per joint");
            return false;
        }
    }

    auto index_of = [&](u64 node) { return (u32)(std::find(kept.begin(), kept.end(), node) - kept.begin()); };
    joints->resize(kept.size());
    for (u64 i = 0; i < kept.size(); ++i)
    {
        Skin_Joint &joint = (*joints)[i];
        const Json &node  = nodes[kept[i]];
        snprintf(joint.name, sizeof(joint.name), "%s", node["name"].string.c_str());
        joint.parent       = parents[kept[i]] == UINT64_MAX ? NO_JOINT : index_of(parents[kept[i]]);
        joint.rest         = gltf_node_pose(node);
        joint.inverse_bind = identity();
    }
    for (u64 k = 0; k < skin_joints.size() && has_inverse_binds; ++k)
        memcpy(&(*joints)[index_of(skin_joints[k])].inverse_bind, inverse_binds.data + k * inverse_binds.stride,
               sizeof(Matrix4));

    for (Skin_Weights &weights : loader->weights)
    {
        for (u32 k = 0; k < 4; ++k)
        {
            if (weights.weights[k] == 0.0f) { weights.joints[k] = 0; continue; }
            if (weights.joints[k] >= skin_joints.size())
            {
                log_error("glTF: a vertex's joint %u is past the skin's %llu", weights.joints[k],
                          (unsigned long long)skin_joints.size());
                return false;
            }
            weights.joints[k] = (u16)index_of(skin_joints[weights.joints[k]]);
        }
    }
    return true;
}

// The .glb container's JSON and binary chunks. Returns false when it isn't one (logged).
static bool gltf_chunks(const u8 *data, u64 size, const u8 **json_data, u64 *json_size, const u8 **bin_data,
                        u64 *bin_size)
{
    *json_data = *bin_data = nullptr;
    *json_size = *bin_size = 0;

    if (size < 12 || gltf_read_u32(data) != GLB_MAGIC)
    {
        log_error("glTF: not a .glb file");
        return false;
    }
    if (gltf_read_u32(data + 4) != 2)
    {
        log_error("glTF: version %u isn't supported, only 2", gltf_read_u32(data + 4));
        return false;
    }
    u64 length = gltf_read_u32(data + 8);
    if (length > size) length = size;

    for (u64 at = 12; at + 8 <= length;)
    {
        u64 chunk_size = gltf_read_u32(data + at);
        u32 chunk_type = gltf_read_u32(data + at + 4);
        at += 8;
        if (chunk_size > length - at) break;

        if (chunk_type == GLB_CHUNK_JSON && !*json_data) { *json_data = data + at; *json_size = chunk_size; }
        if (chunk_type == GLB_CHUNK_BIN  && !*bin_data)  { *bin_data  = data + at; *bin_size  = chunk_size; }

        at += chunk_size;
    }

    if (!*json_data)
    {
        log_error("glTF: the .glb file has no JSON chunk");
        return false;
    }
    return true;
}

// How far `at` is from the next multiple of `alignment`.
static u64 gltf_padding(const void *at, u64 alignment)
{
    return (alignment - (uintptr_t)at % alignment) % alignment;
}

u64 load_glb_from_memory(const u8 *data, u64 size, const char *directory, Model *model, Texture_Library *textures,
                         void *storage, u64 storage_size)
{
    *model = {};

    const u8 *json_data, *bin_data;
    u64       json_size, bin_size;
    if (!gltf_chunks(data, size, &json_data, &json_size, &bin_data, &bin_size)) return 0;

    Json json;
    if (!Json::parse(std::string_view((const char *)json_data, json_size), &json)) return 0;

    Gltf_Loader loader = {};
    loader.json     = &json;
    loader.bin      = bin_data;
    loader.bin_size = bin_size;

    const Json &scenes = json["scenes"];
    if (scenes.size() > 0)
    {
        const Json &scene = scenes[gltf_integer_or(json["scene"], 0)];
        for (const Json &node : scene["nodes"].items)
        {
            if (!gltf_add_node(&loader, node, identity(), 0)) return 0;
        }
    }
    else
    {
        // No scene to place them: every mesh as it is.
        for (u64 i = 0; i < json["meshes"].size(); ++i)
        {
            if (!gltf_add_mesh(&loader, i, identity(), false)) return 0;
        }
    }

    u64 index_count = 0;
    for (const Gltf_Part &part : loader.parts) index_count += part.indices.size();
    if (index_count == 0)
    {
        log_error("glTF: no triangles to load");
        return 0;
    }
    if (index_count > UINT32_MAX)
    {
        log_error("glTF: too many indices");
        return 0;
    }
    if (loader.parts.size() > MODEL_MAX_PARTS)
    {
        log_error("glTF: the model has %llu materials, at most %u are supported",
                  (unsigned long long)loader.parts.size(), MODEL_MAX_PARTS);
        return 0;
    }

    std::vector<Skin_Joint> joints;
    if (loader.skin != GLTF_NO_SKIN && !gltf_skin(&loader, &joints)) joints.clear();

    // The skin after the mesh, aligned for its pointers and matrices.
    u32 vertex_count = (u32)loader.vertices.size();
    u64 needed       = mesh_storage_size(vertex_count, (u32)index_count);
    u64 skin_start   = needed + gltf_padding((u8 *)storage + needed, alignof(Skin));
    if (!joints.empty())
        needed = skin_start + sizeof(Skin) + joints.size() * sizeof(Skin_Joint) + (u64)vertex_count * sizeof(Skin_Weights);

    if ((uintptr_t)storage % alignof(u32) != 0 || needed > storage_size)
    {
        log_error("glTF: the mesh needs %llu bytes of 4-byte aligned storage, %llu given",
                  (unsigned long long)needed, (unsigned long long)storage_size);
        return 0;
    }

    Mesh &mesh = model->mesh;
    mesh.vertices     = (Mesh_Vertex *)storage;
    mesh.indices      = (u32 *)((u8 *)storage + (u64)vertex_count * sizeof(Mesh_Vertex));
    mesh.vertex_count = vertex_count;
    mesh.index_count  = (u32)index_count;
    memcpy(mesh.vertices, loader.vertices.data(), (u64)vertex_count * sizeof(Mesh_Vertex));

    if (!joints.empty())
    {
        Skin *skin        = (Skin *)((u8 *)storage + skin_start);
        skin->joint_count = (u32)joints.size();
        skin->joints      = (Skin_Joint *)(skin + 1);
        skin->vertices    = (Skin_Weights *)(skin->joints + skin->joint_count);
        memcpy(skin->joints, joints.data(), joints.size() * sizeof(Skin_Joint));
        memcpy(skin->vertices, loader.weights.data(), (u64)vertex_count * sizeof(Skin_Weights));
        model->skin = skin;
    }

    // The parts' indices one after the other, and their materials' textures after them.
    u64 used = needed;
    u32 first_index = 0;
    for (const Gltf_Part &part : loader.parts)
    {
        Model_Part &model_part = model->parts[model->part_count++];
        model_part.first_index = first_index;
        model_part.index_count = (u32)part.indices.size();
        memcpy(mesh.indices + first_index, part.indices.data(), part.indices.size() * sizeof(u32));
        first_index += model_part.index_count;

        used += gltf_material(json, part.material, directory, textures, &model_part.material,
                              (u8 *)storage + used, storage_size - used);
    }

    return used;
}

u64 load_animation_glb_from_memory(const u8 *data, u64 size, Animation *animation, void *storage, u64 storage_size)
{
    *animation = {};

    const u8 *json_data, *bin_data;
    u64       json_size, bin_size;
    if (!gltf_chunks(data, size, &json_data, &json_size, &bin_data, &bin_size)) return 0;

    Json json;
    if (!Json::parse(std::string_view((const char *)json_data, json_size), &json)) return 0;

    Gltf_Loader loader = {};
    loader.json     = &json;
    loader.bin      = bin_data;
    loader.bin_size = bin_size;

    const Json &source = json["animations"][(u64)0];
    if (!source.is_object())
    {
        log_error("glTF: no animation to load");
        return 0;
    }

    // The channels as they'll be kept, their keys gathered first.
    struct Channel
    {
        Animation_Channel channel;
        std::vector<r32>  times;
        std::vector<r32>  values;
    };
    std::vector<Channel> channels;
    for (const Json &target_channel : source["channels"].items)
    {
        const Json &target = target_channel["target"];
        const str  &path   = target["path"].string;
        if (path == "weights") continue;

        Channel kept = {};
        Animation_Channel &channel = kept.channel;
        if (path == "translation")   channel.path = Animation_Path::Translation;
        else if (path == "rotation") channel.path = Animation_Path::Rotation;
        else if (path == "scale")    channel.path = Animation_Path::Scale;
        else
        {
            log_error("glTF: an animation channel of an unknown path \"%s\"", path.c_str());
            return 0;
        }

        const Json &node = json["nodes"][gltf_integer_or(target["node"], UINT64_MAX)];
        if (!node.is_object())
        {
            log_error("glTF: an animation channel without a node");
            return 0;
        }
        snprintf(channel.joint, sizeof(channel.joint), "%s", node["name"].string.c_str());

        const Json &sampler = source["samplers"][gltf_integer_or(target_channel["sampler"], UINT64_MAX)];
        const str  &interpolation = sampler["interpolation"].string;
        bool cubic   = interpolation == "CUBICSPLINE";
        channel.step = interpolation == "STEP";

        Gltf_Accessor input, output;
        if (!sampler.is_object() || !gltf_accessor(&loader, sampler["input"], &input) ||
            !gltf_accessor(&loader, sampler["output"], &output))
        {
            log_error("glTF: an animation channel without its keys");
            return 0;
        }
        u32 components = channel.path == Animation_Path::Rotation ? 4 : 3;
        u64 per_key    = cubic ? 3 : 1; // A cubic spline's keys have their in and out tangents either side
        if (input.component_type != GLTF_FLOAT || input.components != 1 || output.component_type != GLTF_FLOAT ||
            output.components != components || output.count != input.count * per_key || input.count > UINT32_MAX)
        {
            log_error("glTF: an animation channel's keys must be float times and values, one value per time");
            return 0;
        }

        channel.key_count = (u32)input.count;
        for (u64 k = 0; k < input.count; ++k)
        {
            r32 time;
            memcpy(&time, input.data + k * input.stride, sizeof(time));
            kept.times.push_back(time);

            const u8 *value = output.data + (k * per_key + (cubic ? 1 : 0)) * output.stride;
            for (u32 c = 0; c < components; ++c)
            {
                r32 component;
                memcpy(&component, value + c * sizeof(r32), sizeof(component));
                kept.values.push_back(component);
            }
        }
        if (!kept.times.empty()) animation->duration = std::max(animation->duration, kept.times.back());
        channels.push_back(std::move(kept));
    }

    // The channels, aligned for their pointers, then their keys.
    u64 start  = gltf_padding(storage, alignof(Animation_Channel));
    u64 needed = start + channels.size() * sizeof(Animation_Channel);
    for (const Channel &kept : channels) needed += (kept.times.size() + kept.values.size()) * sizeof(r32);
    if (needed > storage_size)
    {
        log_error("glTF: the animation needs %llu bytes of storage, %llu given", (unsigned long long)needed,
                  (unsigned long long)storage_size);
        *animation = {};
        return 0;
    }

    animation->channel_count = (u32)channels.size();
    animation->channels      = (Animation_Channel *)((u8 *)storage + start);
    r32 *keys = (r32 *)(animation->channels + channels.size());
    for (u64 i = 0; i < channels.size(); ++i)
    {
        Animation_Channel &channel = animation->channels[i];
        channel        = channels[i].channel;
        channel.times  = keys;
        keys           = std::copy(channels[i].times.begin(), channels[i].times.end(), keys);
        channel.values = keys;
        keys           = std::copy(channels[i].values.begin(), channels[i].values.end(), keys);
    }
    return needed;
}

u64 load_animation_glb(const char *path, Animation *animation, void *storage, u64 storage_size)
{
    *animation = {};

    str data = read_file(path);
    if (data.empty())
    {
        log_error("Could not read \"%s\"", path);
        return 0;
    }

    u64 used = load_animation_glb_from_memory((const u8 *)data.data(), data.size(), animation, storage, storage_size);
    if (used)
        log_info("Loaded \"%s\": %u channels, %.2f s", path, animation->channel_count, animation->duration);
    else
        log_error("Could not load \"%s\"", path);

    return used;
}

u64 load_glb(const char *path, Model *model, Texture_Library *textures, void *storage, u64 storage_size)
{
    *model = {};

    str data = read_file(path);
    if (data.empty())
    {
        log_error("Could not read \"%s\"", path);
        return 0;
    }

    // Images are next to the file.
    std::string directory = path;
    u64 slash = directory.find_last_of("/\\");
    directory = slash == std::string::npos ? "" : directory.substr(0, slash);

    u64 used = load_glb_from_memory((const u8 *)data.data(), data.size(), directory.c_str(), model, textures, storage,
                                    storage_size);
    if (used)
        log_info("Loaded \"%s\": %u vertices, %u triangles, %u materials%s", path, model->mesh.vertex_count,
                 model->mesh.index_count / 3, model->part_count, model->skin ? ", skinned" : "");
    else
        log_error("Could not load \"%s\"", path);

    return used;
}
