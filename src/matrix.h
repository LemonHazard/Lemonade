#pragma once

#include "core.h"
#include "vector.h"

#include <cmath>

// 4x4 matrices for transforms, stored column-major like GLSL's mat4, so a Matrix4
// can go to a shader as is. Coordinates are right-handed with +Y up and the camera
// looking down -Z (glTF's convention); perspective() maps them to Vulkan clip space.

struct Matrix4
{
    Vector4 columns[4];

    float &at(u32 row, u32 column)             { return (&columns[column].x)[row]; }
    float  at(u32 row, u32 column) const       { return (&columns[column].x)[row]; }
};

inline Vector3 operator+(Vector3 a, Vector3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 operator*(Vector3 v, float s)   { return { v.x * s, v.y * s, v.z * s }; }
inline Vector3 &operator+=(Vector3 &a, Vector3 b) { a = a + b; return a; }

inline float dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vector3 v)         { return std::sqrt(dot(v, v)); }

inline Vector3 cross(Vector3 a, Vector3 b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// Returns zero for a zero vector.
inline Vector3 normalize(Vector3 v)
{
    float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

inline Matrix4 identity()
{
    Matrix4 m = {};
    m.at(0, 0) = m.at(1, 1) = m.at(2, 2) = m.at(3, 3) = 1.0f;
    return m;
}

inline Matrix4 operator*(const Matrix4 &a, const Matrix4 &b)
{
    Matrix4 m = {};
    for (u32 column = 0; column < 4; ++column)
        for (u32 row = 0; row < 4; ++row)
            for (u32 i = 0; i < 4; ++i)
                m.at(row, column) += a.at(row, i) * b.at(i, column);
    return m;
}

inline Vector3 transform_point(const Matrix4 &m, Vector3 p)
{
    Vector3 result;
    for (u32 row = 0; row < 3; ++row)
        (&result.x)[row] = m.at(row, 0) * p.x + m.at(row, 1) * p.y + m.at(row, 2) * p.z + m.at(row, 3);
    return result;
}

// Ignores the translation: for directions.
inline Vector3 transform_vector(const Matrix4 &m, Vector3 v)
{
    Vector3 result;
    for (u32 row = 0; row < 3; ++row)
        (&result.x)[row] = m.at(row, 0) * v.x + m.at(row, 1) * v.y + m.at(row, 2) * v.z;
    return result;
}

inline Matrix4 translation(Vector3 t)
{
    Matrix4 m = identity();
    m.at(0, 3) = t.x;
    m.at(1, 3) = t.y;
    m.at(2, 3) = t.z;
    return m;
}

inline Matrix4 scaling(Vector3 s)
{
    Matrix4 m = {};
    m.at(0, 0) = s.x;
    m.at(1, 1) = s.y;
    m.at(2, 2) = s.z;
    m.at(3, 3) = 1.0f;
    return m;
}

// From a unit quaternion (x, y, z, w), as glTF stores rotations.
inline Matrix4 rotation(Vector4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;

    Matrix4 m = identity();
    m.at(0, 0) = 1 - 2 * (y * y + z * z);
    m.at(0, 1) = 2 * (x * y - z * w);
    m.at(0, 2) = 2 * (x * z + y * w);
    m.at(1, 0) = 2 * (x * y + z * w);
    m.at(1, 1) = 1 - 2 * (x * x + z * z);
    m.at(1, 2) = 2 * (y * z - x * w);
    m.at(2, 0) = 2 * (x * z - y * w);
    m.at(2, 1) = 2 * (y * z + x * w);
    m.at(2, 2) = 1 - 2 * (x * x + y * y);
    return m;
}

// Of the upper 3x3: negative when `m` mirrors, which turns triangles inside out.
inline float determinant3x3(const Matrix4 &m)
{
    Vector3 row0 = { m.at(0, 0), m.at(0, 1), m.at(0, 2) };
    Vector3 row1 = { m.at(1, 0), m.at(1, 1), m.at(1, 2) };
    Vector3 row2 = { m.at(2, 0), m.at(2, 1), m.at(2, 2) };
    return dot(row0, cross(row1, row2));
}

// Transforms normals the way `m` transforms positions, up to their length (normalize
// them after): the cofactor matrix of the upper 3x3, i.e. its inverse transpose
// scaled by the determinant, with the sign kept for mirroring transforms.
inline Matrix4 normal_matrix(const Matrix4 &m)
{
    Vector3 row0 = { m.at(0, 0), m.at(0, 1), m.at(0, 2) };
    Vector3 row1 = { m.at(1, 0), m.at(1, 1), m.at(1, 2) };
    Vector3 row2 = { m.at(2, 0), m.at(2, 1), m.at(2, 2) };

    Vector3 rows[3] = { cross(row1, row2), cross(row2, row0), cross(row0, row1) };
    float   sign    = dot(row0, rows[0]) < 0.0f ? -1.0f : 1.0f;

    Matrix4 normal = identity();
    for (u32 row = 0; row < 3; ++row)
        for (u32 column = 0; column < 3; ++column)
            normal.at(row, column) = sign * (&rows[row].x)[column];
    return normal;
}

// Camera at `eye` looking at `target`, `up` pointing roughly up.
inline Matrix4 look_at(Vector3 eye, Vector3 target, Vector3 up)
{
    Vector3 forward = normalize(target - eye);
    Vector3 right   = normalize(cross(forward, up));
    Vector3 true_up = cross(right, forward);

    Matrix4 m = identity();
    m.at(0, 0) = right.x;    m.at(0, 1) = right.y;    m.at(0, 2) = right.z;    m.at(0, 3) = -dot(right, eye);
    m.at(1, 0) = true_up.x;  m.at(1, 1) = true_up.y;  m.at(1, 2) = true_up.z;  m.at(1, 3) = -dot(true_up, eye);
    m.at(2, 0) = -forward.x; m.at(2, 1) = -forward.y; m.at(2, 2) = -forward.z; m.at(2, 3) = dot(forward, eye);
    return m;
}

// Perspective projection into Vulkan clip space: +Y down and depth from 0 (near)
// to 1 (far).
inline Matrix4 perspective(float vertical_fov_radians, float aspect, float z_near, float z_far)
{
    float f = 1.0f / std::tan(vertical_fov_radians * 0.5f);

    Matrix4 m = {};
    m.at(0, 0) = f / aspect;
    m.at(1, 1) = -f;
    m.at(2, 2) = z_far / (z_near - z_far);
    m.at(2, 3) = z_near * z_far / (z_near - z_far);
    m.at(3, 2) = -1.0f;
    return m;
}

// Orthographic projection into Vulkan clip space, of the box from (left, bottom) to
// (right, top) in view space, between distances `z_near` and `z_far` in front of
// the camera (either can be negative): +Y down and depth from 0 (near) to 1 (far).
inline Matrix4 orthographic(float left, float right, float bottom, float top, float z_near, float z_far)
{
    Matrix4 m = identity();
    m.at(0, 0) = 2.0f / (right - left);
    m.at(0, 3) = -(right + left) / (right - left);
    m.at(1, 1) = -2.0f / (top - bottom);
    m.at(1, 3) = (top + bottom) / (top - bottom);
    m.at(2, 2) = -1.0f / (z_far - z_near);
    m.at(2, 3) = -z_near / (z_far - z_near);
    return m;
}
