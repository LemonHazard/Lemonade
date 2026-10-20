#include <catch2/catch_test_macros.hpp>

#include "matrix.h"
#include "mesh.h"

TEST_CASE("build_plane makes an upward square, its texture repeating every tile", "[mesh]")
{
    alignas(4) u8 storage[256];
    Mesh mesh;
    REQUIRE(build_plane(&mesh, 10.0f, 4.0f, storage, sizeof(storage)) == mesh_storage_size(4, 6));
    REQUIRE(mesh.vertex_count == 4);
    REQUIRE(mesh.index_count == 6);
    REQUIRE((u8 *)mesh.vertices == storage);

    for (u32 i = 0; i < mesh.vertex_count; ++i)
    {
        const Mesh_Vertex &vertex = mesh.vertices[i];
        REQUIRE((vertex.position.x == 10.0f || vertex.position.x == -10.0f));
        REQUIRE((vertex.position.z == 10.0f || vertex.position.z == -10.0f));
        REQUIRE(vertex.position.y == 0.0f);
        REQUIRE(vertex.normal.y == 1.0f);
        REQUIRE(vertex.uv.x == (vertex.position.x > 0.0f ? 5.0f : 0.0f));
        REQUIRE(vertex.uv.y == (vertex.position.z > 0.0f ? 5.0f : 0.0f));
    }

    // Counter-clockwise seen from above.
    for (u32 i = 0; i < mesh.index_count; i += 3)
    {
        Vector3 a = mesh.vertices[mesh.indices[i]].position;
        Vector3 b = mesh.vertices[mesh.indices[i + 1]].position;
        Vector3 c = mesh.vertices[mesh.indices[i + 2]].position;
        REQUIRE(cross(b - a, c - a).y > 0.0f);
    }
}

TEST_CASE("build_plane leaves the mesh empty when it doesn't fit", "[mesh]")
{
    alignas(4) u8 storage[64];
    Mesh mesh;
    REQUIRE(build_plane(&mesh, 1.0f, 1.0f, storage, sizeof(storage)) == 0);
    REQUIRE(mesh.vertex_count == 0);
    REQUIRE(mesh.vertices == nullptr);
}
