#include <catch2/catch_test_macros.hpp>

#include "gltf.h"
#include "png.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// A .glb with one triangle in the XY plane, counter-clockwise seen from +Z, no normals
// and, when `with_uvs`, texture coordinates. `node` is the JSON of the node that holds
// the mesh. `primitive` is added to the JSON of its one primitive, `primitives` more
// primitives after it (all of the same triangle), and `extra` to the top object.
static std::vector<u8> gltf_spec_triangle_glb(const std::string &node, bool with_uvs = false,
                                              const std::string &primitive = "", const std::string &primitives = "",
                                              const std::string &extra = "")
{
    const float positions[9] = { 0, 0, 0,   1, 0, 0,   0, 1, 0 };
    const u16   indices[4]   = { 0, 1, 2,   0 }; // Padded to 4 bytes
    const float uvs[6]       = { 0.25f, 0.5f,   1, 0.5f,   0.25f, 0 };

    std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
        R"("nodes":[)" + node + R"(],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0)" + (with_uvs ? R"(,"TEXCOORD_0":2)" : "") + R"(},"indices":1)" +
        primitive + "}" + primitives + R"(]}],)" + extra +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},)"
        R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},)"
        R"({"buffer":0,"byteOffset":44,"byteLength":24}],)"
        R"("buffers":[{"byteLength":68}]})";
    while (json.size() % 4) json += ' ';

    std::vector<u8> bin(68);
    memcpy(bin.data(), positions, sizeof(positions));
    memcpy(bin.data() + 36, indices, sizeof(indices));
    memcpy(bin.data() + 44, uvs, sizeof(uvs));

    std::vector<u8> glb;
    auto put_u32 = [&](u32 value) { glb.insert(glb.end(), (u8 *)&value, (u8 *)&value + 4); };

    put_u32(0x46546C67);
    put_u32(2);
    put_u32((u32)(12 + 8 + json.size() + 8 + bin.size()));
    put_u32((u32)json.size());
    put_u32(0x4E4F534A);
    glb.insert(glb.end(), json.begin(), json.end());
    put_u32((u32)bin.size());
    put_u32(0x004E4942);
    glb.insert(glb.end(), bin.begin(), bin.end());

    return glb;
}

TEST_CASE("load_glb_from_memory applies the node's transform", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0,"translation":[10,0,0],"scale":[2,2,2]})");

    alignas(4) u8 storage[256];
    Model model;
    Mesh &mesh = model.mesh;
    u64 used = load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage));

    REQUIRE(used == mesh_storage_size(3, 3));
    REQUIRE(mesh.vertex_count == 3);
    REQUIRE(mesh.index_count == 3);
    REQUIRE(mesh.vertices[1].position.x == 12.0f);
    REQUIRE(mesh.vertices[2].position.y == 2.0f);
    REQUIRE(mesh.indices[0] == 0);
    REQUIRE(mesh.indices[1] == 1);
    REQUIRE(mesh.indices[2] == 2);
}

TEST_CASE("load_glb_from_memory computes missing normals from the winding", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})");

    alignas(4) u8 storage[256];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    for (u32 i = 0; i < 3; ++i)
    {
        REQUIRE(mesh.vertices[i].normal.x == 0.0f);
        REQUIRE(mesh.vertices[i].normal.y == 0.0f);
        REQUIRE(mesh.vertices[i].normal.z == 1.0f);
    }
}

TEST_CASE("load_glb_from_memory keeps mirrored triangles facing out", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0,"scale":[1,1,-1]})");

    alignas(4) u8 storage[256];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    // The Z flip leaves an XY triangle in place, so swapping two corners turns it to -Z.
    REQUIRE(mesh.indices[1] == 2);
    REQUIRE(mesh.indices[2] == 1);
    REQUIRE(mesh.vertices[0].normal.z == -1.0f);
}

TEST_CASE("load_glb_from_memory reads texture coordinates", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})", true);

    alignas(4) u8 storage[256];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    REQUIRE(mesh.vertices[0].uv.x == 0.25f);
    REQUIRE(mesh.vertices[0].uv.y == 0.5f);
    REQUIRE(mesh.vertices[1].uv.x == 1.0f);
    REQUIRE(mesh.vertices[2].uv.y == 0.0f);
}

TEST_CASE("load_glb_from_memory leaves texture coordinates at zero when missing", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})");

    alignas(4) u8 storage[256];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    for (u32 i = 0; i < 3; ++i)
    {
        REQUIRE(mesh.vertices[i].uv.x == 0.0f);
        REQUIRE(mesh.vertices[i].uv.y == 0.0f);
    }
}

