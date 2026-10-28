#include "gizmo.h"
#include "matrix.h"
#include "render_commands.h"
#include "world.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

// The gizmo is GIZMO_PIXELS long on the screen however far it is: its arrows are, and
// its squares go from a quarter of that to half of it. The pointer grabs an arrow
// GIZMO_GRAB_PIXELS from it, and Ctrl snaps what's moved to every GIZMO_SNAP meters.
// Alt snaps it to touch what's GIZMO_REACH_PIXELS from it at most, found by
// GIZMO_TOUCH_RAYS by as many rays across its leading side, GIZMO_TOUCH_INSET of the way
// in from its edges, so what it stands on beside them isn't in the way.
constexpr r32 GIZMO_PIXELS       = 100.0f;
constexpr r32 GIZMO_GRAB_PIXELS  = 8.0f;
constexpr r32 GIZMO_SNAP         = 0.5f;
constexpr r32 GIZMO_REACH_PIXELS = 50.0f;
constexpr u32 GIZMO_TOUCH_RAYS   = 5;
constexpr r32 GIZMO_TOUCH_INSET  = 0.05f;

// The gizmo as it's seen: where its arrows' tips are on the screen, in ImGui's window
// coordinates, and the corners of its squares, each indexed by the axis it's not on.
// Those seen end-on aren't shown, nor grabbed.
struct Gizmo
{
    Vector3 center;
    r32     size;  // Meters long, for GIZMO_PIXELS on the screen
    Vector3 side;  // 1 or -1 on each axis: the squares are on the camera's side of it
    Vector2 screen_center;
    Vector2 tips[3];
    bool    tip_shown[3];
    Vector2 squares[3][4];
    bool    square_shown[3];
};

static ImVec2 imgui_vector(Vector2 v)
{
    return { v.x, v.y };
}

static Vector3 axis_vector(u32 axis)
{
    Vector3 v = {};
    (&v.x)[axis] = 1.0f;
    return v;
}

// Bits of Gizmo_Drag::axes: an axis', or the two of the plane `normal` is the normal of.
static u32 axis_bit(u32 axis)
{
    return 1u << axis;
}

static u32 plane_bits(u32 normal)
{
    return 7u & ~axis_bit(normal);
}

// Where `point` is on the screen, as `world_to_clip` sees it: false when it's behind
// the camera.
static bool on_screen(const Matrix4 &world_to_clip, Vector3 point, Vector2 *at)
{
    Vector4 clip = world_to_clip * Vector4{ point.x, point.y, point.z, 1.0f };
    if (clip.w <= 1e-4f) return false;

    ImVec2 display = ImGui::GetIO().DisplaySize;
    *at = { (clip.x / clip.w + 1.0f) * 0.5f * display.x, (clip.y / clip.w + 1.0f) * 0.5f * display.y };
    return true;
}

