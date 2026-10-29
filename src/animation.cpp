#include "animation.h"
#include "gltf.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

u32 find_joint(const Skin &skin, const char *name)
{
    for (u32 i = 0; i < skin.joint_count; ++i)
    {
        if (strcmp(skin.joints[i].name, name) == 0) return i;
    }
    return NO_JOINT;
}

void rest_pose(const Skin &skin, Joint_Pose *poses)
{
    for (u32 i = 0; i < skin.joint_count; ++i) poses[i] = skin.joints[i].rest;
}

static Vector4 normalize(Vector4 q)
{
    r32 length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length == 0.0f) return { 0.0f, 0.0f, 0.0f, 1.0f };
    return { q.x / length, q.y / length, q.z / length, q.w / length };
}

// Between two rotations, `t` of the way from `a` to `b`, the short way round: close
// enough to a slerp between keys a frame apart.
static Vector4 nlerp(Vector4 a, Vector4 b, r32 t)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = { -b.x, -b.y, -b.z, -b.w };
    return normalize({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}

void animate_pose(const Animation &animation, r32 time, const Skin &skin, Joint_Pose *poses)
{
    for (u32 c = 0; c < animation.channel_count; ++c)
    {
        const Animation_Channel &channel = animation.channels[c];
        u32 joint = find_joint(skin, channel.joint);
        if (joint == NO_JOINT || channel.key_count == 0) continue;

        // The keys either side of `time`, and how far it is from one to the next.
        u32 next = 0;
        while (next < channel.key_count && channel.times[next] <= time) ++next;
        u32 key  = next ? next - 1 : 0;
        if (next == channel.key_count) next = key;
        r32 span = channel.times[next] - channel.times[key];
        r32 t    = channel.step || span <= 0.0f ? 0.0f : clamp((time - channel.times[key]) / span, 0.0f, 1.0f);

        u32        size = channel.path == Animation_Path::Rotation ? 4 : 3;
        const r32 *a    = channel.values + key * size;
        const r32 *b    = channel.values + next * size;
        Vector3    lerp = { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };

        Joint_Pose &pose = poses[joint];
        switch (channel.path)
        {
            case Animation_Path::Translation: pose.translation = lerp; break;
            case Animation_Path::Scale:       pose.scale       = lerp; break;
            case Animation_Path::Rotation:
                pose.rotation = nlerp({ a[0], a[1], a[2], a[3] }, { b[0], b[1], b[2], b[3] }, t);
                break;
        }
    }
}

void blend_poses(const Skin &skin, const Joint_Pose *from, const Joint_Pose *to, r32 weight, Joint_Pose *poses)
{
    auto lerp = [&](Vector3 a, Vector3 b) { return a + (b - a) * weight; };
    for (u32 i = 0; i < skin.joint_count; ++i)
    {
        Joint_Pose pose = { lerp(from[i].translation, to[i].translation), nlerp(from[i].rotation, to[i].rotation, weight),
                            lerp(from[i].scale, to[i].scale) };
        poses[i] = pose;
    }
}

void skin_matrices(const Skin &skin, const Joint_Pose *poses, Matrix4 *matrices)
{
    // In the model's space first, parents before their children, then times the inverse binds.
    for (u32 i = 0; i < skin.joint_count; ++i)
    {
        const Joint_Pose &pose  = poses[i];
        Matrix4           local = translation(pose.translation) * rotation(pose.rotation) * scaling(pose.scale);
        u32               parent = skin.joints[i].parent;
        matrices[i] = parent == NO_JOINT ? local : matrices[parent] * local;
    }
    for (u32 i = 0; i < skin.joint_count; ++i) matrices[i] = matrices[i] * skin.joints[i].inverse_bind;
}

void skin_vertices(const Mesh &mesh, const Skin &skin, const Matrix4 *matrices, Mesh_Vertex *posed)
{
    for (u32 v = 0; v < mesh.vertex_count; ++v)
    {
        const Mesh_Vertex  &vertex = mesh.vertices[v];
        const Skin_Weights &skin_weights = skin.vertices[v];

        Matrix4 blended = {};
        r32     total   = 0.0f;
        for (u32 i = 0; i < 4; ++i)
        {
            r32 weight = skin_weights.weights[i];
            if (weight == 0.0f) continue;

            const Matrix4 &matrix = matrices[skin_weights.joints[i]];
            for (u32 column = 0; column < 4; ++column)
            {
                blended.columns[column].x += matrix.columns[column].x * weight;
                blended.columns[column].y += matrix.columns[column].y * weight;
                blended.columns[column].z += matrix.columns[column].z * weight;
                blended.columns[column].w += matrix.columns[column].w * weight;
            }
            total += weight;
        }

        posed[v] = vertex;
        if (total == 0.0f) continue;

        posed[v].position = transform_point(blended, vertex.position);
        posed[v].normal   = normalize(transform_vector(blended, vertex.normal));
    }
}

// Seconds into the animation, looping over its duration when it `loops`.
static r32 looped(const Animation &animation, r32 time, bool loops)
{
    if (!loops || animation.duration <= 0.0f) return time;
    time = std::fmod(time, animation.duration);
    return time < 0.0f ? time + animation.duration : time;
}

const Mesh_Vertex *push_posed_vertices(Render_Commands *commands, const Model &model, const Animation &animation,
                                       r32 time, const Animation *faded, r32 faded_time, r32 weight)
{
    if (!model.skin) return nullptr;
    const Skin &skin = *model.skin;

    auto *posed = (Mesh_Vertex *)push_render_data(commands, (u64)model.mesh.vertex_count * sizeof(Mesh_Vertex));
    if (!posed) return nullptr;

    Joint_Pose poses[SKIN_MAX_JOINTS];
    Matrix4    matrices[SKIN_MAX_JOINTS];
    rest_pose(skin, poses);
    animate_pose(animation, time, skin, poses);
    if (faded)
    {
        Joint_Pose faded_poses[SKIN_MAX_JOINTS];
        rest_pose(skin, faded_poses);
        animate_pose(*faded, faded_time, skin, faded_poses);
        blend_poses(skin, faded_poses, poses, clamp(weight, 0.0f, 1.0f), poses);
    }
    skin_matrices(skin, poses, matrices);
    skin_vertices(model.mesh, skin, matrices, posed);
    return posed;
}

void play_animation(Animation_State *state, u32 animation, r32 fade, bool loops)
{
    if (state->current == animation) return;

    if (animation == state->faded && state->faded != NO_ANIMATION && loops && state->faded_loops)
    {
        std::swap(state->current, state->faded);
        std::swap(state->time, state->faded_time);
        std::swap(state->loops, state->faded_loops);
        state->fade_time = std::max(state->fade_duration - state->fade_time, 0.0f);
        return;
    }

    bool fades = fade > 0.0f && state->current != NO_ANIMATION;
    state->faded         = fades ? state->current : NO_ANIMATION;
    state->faded_time    = state->time;
    state->faded_loops   = state->loops;
    state->current       = animation;
    state->time          = 0.0f;
    state->loops         = loops;
    state->fade_time     = 0.0f;
    state->fade_duration = fades ? fade : 0.0f;
}

void advance_animation(Animation_State *state, r32 dt, r32 speed)
{
    state->time += dt * speed;
    if (state->faded == NO_ANIMATION) return;

    state->faded_time += dt;
    state->fade_time  += dt;
    if (state->fade_time >= state->fade_duration) state->faded = NO_ANIMATION;
}

r32 animation_left(const Animation_State &state, const Animation_Library &library)
{
    if (state.current == NO_ANIMATION || state.loops) return INFINITY;
    return std::max(library.animations[state.current].duration - state.time, 0.0f);
}

const Mesh_Vertex *push_posed_vertices(Render_Commands *commands, const Model &model, const Animation_State &state,
                                       const Animation_Library &library)
{
    if (state.current == NO_ANIMATION) return nullptr;

    const Animation &current = library.animations[state.current];
    r32              time    = looped(current, state.time, state.loops);
    if (state.faded == NO_ANIMATION) return push_posed_vertices(commands, model, current, time);

    const Animation &faded = library.animations[state.faded];
    return push_posed_vertices(commands, model, current, time, &faded, looped(faded, state.faded_time, state.faded_loops),
                               state.fade_time / state.fade_duration);
}

u64 library_animation(Animation_Library *library, const char *path, u32 *index, void *storage, u64 storage_size)
{
    for (u32 i = 0; i < library->count; ++i)
    {
        if (strcmp(library->paths[i], path) == 0)
        {
            *index = i;
            return 0;
        }
    }

    *index = NO_ANIMATION;
    if (library->count == ANIMATION_LIBRARY_SIZE)
    {
        log_error("Can't load \"%s\": the animation library is full (%u animations)", path, ANIMATION_LIBRARY_SIZE);
        return 0;
    }
    if (strlen(path) >= MODEL_PATH_SIZE)
    {
        log_error("Can't load \"%s\": its path is longer than %u bytes", path, MODEL_PATH_SIZE - 1);
        return 0;
    }

    // Kept even when it fails, so it isn't tried again on the next ask.
    *index = library->count++;
    strcpy(library->paths[*index], path);
    return load_animation_glb(path, &library->animations[*index], storage, storage_size);
}
