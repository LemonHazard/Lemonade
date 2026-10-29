#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation.h"
#include "gltf.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using Catch::Approx;

// A .glb of `json` (its "buffers" added) and its binary chunk, `bin`.
static std::vector<u8> animation_spec_glb(std::string json, std::vector<u8> bin)
{
    while (bin.size() % 4) bin.push_back(0);
    json.insert(json.size() - 1, R"(,"buffers":[{"byteLength":)" + std::to_string(bin.size()) + "}]");
    while (json.size() % 4) json += ' ';

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

// Appends `data` to `bin` and returns the JSON of a buffer view of it.
template <typename T>
static std::string animation_spec_view(std::vector<u8> *bin, const std::vector<T> &data)
{
    while (bin->size() % 4) bin->push_back(0);
    u64 offset = bin->size();
    bin->insert(bin->end(), (const u8 *)data.data(), (const u8 *)(data.data() + data.size()));
    return R"({"buffer":0,"byteOffset":)" + std::to_string(offset) + R"(,"byteLength":)" +
           std::to_string(data.size() * sizeof(T)) + "}";
}

static Joint_Pose animation_spec_pose(Vector3 translation, Vector4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f })
{
    return { translation, rotation, { 1.0f, 1.0f, 1.0f } };
}

// A turn of `degrees` about +Z, as a quaternion.
static Vector4 animation_spec_about_z(r32 degrees)
{
    r32 half = radians(degrees) * 0.5f;
    return { 0.0f, 0.0f, std::sin(half), std::cos(half) };
}

TEST_CASE("load_glb_from_memory reads the skin's joints, parents first, and its vertices' joints and weights", "[animation]")
{
    // Under an armature, a hip and a knee above it; the skin lists the knee first, and the
    // skinned mesh's node is moved, which glTF leaves out.
    std::vector<u8> bin;
    std::vector<float> positions     = { 0, 0, 0,   1, 0, 0,   0, 1, 0 };
    std::vector<u16>   indices       = { 0, 1, 2 };
    std::vector<u8>    joints        = { 0, 1, 0, 0,   1, 0, 0, 0,   0, 0, 0, 0 };
    std::vector<float> weights       = { 0.5f, 0.5f, 0, 0,   1, 0, 0, 0,   0, 0, 0, 0 };
    std::vector<float> inverse_binds(32, 0.0f);
    for (u32 i = 0; i < 4; ++i) inverse_binds[i * 5] = inverse_binds[16 + i * 5] = 1.0f;
    inverse_binds[13] = -2.0f; // The knee's: its Y translation

    std::string views = animation_spec_view(&bin, positions) + "," + animation_spec_view(&bin, indices) + "," +
                        animation_spec_view(&bin, joints) + "," + animation_spec_view(&bin, weights) + "," +
                        animation_spec_view(&bin, inverse_binds);
    std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,3]}],)"
        R"("nodes":[{"name":"Armature","children":[1],"scale":[2,2,2]},{"name":"Hip","children":[2],"translation":[0,1,0]},)"
        R"({"name":"Knee","translation":[0,1,0]},{"mesh":0,"skin":0,"translation":[100,0,0]}],)"
        R"("skins":[{"joints":[2,1],"inverseBindMatrices":4}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},)"
        R"({"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},)"
        R"({"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},)"
        R"({"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],)"
        R"("bufferViews":[)" + views + "]}";
    std::vector<u8> glb = animation_spec_glb(json, bin);

    alignas(8) u8 storage[4096];
    Model model;
    REQUIRE(load_glb_from_memory(glb.data(), glb.size(), "", &model, nullptr, storage, sizeof(storage)) > 0);

    REQUIRE(model.mesh.vertices[1].position.x == 1.0f); // Not moved by its node
    REQUIRE(model.skin);
    const Skin &skin = *model.skin;
    REQUIRE(skin.joint_count == 3);
    REQUIRE(std::string(skin.joints[0].name) == "Armature");
    REQUIRE(std::string(skin.joints[1].name) == "Hip");
    REQUIRE(std::string(skin.joints[2].name) == "Knee");
    REQUIRE(skin.joints[0].parent == NO_JOINT);
    REQUIRE(skin.joints[1].parent == 0);
    REQUIRE(skin.joints[2].parent == 1);
    REQUIRE(skin.joints[0].rest.scale.x == 2.0f);
    REQUIRE(skin.joints[2].rest.translation.y == 1.0f);
    REQUIRE(skin.joints[2].inverse_bind.at(1, 3) == -2.0f);
    REQUIRE(skin.joints[0].inverse_bind.at(1, 3) == 0.0f); // Not in the glTF skin: identity

    REQUIRE(skin.vertices[0].joints[0] == 2); // The knee, first in the glTF skin
    REQUIRE(skin.vertices[0].joints[1] == 1);
    REQUIRE(skin.vertices[0].weights[0] == 0.5f);
    REQUIRE(skin.vertices[1].joints[0] == 1);
    REQUIRE(skin.vertices[1].weights[0] == 1.0f);
    REQUIRE(skin.vertices[2].weights[0] == 0.0f);
}