// The gizmo at `center`, false when it's not seen.
static bool see_gizmo(const Render_Commands &render, Vector3 center, Gizmo *gizmo)
{
    *gizmo        = {};
    gizmo->center = center;
    if (!on_screen(render.view_projection, center, &gizmo->screen_center)) return false;

    // How many pixels a meter is across the view there.
    Vector3 towards = center - render.camera_position;
    Vector3 across  = normalize(cross(towards, { 0.0f, 1.0f, 0.0f }));
    if (!(length(across) > 0.0f)) across = { 1.0f, 0.0f, 0.0f };
    Vector2 meter;
    if (!on_screen(render.view_projection, center + across, &meter)) return false;
    r32 pixels = length(meter - gizmo->screen_center);
    if (!(pixels > 0.0f)) return false;
    gizmo->size = GIZMO_PIXELS / pixels;

    for (u32 axis = 0; axis < 3; ++axis)
    {
        (&gizmo->side.x)[axis] = towards[axis] > 0.0f ? -1.0f : 1.0f;

        Vector2 &tip = gizmo->tips[axis];
        gizmo->tip_shown[axis] = on_screen(render.view_projection, center + axis_vector(axis) * gizmo->size, &tip) &&
                                 length(tip - gizmo->screen_center) > 0.15f * GIZMO_PIXELS;
    }

    for (u32 normal = 0; normal < 3; ++normal)
    {
        u32     u          = (normal + 1) % 3;
        u32     v          = (normal + 2) % 3;
        Vector3 a          = axis_vector(u) * (gizmo->side[u] * gizmo->size);
        Vector3 b          = axis_vector(v) * (gizmo->side[v] * gizmo->size);
        Vector3 corners[4] = { a * 0.25f + b * 0.25f, a * 0.5f + b * 0.25f, a * 0.5f + b * 0.5f, a * 0.25f + b * 0.5f };

        Vector2 *square = gizmo->squares[normal];
        bool     shown  = true;
        for (u32 i = 0; i < 4; ++i) shown = shown && on_screen(render.view_projection, center + corners[i], &square[i]);

        // Its area on the screen, by the shoelace formula: next to none edge-on.
        r32 area = 0.0f;
        for (u32 i = 0; i < 4; ++i) area += 0.5f * cross(square[i], square[(i + 1) % 4]);
        r32 full = 0.25f * GIZMO_PIXELS * 0.25f * GIZMO_PIXELS;
        gizmo->square_shown[normal] = shown && std::fabs(area) > 0.1f * full;
    }
    return true;
}

