#pragma once

#include "core.h"
#include "matrix.h"
#include "model.h"
#include "render_commands.h"
#include "vector.h"

// Skinned models and the animations that pose them. A skinned model's vertices follow
// the joints of its skin, a tree of transforms each vertex is weighed between (up to
// four of them). An animation turns, moves and scales joints over time, by their
// names, so that models whose skins name their joints alike share it. Posing is done
// on the CPU, into vertices the frame draws instead of the mesh's (see push_mesh()).

constexpr u32 SKIN_MAX_JOINTS = 256;
constexpr u32 JOINT_NAME_SIZE = 64;
constexpr u32 NO_JOINT        = 0xFFFFFFFF;

// A joint's transform under its parent: scaled first, then turned, then moved.
struct Joint_Pose
{
    Vector3 translation;
    Vector4 rotation;    // A unit quaternion (x, y, z, w)
    Vector3 scale;
};

struct Skin_Joint
{
    char       name[JOINT_NAME_SIZE];
    u32        parent;       // Into the skin's joints, before this one: NO_JOINT for those at the top
    Joint_Pose rest;         // When nothing poses it
    Matrix4    inverse_bind; // From the model's space to the joint's, as the mesh was bound to it
};

// The joints a vertex follows, into the skin's, and how much: weights adding up to 1,
// or all 0 for a vertex that doesn't move.
struct Skin_Weights
{
    u16 joints[4];
    r32 weights[4];
};

// What load_glb() reads of a glTF skin, pointing into the model's storage. Its joints
// are the glTF skin's joints and the nodes above them, which move them too: those at
// the top are the scene's roots, in the model's space.
struct Skin
{
    u32           joint_count;
    Skin_Joint   *joints;   // Parents before their children
    Skin_Weights *vertices; // One per vertex of the model's mesh
};

// The joint of the skin named `name`, or NO_JOINT.
u32 find_joint(const Skin &skin, const char *name);

// The skin's joints at rest, into `poses` (joint_count of them).
void rest_pose(const Skin &skin, Joint_Pose *poses);

enum class Animation_Path : u8
{
    Translation,
    Rotation,
    Scale,
};

// One of a joint's transforms over time: keys at increasing times, each with its value,
// 3 floats (translation, scale) or 4 (rotation), linearly interpolated between them
// (rotations the short way round) or held until the next with `step`.
struct Animation_Channel
{
    char           joint[JOINT_NAME_SIZE];
    Animation_Path path;
    bool           step;
    u32            key_count;
    r32           *times;  // Seconds
    r32           *values;
};

struct Animation
{
    r32                duration; // Seconds, to its last key
    u32                channel_count;
    Animation_Channel *channels;
};

// Poses the skin's joints the animation has channels for as they are `time` seconds in,
// before its first key as at its first and after its last as at its last; the others'
// `poses` are left as they are. Channels for joints the skin hasn't are skipped.
void animate_pose(const Animation &animation, r32 time, const Skin &skin, Joint_Pose *poses);

// Between two poses of the skin's joints, `weight` of the way from `from` to `to`, into
// `poses` (which may be either): translations and scales linearly, rotations the short
// way round.
void blend_poses(const Skin &skin, const Joint_Pose *from, const Joint_Pose *to, r32 weight, Joint_Pose *poses);

// What each joint of the skin does to the vertices that follow it, posed by `poses`:
// its transform in the model's space, through its parents', times its inverse bind matrix.
void skin_matrices(const Skin &skin, const Joint_Pose *poses, Matrix4 *matrices);

// The mesh's vertices moved by the skin's `matrices` (skin_matrices()), into `posed`
// (vertex_count of them): each by its joints' matrices, weighed.
void skin_vertices(const Mesh &mesh, const Skin &skin, const Matrix4 *matrices, Mesh_Vertex *posed);

// The skinned model's vertices posed by the animation `time` seconds in, looping over its
// duration, into the frame's commands (see push_render_data()), to draw them with
// push_model(). With an animation `faded` from, `faded_time` seconds into it, they're
// posed `weight` of the way from its pose to the animation's (see blend_poses()).
// Returns null when the model has no skin, or they don't fit in the commands (counted as
// a dropped command).
const Mesh_Vertex *push_posed_vertices(Render_Commands *commands, const Model &model, const Animation &animation,
                                       r32 time, const Animation *faded = nullptr, r32 faded_time = 0.0f,
                                       r32 weight = 1.0f);

// Animations loaded from .glb files (see load_animation_glb()), once each however many
// times they're asked for, as the model library loads models.

constexpr u32 ANIMATION_LIBRARY_SIZE = 256;
constexpr u32 NO_ANIMATION           = 0xFFFFFFFF;

struct Animation_Library
{
    u32       count;
    char      paths[ANIMATION_LIBRARY_SIZE][MODEL_PATH_SIZE];
    Animation animations[ANIMATION_LIBRARY_SIZE];
};

// The .glb at `path`'s first animation, loaded into `storage` the first time it's asked
// for. Returns the bytes of storage used (0 when it was already loaded) and sets `index`
// to the animation's, into the library's animations. One that can't be loaded (logged)
// is kept without channels, posing nothing, so that it isn't tried again on the next ask.
// When the library is full (logged), `index` is NO_ANIMATION.
u64 library_animation(Animation_Library *library, const char *path, u32 *index, void *storage, u64 storage_size);