TEST_CASE("load_animation_glb_from_memory reads the first animation's channels by their nodes' names", "[animation]")
{
    std::vector<u8> bin;
    std::vector<float> times        = { 0.0f, 0.5f };
    std::vector<float> translations = { 0, 0, 0,   1, 2, 3 };
    std::vector<float> rotations    = { 0, 0, 0, 1,   0, 1, 0, 0 };
    // A cubic spline's keys: in tangent, value, out tangent each.
    std::vector<float> scales       = { 9, 9, 9,   1, 1, 1,   9, 9, 9,   9, 9, 9,   2, 2, 2,   9, 9, 9 };
    std::vector<float> long_times   = { 0.0f, 0.75f };

    std::string views = animation_spec_view(&bin, times) + "," + animation_spec_view(&bin, translations) + "," +
                        animation_spec_view(&bin, rotations) + "," + animation_spec_view(&bin, scales) + "," +
                        animation_spec_view(&bin, long_times);
    std::string json =
        R"({"asset":{"version":"2.0"},"nodes":[{"name":"Hips"},{"name":"Spine"}],)"
        R"("animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}},)"
        R"({"sampler":1,"target":{"node":1,"path":"rotation"}},{"sampler":2,"target":{"node":1,"path":"scale"}},)"
        R"({"sampler":0,"target":{"node":1,"path":"weights"}}],)"
        R"("samplers":[{"input":0,"output":1},{"input":0,"output":2,"interpolation":"STEP"},)"
        R"({"input":4,"output":3,"interpolation":"CUBICSPLINE"}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"},)"
        R"({"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"},)"
        R"({"bufferView":2,"componentType":5126,"count":2,"type":"VEC4"},)"
        R"({"bufferView":3,"componentType":5126,"count":6,"type":"VEC3"},)"
        R"({"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"}],)"
        R"("bufferViews":[)" + views + "]}";
    std::vector<u8> glb = animation_spec_glb(json, bin);

    alignas(8) u8 storage[1024];
    Animation animation;
    u64 used = load_animation_glb_from_memory(glb.data(), glb.size(), &animation, storage, sizeof(storage));

    REQUIRE(used > 0);
    REQUIRE(animation.duration == 0.75f);
    REQUIRE(animation.channel_count == 3); // Weights skipped

    const Animation_Channel &moves = animation.channels[0];
    REQUIRE(std::string(moves.joint) == "Hips");
    REQUIRE(moves.path == Animation_Path::Translation);
    REQUIRE(!moves.step);
    REQUIRE(moves.key_count == 2);
    REQUIRE(moves.times[1] == 0.5f);
    REQUIRE(moves.values[5] == 3.0f);

    const Animation_Channel &turns = animation.channels[1];
    REQUIRE(std::string(turns.joint) == "Spine");
    REQUIRE(turns.path == Animation_Path::Rotation);
    REQUIRE(turns.step);
    REQUIRE(turns.values[5] == 1.0f);

    const Animation_Channel &scales_channel = animation.channels[2];
    REQUIRE(scales_channel.path == Animation_Path::Scale);
    REQUIRE(scales_channel.key_count == 2);
    REQUIRE(scales_channel.values[0] == 1.0f); // The keys' values, not their tangents
    REQUIRE(scales_channel.values[3] == 2.0f);

    REQUIRE(load_animation_glb_from_memory(glb.data(), glb.size(), &animation, storage, 64) == 0);
    REQUIRE(animation.channel_count == 0);
}

