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
constexpr r32 GIZMO_PIXELS      = 100.0f;
constexpr r32 GIZMO_GRAB_PIXELS = 8.0f;
constexpr r32 GIZMO_SNAP        = 0.5f;

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

bool move_with_gizmo(Gizmo_Drag *drag, Vector3 *position, const Render_Commands &render, bool on_level, Vector3 from,
                     Vector3 along)
{
    Gizmo gizmo;
    bool  seen = see_gizmo(render, *position, &gizmo);

    if (drag->axes && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag->axes = 0;

    u32 pointed = seen && on_level && !drag->axes ? gizmo_under_mouse(gizmo) : 0;
    if (pointed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        point_on_gizmo(*position, pointed, from, along, &drag->grabbed))
    {
        drag->axes = pointed;
        drag->from = *position;
    }

    Vector3 at;
    if (drag->axes && on_level && point_on_gizmo(drag->from, drag->axes, from, along, &at))
    {
        Vector3 moved = at - drag->grabbed;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!(drag->axes & axis_bit(axis))) continue;
            r32 &moved_axis = (&position->x)[axis];
            moved_axis      = drag->from[axis] + moved[axis];
            if (ImGui::GetIO().KeyCtrl) moved_axis = std::round(moved_axis / GIZMO_SNAP) * GIZMO_SNAP;
        }
        seen = see_gizmo(render, *position, &gizmo);
    }

    if (seen) draw_gizmo(gizmo, drag->axes ? drag->axes : pointed);
    return pointed || drag->axes;
}
