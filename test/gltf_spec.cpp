#include <catch2/catch_test_macros.hpp>

#include "gltf.h"

#include <cstring>
#include <string>
#include <vector>

// A .glb with one triangle in the XY plane, counter-clockwise seen from +Z, no normals
// and, when `with_uvs`, texture coordinates. `node` is the JSON of the node that holds
// the mesh.
static std::vector<u8> gltf_spec_triangle_glb(const std::string &node, bool with_uvs = false)
{
    const float positions[9] = { 0, 0, 0,   1, 0, 0,   0, 1, 0 };
    const u16   indices[4]   = { 0, 1, 2,   0 }; // Padded to 4 bytes
    const float uvs[6]       = { 0.25f, 0.5f,   1, 0.5f,   0.25f, 0 };

    std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
        R"("nodes":[)" + node + R"(],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0)" + (with_uvs ? R"(,"TEXCOORD_0":2)" : "") + R"(},"indices":1}]}],)"
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
    Mesh mesh;
    u64 used = load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage));

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
    Mesh mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage)));

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
    Mesh mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage)));

    // The Z flip leaves an XY triangle in place, so swapping two corners turns it to -Z.
    REQUIRE(mesh.indices[1] == 2);
    REQUIRE(mesh.indices[2] == 1);
    REQUIRE(mesh.vertices[0].normal.z == -1.0f);
}

TEST_CASE("load_glb_from_memory reads texture coordinates", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})", true);

    alignas(4) u8 storage[256];
    Mesh mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage)));

    REQUIRE(mesh.vertices[0].uv.x == 0.25f);
    REQUIRE(mesh.vertices[0].uv.y == 0.5f);
    REQUIRE(mesh.vertices[1].uv.x == 1.0f);
    REQUIRE(mesh.vertices[2].uv.y == 0.0f);
}

TEST_CASE("load_glb_from_memory leaves texture coordinates at zero when missing", "[gltf]")
{
    std::vector<u8> glb = gltf_spec_triangle_glb(R"({"mesh":0})");

    alignas(4) u8 storage[256];
    Mesh mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage)));

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
    Mesh mesh;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), &mesh, storage, sizeof(storage)) == 0);
    REQUIRE(mesh.vertex_count == 0);
}

TEST_CASE("load_glb_from_memory rejects files that aren't .glb", "[gltf]")
{
    const u8 text[] = "{\"asset\":{\"version\":\"2.0\"}}";

    alignas(4) u8 storage[16];
    Mesh mesh;
    REQUIRE(load_glb_from_memory(text, sizeof(text), &mesh, storage, sizeof(storage)) == 0);
}