TEST_CASE("animate_pose interpolates between keys, and holds the first and last ones beyond them", "[animation]")
{
    Skin_Joint joints[2] = {
        { "Hips", NO_JOINT, animation_spec_pose({}), identity() },
        { "Spine", 0, animation_spec_pose({ 0.0f, 1.0f, 0.0f }), identity() },
    };
    Skin skin = { 2, joints, nullptr };

    r32 times[2]     = { 0.0f, 1.0f };
    r32 moves[6]     = { 0, 0, 0,   2, 0, 0 };
    Vector4 quarter  = animation_spec_about_z(90.0f);
    r32 turns[8]     = { 0, 0, 0, 1,   quarter.x, quarter.y, quarter.z, quarter.w };
    r32 steps[6]     = { 0, 0, 0,   5, 5, 5 };
    Animation_Channel channels[4] = {
        { "Hips", Animation_Path::Translation, false, 2, times, moves },
        { "Spine", Animation_Path::Rotation, false, 2, times, turns },
        { "Hips", Animation_Path::Scale, true, 2, times, steps },
        { "Tail", Animation_Path::Translation, false, 2, times, moves }, // Not in the skin
    };
    Animation animation = { 1.0f, 4, channels };

    Joint_Pose poses[2];
    rest_pose(skin, poses);
    animate_pose(animation, 0.25f, skin, poses);
    REQUIRE(poses[0].translation.x == Approx(0.5f));
    REQUIRE(poses[0].scale.x == 0.0f); // Held until the next key
    REQUIRE(poses[1].translation.y == 1.0f); // Not animated: as it was
    Vector4 eighth = animation_spec_about_z(22.5f);
    REQUIRE(poses[1].rotation.z == Approx(eighth.z).margin(0.01f));
    REQUIRE(poses[1].rotation.w == Approx(eighth.w).margin(0.01f));

    animate_pose(animation, -1.0f, skin, poses);
    REQUIRE(poses[0].translation.x == 0.0f);
    animate_pose(animation, 5.0f, skin, poses);
    REQUIRE(poses[0].translation.x == 2.0f);
    REQUIRE(poses[0].scale.x == 5.0f);
    REQUIRE(poses[1].rotation.z == Approx(quarter.z));
}