TEST_CASE("load_glb_from_memory fails when the storage is too small", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})");

    alignas(4) u8 storage[16];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)) == 0);
    REQUIRE(mesh.vertex_count == 0);
}

TEST_CASE("load_glb_from_memory rejects files that aren't .glb", "[gltf]")
{
    const u8 text[] = "{\"asset\":{\"version\":\"2.0\"}}";

    alignas(4) u8 storage[16];
    Model model;
    Mesh &mesh = model.mesh;
    REQUIRE(load_glb_from_memory(text, sizeof(text), "", &model, nullptr, storage, sizeof(storage)) == 0);
}

TEST_CASE("load_glb_from_memory puts each material's triangles in a part", "[gltf]")
{
    // The first and third primitives have material 1, the second none.
    std::vector<u8> glb = gltf_spec_triangle_glb(
        R"({"mesh":0})", false, R"(,"material":1)",
        R"(,{"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1,"material":1})",
        R"("materials":[{},{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,1,1],"metallicFactor":0.25,"roughnessFactor":0.75},)"
        R"("normalTexture":{"index":0,"scale":0.5},"emissiveFactor":[1,0.5,0],)"
        R"("extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}}}],)");

    alignas(4) u8 storage[512];
    Model model;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    REQUIRE(model.mesh.vertex_count == 9);
    REQUIRE(model.mesh.index_count == 9);
    REQUIRE(model.part_count == 2);

    // In the order the materials first appear, each part's indices together.
    const Model_Part &first = model.parts[0];
    REQUIRE(first.first_index == 0);
    REQUIRE(first.index_count == 6);
    REQUIRE(model.mesh.indices[3] == 6); // The third primitive's first vertex
    REQUIRE(first.material.base_color.y == 0.25f);
    REQUIRE(first.material.metallic == 0.25f);
    REQUIRE(first.material.roughness == 0.75f);
    REQUIRE(first.material.normal_scale == 0.5f);
    REQUIRE(first.material.emissive.x == 4.0f);
    REQUIRE(first.material.emissive.y == 2.0f);
    REQUIRE(!first.material.normal_texture.pixels); // No library to load it with

    const Model_Part &second = model.parts[1];
    REQUIRE(second.first_index == 6);
    REQUIRE(second.index_count == 3);
    REQUIRE(second.material.base_color.x == 1.0f);
    REQUIRE(second.material.metallic == 0.0f); // plain_material(), not glTF's metallic default
    REQUIRE(second.material.roughness == 1.0f);
}

TEST_CASE("load_glb_from_memory takes materials' factors from glTF's defaults", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})", false, R"(,"material":0)", "", R"("materials":[{}],)");

    alignas(4) u8 storage[256];
    Model model;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)));

    const Material &material = model.parts[0].material;
    REQUIRE(material.base_color.w == 1.0f);
    REQUIRE(material.metallic == 1.0f);
    REQUIRE(material.roughness == 1.0f);
    REQUIRE(material.normal_scale == 1.0f);
    REQUIRE(material.emissive.x == 0.0f);
}

TEST_CASE("load_glb_from_memory loads materials' textures through the library, once each", "[gltf]")
{
    // A 1x1 PNG next to the model, used as both its base color and emissive textures.
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "lemonade_gltf_spec";
    std::filesystem::create_directories(directory);
    u8 pixel[4] = { 10, 20, 30, 255 };
    REQUIRE(save_png((directory / "color.png").string().c_str(), { pixel, 1, 1 }));

    std::vector<u8> glb = gltf_spec_triangle_glb(
        R"({"mesh":0})", false, R"(,"material":0)", "",
        R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"emissiveTexture":{"index":1},)"
        R"("normalTexture":{"index":2}}],)"
        R"("textures":[{"source":0},{"source":0},{"source":1}],)"
        R"("images":[{"uri":"color.png"},{"uri":"missing.png"}],)");

    alignas(4) u8 storage[256];
    Model model;
    Texture_Library library = {};
    u64 used = load_glb_from_memory(glb.data(), glb.size(), directory.string().c_str(), &model, &library,
                                    storage, sizeof(storage));

    REQUIRE(used == mesh_storage_size(3, 3) + 4); // The PNG's one pixel, once
    const Material &material = model.parts[0].material;
    REQUIRE(material.base_color_texture.width == 1);
    REQUIRE(material.base_color_texture.pixels[2] == 30);
    REQUIRE(material.emissive_texture.pixels == material.base_color_texture.pixels);
    REQUIRE(!material.normal_texture.pixels); // Its file is missing
    REQUIRE(library.count == 2);

    std::filesystem::remove_all(directory);
}