static r32 distance_to_segment(Vector2 p, Vector2 a, Vector2 b)
{
    Vector2 ab = b - a;
    r32     t  = clamp(dot(p - a, ab) / std::max(dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    return length(p - (a + ab * t));
}

// Whether `p` is in the convex quad, its corners in either order.
static bool in_quad(Vector2 p, const Vector2 quad[4])
{
    bool left = false, right = false;
    for (u32 i = 0; i < 4; ++i)
    {
        r32 turn = cross(quad[(i + 1) % 4] - quad[i], p - quad[i]);
        left  = left || turn < 0.0f;
        right = right || turn > 0.0f;
    }
    return !(left && right);
}

// The axes of what the pointer is on, as Gizmo_Drag::axes has them: a square first, as
// they're over the arrows, then the nearest arrow. 0 when it's on none.
static u32 gizmo_under_mouse(const Gizmo &gizmo)
{
    ImVec2  io_mouse = ImGui::GetIO().MousePos;
    Vector2 mouse    = { io_mouse.x, io_mouse.y };
    for (u32 normal = 0; normal < 3; ++normal)
        if (gizmo.square_shown[normal] && in_quad(mouse, gizmo.squares[normal])) return plane_bits(normal);

    u32 nearest  = 0;
    r32 distance = GIZMO_GRAB_PIXELS;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        if (!gizmo.tip_shown[axis]) continue;
        r32 d = distance_to_segment(mouse, gizmo.screen_center, gizmo.tips[axis]);
        if (d <= distance)
        {
            nearest  = axis_bit(axis);
            distance = d;
        }
    }
    return nearest;
}

// Where the ray meets the gizmo's `axes` through `center`: the point of an axis nearest
// to it, or where it goes through a plane. False when it's along them, or meets them
// behind the camera.
static bool point_on_gizmo(Vector3 center, u32 axes, Vector3 from, Vector3 along, Vector3 *at)
{
    for (u32 axis = 0; axis < 3; ++axis)
    {
        if (axes != axis_bit(axis)) continue;

        // The nearest points of two lines.
        Vector3 line  = axis_vector(axis);
        Vector3 apart = center - from;
        r32     b = dot(line, along), c = dot(along, along), d = dot(line, apart), e = dot(along, apart);
        r32     denominator = c - b * b;
        if (denominator <= 1e-4f * c) return false;
        if ((e - b * d) / denominator <= 0.0f) return false;

        *at = center + line * ((b * e - c * d) / denominator);
        return true;
    }

    for (u32 normal = 0; normal < 3; ++normal)
    {
        if (axes != plane_bits(normal)) continue;

        Vector3 n       = axis_vector(normal);
        r32     towards = dot(along, n);
        if (std::fabs(towards) <= 0.02f * length(along)) return false;
        r32 t = dot(center - from, n) / towards;
        if (t <= 0.0f) return false;

        *at = from + along * t;
        return true;
    }
    return false;
}

// Around the entity in the level's space: around its model's bounds as it's turned and
// scaled, or the point it's at when it has none.
static Bounds3 entity_bounds(const Entity &entity, const Level_Assets &assets)
{
    if (entity.model == NO_MODEL) return { entity.position, entity.position };

    const Bounds3 &model     = assets.model_bounds[entity.model];
    Matrix4        transform = entity_transform(entity);
    Bounds3        bounds    = { { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
    for (u32 corner = 0; corner < 8; ++corner)
    {
        Vector3 p = { corner & 1 ? model.max.x : model.min.x, corner & 2 ? model.max.y : model.min.y,
                      corner & 4 ? model.max.z : model.min.z };
        p         = transform_point(transform, p);
        for (u32 axis = 0; axis < 3; ++axis)
        {
            (&bounds.min.x)[axis] = std::min(bounds.min[axis], p[axis]);
            (&bounds.max.x)[axis] = std::max(bounds.max[axis], p[axis]);
        }
    }
    return bounds;
}

// How far the `moved` entity goes along `axis`, `toward` its end (1) or start (-1), till
// its bounds touch the nearest of the level's other visible models, from `reach` meters
// back (or half its bounds, when that's less: what's in its back half is behind it) to
// `reach` meters on: false when none is that near. Rays go along it from across the side
// of its bounds it goes to, again only when `touch`'s last ones can't tell: they're slow
// on big models.
static bool distance_to_touch(const Level &level, const Level_Assets &assets, const Entity &moved, u32 axis,
                              r32 toward, r32 reach, Gizmo_Touch *touch, r32 *distance)
{
    Bounds3 bounds  = entity_bounds(moved, assets);
    r32     back    = std::min(reach, 0.5f * (bounds.max[axis] - bounds.min[axis]));
    r32     start   = (toward > 0.0f ? bounds.max[axis] : bounds.min[axis]) - toward * back;
    Vector3 low     = bounds.min;
    Vector3 high    = bounds.max;
    (&low.x)[axis]  = start;
    (&high.x)[axis] = start;
    u32     u       = (axis + 1) % 3;
    u32     v       = (axis + 2) % 3;

    // The same rays further on meet what the last ones did, till they're past it.
    bool same = touch->cast && touch->toward == toward && touch->low[u] == low[u] && touch->low[v] == low[v] &&
                touch->high[u] == high[u] && touch->high[v] == high[v];
    r32  on   = (start - touch->low[axis]) * toward;
    if (!same || on < 0.0f || on >= touch->nearest)
    {
        Vector3 along   = axis_vector(axis) * toward;
        r32     nearest = INFINITY;
        for (u32 i = 0; i < GIZMO_TOUCH_RAYS; ++i)
        {
            for (u32 j = 0; j < GIZMO_TOUCH_RAYS; ++j)
            {
                r32     a = GIZMO_TOUCH_INSET + (1.0f - 2.0f * GIZMO_TOUCH_INSET) * i / (GIZMO_TOUCH_RAYS - 1);
                r32     b = GIZMO_TOUCH_INSET + (1.0f - 2.0f * GIZMO_TOUCH_INSET) * j / (GIZMO_TOUCH_RAYS - 1);
                Vector3 origin = low;
                (&origin.x)[u] = low[u] + (high[u] - low[u]) * a;
                (&origin.x)[v] = low[v] + (high[v] - low[v]) * b;

                r32 t;
                entity_on_ray(level, assets, origin, along, Ray_Hits::Visible, &t, moved.id);
                nearest = std::min(nearest, t);
            }
        }
        *touch = { true, toward, low, high, nearest };
        on     = 0.0f;
    }

    r32 nearest = touch->nearest - on;
    if (nearest - back > reach) return false;

    *distance = nearest - back;
    return true;
}

static void draw_gizmo(const Gizmo &gizmo, u32 lit)
{
    const ImU32 colors[3] = { IM_COL32(230, 60, 60, 255), IM_COL32(110, 205, 50, 255), IM_COL32(60, 115, 255, 255) };
    const ImU32 yellow    = IM_COL32(255, 215, 50, 255);
    ImDrawList *draw      = ImGui::GetBackgroundDrawList();

    // A square has its plane's normal's color, as the ground's (x and z) is green.
    for (u32 normal = 0; normal < 3; ++normal)
    {
        if (!gizmo.square_shown[normal]) continue;
        bool           on     = lit == plane_bits(normal);
        ImU32          color  = on ? yellow : colors[normal];
        const Vector2 *square = gizmo.squares[normal];
        ImVec2         a = imgui_vector(square[0]), b = imgui_vector(square[1]);
        ImVec2         c = imgui_vector(square[2]), d = imgui_vector(square[3]);
        draw->AddQuadFilled(a, b, c, d, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, on ? 150 : 70));
        draw->AddQuad(a, b, c, d, color, 1.5f);
    }

    for (u32 axis = 0; axis < 3; ++axis)
    {
        if (!gizmo.tip_shown[axis]) continue;
        ImU32   color = lit == axis_bit(axis) ? yellow : colors[axis];
        Vector2 tip   = gizmo.tips[axis];
        Vector2 along = normalize(tip - gizmo.screen_center);
        Vector2 aside = { -along.y, along.x };

        const r32 head = 14.0f, half_width = 6.0f;
        Vector2   base = tip - along * head;
        draw->AddLine(imgui_vector(gizmo.screen_center), imgui_vector(base), color, 3.0f);
        draw->AddTriangleFilled(imgui_vector(tip), imgui_vector(base + aside * half_width),
                                imgui_vector(base - aside * half_width), color);
    }
    draw->AddCircleFilled(imgui_vector(gizmo.screen_center), 4.0f, IM_COL32(255, 255, 255, 255));
}

bool move_with_gizmo(Gizmo_Drag *drag, Entity *moved, const Level &level, const Level_Assets &assets,
                     const Render_Commands &render, bool on_level, Vector3 from, Vector3 along)
{
    Vector3 *position = &moved->position;
    Gizmo    gizmo;
    bool  seen = see_gizmo(render, *position, &gizmo);

    if (drag->axes && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag->axes = 0;

    u32 pointed = seen && on_level && !drag->axes ? gizmo_under_mouse(gizmo) : 0;
    if (pointed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        point_on_gizmo(*position, pointed, from, along, &drag->grabbed))
    {
        drag->axes = pointed;
        drag->from = *position;
        for (Gizmo_Touch &touch : drag->touches) touch = {};
    }

    Vector3 at;
    if (drag->axes && on_level && point_on_gizmo(drag->from, drag->axes, from, along, &at))
    {
        // Where the pointer has it first, on all the axes it's moved on, so what it
        // touches on one is found where it is on the others.
        Vector3 dragged = at - drag->grabbed;
        for (u32 axis = 0; axis < 3; ++axis)
            if (drag->axes & axis_bit(axis)) (&position->x)[axis] = drag->from[axis] + dragged[axis];

        const ImGuiIO &io    = ImGui::GetIO();
        r32            reach = gizmo.size * (GIZMO_REACH_PIXELS / GIZMO_PIXELS);
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!(drag->axes & axis_bit(axis))) continue;
            r32 &moved_axis = (&position->x)[axis];
            r32  toward     = dragged[axis] > 0.0f ? 1.0f : -1.0f;
            r32  distance;
            if (io.KeyAlt && dragged[axis] != 0.0f &&
                distance_to_touch(level, assets, *moved, axis, toward, reach, &drag->touches[axis], &distance))
                moved_axis += toward * distance;
            else if (io.KeyCtrl)
                moved_axis = std::round(moved_axis / GIZMO_SNAP) * GIZMO_SNAP;
        }
        seen = see_gizmo(render, *position, &gizmo);
    }

    if (seen) draw_gizmo(gizmo, drag->axes ? drag->axes : pointed);
    return pointed || drag->axes;
}