TEST_CASE("skin_vertices moves each vertex by its joints, weighed, and leaves those without any", "[animation]")
{
    // A joint at the origin and one above it, bound where they stand.
    Skin_Joint joints[2] = {
        { "Root", NO_JOINT, animation_spec_pose({}), identity() },
        { "Child", 0, animation_spec_pose({ 0.0f, 1.0f, 0.0f }), translation({ 0.0f, -1.0f, 0.0f }) },
    };
    Skin_Weights weights[3] = {
        { { 1, 0, 0, 0 }, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { { 0, 1, 0, 0 }, { 0.5f, 0.5f, 0.0f, 0.0f } },
        {},
    };
    Skin skin = { 2, joints, weights };

    Mesh_Vertex vertices[3] = {
        { { 0.0f, 2.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.25f, 0.5f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, {} },
        { { 3.0f, 3.0f, 3.0f }, { 0.0f, 1.0f, 0.0f }, {} },
    };
    Mesh mesh = { vertices, nullptr, 3, 0 };

    // At rest, nothing moves.
    Joint_Pose  poses[2];
    Matrix4     matrices[2];
    Mesh_Vertex posed[3];
    rest_pose(skin, poses);
    skin_matrices(skin, poses, matrices);
    skin_vertices(mesh, skin, matrices, posed);
    REQUIRE(posed[0].position.y == Approx(2.0f));
    REQUIRE(posed[1].position.x == Approx(1.0f));

    // The child turned a quarter about +Z: what's on it swings to the left.
    poses[1].rotation = animation_spec_about_z(90.0f);
    skin_matrices(skin, poses, matrices);
    skin_vertices(mesh, skin, matrices, posed);
    REQUIRE(posed[0].position.x == Approx(-1.0f));
    REQUIRE(posed[0].position.y == Approx(1.0f));
    REQUIRE(posed[0].normal.y == Approx(1.0f));
    REQUIRE(posed[0].uv.x == 0.25f);
    REQUIRE(posed[1].position.x == Approx(0.5f));  // Halfway between (1, 1) and the child's (0, 2)
    REQUIRE(posed[1].position.y == Approx(1.5f));
    REQUIRE(posed[2].position.x == 3.0f);
}

TEST_CASE("push_posed_vertices poses a skinned model into the frame's commands, looping its animation", "[animation]")
{
    Skin_Joint   joints[1]  = { { "Root", NO_JOINT, animation_spec_pose({}), identity() } };
    Skin_Weights weights[1] = { { { 0, 0, 0, 0 }, { 1.0f, 0.0f, 0.0f, 0.0f } } };
    Skin         skin       = { 1, joints, weights };
    Mesh_Vertex  vertices[1] = { { {}, { 0.0f, 1.0f, 0.0f }, {} } };
    Model model = {};
    model.mesh = { vertices, nullptr, 1, 0 };
    model.skin = &skin;

    r32 times[2] = { 0.0f, 2.0f };
    r32 moves[6] = { 0, 0, 0,   4, 0, 0 };
    Animation_Channel channel = { "Root", Animation_Path::Translation, false, 2, times, moves };
    Animation animation = { 2.0f, 1, &channel };

    alignas(16) u8 buffer[1024];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    const Mesh_Vertex *posed = push_posed_vertices(&commands, model, animation, 5.0f); // 1 s in
    REQUIRE(posed);
    REQUIRE((const u8 *)posed > buffer);
    REQUIRE((const u8 *)posed < buffer + commands.used);
    REQUIRE(posed->position.x == Approx(2.0f));
    REQUIRE(((const Render_Command_Header *)buffer)->type == Render_Command_Type::Data);

    model.skin = nullptr;
    REQUIRE(!push_posed_vertices(&commands, model, animation, 0.0f));

    model.skin = &skin;
    commands.used = commands.capacity;
    REQUIRE(!push_posed_vertices(&commands, model, animation, 0.0f));
    REQUIRE(commands.dropped == 1);
}

TEST_CASE("blend_poses goes the weight's way from one pose to the other", "[animation]")
{
    Skin_Joint joints[1] = { { "Root", NO_JOINT, animation_spec_pose({}), identity() } };
    Skin       skin      = { 1, joints, nullptr };

    Joint_Pose from[1] = { animation_spec_pose({ 0.0f, 0.0f, 0.0f }) };
    Joint_Pose to[1]   = { { { 4.0f, 0.0f, 0.0f }, animation_spec_about_z(90.0f), { 3.0f, 3.0f, 3.0f } } };
    Joint_Pose poses[1];
    blend_poses(skin, from, to, 0.25f, poses);
    REQUIRE(poses[0].translation.x == Approx(1.0f));
    REQUIRE(poses[0].scale.x == Approx(1.5f));
    REQUIRE(poses[0].rotation.z == Approx(animation_spec_about_z(22.5f).z).margin(0.01f));

    blend_poses(skin, from, to, 1.0f, from); // Into one of them
    REQUIRE(from[0].translation.x == Approx(4.0f));
}

TEST_CASE("push_posed_vertices fades from one animation to another by the weight", "[animation]")
{
    Skin_Joint   joints[1]   = { { "Root", NO_JOINT, animation_spec_pose({}), identity() } };
    Skin_Weights weights[1]  = { { { 0, 0, 0, 0 }, { 1.0f, 0.0f, 0.0f, 0.0f } } };
    Skin         skin        = { 1, joints, weights };
    Mesh_Vertex  vertices[1] = { { {}, { 0.0f, 1.0f, 0.0f }, {} } };
    Model model = {};
    model.mesh = { vertices, nullptr, 1, 0 };
    model.skin = &skin;

    r32 times[1]  = { 0.0f };
    r32 left[3]   = { -2, 0, 0 };
    r32 right[3]  = { 2, 0, 0 };
    Animation_Channel left_channel  = { "Root", Animation_Path::Translation, false, 1, times, left };
    Animation_Channel right_channel = { "Root", Animation_Path::Translation, false, 1, times, right };
    Animation to_left  = { 0.0f, 1, &left_channel };
    Animation to_right = { 0.0f, 1, &right_channel };

    alignas(16) u8 buffer[1024];
    Render_Commands commands = {};
    commands.buffer   = buffer;
    commands.capacity = sizeof(buffer);

    REQUIRE(push_posed_vertices(&commands, model, to_right, 0.0f, &to_left, 0.0f, 0.25f)->position.x == Approx(-1.0f));
    REQUIRE(push_posed_vertices(&commands, model, to_right, 0.0f, &to_left, 0.0f, 1.0f)->position.x == Approx(2.0f));
}

TEST_CASE("play_animation fades from what was played, and advance_animation moves the fade on", "[animation]")
{
    Animation_State state = {};
    play_animation(&state, 3, 0.5f);
    REQUIRE(state.current == 3);
    REQUIRE(state.faded == NO_ANIMATION); // Nothing to fade from

    advance_animation(&state, 1.0f, 2.0f);
    REQUIRE(state.time == 2.0f);

    play_animation(&state, 3, 0.5f); // Plays it already: goes on
    REQUIRE(state.time == 2.0f);

    play_animation(&state, 5, 0.5f);
    REQUIRE(state.current == 5);
    REQUIRE(state.time == 0.0f);
    REQUIRE(state.faded == 3);
    REQUIRE(state.faded_time == 2.0f);

    advance_animation(&state, 0.125f, 2.0f);
    REQUIRE(state.time == 0.25f);
    REQUIRE(state.faded_time == 2.125f);
    REQUIRE(state.fade_time == 0.125f);

    // Back to the one it's fading from: the fade turns around, from where it is.
    play_animation(&state, 3, 0.5f);
    REQUIRE(state.current == 3);
    REQUIRE(state.time == 2.125f);
    REQUIRE(state.faded == 5);
    REQUIRE(state.fade_time == 0.375f);

    advance_animation(&state, 0.125f);
    REQUIRE(state.faded == NO_ANIMATION); // Faded in

    play_animation(&state, 7, 0.0f); // A cut
    REQUIRE(state.current == 7);
    REQUIRE(state.faded == NO_ANIMATION);
}
