#pragma once

#include <cmath>

struct Vector2 {
    union {
        struct {
            float x;
            float y;
        };
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x; 
    }
};

struct Vector3 {
    union {
        struct {
            float x;
            float y;
            float z;
        };
        struct {
            float r;
            float g;
            float b;
        };
        Vector2 xy;
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x;
    }
};

struct Vector4 {
    union {
        struct {
            float x;
            float y;
            float z;
            float w;
        };
        struct {
            float r;
            float g;
            float b;
            float a;
        };
        Vector2 xy;
        Vector3 xyz;
        Vector3 rgb;
    };
    
    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x; 
    }
};

struct Rotation3 {
    union {
        struct {
            float pitch;
            float yaw;
            float roll;
        };
        struct {
            float p;
            float y;
            float r;
        };
    };

    const float& operator[](size_t index) const {
        return (&pitch)[index];
    }

    operator const float*() const {
        return &pitch;
    }
};

inline Vector2 operator+(Vector2 a, Vector2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vector2 operator-(Vector2 a, Vector2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vector2 operator*(Vector2 v, float s)   { return { v.x * s, v.y * s }; }

inline float dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vector2 v)         { return std::sqrt(dot(v, v)); }

// The z of the 3D cross product: positive when `b` turns counter-clockwise from `a`
// with y up, clockwise with y down.
inline float cross(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }

inline Vector3 operator+(Vector3 a, Vector3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 operator*(Vector3 v, float s)   { return { v.x * s, v.y * s, v.z * s }; }
inline Vector3 &operator+=(Vector3 &a, Vector3 b) { a = a + b; return a; }
inline Vector3 &operator-=(Vector3 &a, Vector3 b) { a = a - b; return a; }

inline float dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vector3 v)         { return std::sqrt(dot(v, v)); }

inline Vector3 cross(Vector3 a, Vector3 b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// Return zero for a zero vector.
inline Vector2 normalize(Vector2 v)
{
    float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

inline Vector3 normalize(Vector3 v)
{
    float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}
